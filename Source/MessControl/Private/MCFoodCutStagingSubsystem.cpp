#include "MCFoodCutStagingSubsystem.h"
#include "MCFoodActor.h"
#include "MCFoodCutGeometry.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void UMCFoodCutStagingSubsystem::Enqueue(AMCFoodActor* Food)
{
    check(IsInGameThread());
    if(IsValid(Food) && Food->GetWorld()==GetWorld())Pending.AddUnique(Food);
}
bool UMCFoodCutStagingSubsystem::IsTickable() const
{
    return !IsTemplate() && !Pending.IsEmpty() && Super::IsTickable();
}
TStatId UMCFoodCutStagingSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMCFoodCutStagingSubsystem,STATGROUP_Tickables);
}
void UMCFoodCutStagingSubsystem::Tick(float)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodCutStagingTick);
    check(IsInGameThread());
    LastStagedActors=0;
    ++MCFoodCutGeometry::PreparationStats().StagingTicks;
    const double Deadline=FPlatformTime::Seconds()+FrameBudgetSeconds;
    // Round-robin: simultaneous cuts share one budget rather than each gaining
    // a separate actor allowance. One actor is the smallest indivisible unit.
    int32 RemainingChecks=Pending.Num();
    while(!Pending.IsEmpty() && LastStagedActors<MaxStagedActorsPerTick && RemainingChecks>0) {
        const TWeakObjectPtr<AMCFoodActor> Weak=Pending[0];Pending.RemoveAt(0);
        auto* Food=Weak.Get();
        if(!IsValid(Food) || !Food->IsCutTransactionPending()){--RemainingChecks;continue;}
        const uint64 Before=MCFoodCutGeometry::PreparationStats().StagedActors;
        const uint64 BeforeCommit=MCFoodCutGeometry::PreparationStats().CommittedCuts;
        const bool More=Food->AdvancePendingCut();
        LastStagedActors+=int32(MCFoodCutGeometry::PreparationStats().StagedActors-Before);
        if(More)Pending.Add(Weak);
        if(MCFoodCutGeometry::PreparationStats().CommittedCuts!=BeforeCommit || FPlatformTime::Seconds()>=Deadline)break;
        RemainingChecks=Pending.Num();
    }
}
void UMCFoodCutStagingSubsystem::Deinitialize()
{
    for(const auto& Weak:Pending)if(auto* Food=Weak.Get())Food->CancelPendingCut(false);
    Pending.Reset();
    Super::Deinitialize();
}
