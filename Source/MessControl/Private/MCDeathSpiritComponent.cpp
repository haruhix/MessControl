#include "MCDeathSpiritComponent.h"
#include "MCDeathRespawnPresentation.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "MCPlayerNameComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCWorldPresentationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
constexpr float RiseDuration=.42f;
constexpr float RiseHeight=98.f;
constexpr float SpiritOpacity=.40f;
const FLinearColor SpiritColor(.10f,.62f,1.05f);
const FName SpiritSource(TEXT("Character.DeathSpirit"));
const FName SpiritOpacityParam(TEXT("Opacity"));
const FName SpiritColorParam(TEXT("Color"));
const FName SpiritWhiteoutParam(TEXT("Whiteout"));
const FSoftObjectPath SpiritSurfacePath(TEXT("/Game/Gameplay/VFX/DeathSpirit/M_DeathSpirit_Translucent.M_DeathSpirit_Translucent"));
}

UMCDeathSpiritComponent::UMCDeathSpiritComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
}

void UMCDeathSpiritComponent::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer || !GetWorld() || !GetWorld()->GetGameState<AMCGameState>()) return;
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    auto* Model=Hero&&Hero->GetMesh()?Hero->GetMesh()->GetSkeletalMeshAsset():nullptr;
    auto* Surface=Cast<UMaterialInterface>(SpiritSurfacePath.TryLoad());
    if(!Model || !Surface) return;

    // Freeze the actual equipped skeletal asset in its authored neutral pose.
    // Prepare its material instance while the pawn is joining the arena.
    SpiritStandingTransform=Hero->StandingMeshTransform();
    SpiritMaterial=UMaterialInstanceDynamic::Create(Surface,this,TEXT("DeathSpiritSurface"));
    SpiritMaterial->SetVectorParameterValue(SpiritColorParam,SpiritColor);
    SpiritMaterial->SetScalarParameterValue(SpiritOpacityParam,0.f);
    SpiritMesh=NewObject<UPoseableMeshComponent>(GetOwner(),TEXT("SpiritMesh"));
    SpiritMesh->ComponentTags.Add(TEXT("MC.DeathSpirit"));
    SpiritMesh->SetMobility(EComponentMobility::Movable);
    SpiritMesh->SetSkinnedAssetAndUpdate(Model);
    SpiritMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SpiritMesh->SetGenerateOverlapEvents(false);
    SpiritMesh->SetCanEverAffectNavigation(false);
    SpiritMesh->SetCastShadow(false);
    SpiritMesh->bReceivesDecals=false;
    SpiritMesh->SetVisibility(false);
    for(int32 Slot=0;Slot<SpiritMesh->GetNumMaterials();++Slot) SpiritMesh->SetMaterial(Slot,SpiritMaterial);
    SpiritMesh->RegisterComponent();
    OnRep_State();
}

void UMCDeathSpiritComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(SpiritMesh) SpiritMesh->DestroyComponent();
    Super::EndPlay(Reason);
}

void UMCDeathSpiritComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UMCDeathSpiritComponent,State);
}

double UMCDeathSpiritComponent::Clock() const
{
    const UWorld* World=GetWorld();
    if(!World) return 0.;
    const AGameStateBase* Game=World->GetGameState();
    return Game?Game->GetServerWorldTimeSeconds():World->GetTimeSeconds();
}

void UMCDeathSpiritComponent::StartSpirit(double DeathTime,FVector Origin)
{
    if(!GetOwner()->HasAuthority() || IsSpiritActive() || !FMath::IsFinite(DeathTime) || Origin.ContainsNaN()) return;
    State=FMCDeathSpiritState();
    State.DeathTime=DeathTime;
    State.Origin=Origin;
    State.FacingYaw=GetOwner()->GetActorRotation().Yaw;
    OnRep_State();
    GetOwner()->ForceNetUpdate();
}

void UMCDeathSpiritComponent::StartPull(double StartedAt,double ReleaseAt,FVector Target,AMCArenaTooth* Reserve,FVector ExitPosition)
{
    if(!GetOwner()->HasAuthority() || !IsSpiritActive() || IsPulling() || !FMath::IsFinite(StartedAt)
        || !FMath::IsFinite(ReleaseAt) || ReleaseAt<=StartedAt || Target.ContainsNaN() || ExitPosition.ContainsNaN()) return;
    State.PullOrigin=State.Origin+HoverOffset(StartedAt);
    State.PullTarget=Target;
    State.PullReserve=Reserve;
    State.ReleasePosition=ExitPosition;
    State.PullStartedAt=StartedAt;
    State.PullReleaseAt=ReleaseAt;
    OnRep_State();
    GetOwner()->ForceNetUpdate();
}

