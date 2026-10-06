#include "MCBossAIController.h"
#include "MCBossCharacter.h"
#include "MCToothCharacter.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"

namespace
{
    float SafeSetting(float Value,float Default,float Min,float Max)
    { return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Default; }
}

AMCBossAIController::AMCBossAIController()
{
    bStartAILogicOnPossess=false;
    bStopAILogicOnUnposses=true;
}

void AMCBossAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    Boss=Cast<AMCBossCharacter>(InPawn);
    if (Boss && Boss->HasActorBegunPlay() && Boss->Runtime.State!=EMCBossState::Dormant) StartBossBrain();
}

void AMCBossAIController::OnUnPossess()
{
    StopBossBrain();
    Boss=nullptr;
    Super::OnUnPossess();
}

void AMCBossAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopBossBrain();
    Boss=nullptr;
    Super::EndPlay(EndPlayReason);
}

void AMCBossAIController::StartBossBrain()
{
    if (!HasAuthority() || !IsValid(Boss) || !Boss->IsBossAlive() || Boss->Runtime.State==EMCBossState::Dormant) return;
    if (GetWorldTimerManager().IsTimerActive(DecisionTimer)) return;
    const auto* Profile=Boss->GetResolvedProfile();
    const float Interval=Profile?SafeSetting(Profile->DecisionInterval,.2f,.1f,2.f):.2f;
    NextTargetScanAt=0;
    NextPathRequestAt=0;
    TargetLastVisibleAt=GetWorld()->GetTimeSeconds();
    GetWorldTimerManager().SetTimer(DecisionTimer,this,&AMCBossAIController::Decide,Interval,true,.01f);
}

void AMCBossAIController::StopBossBrain()
{
    GetWorldTimerManager().ClearTimer(DecisionTimer);
    StopMovement();
    ClearFocus(EAIFocusPriority::Gameplay);
}

AMCToothCharacter* AMCBossAIController::FindVisiblePlayer() const
{
    if (!IsValid(Boss)) return nullptr;
    const auto* Profile=Boss->GetResolvedProfile();
    float BestDistance=FMath::Square(Profile?SafeSetting(Profile->SightRadius,2500.f,1.f,100000.f):2500.f);
    AMCToothCharacter* Best=nullptr;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Player=*It;
        if (!AMCBossCharacter::IsLivingPlayer(Player)) continue;
        const float Distance=FVector::DistSquared(Boss->GetActorLocation(),Player->GetActorLocation());
        if (Distance<BestDistance && Boss->CanSeePlayer(Player)) { Best=Player; BestDistance=Distance; }
    }
    return Best;
}

