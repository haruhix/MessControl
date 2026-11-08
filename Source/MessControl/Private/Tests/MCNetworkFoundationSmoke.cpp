#if !UE_BUILD_SHIPPING
#include "MCActiveSkillMotion.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace MCNetworkFoundationSmoke
{
constexpr int32 FixtureID=5518;
struct FRun
{
    TWeakObjectPtr<UWorld> World;
    double StartedWall=FPlatformTime::Seconds(),NextLog=0;
    bool Setup=false,Failed=false,Done=false,Started=false,Ended=false,SmallHit=false,LargeHit=false;
    bool SawFalling=false,SawRagdoll=false,SawRecovered=false,RecoveryStarted=false,FinalChecked=false;
    bool SawSteeredSourceTurn=false,SteeringChecked=false;
    int32 LocalStage=INDEX_NONE,Leases=0;
    uint8 ObservedStages=0;
    FMCActiveSkillMotionLease Motion;
    FGuid Operation;
    FVector StageStart=FVector::ZeroVector,RecoveryStart=FVector::ZeroVector;
    float StageDistance=0,RecoveryDistance=0,MaxRenderStep=0;
    FVector PreviousRendered=FVector::ZeroVector;
    bool HasRendered=false;
};
bool Ready(const AMCPlayerController* PC,const AMCToothCharacter* Hero)
{
    return PC && Hero && PC->GetPawn()==Hero && PC->IsWorldPresentationReady() && PC->HasAcknowledgedGameplay()
        && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored() && !PC->bShowMouseCursor && !Hero->IsArenaArrivalHeld();
}
bool Place(AMCTongue* Tongue,AMCToothCharacter* Hero,FVector Point)
{
    FHitResult Floor;if(!Tongue->SurfacePoint(Point,Floor) || Floor.ImpactNormal.Z<.65f) return false;
    Hero->CancelGameplayInput();Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),
        FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);Hero->NotifyNetworkPositionReset();Hero->ForceNetUpdate();return true;
}
}