void UMCDeathSpiritComponent::CancelPull()
{
    if(!GetOwner()->HasAuthority() || !IsPulling()) return;
    const double Now=Clock();
    State.Origin=GetSpiritLocation()-HoverOffset(Now);
    State.PullStartedAt=-1.;
    State.PullReleaseAt=-1.;
    State.PullReserve=nullptr;
    OnRep_State();
    GetOwner()->ForceNetUpdate();
}

void UMCDeathSpiritComponent::EndSpirit(TOptional<FVector> ReleasePosition)
{
    if(!GetOwner()->HasAuthority() || !IsSpiritActive()) return;
    if(ReleasePosition.IsSet() && !ReleasePosition->ContainsNaN()) {
        State.ReleasePosition=ReleasePosition.GetValue();
        State.ReleasedAt=Clock();
    }
    State.bRetired=true;
    OnRep_State();
    GetOwner()->ForceNetUpdate();
}

FVector UMCDeathSpiritComponent::HoverOffset(double Now) const
{
    const float Age=FMath::Max(0.f,float(Now-State.DeathTime));
    const float Lift=FMath::SmoothStep(0.f,RiseDuration,Age);
    return FVector(5.f*FMath::Sin(3.f*Age),6.f*FMath::Sin(4.f*Age),RiseHeight+7.f*FMath::Sin(5.f*Age))*Lift;
}

float UMCDeathSpiritComponent::PullProgress(double Now) const
{
    const double Travel=FMath::Max(.01,State.PullReleaseAt-State.PullStartedAt-MCDeathRespawnPresentation::MergeLead);
    return FMath::Clamp(float((Now-State.PullStartedAt)/Travel),0.f,1.f);
}

float UMCDeathSpiritComponent::ExitProgress(double Now) const
{
    const double Remaining=FMath::Clamp((State.PullReleaseAt-Now)/MCDeathRespawnPresentation::SpitLead,0.,1.);
    return float(1.-Remaining*Remaining*Remaining);
}

FVector UMCDeathSpiritComponent::PullTarget() const
{
    return IsValid(State.PullReserve) && State.PullReserve->IsAvailable()
        ?State.PullReserve->RespawnMergeCenter():State.PullTarget;
}

FVector UMCDeathSpiritComponent::GetSpiritLocation() const
{
    if(!IsSpiritActive()) return GetOwner()?GetOwner()->GetActorLocation():FVector::ZeroVector;
    const double Now=Clock();
    if(!IsPulling()) return State.Origin+HoverOffset(Now);
    const FVector Crown=PullTarget();
    const float Exit=ExitProgress(Now);
    if(Exit>0.f) return FMath::Lerp(Crown,State.ReleasePosition,Exit);

    // Hold the outer silhouette in view as it starts to whiten, then enter
    // the crown. A single eased path to the center hides the early white cue.
    if(IsValid(State.PullReserve) && State.PullReserve->Body) {
        const FVector ToOrigin=State.PullOrigin-Crown;
        const FVector Direction=ToOrigin.GetSafeNormal2D();
        const UBoxComponent* Box=State.PullReserve->Body;
        const FVector LocalDirection=Box->GetComponentQuat().UnrotateVector(Direction);
        const FVector Extent=Box->GetScaledBoxExtent();
        const double Edge=FMath::Min(Extent.X/FMath::Max(FMath::Abs(LocalDirection.X),.001),
            Extent.Y/FMath::Max(FMath::Abs(LocalDirection.Y),.001));
        const double StandOff=FMath::Min(ToOrigin.Size2D()*.86,Edge*1.24+25.);
        const FVector Approach=Crown+Direction*StandOff+FVector(0,0,40);
        const double WhiteStarts=State.PullReleaseAt-MCDeathRespawnPresentation::WhiteLead;
        const double EnterAt=WhiteStarts+.035;
        const double MergeAt=State.PullReleaseAt-MCDeathRespawnPresentation::MergeLead;
        const double ApproachTime=FMath::Max(.01,EnterAt-State.PullStartedAt);
        const double EntryTime=FMath::Max(.01,MergeAt-EnterAt);
        const double ApproachSpeed=1.5*FVector::Dist2D(State.PullOrigin,Approach)/ApproachTime;
        const double EntrySpeed=1.5*StandOff/EntryTime;
        const FVector EntryVelocity=-Direction*FMath::Min(ApproachSpeed,EntrySpeed);
        if(Now>=EnterAt) {
            const double Alpha=FMath::Clamp((Now-EnterAt)/EntryTime,0.,1.);
            return FMath::CubicInterp(Approach,EntryVelocity*EntryTime,Crown,FVector::ZeroVector,Alpha);
        }
        const double Alpha=FMath::Clamp((Now-State.PullStartedAt)/ApproachTime,0.,1.);
        const double Arc=FMath::Square(FMath::Sin(Alpha*PI))*42.;
        return FMath::CubicInterp(State.PullOrigin,FVector::ZeroVector,Approach,EntryVelocity*ApproachTime,Alpha)+FVector(0,0,Arc);
    }
    const float Pull=FMath::InterpEaseInOut(0.f,1.f,PullProgress(Now),1.65f);
    return FMath::Lerp(State.PullOrigin,Crown,Pull)+FVector(0,0,FMath::Sin(Pull*PI)*66.f);
}

