#include "MCNutLandingShadow.h"

#include "MCFoodActor.h"
#include "MCNutCombatEffect.h"
#include "MCNutRainEvent.h"
#include "MCVFXPresentationComponent.h"
#include "MCNutVFXCatalog.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
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
#include "UObject/Package.h"

AMCNutLandingShadow::AMCNutLandingShadow()
{
    bReplicates=true; bAlwaysRelevant=true; SetNetUpdateFrequency(10);
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.05f;
    Shadow=CreateDefaultSubobject<UDecalComponent>(TEXT("LandingShadow")); SetRootComponent(Shadow);
    Shadow->SetFadeScreenSize(.0001f); Shadow->SetSortOrder(2); Shadow->SetVisibility(false);
    Presentation=CreateDefaultSubobject<UMCVFXPresentationComponent>(TEXT("NutEntryPresentation"));
    Presentation->SourceID=TEXT("Event.NutRain.Entry");Presentation->Events={TEXT("Approach"),TEXT("Impact")};
    ShadowMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/M_NutLandingShadow.M_NutLandingShadow")));
}

double AMCNutLandingShadow::ServerTime() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}

void AMCNutLandingShadow::Configure(AMCTongue* OnTongue,AMCFoodActor* Food,FVector Landing,float InFlightSeconds,float InRadius,float InOpacity,float InDamage,float InPushSpeed,FName OriginSource,UMCVFXPresentationComponent* PreviewSource)
{
    if(!HasAuthority() || !IsValid(OnTongue) || !IsValid(Food) || Landing.ContainsNaN()) return;
    Tongue=OnTongue; TrackedFood=Food;PresentationSource=OriginSource;
    if(const auto* Contract=MCNutVFX::Find(PresentationSource)) {
        Presentation->SourceID=PresentationSource;Presentation->Events=Contract->Events;
        FString SocketError;Presentation->BindSourceAudioSocket(PresentationSource,Food->Visual,SocketError);
        if(IsValid(PreviewSource) && PreviewSource->IsSourcePrepared(PresentationSource)) {
            FString CopyError;PreviewSource->CopyPreparedSourceTo(PresentationSource,Presentation,CopyError);
        }
        if(PresentationSource==MCNutVFX::GravityWavesSource()) {
            FString Error;
            MCNutVFX::Prepare(Presentation,PresentationSource,Error,false);
        }
    }
    SurfaceAnchor=Tongue->GetActorTransform().InverseTransformPosition(Landing);
    FlightSeconds=FMath::IsFinite(InFlightSeconds)?FMath::Clamp(InFlightSeconds,.75f,3.f):1.65f;
    Radius=FMath::IsFinite(InRadius)?FMath::Clamp(InRadius,35.f,480.f):300.f;
    MaxOpacity=FMath::IsFinite(InOpacity)?FMath::Clamp(InOpacity,.03f,.35f):.20f;
    ImpactDamage=FMath::IsFinite(InDamage)?FMath::Clamp(InDamage,0.f,40.f):18.f;
    ImpactPushSpeed=FMath::IsFinite(InPushSpeed)?FMath::Clamp(InPushSpeed,0.f,500.f):260.f;
    ImpactAt=ServerTime()+FlightSeconds; ForceNetUpdate();
}

void AMCNutLandingShadow::ConfigureWizardGravityLift(float LiftAndHoldSeconds)
{
    if(HasAuthority() && PresentationSource==MCNutVFX::GravityWavesSource() && FMath::IsFinite(LiftAndHoldSeconds))
        WizardGravityLiftSeconds=FMath::Clamp(LiftAndHoldSeconds,.05f,FlightSeconds);
}

void AMCNutLandingShadow::UpdateWizardGravityDropTime(float Seconds)
{
    if(!HasAuthority() || WizardGravityLiftSeconds<=0.f || !FMath::IsFinite(Seconds))return;
    WizardGravityLiftSeconds=0.f;
    const float Remaining=FMath::Clamp(Seconds,.05f,3.f);
    ImpactAt=ServerTime()+Remaining;
    SetLifeSpan(Remaining+1.25f);
    RefreshShadow();
    ForceNetUpdate();
}

