#include "MCCombatFeedbackComponent.h"

#include "MCReactionVFX.h"
#include "MCWeaponImpact.h"
#include "MCWeaponTrailComponent.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace MCWeaponImpact
{
namespace
{
struct FContactHold
{
    double StartedAt=0;
    float Seconds=0, SwingAtContact=0;
};
// Weak actor keys keep PIE sessions and destroyed enemies out of subsequent fights.
TMap<TWeakObjectPtr<const AActor>,FContactHold> Contacts;

bool IsHandTool(const AMCToothCharacter* Hero)
{
    return IsValid(Hero) && Hero->Inventory && Hero->GetNetMode()!=NM_DedicatedServer
        && !Hero->ActorHasTag(TEXT("MC.LegacyCombatFeedback"))
        && Hero->Inventory->SelectedUpgrade()==EMCToolUpgrade::None
        && (Hero->Inventory->Selected==EMCToolSlot::Knife || Hero->Inventory->Selected==EMCToolSlot::Pickaxe);
}

const FContactHold* Find(const AActor* Actor)
{
    if(!IsValid(Actor) || !Actor->GetWorld()) return nullptr;
    const auto* Hold=Contacts.Find(Actor);
    const double Age=Hold?Actor->GetWorld()->GetTimeSeconds()-Hold->StartedAt:-1;
    return Age>=0 && Age<2.?Hold:nullptr;
}

void Store(const AActor* Actor,float Seconds,float SwingAtContact=0)
{
    for(auto It=Contacts.CreateIterator();It;++It)
        if(!It.Key().IsValid() || !It.Key()->GetWorld()
            || It.Key()->GetWorld()->GetTimeSeconds()-It.Value().StartedAt>=2.) It.RemoveCurrent();
    Contacts.Add(Actor,{Actor->GetWorld()->GetTimeSeconds(),Seconds,SwingAtContact});
}
}

void BeginSwing(const AMCToothCharacter* Hero)
{
    Contacts.Remove(Hero);
    if(IsValid(Hero)) if(auto* Trail=Hero->FindComponentByClass<UMCWeaponTrailComponent>()) Trail->BeginSwing();
}

void ConfirmSwing(AMCToothCharacter* Hero,uint8 ToolSlot)
{
    if(!IsHandTool(Hero) || ToolSlot!=uint8(Hero->Inventory->Selected)) return;
    const float Elapsed=Hero->GetToolSwingElapsed();
    if(Elapsed<0 || Elapsed>=Hero->Inventory->SwingDuration() || Find(Hero)) return;
    const float Hold=FMath::Clamp((Hero->Inventory->Selected==EMCToolSlot::Pickaxe?.055f:.045f)
        /Hero->Inventory->RogueAttackRate(),.016f,.055f);
    Store(Hero,Hold,Elapsed);
}

void HoldTarget(AActor* Target,const AMCToothCharacter* Source)
{
    if(IsValid(Target) && IsHandTool(Source) && !Target->ActorHasTag(TEXT("MC.LegacyCombatFeedback")))
        Store(Target,.045f);
}

bool IsPoseHeld(const AActor* Actor)
{
    const auto* Hold=Find(Actor);
    return Hold && Actor->GetWorld()->GetTimeSeconds()-Hold->StartedAt<Hold->Seconds;
}

float VisualSwingElapsed(const AMCToothCharacter* Hero)
{
    const float Raw=Hero->GetToolSwingElapsed();
    const auto* Hold=IsHandTool(Hero)?Find(Hero):nullptr;
    return Hold?Raw-FMath::Clamp(Raw-Hold->SwingAtContact,0.f,Hold->Seconds):Raw;
}

float Recoil(const AMCToothCharacter* Hero)
{
    const auto* Hold=IsHandTool(Hero)?Find(Hero):nullptr;
    if(!Hold) return 0;
    const float T=FMath::Clamp(float(Hero->GetWorld()->GetTimeSeconds()-Hold->StartedAt-Hold->Seconds)/.18f,0.f,1.f);
    return FMath::Sin(T*PI)*(1-T);
}
}

UMCCombatFeedbackComponent::UMCCombatFeedbackComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
}

void UMCCombatFeedbackComponent::BeginPlay()
{
    Super::BeginPlay();
    // Uniform additive flash works independently of an artist's vertex paint.
    // Preload before combat so accepting a hit never loads an artist material.
    if(bUseBodyOverlay && !IsLegacy() && GetNetMode()!=NM_DedicatedServer)
        OverlayBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_CombatHitOverlay.M_CombatHitOverlay"));
}

double UMCCombatFeedbackComponent::ServerNow() const
{
    const UWorld* World=GetWorld();
    const auto* State=World?World->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():(World?World->GetTimeSeconds():0.);
}

bool UMCCombatFeedbackComponent::IsLegacy() const
{
    return GetOwner() && GetOwner()->ActorHasTag(TEXT("MC.LegacyCombatFeedback"));
}

