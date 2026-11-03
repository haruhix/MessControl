#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "MCReactionVFX.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMCFirePatch::AMCFirePatch()
{bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FireOrigin")));}
AMCFirePatch* AMCFirePatch::Ignite(UObject* Context,FVector Point,float Radius,float Seconds,int32 InBatch,bool CreateLesion)
{
    UWorld* W=Context?Context->GetWorld():nullptr;if(!W || W->GetNetMode()==NM_Client) return nullptr;
    const FTransform T(Point);auto* Fire=W->SpawnActorDeferred<AMCFirePatch>(StaticClass(),T);
    if(Fire) {Fire->BurnRadius=FMath::Clamp(Radius,30.f,250.f);Fire->BurnSeconds=Seconds>0?FMath::Clamp(Seconds,1.f,60.f):0;Fire->Batch=InBatch;Fire->bCreateLesion=CreateLesion;Fire->FinishSpawning(T);}return Fire;
}
void AMCFirePatch::BeginPlay()
{
    Super::BeginPlay();
    // All rendering peers prepare only already-resident location data. No load at contact.
    MCWorldPresentation::PrepareOwner(this,{TEXT("World.FirePatch")});
    if(!HasAuthority()) return;StartedAt=GetWorld()->GetTimeSeconds();if(BurnSeconds>0) SetLifeSpan(BurnSeconds);
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
        FHitResult Hit;if(It->SurfacePoint(GetActorLocation(),Hit) && FMath::Abs(GetActorLocation().Z-Hit.ImpactPoint.Z)<150) {
            Tongue=*It;SurfaceAnchor=It->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);break;
        }
    }
    if(bCreateLesion) Lesion=AMCMouthSurface::SpawnDamageUlcer(GetWorld(),GetActorLocation(),Batch);
    Flames=AMCReactionVFX::SpawnForPresentation(GetWorld(),TEXT("World.FirePatch"),TEXT("Loop"),GetActorLocation(),EMCReactionEffect::Fire,BurnSeconds,BurnRadius,FVector::ForwardVector,0,this);
    if(Flames) Flames->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);
    ForceNetUpdate();
}
bool AMCFirePatch::Extinguish(AMCToothCharacter* Worker,float Dt,float Power)
{
    if(!HasAuthority() || !IsBurning() || !IsValid(Worker) || !Worker->CanWork() || !FMath::IsFinite(Dt) || Dt<=0) return false;
    if(LastTreatmentFrame==GFrameCounter) return true;
    LastTreatmentFrame=GFrameCounter;
    if(IsValid(Lesion)) Lesion->ApplyAnesthetic(.3f);
    Heat=FMath::Max(0.f,Heat-FMath::Min(Dt,.2f)/FMath::Max(.1f,ExtinguishSeconds)*FMath::Clamp(Power,1.f,3.f));
    if(Heat<.0001f) {
        Heat=0;SetLifeSpan(.7f);
        AMCReactionVFX::SpawnForPresentation(GetWorld(),TEXT("World.FirePatch"),TEXT("Steam"),GetActorLocation(),EMCReactionEffect::Steam,1.2f,BurnRadius,FVector::ForwardVector,0,this);
        bool OtherFire=false;
        for(TActorIterator<AMCFirePatch> It(GetWorld());It;++It) if(*It!=this && It->IsBurning() && (Lesion?It->Lesion==Lesion:It->Batch==Batch)) {OtherFire=true;break;}
        if(!OtherFire && !IsValid(Lesion)) Worker->NotifyTaskFeedback(true,GetActorLocation());
    }
    ForceNetUpdate();return true;
}
void AMCFirePatch::EndPlay(const EEndPlayReason::Type Reason)
{if(HasAuthority() && IsValid(Flames)) Flames->Destroy();Super::EndPlay(Reason);}
void AMCFirePatch::Tick(float Dt)
{
    Super::Tick(Dt);
    if(Tongue) {FHitResult Hit;if(Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(SurfaceAnchor),Hit)) SetActorLocation(Hit.ImpactPoint+Hit.ImpactNormal*5);}
    if(!HasAuthority() || !IsBurning() || GetWorld()->GetTimeSeconds()<NextDamageAt) return;NextDamageAt=GetWorld()->GetTimeSeconds()+.5;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->Status->IsAlive()
        && FVector::DistSquared2D(It->GetActorLocation(),GetActorLocation())<FMath::Square(BurnRadius)
        && FMath::Abs(It->GetActorLocation().Z-GetActorLocation().Z)<120) {
        const FVector Direction=(It->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
        const float Before=It->Status->State.Health;
        // Exact old damage arguments and cadence. Rejected/protected hits never emit Contact.
        if(It->Status->Damage(3,Direction)) {
            const float Applied=Before-It->Status->State.Health;
            if(Applied>0)MulticastAcceptedBurnContact(It->GetActorLocation(),Direction,Applied);
        }
    }
}
void AMCFirePatch::MulticastAcceptedBurnContact_Implementation(FVector_NetQuantize Point,FVector_NetQuantizeNormal Direction,float Applied)
{
    if(GetNetMode()==NM_DedicatedServer||!GetWorld()||GetWorld()->bIsTearingDown||IsActorBeingDestroyed()
        ||Point.ContainsNaN()||Direction.ContainsNaN()||!FMath::IsFinite(Applied)||Applied<=0)return;
    constexpr auto ID=TEXT("World.FirePatch");
    auto* P=FindComponentByClass<UMCWorldPresentationComponent>();auto* Source=P?P->GetSource(ID):nullptr;
    if(!Source){UE_LOG(LogTemp,Warning,TEXT("MC_FIRE_CONTACT_UNPREPARED: accepted burn presentation dropped; no event-time load."));return;}
    FMCPerkVFXGameplayValues V;V.Direction=Direction;V.Intensity=Applied;V.Radius=BurnRadius;V.Duration=.15f;V.LocalStateAgeSeconds=0;
    V.bHasEndpoints=true;V.SourcePosition=GetActorLocation();V.TargetPosition=Point;
    FVector Eye=Point+FVector(-1,0,1);
    if(const auto* PC=GetWorld()->GetFirstPlayerController();PC&&PC->PlayerCameraManager)Eye=PC->PlayerCameraManager->GetCameraLocation();
    const FVector View=(Eye-FVector(Point)).GetSafeNormal(),Right=FVector::CrossProduct(FVector::UpVector,View).GetSafeNormal(),Up=FVector::CrossProduct(View,Right).GetSafeNormal();
    AMCReactionVFX::AppendNativeContext(V.NativeInputs,0,.15f,BurnRadius,Point,Direction,View,Right,Up,FVector::OneVector,false,!View.IsNearlyZero()&&!Right.IsNearlyZero()&&!Up.IsNearlyZero(),false,false,false,false,Applied);
    const auto Result=Source->PlayLocalSourceEvent(ID,TEXT("Contact"),FTransform(Direction.Rotation(),Point),V);
    if(Result==EMCVFXPresentationResult::Rejected)UE_LOG(LogTemp,Warning,TEXT("MC_FIRE_CONTACT_REJECTED: %s"),*Source->GetLastError());
}
void AMCFirePatch::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCFirePatch,BurnRadius);DOREPLIFETIME(AMCFirePatch,BurnSeconds);DOREPLIFETIME(AMCFirePatch,Batch);DOREPLIFETIME(AMCFirePatch,StartedAt);DOREPLIFETIME(AMCFirePatch,Tongue);DOREPLIFETIME(AMCFirePatch,SurfaceAnchor);DOREPLIFETIME(AMCFirePatch,Lesion);DOREPLIFETIME(AMCFirePatch,Heat);DOREPLIFETIME(AMCFirePatch,ExtinguishSeconds);DOREPLIFETIME(AMCFirePatch,bCreateLesion);}
