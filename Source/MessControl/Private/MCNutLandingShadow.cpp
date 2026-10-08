#include "MCNutLandingShadow.h"

#include "MCFoodActor.h"
#include "MCNutCombatEffect.h"
#include "MCNutRainEvent.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

AMCNutLandingShadow::AMCNutLandingShadow()
{
    bReplicates=true; bAlwaysRelevant=true; SetNetUpdateFrequency(10);
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.05f;
    Shadow=CreateDefaultSubobject<UDecalComponent>(TEXT("LandingShadow")); SetRootComponent(Shadow);
    Shadow->SetFadeScreenSize(.0001f); Shadow->SetSortOrder(2); Shadow->SetVisibility(false);
    ShadowMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/M_NutLandingShadow.M_NutLandingShadow")));
}

double AMCNutLandingShadow::ServerTime() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}

void AMCNutLandingShadow::Configure(AMCTongue* OnTongue,AMCFoodActor* Food,FVector Landing,float InFlightSeconds,float InRadius,float InOpacity,float InDamage,float InPushSpeed)
{
    if(!HasAuthority() || !IsValid(OnTongue) || !IsValid(Food) || Landing.ContainsNaN()) return;
    Tongue=OnTongue; TrackedFood=Food;
    SurfaceAnchor=Tongue->GetActorTransform().InverseTransformPosition(Landing);
    FlightSeconds=FMath::IsFinite(InFlightSeconds)?FMath::Clamp(InFlightSeconds,.75f,3.f):1.65f;
    Radius=FMath::IsFinite(InRadius)?FMath::Clamp(InRadius,35.f,480.f):300.f;
    MaxOpacity=FMath::IsFinite(InOpacity)?FMath::Clamp(InOpacity,.03f,.35f):.20f;
    ImpactDamage=FMath::IsFinite(InDamage)?FMath::Clamp(InDamage,0.f,40.f):18.f;
    ImpactPushSpeed=FMath::IsFinite(InPushSpeed)?FMath::Clamp(InPushSpeed,0.f,500.f):260.f;
    ImpactAt=ServerTime()+FlightSeconds; ForceNetUpdate();
}

void AMCNutLandingShadow::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer) {
        if(auto* Material=ShadowMaterial.LoadSynchronous()) {
            Shadow->SetDecalMaterial(Material); ShadowMID=Shadow->CreateDynamicMaterialInstance();
        }
    }
    if(HasAuthority()) {
        if(TrackedFood.IsValid()) {
            TrackedFood->ManageEntryImpact();
            TrackedFood->OnEntryLanding.AddUObject(this,&AMCNutLandingShadow::Landed);
        }
        SetLifeSpan(FlightSeconds+1.25f);
    }
    RefreshShadow();
}

float AMCNutLandingShadow::SecondsToImpact() const
{
    return float(ImpactAt-ServerTime());
}

bool AMCNutLandingShadow::IsWarningActive() const
{
    return !bImpacted && IsValid(Tongue) && !Tongue->IsActorBeingDestroyed() && SecondsToImpact()>0 && !IsActorBeingDestroyed();
}

float AMCNutLandingShadow::GetShadowStrength() const
{
    const float Remaining=SecondsToImpact();
    if(bImpacted) return 0;
    if(Remaining<-.15f || Remaining>FlightSeconds) return 0;
    const float Progress=FMath::Clamp(1.f-Remaining/FMath::Max(.75f,FlightSeconds),0.f,1.f);
    const float FadeIn=FMath::Clamp(Progress*10.f,0.f,1.f);
    const float FadeOut=FMath::Clamp((Remaining+.15f)/.15f,0.f,1.f);
    return MaxOpacity*FMath::Lerp(.40f,1.f,Progress)*FadeIn*FadeOut;
}

