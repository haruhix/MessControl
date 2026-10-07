#if !UE_BUILD_SHIPPING
#include "MCGameDirector.h"
#include "MCGameDirectorProfile.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayPlan.h"
#include "MCFoodActor.h"
#include "MCFoodDirectorHooks.h"
#include "MCArenaTooth.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCToothStatusComponent.h"
#include "MCRoguelikeDirector.h"
#include "MCMouthSurface.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

/** Opt-in saved L_Mouth integration/replication check, not a bot campaign. */
void MCTickGameDirectorValidation(UWorld* World)
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCFoodActor> Whole,LastFragment;
        TWeakObjectPtr<AMCThroat> Throat;
        int32 Stage=0,Checks=0,ExpectedPlayers=1,RewardBaseline=0,StuckPlacements=0;
        uint32 ClientSeen=0;
        float Age=0;
        double StageAt=0;
        bool Failed=false,Finished=false,Capture=false,CapturedProduction=false,FrozenProduction=false,CapturedFragments=false,CapturedComplete=false;
        bool ProductionOnly=false,CapturedChoices=false,CapturedDecision=false,EligibleSeen=false,SelectionSeen=false;
        FString Report;
    };
    static FRun R;
    if(R.World!=World)
    {
        R=FRun();R.World=World;
        FParse::Value(FCommandLine::Get(),TEXT("MCGameDirectorExpectedPlayers="),R.ExpectedPlayers);
        R.ExpectedPlayers=FMath::Clamp(R.ExpectedPlayers,1,4);
        R.Capture=World->GetNetMode()!=NM_Client && FParse::Param(FCommandLine::Get(),TEXT("MCGameDirectorCapture"));
        R.ProductionOnly=FParse::Param(FCommandLine::Get(),TEXT("MCGameDirectorProductionOnly"));
    }
    if(R.Finished) return;
    R.Age+=World->GetDeltaSeconds();
    auto* State=World->GetGameState<AMCGameState>();
    auto Check=[&](bool OK,const TCHAR* Message)
    {
        ++R.Checks;R.Failed|=!OK;
        const FString Line=FString::Printf(TEXT("MC_DIRECTOR_CHECK %s %s\n"),OK?TEXT("PASS"):TEXT("FAIL"),Message);
        R.Report+=Line;UE_LOG(LogTemp,Display,TEXT("%s"),*Line.TrimEnd());
    };
    auto Finish=[&]()
    {
        R.Finished=true;
        const FString Result=FString::Printf(TEXT("MC_DIRECTOR_SMOKE_%s role=%s stage=%d checks=%d remote_seen=%u stuck_placements=%d scope=%s eligible_seen=%d selection_seen=%d"),
            R.Failed?TEXT("FAIL"):TEXT("PASS"),World->GetNetMode()==NM_Client?TEXT("client"):TEXT("authority"),R.Stage,R.Checks,R.ClientSeen,R.StuckPlacements,
            R.ProductionOnly?TEXT("runtime_policy_observation"):TEXT("food_pipeline_monitor_and_stuck_placement"),R.EligibleSeen?1:0,R.SelectionSeen?1:0);
        UE_LOG(LogTemp,Display,TEXT("%s"),*Result);
        const FString File=FString::Printf(TEXT("Director_%s_%u_Validation.txt"),
            World->GetNetMode()==NM_Client?TEXT("Client"):TEXT("Server"),FPlatformProcess::GetCurrentProcessId());
        FFileHelper::SaveStringToFile(R.Report+Result+TEXT("\n"),*(FPaths::ProjectSavedDir()/File));
        FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);
    };
    if(R.Age>100) {Check(false,TEXT("saved-arena Director check timed out"));Finish();return;}
    if(!State || R.Age<3) return;
    const auto& Snapshot=State->DirectorState;
    bool Finite=true;
    const float Telemetry[]={Snapshot.Pressure,Snapshot.WorkSeconds,Snapshot.TargetPressure,Snapshot.Difficulty,
        Snapshot.Throughput,Snapshot.TeamHealth,Snapshot.TeamStamina,Snapshot.Stress,Snapshot.DayProgress,
        Snapshot.CompletionProgress,Snapshot.WaitProbability};
    for(const float Value:Telemetry) Finite&=FMath::IsFinite(Value) && Value>=0;
    for(const auto& Candidate:Snapshot.Candidates)
        Finite&=FMath::IsFinite(Candidate.BaseWeight) && FMath::IsFinite(Candidate.EffectiveWeight)
            && FMath::IsFinite(Candidate.Probability) && FMath::IsFinite(Candidate.ForecastPressure)
            && Candidate.Probability>=0 && Candidate.Probability<=1;
    if(Snapshot.bEnabled && !Snapshot.Candidates.IsEmpty())
    {
        float Probability=Snapshot.WaitProbability;
        for(const auto& Candidate:Snapshot.Candidates) Probability+=Candidate.Probability;
        Finite&=FMath::IsNearlyEqual(Probability,1.f,.002f);
    }
    if(!Finite || Snapshot.DecisionLog.Num()>12)
    {Check(false,TEXT("runtime monitor telemetry/candidates/log are invalid"));Finish();return;}
    if(World->GetNetMode()==NM_Client)
    {
        // The client has no authoritative Director: every bit comes from the
        // actual replicated GameState, not from a locally recreated plan.
        if(Snapshot.bEnabled) R.ClientSeen|=1;
        if(Snapshot.PlannedFood==1) R.ClientSeen|=2;
        if(Snapshot.SpawnedFood==1) R.ClientSeen|=4;
        if(Snapshot.Fragments>0) R.ClientSeen|=8;
        if(Snapshot.ThroatQueued>0) R.ClientSeen|=16;
        if(!Snapshot.DecisionReason.IsEmpty() && !Snapshot.DecisionLog.IsEmpty()) R.ClientSeen|=32;
        if(Snapshot.FinishedFood==1) R.ClientSeen|=64;
        if(Snapshot.Candidates.Num()>0) R.ClientSeen|=128;
        if(Snapshot.TargetPressure>0 && Snapshot.Difficulty>0 && Snapshot.Throughput>0) R.ClientSeen|=256;
        if(R.ClientSeen==511)
        {
            Check(true,TEXT("remote client received selected meal, actual pipeline, candidates, adaptive target and log"));
            Check(State->DayPlan && State->DayPlan->IsAsset(),TEXT("remote GameState resolves the saved mechanics asset"));
            Finish();
        }
        return;
    }
    auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    auto* PC=World->GetFirstPlayerController();
    auto* Director=Mode?Mode->GameDirector.Get():nullptr;
    if(R.ProductionOnly)
    {
        // This opt-in visual observation never replaces the real profile,
        // disables rules, cleans work, spawns a fixture or manufactures choices.
        if(!Director || !Director->Settings || !PC) return;
        for(const auto& Candidate:Snapshot.Candidates)
            R.EligibleSeen|=Candidate.EffectiveWeight>0 && Candidate.Probability>0;
        for(const auto& Entry:Snapshot.DecisionLog)
            R.SelectionSeen|=Entry.Contains(TEXT("Выбор:")) || Entry.Contains(TEXT("Выбор "));
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("DirectorValidation");
        if(!R.CapturedChoices && (R.EligibleSeen || R.Age>=12))
        {
            Check(R.Capture,TEXT("production observation requires the explicit render capture flag"));
            if(R.Failed) {Finish();return;}
            IFileManager::Get().MakeDirectory(*Folder,true);
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("ProductionChoices.png"),true,false);
            R.CapturedChoices=true;R.StageAt=State->GetServerWorldTimeSeconds();return;
        }
        if(R.CapturedChoices && !R.CapturedDecision && State->GetServerWorldTimeSeconds()-R.StageAt>.75
            && !Snapshot.DecisionReason.IsEmpty())
        {
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("ProductionDecision.png"),true,false);
            R.CapturedDecision=true;
        }
        if(R.Age>=20)
        {
            Check(Mode->bUseAdaptiveDirector && Snapshot.bEnabled && Director->Settings->Days.Num()==7,
                TEXT("production policy remains active throughout observation"));
            bool ActiveRules=false;
            for(const auto& Rule:Director->Settings->Events) ActiveRules|=Rule.Weight>0;
            Check(ActiveRules && !Snapshot.Candidates.IsEmpty(),TEXT("authored active weights and actual candidate diagnostics are present"));
            Check(R.CapturedChoices && R.CapturedDecision,TEXT("actual production candidates and current decision were captured"));
            Check(!R.FrozenProduction && R.Stage==0,TEXT("production observation never entered the controlled protocol"));
            // An idle team may remain in Drain with all choices blocked. Report
            // that fact explicitly instead of forging an eligible event.
            Finish();
        }
        return;
    }
    if(Director && Director->Settings && !R.FrozenProduction)
    {
        // Reserve this opt-in test before production's initial cleaning ends.
        // Preserve all real actors and work; never initialize over a selected
        // production event and silently lose its bookkeeping.
        Check(Snapshot.PlannedFood==0 && Snapshot.SpawnedFood==0 && Snapshot.QueuedEvents==0,
            TEXT("controlled protocol reserves its fixture before any production selection"));
        if(R.Failed) {Finish();return;}
        if(R.Capture && !R.CapturedProduction && PC)
        {
            Check(Mode->bUseAdaptiveDirector && Director->Settings->Days.Num()==7,
                TEXT("production capture starts from the default seven-day adaptive profile"));
            for(const auto& Day:Director->Settings->Days)
                Check(Day.DaySeconds>=360 && Day.DaySeconds<=480,
                    TEXT("production capture retains the six-to-eight-minute day durations"));
            if(R.Failed) {Finish();return;}
            const FString Folder=FPaths::ProjectSavedDir()/TEXT("DirectorValidation");
            IFileManager::Get().MakeDirectory(*Folder,true);
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("DirectorProduction.png"),true,false);
            R.CapturedProduction=true;return;
        }
        if(R.Capture && !R.CapturedProduction) return;
        for(auto& Rule:Director->Settings->Events) Rule.Weight=0;
        R.FrozenProduction=true;
    }
    if(!Mode || !PC || !Director || (R.Stage==0 && State->PlayerArray.Num()<R.ExpectedPlayers) || R.Age<10) return;
    const double Now=State->GetServerWorldTimeSeconds();
    auto Next=[&]() {++R.Stage;R.StageAt=Now;State->ForceNetUpdate();};
    auto Clean=[&]()
    {
        for(TActorIterator<AActor> It(World);It;++It)
            if(auto* Status=It->FindComponentByClass<UMCToothStatusComponent>())
                for(int32 I=0;I<150;++I) {Status->CareContact(true);Status->CareContact(false);}
        // Controlled protocol fixture; actual liquid wiping is covered elsewhere.
        for(TActorIterator<AMCMouthSurface> It(World);It;++It) if(!It->bUlcer) It->Destroy();
    };
    if(R.Stage==0)
    {
        Check(Mode->bUseAdaptiveDirector,TEXT("saved mouth GameMode enables the new Director by default"));
        Check(Director->Settings && Director->Settings->Days.Num()==7,TEXT("saved arena starts with all seven day profiles"));
        if(Director->Settings)
            for(const auto& Day:Director->Settings->Days)
                Check(Day.DaySeconds>=360 && Day.DaySeconds<=480,TEXT("production day profile has six-to-eight-minute duration"));
        bool TongueFound=false;
        for(TActorIterator<AMCTongue> It(World);It;++It) TongueFound=true;
        for(TActorIterator<AMCThroat> It(World);It;++It) {R.Throat=*It;break;}
        Check(TongueFound && R.Throat.IsValid(),TEXT("saved arena has real tongue and throat producer actors"));
        if(R.Failed) {Finish();return;}
        // A single explicit test meal makes server/client states observable in
        // a bounded run. This is not the production seven-day balance/profile.
        auto* Profile=NewObject<UMCGameDirectorProfile>(Director);
        for(auto& Day:Profile->Days)
        {
            Day.InitialPatches=0;
            Day.DaySeconds=120;Day.MaxFoodBatch=1;Day.RestSeconds=0;Day.EventGap=1;Day.FinalCleanupSeconds=10;
        }
        for(auto& Rule:Profile->Events)
        {
            Rule.Weight=Rule.Kind==EMCGameDirectorEvent::Food?1.f:0.f;
            Rule.FirstDay=1;Rule.MinimumDifficulty=0;Rule.MaxPerDay=1;Rule.CooldownSeconds=3600;
        }
        Profile->TeamScaling=0;Profile->InitialCleaningSeconds=1;Profile->DecisionInterval=.25f;
        auto* Mechanics=Mode->FirstDayPlan.LoadSynchronous();
        Check(Mechanics && Mechanics->IsAsset(),TEXT("controlled protocol retains the saved production mechanics asset"));
        if(R.Failed) {Finish();return;}
        Director->InitializeRun(Mechanics,Profile);Director->BeginDay(1);
        Mode->SetActorTickEnabled(false);Clean();
        // Bounded saved-arena placement probes use the actual Fibre menu row and
        // tooth/tongue packages. Null attempts are legitimate unavailable bands.
        // These diagnostic actors are not Director tickets and are retired here.
        for(int32 Seed=6100;Seed<6120;++Seed)
        {
            FRandomStream PlacementRandom(Seed);
            auto* Probe=MCSpawnDirectedStuckFood(World,Director->Mechanics,TEXT("Fibre"),910000+Seed,PlacementRandom);
            if(!Probe) continue;
            ++R.StuckPlacements;
            const auto* Anchor=Cast<AMCArenaTooth>(Probe->StuckTooth.Get());
            Check(!Probe->IsMouthEntryActive() && Probe->Phase==EMCFoodPhase::Stuck && Anchor && Anchor->IsAvailable(),
                TEXT("directed Fibre rests against an available real tooth without entry flight"));
            bool Supported=false;
            const FVector Extent=Probe->Body->GetScaledBoxExtent();
            for(TActorIterator<AMCTongue> It(World);It;++It)
            {
                FHitResult Floor;
                if(It->GameplaySpawnZone(Probe->GetActorLocation())!=INDEX_NONE
                    && It->GameplaySpawnFootprint(Probe->GetActorLocation(),Extent.Size2D()+20.f,Floor)
                    && FMath::Abs(Probe->GetActorLocation().Z-Floor.ImpactPoint.Z-Extent.Z-5.f)<3.f)
                    {Supported=true;break;}
            }
            Check(Supported,TEXT("directed stuck food retains a supported interior footprint and full body margin"));
            Probe->Dispose();
        }
        Check(R.StuckPlacements>0,TEXT("at least one of twenty bounded saved-arena stuck-placement attempts succeeds"));
        Check(Director->Observe().PlannedFood==0 && Director->Observe().SpawnedFood==0,
            TEXT("diagnostic placement probes neither select nor fabricate a Director meal"));
        if(R.Failed) {Finish();return;}
        R.RewardBaseline=Mode->RoguelikeDirector?Mode->RoguelikeDirector->RewardsSpawned:0;
        Next();return;
    }
    if(R.Stage==1)
    {
        if(Snapshot.SpawnedFood<1) {Clean();return;}
        for(TActorIterator<AMCFoodActor> It(World);It;++It)
            if(!It->IsDisposed() && !It->bBrushTool && It->FoodData.Kind==EMCFoodKind::Food && !It->bFragment)
            {R.Whole=*It;break;}
        Check(R.Whole.IsValid(),TEXT("one whole food is launched through the real arena entry path"));
        Check(Director->HasOutstandingWork(),TEXT("spawned count does not mark undelivered food complete"));
        if(R.Failed) {Finish();return;}
        Next();return;
    }
    if(R.Stage==2)
    {
        if(!R.Whole.IsValid()) {Check(false,TEXT("whole food vanished before cutting"));Finish();return;}
        if(Now-R.StageAt<2 || R.Whole->IsMouthEntryActive()) return;
        Check(R.Whole->HitFood(100000,FVector::ForwardVector),TEXT("production whole-food fracture path accepts cutting"));
        Check(R.Whole->IsDisposed(),TEXT("fracture retires the whole parent"));
        Check(Director->Observe().Fragments>0 && Director->HasOutstandingWork(),TEXT("live fragments preserve the retired parent's unfinished meal"));
        if(R.Failed) {Finish();return;}
        Next();return;
    }
    if(R.Stage==3)
    {
        if(R.Capture && !R.CapturedFragments && Now-R.StageAt>1)
        {
            const FString Folder=FPaths::ProjectSavedDir()/TEXT("DirectorValidation");
            IFileManager::Get().MakeDirectory(*Folder,true);
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("DirectorMonitor.png"),true,false);
            R.CapturedFragments=true;
        }
        // Hold this state long enough for remote peers to receive it.
        if(Now-R.StageAt<2) return;
        for(TActorIterator<AMCFoodActor> It(World);It;++It)
            if(It->bFragment && !It->bBrushTool && It->FoodData.Kind==EMCFoodKind::Food && !It->IsDisposed())
            {
                if(!R.LastFragment.IsValid()) R.LastFragment=*It; else It->Dispose();
            }
        Check(R.LastFragment.IsValid(),TEXT("one last fragment remains"));
        Check(Director->Observe().Fragments==1 && Director->HasOutstandingWork(),TEXT("CountFood one still blocks completion on the saved map"));
        if(R.Failed) {Finish();return;}
        Next();return;
    }
    if(R.Stage==4)
    {
        if(Now-R.StageAt<2) return;
        if(!R.LastFragment.IsValid() || !R.Throat.IsValid()) {Check(false,TEXT("intake actors were lost"));Finish();return;}
        auto* Food=R.LastFragment.Get();auto* Throat=R.Throat.Get();
        Food->Body->SetSimulatePhysics(false);Food->Phase=EMCFoodPhase::Free;
        FVector Position=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
        for(TActorIterator<AMCTongue> It(World);It;++It)
        {FHitResult Floor;if(It->SurfacePoint(Position,Floor)) {Position.Z=Floor.ImpactPoint.Z+10;break;}}
        Food->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
        Check(Throat->AcceptDelivery(Food),TEXT("last fragment enters actual throat gathering queue"));
        Check(!Food->IsDisposed() && Director->HasOutstandingWork(),TEXT("queued delivery remains work until actual swallow commit"));
        if(R.Failed) {Finish();return;}
        Next();return;
    }
    if(R.Stage==5)
    {
        if(!R.LastFragment.IsValid() || !R.LastFragment->IsDisposed()) return;
        Check(R.Throat->QueuedFoodCount==0,TEXT("the real swallow consumes its reserved ingredient once"));
        Check(!Mode->RoguelikeDirector || Mode->RoguelikeDirector->RewardsSpawned==R.RewardBaseline,
            TEXT("reward retry timer cannot inject unplanned booster chests"));
        Clean();Next();return;
    }
    if(R.Stage==6 && R.Capture && !R.CapturedComplete && Now-R.StageAt>1)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectorValidation/DirectorComplete.png"),true,false);
        R.CapturedComplete=true;
    }
    if(R.Stage==6 && Now-R.StageAt>5)
    {
        Check(Snapshot.PlannedFood==1 && Snapshot.SpawnedFood==1 && Snapshot.FinishedFood==1,
            TEXT("monitor reports exactly one runtime-selected meal after every fragment disposes"));
        Check(Snapshot.Candidates.Num()>0 && Snapshot.TargetPressure>0 && Snapshot.Difficulty>0,
            TEXT("runtime probabilities and adaptive target remain visible after the selected meal completes"));
        Check(Snapshot.DecisionLog.Num()>0 && !Snapshot.DecisionReason.IsEmpty(),TEXT("monitor publishes recent decisions and current reason"));
        Check(Snapshot.WholeFood==0 && Snapshot.Fragments==0 && Snapshot.ThroatQueued==0,
            TEXT("pipeline observation has no phantom food or reserved intake"));
        Finish();
    }
}
#endif