float UMCDeathSpiritComponent::GetDeathFilterStrength() const
{
    return IsSpiritActive()?FMath::SmoothStep(0.f,.24f,float(Clock()-State.DeathTime)):0.f;
}

void UMCDeathSpiritComponent::OnRep_State()
{
    const double Now=Clock();
    UpdateBody(Now);
    SetComponentTickEnabled(IsSpiritActive() || bHidBody || bHidNameLabel || bDisabledBodyCollision);
    UpdateVisual(Now);
    PresentAcceptedEvent(Now);
}

void UMCDeathSpiritComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    UpdateSteering(DeltaTime);
    const double Now=Clock();
    UpdateBody(Now);
    UpdateVisual(Now);
    if(IsPulling()) PresentAcceptedEvent(Now);
    if(!IsSpiritActive() && !bHidBody && !bHidNameLabel && !bDisabledBodyCollision) SetComponentTickEnabled(false);
}

void UMCDeathSpiritComponent::UpdateBody(double Now)
{
    auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    if(!Hero || !Hero->Status) return;
    const bool bAlive=Hero->Status->IsAlive();
    if(auto* Name=Hero->PlayerNameLabel.Get()) {
        if(bAlive && bHidNameLabel) { Name->SetHiddenInGame(false); bHidNameLabel=false; }
        if(!bAlive && State.DeathTime>=0. && !Name->bHiddenInGame) { Name->SetHiddenInGame(true); bHidNameLabel=true; }
    }
    auto* Mesh=Hero?Hero->GetMesh():nullptr;
    if(!Mesh || Hero->IsFreezingToDeath()) return;
    if(bAlive) {
        if(bHidBody) Mesh->SetHiddenInGame(false,true);
        if(bDisabledBodyCollision && Mesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision
            && Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) {
            Mesh->SetCollisionEnabled(BodyCollision);
            if(bBodyWasSimulating) Mesh->SetAllBodiesSimulatePhysics(true);
        }
        bHidBody=false;
        bDisabledBodyCollision=false;
        return;
    }
    if(State.DeathTime<0. || (!State.bRetired && Now-State.DeathTime<.20)) return;
    if(!Mesh->bHiddenInGame) {
        Mesh->SetHiddenInGame(true,true);
        bHidBody=true;
    }
    if(Hero->HasAuthority() && !bDisabledBodyCollision && Mesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision) {
        BodyCollision=Mesh->GetCollisionEnabled();
        bBodyWasSimulating=Mesh->IsAnySimulatingPhysics();
        Mesh->SetAllBodiesSimulatePhysics(false);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        bDisabledBodyCollision=true;
    }
}