void AMCNutLandingShadow::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer) {
        if(auto* Material=ShadowMaterial.LoadSynchronous()) {
            Shadow->SetDecalMaterial(Material); ShadowMID=Shadow->CreateDynamicMaterialInstance();
        }
    }
    if(const auto* Contract=MCNutVFX::Find(PresentationSource)) {
        Presentation->SourceID=PresentationSource;Presentation->Events=Contract->Events;
        FString Error;
        if(IsValid(TrackedFood.Get()))Presentation->BindSourceAudioSocket(PresentationSource,TrackedFood->Visual,Error);
        MCNutVFX::Prepare(Presentation,PresentationSource,Error,false);
    }
    if(HasAuthority()) {
        if(IsValid(TrackedFood)) {
            TrackedFood->ManageEntryImpact();
            TrackedFood->OnEntryLanding.AddUObject(this,&AMCNutLandingShadow::Landed);
        }
        if(!bDeferIsolatedPreviewPresentation)SetLifeSpan(FlightSeconds+1.25f);
    }
    RefreshShadow();
}

float AMCNutLandingShadow::SecondsToImpact() const
{
    return float(ImpactAt-ServerTime());
}

bool AMCNutLandingShadow::IsWarningActive() const
{
    return !bDeferIsolatedPreviewPresentation && !bImpacted && IsValid(Tongue) && !Tongue->IsActorBeingDestroyed() && SecondsToImpact()>0 && !IsActorBeingDestroyed();
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
    if(bDeferIsolatedPreviewPresentation) {Shadow->SetVisibility(false);return;}
    FHitResult Floor;
    if(!IsValid(Tongue) || !Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(SurfaceAnchor),Floor)) {
        Shadow->SetVisibility(false);
        if(Presentation)Presentation->StopLocalSourceStates(PresentationSource);
        return;
    }
    SetActorLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*3.f,Floor.ImpactNormal.Rotation());
    const float Progress=FMath::Clamp(1.f-SecondsToImpact()/FMath::Max(.75f,FlightSeconds),0.f,1.f);
    // The native footprint is protected gameplay geometry; only decorative accents are authorable.
    if(Presentation) {
        FString Error;
        if(IsValid(TrackedFood.Get()))Presentation->BindSourceAudioSocket(PresentationSource,TrackedFood->Visual,Error);
        MCNutVFX::Prepare(Presentation,PresentationSource,Error,false);
        FMCPerkVFXGameplayValues Values;Values.Radius=Radius;Values.Fill=Progress;Values.Duration=FlightSeconds;
        Values.Direction=Floor.ImpactNormal;Values.SourcePosition=Floor.ImpactPoint;Values.TargetPosition=Floor.ImpactPoint;Values.bHasEndpoints=true;
        Values.LocalStateAgeSeconds=FMath::Clamp(FlightSeconds-SecondsToImpact(),0.f,FlightSeconds);
        const bool Active=IsWarningActive();
        Presentation->UpdateLocalSourceState(PresentationSource,TEXT("Approach"),Active,FTransform(Floor.ImpactNormal.Rotation(),Floor.ImpactPoint),Values,nullptr,Error);
        if(PresentationSource==FName(TEXT("Boss.NutMage.NutRain"))) {
            Values.VisualSubject=IsValid(TrackedFood)?TrackedFood->Visual.Get():nullptr;
            Values.FrameComponent=IsValid(TrackedFood)?TrackedFood->Body.Get():nullptr;Values.FrameSocket=NAME_None;
            if(IsValid(TrackedFood))Values.Direction=TrackedFood->GetVelocity().GetSafeNormal();
            Presentation->UpdateLocalSourceState(PresentationSource,TEXT("DropTrail"),Active && IsValid(TrackedFood),
                IsValid(TrackedFood)?TrackedFood->GetActorTransform():FTransform::Identity,Values,IsValid(TrackedFood)?TrackedFood->Body.Get():nullptr,Error);
        } else if(PresentationSource==MCNutVFX::GravityWavesSource()) {
            const bool bLiftActive=Active && WizardGravityLiftSeconds>0.f && IsValid(TrackedFood);
            if(bLiftActive) {
                Values.VisualSubject=TrackedFood->Visual.Get();
                Values.FrameComponent=TrackedFood->Body.Get();
                Values.TargetPosition=TrackedFood->GetActorLocation();
                Values.Radius=FMath::Clamp(float(TrackedFood->Body->Bounds.SphereRadius),25.f,160.f);
                Values.Duration=WizardGravityLiftSeconds;
                Values.LocalStateAgeSeconds=FMath::Clamp(FlightSeconds-SecondsToImpact(),0.f,WizardGravityLiftSeconds);
                Values.Fill=Values.LocalStateAgeSeconds/WizardGravityLiftSeconds;
                auto AddFloat=[&](FName Name,float Value) {
                    auto& Input=Values.NativeInputs.Entries.Emplace_GetRef();
                    Input.Name=Name;Input.Type=EMCVFXNativeInputType::Float;Input.FloatValue=Value;
                };
                auto AddVector=[&](FName Name,const FVector& Value,bool bPosition=false) {
                    auto& Input=Values.NativeInputs.Entries.Emplace_GetRef();Input.Name=Name;
                    Input.Type=bPosition?EMCVFXNativeInputType::Position:EMCVFXNativeInputType::Vector;
                    if(bPosition)Input.PositionValue=Value;else Input.VectorValue=Value;
                };
                const FVector Up=Floor.ImpactNormal;
                const FVector Right=FVector::VectorPlaneProject(FVector::ForwardVector,Up).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
                AddFloat(TEXT("User.Native.CueAge"),Values.LocalStateAgeSeconds);
                AddFloat(TEXT("User.Native.WindupSeconds"),0.f);
                AddFloat(TEXT("User.Native.ActiveSeconds"),WizardGravityLiftSeconds);
                AddVector(TEXT("User.Native.ActorPosition"),Values.TargetPosition,true);
                AddVector(TEXT("User.Native.GroundNormal"),Up);
                AddVector(TEXT("User.Native.SurfaceRight"),Right);
                AddVector(TEXT("User.Native.SurfaceSide"),FVector::CrossProduct(Up,Right));
            }
            Presentation->UpdateLocalSourceState(PresentationSource,TEXT("NutLift"),bLiftActive,
                bLiftActive?TrackedFood->GetActorTransform():FTransform::Identity,Values,bLiftActive?TrackedFood->Body.Get():nullptr,Error);
        }
    }
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