void AMCNutLandingShadow::RefreshShadow()
{
    FHitResult Floor;
    if(!IsValid(Tongue) || !Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(SurfaceAnchor),Floor)) {
        Shadow->SetVisibility(false); return;
    }
    SetActorLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*3.f,Floor.ImpactNormal.Rotation());
    const float Progress=FMath::Clamp(1.f-SecondsToImpact()/FMath::Max(.75f,FlightSeconds),0.f,1.f);
    // The outline always advertises the exact radius; only its fill counts down.
    // Projection depth covers the curved tongue rather than clipping its edges.
    Shadow->DecalSize=FVector(100.f,Radius,Radius);
    Shadow->UpdateBounds(); Shadow->MarkRenderTransformDirty();
    Shadow->SetVisibility(ShadowMID && GetShadowStrength()>0);
    if(ShadowMID) {
        ShadowMID->SetScalarParameterValue(TEXT("ShadowStrength"),GetShadowStrength());
        ShadowMID->SetScalarParameterValue(TEXT("LandingProgress"),Progress);
    }
}

void AMCNutLandingShadow::Landed(const FHitResult& Hit)
{
    if(!HasAuthority() || bImpacted || !TrackedFood.IsValid() || !IsValid(Tongue)
        || Hit.GetActor()!=Tongue || Tongue->IsActorBeingDestroyed()) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost)) return;
    FHitResult Floor;
    if(!Tongue->SurfacePoint(TrackedFood->GetActorLocation(),Floor)) return;
    bImpacted=true; ImpactAt=ServerTime();
    SurfaceAnchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    ForceNetUpdate(); RefreshShadow(); SetLifeSpan(.3f);
    const FVector Center=Floor.ImpactPoint,Up=Floor.ImpactNormal;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Hero=*It;
        if(!Hero->Status || !Hero->Status->IsAlive() || Hero->IsActorBeingDestroyed() || Hero->IsMimicCaptured()) continue;
        const FVector Delta=Hero->GetActorLocation()-Center;
        const float Reach=Radius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(FVector::VectorPlaneProject(Delta,Up).SizeSquared()>FMath::Square(Reach)
            || FMath::Abs(FVector::DotProduct(Feet-Center,Up))>120) continue;
        FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCNutLandingContact),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(TrackedFood.Get()); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Center+Up*45,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        FVector Direction=Delta.GetSafeNormal2D(); if(Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
        const bool Damaged=Hero->Status->Damage(ImpactDamage,Direction);
        if((Damaged || ImpactDamage<=0) && Hero->ToothPhysics && ImpactPushSpeed>0)
            Hero->ToothPhysics->ApplyHit(Direction*ImpactPushSpeed+Up*55,Hero->GetActorLocation());
    }
    // The independent replicated cue survives this warning's immediate expiry.
    if(auto* Event=Cast<AMCNutRainEvent>(GetOwner()))
        if(auto* Effect=AMCNutCombatEffect::Spawn(Event,Tongue,EMCNutCombatCue::EntranceImpact,Center,Center,Radius,0,.65f,int32(GetUniqueID())))
            Event->TrackEncounterActor(Effect);
}

void AMCNutLandingShadow::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(HasAuthority() && (!IsValid(Tongue) || !TrackedFood.IsValid() || (!bImpacted && !TrackedFood->IsMouthEntryActive()))) { Destroy(); return; }
    RefreshShadow();
}

void AMCNutLandingShadow::EndPlay(const EEndPlayReason::Type Reason)
{
    if(TrackedFood.IsValid()) TrackedFood->OnEntryLanding.RemoveAll(this);
    Super::EndPlay(Reason);
}

void AMCNutLandingShadow::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCNutLandingShadow,Tongue); DOREPLIFETIME(AMCNutLandingShadow,SurfaceAnchor);
    DOREPLIFETIME(AMCNutLandingShadow,ImpactAt); DOREPLIFETIME(AMCNutLandingShadow,FlightSeconds);
    DOREPLIFETIME(AMCNutLandingShadow,Radius); DOREPLIFETIME(AMCNutLandingShadow,MaxOpacity);
    DOREPLIFETIME(AMCNutLandingShadow,bImpacted);
}
