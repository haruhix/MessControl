#if !UE_BUILD_SHIPPING
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCPlaytestBotController.h"
#include "MCPlaytestSession.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Navigation/PathFollowingComponent.h"

// CLI-only observation of the ordinary production run. This harness never
// changes objectives, actions, health, positions or the navigation mesh.
void MCTickPlaytestBotsSmoke(UWorld* World)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("MCBotsSmoke"))) return;
    if(!World || !World->IsGameWorld() || World->GetNetMode()==NM_Client) return;
    struct FBotSample
    {
        TWeakObjectPtr<AMCToothCharacter> Pawn;
        FVector StartedAt=FVector::ZeroVector;
        double MaxDisplacement=0;
        float MaxSpeed=0,MaxContactProgress=0;
        int32 BrushContacts=0,ConfirmedHits=0,Points=0;
        bool bMovedOnPath=false;
    };
    struct FSmokeRun
    {
        struct FDirtySample
        {
            float StartedRemaining=1,MinRemaining=1;
            bool bTooth=false,bCompleted=false;
        };
        TWeakObjectPtr<UWorld> World;
        TMap<TWeakObjectPtr<AMCPlaytestBotController>,FBotSample> Bots;
        TMap<TWeakObjectPtr<AActor>,FDirtySample> Dirt;
        double StartedAt=-1;
        double NextDirtSampleAt=0;
        float Duration=90;
        bool bFinished=false;
    };
    static FSmokeRun Run;
    if(Run.World!=World) { Run=FSmokeRun(); Run.World=World; }
    if(Run.bFinished) return;
    const auto* State=World->GetGameState<AMCGameState>();
    auto* Session=AMCPlaytestSession::Find(World);
    if(!State || !Session || !Session->IsActive()) return;
    const double Now=State->GetServerWorldTimeSeconds();
    if(Run.StartedAt<0)
    {
        Run.StartedAt=Now;
        FParse::Value(FCommandLine::Get(),TEXT("MCBotsSmokeSeconds="),Run.Duration);
        Run.Duration=FMath::IsFinite(Run.Duration)?FMath::Clamp(Run.Duration,30.f,180.f):90.f;
        UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_START map=%s seed=%d duration=%.1f"),*World->GetMapName(),State->RunSeed,Run.Duration);
    }
    for(TActorIterator<AMCPlaytestBotController> It(World);It;++It)
    {
        if(It->IsActorBeingDestroyed()) continue;
        auto& Sample=Run.Bots.FindOrAdd(*It);
        auto* Pawn=Cast<AMCToothCharacter>(It->GetPawn());
        if(!IsValid(Pawn)) continue;
        // A reserve-tooth respawn is not navigation movement. Start a new
        // positional baseline while preserving observed contribution counters.
        if(Sample.Pawn!=Pawn) { Sample.Pawn=Pawn; Sample.StartedAt=Pawn->GetActorLocation(); }
        const double Displacement=FVector::Dist2D(Sample.StartedAt,Pawn->GetActorLocation());
        const float Speed=Pawn->GetVelocity().Size2D();
        Sample.MaxDisplacement=FMath::Max(Sample.MaxDisplacement,Displacement);
        Sample.MaxSpeed=FMath::Max(Sample.MaxSpeed,Speed);
        Sample.bMovedOnPath|=Displacement>100 && Speed>20 && It->GetMoveStatus()==EPathFollowingStatus::Moving;
        Sample.BrushContacts=FMath::Max(Sample.BrushContacts,Pawn->SuccessfulBrushContacts);
        Sample.ConfirmedHits=FMath::Max(Sample.ConfirmedHits,Pawn->ConfirmedHitCount);
        Sample.MaxContactProgress=FMath::Max(Sample.MaxContactProgress,Pawn->ContactProgress);
        if(const auto* Identity=It->GetPlayerState<AMCPlayerState>()) Sample.Points=FMath::Max(Sample.Points,Identity->Points);
    }
    if(Now>=Run.NextDirtSampleAt)
    {
        Run.NextDirtSampleAt=Now+.25;
        auto ObserveDirt=[&](AActor* Actor,bool bTooth,bool bDirty,bool bClean,float Remaining)
        {
            // A contact or a score increment can leave most of a crown dirty.
            // Only observe existing objectives; never create or finish one here.
            auto* Sample=Run.Dirt.Find(Actor);
            if(!Sample && bDirty)
            {
                auto& NewSample=Run.Dirt.Add(Actor);
                NewSample.bTooth=bTooth;
                NewSample.StartedRemaining=NewSample.MinRemaining=Remaining;
                Sample=&NewSample;
            }
            if(!Sample) return;
            Sample->MinRemaining=FMath::Min(Sample->MinRemaining,Remaining);
            Sample->bCompleted|=bClean && Remaining<.025f;
        };
        for(TActorIterator<AMCArenaTooth> It(World);It;++It)
        {
            if(!It->IsAvailable() || It->IsActorBeingDestroyed()) continue;
            const bool bDirty=It->Status->NeedsCare(true);
            ObserveDirt(*It,true,bDirty,!bDirty,It->RemainingGrime());
        }
        for(TActorIterator<AMCMouthSurface> It(World);It;++It)
        {
            if(It->bUlcer || It->IsActorBeingDestroyed()) continue;
            const bool bClean=It->IsClean();
            ObserveDirt(*It,false,!bClean,bClean,It->RemainingLiquid());
        }
    }
    const bool bTerminal=State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost || State->bDayOneComplete;
    const double Elapsed=Now-Run.StartedAt;
    if(!bTerminal && Elapsed<Run.Duration) return;
    Run.bFinished=true;
    int32 MovedBots=0,ContributingBots=0;
    for(const auto& Pair:Run.Bots)
    {
        const auto& Sample=Pair.Value;
        MovedBots+=Sample.bMovedOnPath?1:0;
        const bool bContribution=Sample.BrushContacts>0 || Sample.ConfirmedHits>0 || Sample.Points>0 || Sample.MaxContactProgress>KINDA_SMALL_NUMBER;
        ContributingBots+=bContribution?1:0;
        UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_BOT bot=%s moved_on_path=%d displacement=%.1f speed=%.1f brush_contacts=%d confirmed_hits=%d points=%d contact_progress=%.4f path_failures=%d activity=%s"),
            *GetNameSafe(Pair.Key.Get()),Sample.bMovedOnPath,Sample.MaxDisplacement,Sample.MaxSpeed,
            Sample.BrushContacts,Sample.ConfirmedHits,Sample.Points,Sample.MaxContactProgress,
            Pair.Key.IsValid()?Pair.Key->GetPathFailureCount():-1,Pair.Key.IsValid()?*Pair.Key->GetActivity():TEXT("missing"));
    }
    int32 CompletedGrime=0,CompletedLiquid=0;
    for(const auto& Pair:Run.Dirt)
    {
        const auto& Sample=Pair.Value;
        if(Sample.bCompleted) (Sample.bTooth?CompletedGrime:CompletedLiquid)++;
        UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_DIRT target=%s kind=%s completed=%d started_remaining=%.4f min_remaining=%.4f"),
            *GetNameSafe(Pair.Key.Get()),Sample.bTooth?TEXT("tooth"):TEXT("liquid"),Sample.bCompleted,
            Sample.StartedRemaining,Sample.MinRemaining);
    }
    Session->PrintReport();
    const bool bPass=MovedBots>0 && ContributingBots>0 && CompletedGrime>0;
    UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_%s elapsed=%.1f bots=%d moved_bots=%d contributing_bots=%d completed_grime=%d completed_liquid=%d terminal=%d phase=%d day=%d step=%d csv=%s"),
        bPass?TEXT("PASS"):TEXT("FAIL"),Elapsed,Run.Bots.Num(),MovedBots,ContributingBots,CompletedGrime,CompletedLiquid,bTerminal,
        static_cast<int32>(State->Phase),State->Day,State->StepIndex,*Session->GetReportPath());
    FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
}
#endif