bool AMCNutLandingShadow::StartIsolatedPreviewPresentation()
{
#if WITH_EDITOR
    if(!HasAuthority() || !GetWorld() || GetWorld()->WorldType!=EWorldType::Game
        || GetWorld()->GetOutermost()!=GetTransientPackage() || !bDeferIsolatedPreviewPresentation)return false;
    bDeferIsolatedPreviewPresentation=false;ImpactAt=ServerTime()+FlightSeconds;
    SetLifeSpan(FlightSeconds+1.25f);RefreshShadow();return true;
#else
    return false;
#endif
}

void AMCNutLandingShadow::Landed(const FHitResult& Hit)
{
    if(!HasAuthority() || bImpacted || !IsValid(TrackedFood) || !IsValid(Tongue)
        || Hit.GetActor()!=Tongue || Tongue->IsActorBeingDestroyed()) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost)) return;
    FHitResult Floor;
    if(!Tongue->SurfacePoint(TrackedFood->GetActorLocation(),Floor)) return;
    const bool bGravityDrop=PresentationSource==MCNutVFX::GravityWavesSource();
    const FVector Advertised=Tongue->GetActorTransform().TransformPosition(SurfaceAnchor);
    bImpacted=true; ImpactAt=ServerTime();
    SurfaceAnchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    ForceNetUpdate(); RefreshShadow(); SetLifeSpan(.3f);
    const FVector Center=Floor.ImpactPoint,Up=Floor.ImpactNormal;
    auto* Event=Cast<AMCNutRainEvent>(GetOwner());
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Hero=*It;
        if(!Hero->Status || !Hero->Status->IsAlive() || Hero->IsActorBeingDestroyed() || Hero->IsMimicCaptured()) continue;
        const FVector Delta=Hero->GetActorLocation()-Center;
        const float Reach=Radius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
        // A collision may deflect the nut during flight. The shown footprint remains the limit of its damage.
        if(bGravityDrop && FVector::DistSquared2D(Hero->GetActorLocation(),Advertised)>FMath::Square(Reach)) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(FVector::VectorPlaneProject(Delta,Up).SizeSquared()>FMath::Square(Reach)
            || FMath::Abs(FVector::DotProduct(Feet-Center,Up))>120) continue;
        FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCNutLandingContact),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(TrackedFood.Get()); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Center+Up*45,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        if(bGravityDrop && (!Event || !Event->ClaimWizardGravityWaveHit(this,Hero))) continue;
        FVector Direction=Delta.GetSafeNormal2D(); if(Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
        const bool Damaged=Hero->Status->Damage(ImpactDamage,Direction,this,EMCRogueDamageKind::FallingNut);
        if((Damaged || ImpactDamage<=0) && Hero->ToothPhysics && ImpactPushSpeed>0)
            Hero->ToothPhysics->ApplyHit(Direction*ImpactPushSpeed+Up*55,Hero->GetActorLocation());
    }
    // The independent replicated cue survives this warning's immediate expiry.
    if(Event)
        if(auto* Effect=AMCNutCombatEffect::SpawnPresented(FMCNutCuePresentation(PresentationSource,TEXT("Impact")),this,Tongue,EMCNutCombatCue::EntranceImpact,Center,Center,Radius,0,.65f,int32(GetUniqueID())))
            Event->TrackEncounterActor(Effect);
}