void UMCDeathSpiritComponent::UpdateVisual(double Now)
{
    if(!SpiritMesh) return;
    SpiritMesh->SetVisibility(IsSpiritActive());
    const float Age=FMath::Max(0.f,float(Now-State.DeathTime));
    if(!IsSpiritActive()) return;

    const float Materialize=FMath::SmoothStep(0.f,.28f,Age);
    const float Pull=IsPulling()?PullProgress(Now):0.f;
    const float Exit=IsPulling()?ExitProgress(Now):0.f;
    const float White=IsPulling()?MCDeathRespawnPresentation::WhiteAlpha(Now,State.PullReleaseAt):0.f;
    const float Collapse=IsPulling()?MCDeathRespawnPresentation::CollapseAlpha(Now,State.PullReleaseAt):0.f;
    const double Facing=State.FacingYaw+8.f*FMath::Sin(Age*3.f);
    const FVector Crown=PullTarget();
    const FVector Travel=Crown-State.PullOrigin;
    const double PullYaw=Travel.IsNearlyZero()?Facing:Travel.Rotation().Yaw;
    double Yaw=Facing+(IsPulling()?FMath::FindDeltaAngleDegrees(Facing,PullYaw)*FMath::SmoothStep(0.f,.36f,Pull):0.);
    if(Exit>0.f) Yaw+=FMath::FindDeltaAngleDegrees(Yaw,(State.ReleasePosition-Crown).Rotation().Yaw)*Exit;
    const FRotator Rotation(-9.f*FMath::Sin(Pull*PI),Yaw,4.f*FMath::Sin(Age*4.f)*(1.f-White));
    const float Scale=FMath::Lerp(.72f,.98f,Materialize)*(1.f-.68f*Collapse*(1.f-Exit));
    const FVector Center=GetSpiritLocation();
    const FTransform BodyFrame(Rotation,Center,GetOwner()->GetActorScale3D()*Scale);
    FTransform MeshFrame=SpiritStandingTransform*BodyFrame;
    const float CenterWeight=FMath::SmoothStep(.05f,.42f,Pull)*(1.f-Exit);
    if(CenterWeight>0.f && SpiritMesh->GetSkinnedAsset()) {
        const FVector VisibleCenter=MeshFrame.TransformPosition(SpiritMesh->GetSkinnedAsset()->GetBounds().Origin);
        MeshFrame.AddToTranslation((Center-VisibleCenter)*CenterWeight);
    }
    SpiritMesh->SetWorldTransform(MeshFrame);
    SpiritMaterial->SetScalarParameterValue(SpiritWhiteoutParam,White);
    SpiritMaterial->SetScalarParameterValue(SpiritOpacityParam,FMath::Lerp(SpiritOpacity,.58f,White)*Materialize);
}

void UMCDeathSpiritComponent::PresentAcceptedEvent(double Now)
{
    if(!SpiritMesh || State.DeathTime<0.) return;
    const auto Pulse=[&](FName Event,const FTransform& Frame,FVector Start,FVector End,const FLinearColor& Color,double Began,float Duration) {
        FMCPerkVFXGameplayValues Values;
        Values.SourcePosition=Start;
        Values.TargetPosition=End;
        Values.bHasEndpoints=true;
        Values.Direction=(End-Start).GetSafeNormal();
        Values.Color=Color;
        Values.bHasColor=true;
        Values.LocalStateAgeSeconds=FMath::Max(0.f,float(Now-Began));
        if(Values.LocalStateAgeSeconds<Duration)
            MCWorldPresentation::Pulse(GetOwner(),SpiritSource,Event,Frame,Values,Duration);
    };
    if(PresentedDeathTime!=State.DeathTime) {
        PresentedDeathTime=State.DeathTime;
        PresentedMergeForRelease=-1.;
        bPresentedRelease=false;
        if(!State.bRetired) {
            const FTransform Frame(GetOwner()->GetActorQuat(),State.Origin);
            Pulse(TEXT("Rise"),Frame,State.Origin,State.Origin+FVector(0,0,RiseHeight),SpiritColor,State.DeathTime,RiseDuration);
        }
    }
    if(IsPulling()) {
        const double MergeStartsAt=State.PullReleaseAt-MCDeathRespawnPresentation::WhiteLead;
        if(Now>=MergeStartsAt && Now<State.PullReleaseAt && PresentedMergeForRelease!=State.PullReleaseAt) {
            PresentedMergeForRelease=State.PullReleaseAt;
            const FVector Crown=PullTarget();
            const FTransform Frame(GetOwner()->GetActorQuat(),Crown);
            Pulse(TEXT("Merge"),Frame,GetSpiritLocation(),Crown,FLinearColor::White,MergeStartsAt,
                float(MCDeathRespawnPresentation::WhiteLead));
        }
    } else if(PresentedMergeForRelease>=0. && State.ReleasedAt<0.) {
        // A damaged crown or failed spawn cancels the matching presentation.
        PresentedMergeForRelease=-1.;
        if(auto* Presentation=GetOwner()->FindComponentByClass<UMCWorldPresentationComponent>())
            Presentation->StopStates(SpiritSource);
    }
    if(State.bRetired && !bPresentedRelease && State.ReleasedAt>=0.) {
        bPresentedRelease=true;
        const FVector ExitDirection=(State.ReleasePosition-State.PullTarget).GetSafeNormal2D(UE_SMALL_NUMBER,GetOwner()->GetActorForwardVector());
        const FTransform Frame(ExitDirection.Rotation(),State.ReleasePosition-FVector(0,0,22));
        Pulse(TEXT("Release"),Frame,State.PullTarget,State.ReleasePosition,FLinearColor(1.f,.74f,.42f),State.ReleasedAt,.55f);
    }
}
