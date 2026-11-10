#include "MCNutBoss.h"

#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "MCNutVFXCatalog.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXPresentationLibrary.h"

namespace
{
const FName SpinEvent(TEXT("Spin"));
const FName FlightEvent(TEXT("Flight"));
const FName ReelEvent(TEXT("Reel"));

// V6 evaluated on the imported NutTank skeleton. Socket offsets are in
// hand_r space. The release tip and orbit axes are in mesh space, in cm.
const FVector CowboyEye(-139.2428,-41.9303,-11.3758);
const FVector CowboyDock(-136.9658,-38.9366,-10.9969);
const FVector CowboyReleaseTip(-72.0215,175.7810,161.8855);
const FVector CowboyOrbitU(.6202928,0.,.7843703);
const FVector CowboyOrbitV(0.,1.,0.);
constexpr double CowboyOrbitRadius=73.;
constexpr float CowboyFullOrbit=20.f/41.f;

FMCPerkVFXGameplayValues ChainValues(USkeletalMeshComponent* Subject,FVector Start,FVector End)
{
    FMCPerkVFXGameplayValues Values;
    Values.SourcePosition=Start;
    Values.TargetPosition=End;
    Values.Direction=(End-Start).GetSafeNormal();
    Values.bHasEndpoints=true;
    Values.VisualSubject=Subject;
    return Values;
}
}

bool AMCNutBoss::HasCowboyChainAnchor() const
{
    const auto* Mesh=TankModel?TankModel->GetSkeletalMeshAsset():nullptr;
    const auto* Skeleton=Mesh?Mesh->GetSkeleton():nullptr;
    static const FSoftObjectPath CowboyClip(TEXT("/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutKnight_ChainCowboy.A_NutKnight_ChainCowboy"));
    return Skeleton && Skeleton->GetFName()==TEXT("SK_NutTank_Skeleton")
        && BossSettings.TankChainCowboyAnimation.ToSoftObjectPath()==CowboyClip;
}

FVector AMCNutBoss::GetChainOrigin() const
{
    const FVector Forward=Attack==EMCNutBossAttack::Chain?AttackForward:GetActorForwardVector();
    if(HasCowboyChainAnchor()) {
        const FRotator MeshYaw(0.,BossSettings.TankModelYaw,0.);
        const FVector Pivot(0.,0.,-Settings.BodyRadius-4.);
        const FVector Offset=MeshYaw.RotateVector((CowboyReleaseTip-TankModelGroundAnchor)*TankModelScale)+Pivot;
        // The gameplay sweep uses the measured release point on every peer.
        // It is independent of whether the cosmetic skeleton is ticking.
        return GetActorLocation()+Forward.Rotation().RotateVector(Offset);
    }
    return GetActorLocation()+Forward.Rotation().RotateVector(FVector(1.,.72,.14)*Settings.BodyRadius);
}

void AMCNutBoss::InitializeChainPresentation()
{
    const auto* Contract=MCNutVFX::Find(MCNutVFX::ChainSource());
    ChainPresentation->SourceID=Contract->ID;
    ChainPresentation->Events=Contract->Events;
    ChainPresentation->Library=TSoftObjectPtr<UMCVFXPresentationLibrary>(UMCVFXPresentationLibrary::MouthLibraryPath());
}

bool AMCNutBoss::PrepareChainPresentation(FString& Error,bool bAllowLoad)
{
    const FName Source=MCNutVFX::ChainSource();
    return ChainPresentation->BindSourceAudioSocket(Source,TankModel,Error)
        && MCNutVFX::Prepare(ChainPresentation,Source,Error,bAllowLoad);
}

void AMCNutBoss::PresentChainEvent(FName Stage,FVector Position,FVector End)
{
    FString Error;
    if(!PrepareChainPresentation(Error)) return;
    const bool bCowboy=HasCowboyChainAnchor();
    const FVector Grip=bCowboy?TankModel->GetSocketTransform(TEXT("hand_r"),RTS_World).TransformPosition(CowboyEye)
        :GetChainGripTransform().GetLocation();
    auto Values=ChainValues(TankModel,Grip,End);
    if(Stage==TEXT("Tell")) Values.Duration=float(ResolveAt-StateStartedAt);
    if(Stage==TEXT("Tell") || Stage==SpinEvent || Stage==TEXT("End") || (Stage==TEXT("Release") && !bCowboy)) Position=Grip;
    if(Stage==TEXT("CatchPlayer") || Stage==TEXT("CatchNut")) Position=End;
    const FTransform Frame(Values.Direction.IsNearlyZero()?GetActorRotation():Values.Direction.Rotation(),Position);
    // Accepted sound and telemetry use the receipt. The replicated attack
    // phase owns each peer's continuously updated cable geometry.
    ChainPresentation->PlaySourceEvent(MCNutVFX::ChainSource(),Stage,Frame,Values,Stage!=SpinEvent && Stage!=FlightEvent && Stage!=ReelEvent);
}