void AMCNutLandingShadow::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(bDeferIsolatedPreviewPresentation)return;
    if(HasAuthority() && (!IsValid(Tongue) || !IsValid(TrackedFood)
        || (!bImpacted && !TrackedFood->IsMouthEntryActive()
            && !(PresentationSource==MCNutVFX::GravityWavesSource() && TrackedFood->IsWizardGravityLiftActive())))) { Destroy(); return; }
    RefreshShadow();
}

void AMCNutLandingShadow::EndPlay(const EEndPlayReason::Type Reason)
{
    if(IsValid(TrackedFood)) TrackedFood->OnEntryLanding.RemoveAll(this);
    if(Presentation)Presentation->StopLocalSourceStates(PresentationSource);
    Super::EndPlay(Reason);
}

void AMCNutLandingShadow::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCNutLandingShadow,Tongue); DOREPLIFETIME(AMCNutLandingShadow,SurfaceAnchor);
    DOREPLIFETIME(AMCNutLandingShadow,ImpactAt); DOREPLIFETIME(AMCNutLandingShadow,FlightSeconds);
    DOREPLIFETIME(AMCNutLandingShadow,Radius); DOREPLIFETIME(AMCNutLandingShadow,MaxOpacity);
    DOREPLIFETIME(AMCNutLandingShadow,bImpacted);
    DOREPLIFETIME(AMCNutLandingShadow,PresentationSource);DOREPLIFETIME(AMCNutLandingShadow,TrackedFood);
    DOREPLIFETIME(AMCNutLandingShadow,WizardGravityLiftSeconds);
}
