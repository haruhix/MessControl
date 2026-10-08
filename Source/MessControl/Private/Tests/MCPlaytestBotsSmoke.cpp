#if !UE_BUILD_SHIPPING
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCPlaytestBotController.h"
#include "MCPlaytestSession.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCProgressionComponent.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCNutRainEvent.h"
#include "MCSingleDayDirector.h"
#include "MCGameMode.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/PlayerController.h"

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
        bool bMovedOnPath=false,bTrueBot=true,bInitialPersonalPerk=false;
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
        TSet<TWeakObjectPtr<AMCNutEnemy>> DamagedNuts;
        double StartedAt=-1;
        double NextDirtSampleAt=0;
        double NextCombatLogAt=0;
        float Duration=420;
        int32 ConfirmedHitsBeforeEnemies=0,MaxNutCombatHits=0;
        bool bFinished=false,bSingleDay=false,bTutorialStayedInactive=true;
        bool bSawRain=false,bSawEnemies=false,bReachedDirector=false;
        bool bSawPair=false,bHitTank=false,bHitMage=false;
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
        Run.bSingleDay=State->bSingleDayLoop;
        FParse::Value(FCommandLine::Get(),TEXT("MCBotsSmokeSeconds="),Run.Duration);
        Run.Duration=FMath::IsFinite(Run.Duration)?FMath::Clamp(Run.Duration,30.f,420.f):420.f;
        if(Run.bSingleDay)
        {
            for(auto It=World->GetPlayerControllerIterator();It;++It)
                if(AMCGameMode::IsGameplayParticipant(It->Get()))
                {
                    Run.bFinished=true;
                    UE_LOG(LogTemp,Error,TEXT("MC_BOTS_SMOKE_FAIL single-day smoke requires observe; coop contains a human personal-perk gate. Use -LegacyDays for the legacy coop smoke."));
                    FPlatformMisc::RequestExitWithStatus(false,1); return;
                }
        }
        UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_START map=%s seed=%d duration=%.1f single_day=%d tutorial=%d"),*World->GetMapName(),State->RunSeed,Run.Duration,Run.bSingleDay,State->bTutorialActive);
    }
    int32 TotalConfirmedHits=0;
    for(TActorIterator<AMCPlaytestBotController> It(World);It;++It)
    {
        if(It->IsActorBeingDestroyed()) continue;
        auto& Sample=Run.Bots.FindOrAdd(*It);
        if(const auto* Identity=It->GetPlayerState<AMCPlayerState>())
        {
            Sample.bTrueBot&=Identity->IsABot();
            Sample.Points=FMath::Max(Sample.Points,Identity->Points);
            Sample.bInitialPersonalPerk|=Identity->IsABot() && State->Progression && State->Progression->TeamLevel>=2
                && Identity->Perks && !Identity->Perks->ActivePerks.IsEmpty();
        }
        else Sample.bTrueBot=false;
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
        TotalConfirmedHits+=Pawn->ConfirmedHitCount;
        Sample.MaxContactProgress=FMath::Max(Sample.MaxContactProgress,Pawn->ContactProgress);
    }
    if(Run.bSingleDay)
    {
        Run.bTutorialStayedInactive&=!State->bTutorialActive;
        if(const auto* Director=State->SingleDayDirector.Get())
        {
            Run.bReachedDirector|=Director->Stage==EMCSingleDayStage::Director || Director->Stage==EMCSingleDayStage::Boss || Director->Stage==EMCSingleDayStage::Complete;
            if(const auto* Event=Director->NutEvent.Get())
            {
                Run.bSawRain|=Event->Stage==EMCNutRainStage::Rainfall && Event->NutsSpawned>0;
                const bool bEnemies=Event->Stage==EMCNutRainStage::Enemies;
                if(bEnemies && !Run.bSawEnemies) Run.ConfirmedHitsBeforeEnemies=TotalConfirmedHits;
                Run.bSawEnemies|=bEnemies && !Event->Enemies.IsEmpty();
                Run.bSawPair|=bEnemies && Event->Bosses.Num()==2 && Event->CompletedSeries>=4;
                if(bEnemies || Run.bReachedDirector)
                    Run.MaxNutCombatHits=FMath::Max(Run.MaxNutCombatHits,TotalConfirmedHits-Run.ConfirmedHitsBeforeEnemies);
                for(const auto& Enemy:Event->Enemies)
                {
                    // Health changes alone are insufficient: require a real tool impact
                    // recorded by the enemy while its live target is a bot. Observe mode
                    // has no human workers, and this harness never applies damage.
                    if(IsValid(Enemy) && Enemy->Health<Enemy->Settings.MaxHealth-KINDA_SMALL_NUMBER
                        && Enemy->HitAt>=Run.StartedAt && IsValid(Enemy->Target)
                        && Cast<AMCPlaytestBotController>(Enemy->Target->GetController()))
                    {
                        Run.DamagedNuts.Add(Enemy.Get());
                        if(const auto* Boss=Cast<AMCNutBoss>(Enemy.Get())) {
                            if(Boss->BossRole==EMCNutBossRole::Tank) Run.bHitTank=true; else Run.bHitMage=true;
                        }
                    }
                }
                if(bEnemies && Now>=Run.NextCombatLogAt)
                {
                    Run.NextCombatLogAt=Now+30;
                    UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_ENCOUNTER elapsed=%.1f bosses_left=%d enemies_left=%d series=%d combat_hits=%d hit_tank=%d hit_mage=%d"),
                        Now-Run.StartedAt,Event->BossesLeft,Event->EnemiesLeft,Event->CompletedSeries,
                        Run.MaxNutCombatHits,Run.bHitTank,Run.bHitMage);
                    for(const auto& Boss:Event->Bosses) if(IsValid(Boss) && Boss->IsEncounterAlive())
                        UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_BOSS role=%s health=%.1f max_health=%.1f state=%d attack=%d target=%s last_tool_hit=%.1f position=%s"),
                            Boss->BossRole==EMCNutBossRole::Tank?TEXT("tank"):TEXT("mage"),Boss->Health,Boss->Settings.MaxHealth,
                            static_cast<int32>(Boss->State),static_cast<int32>(Boss->Attack),*GetNameSafe(Boss->Target.Get()),
                            Boss->HitAt-Run.StartedAt,*Boss->GetActorLocation().ToCompactString());
                    for(TActorIterator<AMCPlaytestBotController> It(World);It;++It)
                    {
                        const auto* Pawn=Cast<AMCToothCharacter>(It->GetPawn());
                        if(!IsValid(Pawn)) continue;
                        const auto* Movement=Cast<UMCToothMovementComponent>(Pawn->GetCharacterMovement());
                        const auto* Target=It->GetTaskTarget();
                        const auto* Boss=Cast<AMCNutBoss>(Target);
                        const float Distance=IsValid(Target)?FVector::Dist2D(Pawn->GetActorLocation(),Target->GetActorLocation()):-1.f;
                        UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_COMBAT_BOT bot=%s activity=%s target=%s distance=%.1f health=%.1f speed=%.1f primary=%d sprint_requested=%d stamina=%.1f shield_front=%d confirmed_hits=%d path_failures=%d"),
                            *It->GetName(),*It->GetActivity(),*GetNameSafe(Target),Distance,Pawn->Status->State.Health,Pawn->GetVelocity().Size2D(),
                            Pawn->IsPrimaryHeld(),Movement && Movement->WantsToSprint(),Movement?Movement->GetStamina():-1.f,
                            Boss && Boss->IsShieldProtectingFrom(Pawn->GetActorLocation()),Pawn->ConfirmedHitCount,It->GetPathFailureCount());
                    }
                }
            }
        }
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
    const bool bSingleDaySequenceObserved=Run.bSingleDay && Run.bReachedDirector;
    if(!bTerminal && !bSingleDaySequenceObserved && Elapsed<Run.Duration) return;
    Run.bFinished=true;
    int32 MovedBots=0,ContributingBots=0,TrueBots=0,InitialPersonalPerks=0;
    for(const auto& Pair:Run.Bots)
    {
        const auto& Sample=Pair.Value;
        TrueBots+=Sample.bTrueBot?1:0;
        InitialPersonalPerks+=Sample.bInitialPersonalPerk?1:0;
        MovedBots+=Sample.bMovedOnPath?1:0;
        const bool bContribution=Sample.BrushContacts>0 || Sample.ConfirmedHits>0 || Sample.Points>0 || Sample.MaxContactProgress>KINDA_SMALL_NUMBER;
        ContributingBots+=bContribution?1:0;
        UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_BOT bot=%s moved_on_path=%d displacement=%.1f speed=%.1f brush_contacts=%d confirmed_hits=%d points=%d contact_progress=%.4f path_failures=%d activity=%s true_bot=%d initial_personal_perk=%d"),
            *GetNameSafe(Pair.Key.Get()),Sample.bMovedOnPath,Sample.MaxDisplacement,Sample.MaxSpeed,
            Sample.BrushContacts,Sample.ConfirmedHits,Sample.Points,Sample.MaxContactProgress,
            Pair.Key.IsValid()?Pair.Key->GetPathFailureCount():-1,Pair.Key.IsValid()?*Pair.Key->GetActivity():TEXT("missing"),Sample.bTrueBot,Sample.bInitialPersonalPerk);
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
    const bool bPass=Run.bSingleDay
        ? Run.Bots.Num()>0 && TrueBots==Run.Bots.Num() && InitialPersonalPerks==Run.Bots.Num()
            && Run.bTutorialStayedInactive && Run.bSawRain && Run.bSawEnemies && !Run.DamagedNuts.IsEmpty()
            && Run.bSawPair && Run.bHitTank && Run.bHitMage
            && Run.MaxNutCombatHits>0 && Run.bReachedDirector && State->Phase!=EMCShiftPhase::Lost
            && MovedBots>0 && ContributingBots>0
        : MovedBots>0 && ContributingBots>0 && CompletedGrime>0;
    if(Run.bSingleDay)
        UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_SINGLE_DAY tutorial_inactive=%d true_bots=%d personal_perks=%d saw_rain=%d saw_enemies=%d damaged_nuts=%d nut_combat_hits=%d reached_director=%d"),
            Run.bTutorialStayedInactive,TrueBots,InitialPersonalPerks,Run.bSawRain,Run.bSawEnemies,Run.DamagedNuts.Num(),Run.MaxNutCombatHits,Run.bReachedDirector);
    UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SMOKE_%s elapsed=%.1f bots=%d moved_bots=%d contributing_bots=%d completed_grime=%d completed_liquid=%d terminal=%d phase=%d day=%d step=%d csv=%s"),
        bPass?TEXT("PASS"):TEXT("FAIL"),Elapsed,Run.Bots.Num(),MovedBots,ContributingBots,CompletedGrime,CompletedLiquid,bTerminal,
        static_cast<int32>(State->Phase),State->Day,State->StepIndex,*Session->GetReportPath());
    FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
}
#endif