void AMCBossAIController::Decide()
{
    if (!HasAuthority() || !IsValid(Boss) || !Boss->IsBossAlive() || Boss->Runtime.State==EMCBossState::Dormant)
    { StopBossBrain(); return; }
    if (Boss->Runtime.State==EMCBossState::Telegraph || Boss->Runtime.State==EMCBossState::Attacking || Boss->Runtime.State==EMCBossState::Recovering) return;
    const auto* Profile=Boss->GetResolvedProfile();
    const double Now=GetWorld()->GetTimeSeconds();
    const float Sight=Profile?SafeSetting(Profile->SightRadius,2500.f,1.f,100000.f):2500.f;
    const float LoseRange=FMath::Max(Sight,Profile?SafeSetting(Profile->LoseTargetRadius,3200.f,1.f,100000.f):3200.f);
    const float Memory=Profile?SafeSetting(Profile->SightMemorySeconds,3.f,0.f,60.f):3.f;
    const float ScanInterval=Profile?SafeSetting(Profile->TargetScanInterval,.6f,.2f,5.f):.6f;
    const float Retry=Profile?SafeSetting(Profile->PathRetrySeconds,.8f,.1f,10.f):.8f;
    const float Acceptance=Profile?SafeSetting(Profile->ChaseAcceptanceRadius,130.f,1.f,10000.f):130.f;
    AMCToothCharacter* Target=Boss->Runtime.Target;
    bool Visible=false;
    if (!AMCBossCharacter::IsLivingPlayer(Target) || FVector::DistSquared(Boss->GetActorLocation(),Target->GetActorLocation())>FMath::Square(LoseRange)) Target=nullptr;
    if (Target)
    {
        Visible=Boss->CanSeePlayer(Target);
        if (Visible) { TargetLastVisibleAt=Now; LastVisiblePosition=Target->GetActorLocation(); }
        else if (Now-TargetLastVisibleAt>Memory) Target=nullptr;
    }
    if (!Target && Now>=NextTargetScanAt)
    {
        NextTargetScanAt=Now+ScanInterval;
        Target=FindVisiblePlayer();
        Visible=Target!=nullptr;
        if (Target) { TargetLastVisibleAt=Now; LastVisiblePosition=Target->GetActorLocation(); }
    }
    if (!Target)
    {
        StopMovement();
        ClearFocus(EAIFocusPriority::Gameplay);
        Boss->SetBrainState(EMCBossState::Searching,nullptr);
        return;
    }
    Boss->SetBrainState(EMCBossState::Chasing,Target);
    if (Visible)
    {
        SetFocus(Target);
        TArray<const FMCBossAttackDefinition*,TInlineAllocator<8>> Candidates;
        float TotalWeight=0.f;
        const FVector TowardsTarget=Target->GetActorLocation()-Boss->GetActorLocation();
        for (const auto& Attack:Boss->GetAttackDefinitions())
            if (Boss->IsAttackReady(Attack) && Boss->IsPlayerInAttack(Target,Attack,TowardsTarget))
            { Candidates.Add(&Attack); TotalWeight+=Attack.SelectionWeight; }
        if (TotalWeight>0.f)
        {
            float Draw=FMath::FRand()*TotalWeight;
            for (const auto* Attack:Candidates)
            {
                Draw-=Attack->SelectionWeight;
                if (Draw<=0.f) { Boss->BeginAttack(Attack->AttackId,Target); return; }
            }
            Boss->BeginAttack(Candidates.Last()->AttackId,Target);
            return;
        }
    }
    else { ClearFocus(EAIFocusPriority::Gameplay); SetFocalPoint(LastVisiblePosition); }
    if (Now<NextPathRequestAt) return;
    NextPathRequestAt=Now+Retry;
    // Actor tracking only while visible; lost targets use the last seen location, never wall-penetrating steering.
    FAIMoveRequest Request;
    if (Visible) Request.SetGoalActor(Target);
    else Request.SetGoalLocation(LastVisiblePosition);
    Request.SetAcceptanceRadius(Acceptance);
    Request.SetReachTestIncludesAgentRadius(false);
    Request.SetReachTestIncludesGoalRadius(false);
    Request.SetUsePathfinding(true);
    Request.SetAllowPartialPath(false);
    Request.SetRequireNavigableEndLocation(true);
    Request.SetProjectGoalLocation(true);
    if (MoveTo(Request).Code==EPathFollowingRequestResult::Failed) StopMovement();
}

void AMCBossAIController::OnMoveCompleted(FAIRequestID RequestID,const FPathFollowingResult& Result)
{
    Super::OnMoveCompleted(RequestID,Result);
    // Failed paths back off. The next decision can still telegraph an in-range attack.
    if (Result.Code!=EPathFollowingResult::Success && Result.Code!=EPathFollowingResult::Aborted)
    {
        const auto* Profile=Boss?Boss->GetResolvedProfile():nullptr;
        const float Retry=Profile?SafeSetting(Profile->PathRetrySeconds,.8f,.1f,10.f):.8f;
        NextPathRequestAt=GetWorld()->GetTimeSeconds()+Retry;
    }
}