void MCTickNetworkFoundationValidation(UWorld* World)
{
    using namespace MCNetworkFoundationSmoke;
    if(!World || !World->IsGameWorld()) return;
    static FRun R;
    if(R.World.Get()!=World) {R.Motion.End();R=FRun();R.World=World;}
    if(R.Done) return;
    const bool Host=World->GetNetMode()!=NM_Client;
    auto* GS=World->GetGameState<AMCGameState>();
    auto Check=[&](bool Good,const TCHAR* What) {
        R.Failed|=!Good;
        UE_LOG(LogTemp,Display,TEXT("MC_FOUNDATION_NET_CHECK net=%d %s %s"),int32(World->GetNetMode()),Good?TEXT("PASS"):TEXT("FAIL"),What);
    };
    auto Finish=[&](const TCHAR* Reason) {
        if(FCString::Strcmp(Reason,TEXT("Completed"))==0)
            Check(R.FinalChecked && R.ObservedStages==0x7f,TEXT("all seven phases and the final outcome gates ran on this peer"));
        R.Motion.End();R.Done=true;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s NETWORK_FOUNDATION net=%d stage=%d server_lease_requests=%d server_small_hit_request=%d server_large_hit_request=%d falling=%d ragdoll=%d recovered=%d recovery_cm=%.3f max_sampled_render_step_including_resets_cm=%.3f reason=%s"),
            R.Failed?TEXT("FAIL"):TEXT("PASS"),int32(World->GetNetMode()),R.LocalStage,R.Leases,R.SmallHit,R.LargeHit,R.SawFalling,R.SawRagdoll,R.SawRecovered,R.RecoveryDistance,R.MaxRenderStep,Reason);
        if(auto* PC=World->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("Quit"));
    };
    float Timeout=300;FParse::Value(FCommandLine::Get(),TEXT("MCFoundationNetworkTimeout="),Timeout);
    if(FPlatformTime::Seconds()-R.StartedWall>FMath::Clamp(Timeout,60.f,600.f)) {R.Failed=true;Finish(TEXT("WallTimeout"));return;}
    if(!GS) return;
    const double Now=GS->GetServerWorldTimeSeconds();
    // The client exits slightly earlier. The host finalizes from already
    // recorded outcomes, even after the departing client's pawn is destroyed.
    if(R.Setup && R.LocalStage==6 && Now-GS->DayStartedAt>(Host?6.5:5.5)) {Finish(TEXT("Completed"));return;}
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(auto* PS=It->GetPlayerState<AMCPlayerState>();
        PS && PS->GetPawn()==*It && !PS->IsOnlyASpectator() && !It->ActorHasTag(TEXT("MC_VFXLabRuntime"))) Heroes.Add(*It);
    Heroes.Sort([](const auto& A,const auto& B){return A.template GetPlayerState<AMCPlayerState>()->GetPlayerId()<B.template GetPlayerState<AMCPlayerState>()->GetPlayerId();});
    if(Heroes.Num()!=2) return;
    auto* Remote=Heroes[1];
    auto* LocalPC=Cast<AMCPlayerController>(World->GetFirstPlayerController());
    auto* Own=LocalPC?Cast<AMCToothCharacter>(LocalPC->GetPawn()):nullptr;
    AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
    if(!LocalPC || !Own || !Tongue) return;
    if(Host && !R.Setup) {
        for(auto* Hero:Heroes) {
            auto* PC=Cast<AMCPlayerController>(Hero->GetController());
            if(!PC || !PC->IsWorldPresentationReady() || !PC->HasAcknowledgedGameplay() || Hero->IsArenaArrivalHeld()
                || !Hero->CanWork() || (!PC->IsLocalController() && PC->AcknowledgedPawn!=Hero)) return;
        }
        if(!Ready(LocalPC,Own)) return;
        if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        GS->bDevManualEvents=true;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;
        Tongue->bAutomaticYawns=false;Tongue->ResetYawn();Tongue->ResetPain();
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Destroy();
        const FVector Center=Tongue->GetActorLocation()+FVector(0,-200,0);
        Check(Place(Tongue,Remote,Center+FVector(-300,0,0)) && Place(Tongue,Heroes[0],Center+FVector(350,0,0)),TEXT("setup uses supported actual pawns and explicit position resets"));
        GS->TasksTotal=FixtureID;GS->TasksLeft=0;GS->DayStartedAt=Now+2;GS->ForceNetUpdate();R.Setup=true;
    }
    if(GS->TasksTotal!=FixtureID || !GS->bDevManualEvents) return;
    R.Setup=true;
    const int32 Stage=GS->TasksLeft;const double T=Now-GS->DayStartedAt;
    if(Stage!=R.LocalStage) {
        R.Motion.End();R.LocalStage=Stage;R.Started=R.Ended=false;R.StageDistance=0;
        if(Stage>=0 && Stage<=6) R.ObservedStages|=uint8(1u<<Stage);
        R.StageStart=Remote->GetActorLocation();R.HasRendered=false;
        LocalPC->CancelSkillInputs();Own->CancelGameplayInput();Own->SetCameraOrbitView(-25,-12,500);
        UE_LOG(LogTemp,Display,TEXT("MC_FOUNDATION_NET_STAGE net=%d stage=%d local_player=%d role=%d server_now=%.6f start=%.6f"),
            int32(World->GetNetMode()),Stage,Own->GetPlayerState()->GetPlayerId(),int32(Own->GetLocalRole()),Now,GS->DayStartedAt);
    }
    if(T<0) return;
    if(Own->GetActorTransform().ContainsNaN() || Own->GetVelocity().ContainsNaN() || Own->GetPresentedActorTransform().ContainsNaN()) {
        R.Failed=true;Finish(TEXT("NonFinitePose"));return;
    }
    const FVector Rendered=Own->GetPresentedActorTransform().GetLocation();
    if(R.HasRendered) R.MaxRenderStep=FMath::Max(R.MaxRenderStep,float(FVector::Distance(Rendered,R.PreviousRendered)));
    R.PreviousRendered=Rendered;R.HasRendered=true;
    R.StageDistance=FMath::Max(R.StageDistance,float(FVector::Distance(Remote->GetActorLocation(),R.StageStart)));
    R.SawFalling|=Remote->GetCharacterMovement()->IsFalling();
    R.SawRagdoll|=Remote->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll;
    if(Stage==0 && Own==Remote && T>.5 && T<3) Own->AddMovementInput(FVector(FMath::Cos(T*2.5),FMath::Sin(T*2.5),0));
    if(Host && Stage==1 && T>1 && !R.Started) {
        R.Started=true;R.SmallHit=true;
        Remote->ToothPhysics->ApplyHit(FVector(120,0,80).GetSafeNormal()*Remote->ToothPhysics->Settings.FallThreshold*.6f,Remote->GetActorLocation());
        Check(Remote->ToothPhysics->GetBodyState()==EMCBodyState::Standing,TEXT("small real ApplyHit stays in the native CMC domain"));
    }
    if(Host && (Stage==2 || Stage==3) && T>1 && !R.Started) {
        R.Started=true;R.Operation=FGuid::NewGuid();R.StageStart=Remote->GetActorLocation();
        const bool Began=Stage==2?R.Motion.Begin(Remote,R.Operation,FVector(220,0,0),false,1.5f)
            :R.Motion.BeginSteered(Remote,R.Operation,FVector(220,0,0),100.f,1.8f);
        Check(Began,Stage==2?TEXT("shared constant external lease begins"):TEXT("shared steered external lease begins"));
        R.Leases+=Began;
    }
    if(Stage==3 && Own==Remote && T>.5 && T<3.5) Own->AddMovementInput(FVector(FMath::Cos(T*1.1),FMath::Sin(T*1.1),0));
    if(Stage==3 && T>1.5 && T<3.4) {
        auto* Move=CastChecked<UMCToothMovementComponent>(Remote->GetCharacterMovement());
        for(const auto& Source:Move->CurrentRootMotion.RootMotionSources) if(Source && Source->GetScriptStruct()==FMCLocomotionRootMotionSource::StaticStruct()) {
            float Speed=0,Rate=0;bool Locked=false;
            if(Move->ReadAcceptedSteeringParameters(*Source,Speed,Rate,Locked))
                R.SawSteeredSourceTurn|=FMath::Abs(static_cast<const FMCLocomotionRootMotionSource*>(Source.Get())->Force.Y)>50 && FMath::Abs(Move->Velocity.Y)>40;
        }
    }
    if(Stage==3 && T>3.6 && !R.SteeringChecked) {
        R.SteeringChecked=true;
        Check(R.SawSteeredSourceTurn,TEXT("accepted steered source and canonical velocity turned from native remote input on this peer"));
    }
    if(Host && (Stage==2 || Stage==3) && T>3.5 && !R.Ended) {
        R.Ended=true;R.Motion.End();
        Check(R.StageDistance>120,TEXT("the shared lease produced real canonical displacement"));
        Check(!R.Motion.IsOwned(),TEXT("normal external motion completion releases its exact source"));
    }
    if(Host && Stage==4 && T>1 && !R.Started) {
        R.Started=true;R.Operation=FGuid::NewGuid();
        Check(R.Motion.Begin(Remote,R.Operation,FVector(160,0,0),false,5),TEXT("a live external lease exists before body ownership takeover"));
        ++R.Leases;
    }
    if(Host && Stage==4 && T>1.5 && !R.LargeHit) {
        R.LargeHit=true;
        Remote->ToothPhysics->ApplyHit(FVector(-400,120,100).GetSafeNormal()*Remote->ToothPhysics->Settings.FallThreshold*1.8f,Remote->GetActorLocation());
        Check(Remote->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll,TEXT("large real ApplyHit transfers ownership to server physics"));
    }
    if(Stage>=4 && R.SawRagdoll && Remote->ToothPhysics->CanAct()) R.SawRecovered=true;
    if(Host && Stage==5 && T>1 && !R.Started) {
        R.Started=true;
        Check(Place(Tongue,Remote,Tongue->GetActorLocation()+FVector(-180,-150,0)),TEXT("explicit hard reset uses the shared reset gateway"));
    }
    if(Stage==6 && Own==Remote && Ready(LocalPC,Own) && T>.5 && T<3) {
        if(!R.RecoveryStarted) {R.RecoveryStarted=true;R.RecoveryStart=Own->GetActorLocation();}
        Own->AddMovementInput(FVector(0,1,0));
        R.RecoveryDistance=FVector::Distance(Own->GetActorLocation(),R.RecoveryStart);
    }
    if(Host && Stage==6) R.RecoveryDistance=FMath::Max(R.RecoveryDistance,float(FVector::Distance(Remote->GetActorLocation(),R.StageStart)));
    if(Stage==6 && T>4 && !R.FinalChecked) {
        R.FinalChecked=true;
        Check(R.SteeringChecked && R.SawSteeredSourceTurn,TEXT("the live steering outcome gate completed"));
        Check(R.SawRagdoll && R.SawRecovered,TEXT("body-frame takeover and return were observed on this peer"));
        Check(Remote->ToothPhysics->CanAct() && R.RecoveryDistance>100,TEXT("ordinary control works after leases, hit and explicit reset"));
        if(Host) Check(R.Leases==3 && R.SmallHit && R.LargeHit,TEXT("all shared provider cases executed exactly once"));
    }
    const double Duration=Stage==4?8:4.5;
    if(Host && Stage<6 && T>Duration) {R.Motion.End();GS->TasksLeft=Stage+1;GS->DayStartedAt=Now+1;GS->ForceNetUpdate();}
    if(Now>=R.NextLog) {
        R.NextLog=Now+.5;
        UE_LOG(LogTemp,Display,TEXT("MC_FOUNDATION_NET_TICK net=%d stage=%d t=%.3f own=%s role=%d ready=%d body=%d move=%d sim=(%s) shown=(%s)"),
            int32(World->GetNetMode()),Stage,T,*GetNameSafe(Own),int32(Own->GetLocalRole()),Ready(LocalPC,Own),int32(Own->ToothPhysics->GetBodyState()),int32(Own->GetCharacterMovement()->MovementMode),
            *Own->GetActorLocation().ToString(),*Rendered.ToString());
    }
}
#endif