void AMCNutBoss::UpdateChainPresentation()
{
    if(GetNetMode()==NM_DedicatedServer) return;
    const FName Source=MCNutVFX::ChainSource();
    const bool bCowboy=HasCowboyChainAnchor();
    const float Alpha=GetChainPhaseAlpha();
    const bool bSpin=bCowboy && Attack==EMCNutBossAttack::Chain && State==EMCNutBossState::Telegraph
        && Alpha>CowboyUnpark;
    const bool bFlight=ChainPhase==EMCNutBossChainPhase::Flight || ChainPhase==EMCNutBossChainPhase::PullPlayer
        || ChainPhase==EMCNutBossChainPhase::PullNut;
    const bool bReel=ChainPhase==EMCNutBossChainPhase::Reeling;
    if(!bSpin && !bFlight && !bReel) {
        if(ChainPresentation->GetLocalSourceStateCount()>0) ChainPresentation->StopLocalSourceStates(Source);
        return;
    }
    FString Error;
    if(!ChainPresentation->IsSourcePrepared(Source) && !PrepareChainPresentation(Error)) return;
    const FTransform Hand=bCowboy?TankModel->GetSocketTransform(TEXT("hand_r"),RTS_World):GetChainGripTransform();
    const FVector Grip=bCowboy?Hand.TransformPosition(CowboyEye):Hand.GetLocation();
    FVector VisualTip=ChainTip;
    if(bSpin) {
        const float Payout=FMath::GetMappedRangeValueClamped(FVector2f(CowboyUnpark,CowboyFullOrbit),FVector2f(0.f,1.f),Alpha);
        const float Turn=FMath::GetMappedRangeValueClamped(FVector2f(CowboyFullOrbit,1.f),FVector2f(0.f,1.f),Alpha);
        const double Angle=2.*UE_DOUBLE_PI*(FMath::Pow(Turn,1.1)-1.)+FMath::DegreesToRadians(15.)*(Payout-1.);
        const FVector Orbit=(CowboyOrbitU*FMath::Cos(Angle)+CowboyOrbitV*FMath::Sin(Angle))*CowboyOrbitRadius;
        VisualTip=FMath::Lerp(Hand.TransformPosition(CowboyDock),Grip+TankModel->GetComponentTransform().TransformVector(Orbit),
            FMath::SmoothStep(0.f,1.f,Payout));
    } else if(bCowboy && bReel) {
        VisualTip=FMath::Lerp(ChainReelFrom,Hand.TransformPosition(CowboyDock),Alpha);
    }
    auto Values=ChainValues(TankModel,Grip,VisualTip);
    const auto* GameState=GetWorld()->GetGameState();
    const double Now=GameState?GameState->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const double Start=bSpin?FMath::Lerp(StateStartedAt,ResolveAt,double(CowboyUnpark)):ChainPhaseStartedAt;
    const double End=bSpin?ResolveAt:ChainPhaseEndsAt;
    Values.LocalStateAgeSeconds=float(FMath::Max(0.,Now-Start));
    Values.Duration=float(FMath::Max(0.,End-Start));
    const FTransform Frame(Values.Direction.IsNearlyZero()?GetActorRotation():Values.Direction.Rotation(),Grip);
    const FName Event=bSpin?SpinEvent:bFlight?FlightEvent:ReelEvent;
    for(FName Other:{SpinEvent,FlightEvent,ReelEvent})
        if(Other!=Event) ChainPresentation->StopLocalSourceStates(Source,Other);
    ChainPresentation->UpdateLocalSourceState(Source,Event,true,Frame,Values,TankModel,Error,false);
}

void AMCNutBoss::StopChainPresentation()
{
    const FName Source=MCNutVFX::ChainSource();
    ChainPresentation->StopLocalSourceStates(Source);
    ChainPresentation->StopSource(Source);
}