bool UMCCombatFeedbackComponent::ReportAcceptedHit(AActor* Source,FVector ContactPoint,FVector Direction,
    float HealthDamage,float ShieldDamage,bool bShielded,bool bFatal)
{
    AActor* Owner=GetOwner();
    if(!IsValid(Owner) || !Owner->HasAuthority() || !GetWorld()
        || ContactPoint.ContainsNaN() || Direction.ContainsNaN()
        || !FMath::IsFinite(HealthDamage) || !FMath::IsFinite(ShieldDamage)
        || HealthDamage<0.f || ShieldDamage<0.f || (HealthDamage<=0.f && ShieldDamage<=0.f)
        || (IsValid(Source) && Source->GetWorld()!=GetWorld())) return false;
    LastHit.Serial=LastHit.Serial==MAX_int32?1:LastHit.Serial+1;
    LastHit.ServerTime=ServerNow();
    LastHit.Source=IsValid(Source)?Source:nullptr;
    LastHit.ContactPoint=ContactPoint;
    LastHit.Direction=Direction.GetSafeNormal(SMALL_NUMBER,Owner->GetActorForwardVector());
    LastHit.HealthDamage=HealthDamage;
    LastHit.ShieldDamage=ShieldDamage;
    LastHit.bShielded=bShielded || ShieldDamage>0.f;
    LastHit.bFatal=bFatal;
    // The effect actor already owns its replication. Client OnRep only presents the body/event.
    if(!IsLegacy() || (bLegacyHasImpact && HealthDamage>0.f))
        AMCReactionVFX::SpawnHit(Owner,ContactPoint,LastHit.Direction,HealthDamage,LastHit.bShielded);
    PresentHit();
    Owner->ForceNetUpdate();
    return true;
}

float UMCCombatFeedbackComponent::GetHitPulse() const
{
    if(IsLegacy() || LastHit.Serial==0 || LastHit.HealthDamage<=0.f) return 0.f;
    const float Age=FMath::Max(0.f,float(ServerNow()-LastHit.ServerTime));
    return Age<.28f?1.f-FMath::SmoothStep(.04f,.28f,Age):0.f;
}

float UMCCombatFeedbackComponent::GetHitFlash() const
{
    return LastHit.HealthDamage>0.f?PresentationFlash():0.f;
}

float UMCCombatFeedbackComponent::PresentationFlash() const
{
    if(IsLegacy() || LastHit.Serial==0) return 0.f;
    const float Age=FMath::Max(0.f,float(ServerNow()-LastHit.ServerTime));
    return Age<.105f?1.f-FMath::SmoothStep(.045f,.105f,Age):0.f;
}

void UMCCombatFeedbackComponent::SetVisualTarget(UMeshComponent* Mesh)
{
    if(Mesh && Mesh->GetOwner()!=GetOwner()) return;
    if(VisualTarget==Mesh) return;
    RestoreOverlay();
    VisualTarget=Mesh;
    HitOverlay=nullptr;
    UpdateOverlay();
}

void UMCCombatFeedbackComponent::OnRep_LastHit()
{
    PresentHit();
}

void UMCCombatFeedbackComponent::PresentHit()
{
    if(LastHit.Serial==0 || PresentedSerial==LastHit.Serial) return;
    PresentedSerial=LastHit.Serial;
    // Ignore expired snapshots on late join/relevancy; all timing uses the replicated server clock.
    const double Age=ServerNow()-LastHit.ServerTime;
    if(GetNetMode()==NM_DedicatedServer || IsLegacy() || Age>.32) return;
    if(Age<.06 && !LastHit.bFatal && bUseProceduralRecoil)
        MCWeaponImpact::HoldTarget(GetOwner(),Cast<AMCToothCharacter>(LastHit.Source));
    UpdateOverlay();
    OnHitPresented.Broadcast(LastHit);
}

void UMCCombatFeedbackComponent::UpdateOverlay()
{
    const float Flash=PresentationFlash();
    if(!bUseBodyOverlay || !IsValid(VisualTarget) || !OverlayBase || Flash<=0.f)
    {
        RestoreOverlay();
        SetComponentTickEnabled(false);
        return;
    }
    if(!HitOverlay) HitOverlay=UMaterialInstanceDynamic::Create(OverlayBase,this);
    if(!HitOverlay) return;
    if(!bOverlayInstalled)
    {
        // Preserve the raw component override, including nullptr (asset overlay fallback).
        PreviousOverlay=VisualTarget->OverlayMaterial;
        VisualTarget->SetOverlayMaterial(HitOverlay);
        bOverlayInstalled=true;
    }
    else if(VisualTarget->OverlayMaterial!=HitOverlay)
    {
        // Another presentation system took ownership; do not replace its overlay.
        bOverlayInstalled=false;
        PreviousOverlay=nullptr;
        SetComponentTickEnabled(false);
        return;
    }
    const FLinearColor Color=LastHit.bShielded?FLinearColor(.22f,.75f,1.f):FLinearColor(1.8f,1.8f,1.8f);
    HitOverlay->SetVectorParameterValue(TEXT("Color"),Color);
    HitOverlay->SetScalarParameterValue(TEXT("Opacity"),Flash*(LastHit.bShielded?.55f:.85f));
    SetComponentTickEnabled(true);
}

void UMCCombatFeedbackComponent::RestoreOverlay()
{
    if(bOverlayInstalled && IsValid(VisualTarget) && VisualTarget->OverlayMaterial==HitOverlay)
        VisualTarget->SetOverlayMaterial(PreviousOverlay);
    bOverlayInstalled=false;
    PreviousOverlay=nullptr;
}

void UMCCombatFeedbackComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    UpdateOverlay();
}

void UMCCombatFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RestoreOverlay();
    SetComponentTickEnabled(false);
    Super::EndPlay(EndPlayReason);
}

void UMCCombatFeedbackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UMCCombatFeedbackComponent,LastHit);
}
