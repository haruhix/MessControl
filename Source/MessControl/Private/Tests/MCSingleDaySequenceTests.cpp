#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCGameDirector.h"
#include "MCGameDirectorProfile.h"
#include "MCSingleDayDirector.h"
#include "MCTutorialDirector.h"
#include "MCNutRainEvent.h"
#include "MCIceEvent.h"
#include "MCFogBrawlEvent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "MCArenaTooth.h"
#include "MCDayPlan.h"
#include "MCColdCola.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCSingleDaySequenceTestsPrivate
{
struct FSequenceWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;
    AMCSingleDayDirector* Loop=nullptr;
    UMCDayPlan* Plan=nullptr;
    UMCGameDirectorProfile* Support=nullptr;
    UMCSingleDayProfile* Sequence=nullptr;
    FSequenceWorld(int32 SavedPrototypeSlots=0)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
        World->SetGameMode(URL);Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseSingleDayLoop=true;
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        State=World->GetGameState<AMCGameState>();Loop=State->SingleDayDirector;
        if (Mode->TutorialDirector) {Mode->TutorialDirector->Stop();Mode->TutorialDirector->Destroy();Mode->TutorialDirector=nullptr;}
        State->bTutorialActive=State->bLobbyWaiting=false;State->Phase=EMCShiftPhase::Working;
        Plan=NewObject<UMCDayPlan>(World);
        Support=NewObject<UMCGameDirectorProfile>(World);
        Support->InitialCleaningSeconds=1.5f;
        for (auto& Day:Support->Days) {Day.InitialPatches=0;Day.MinimumDifficulty=.55f;Day.MaximumDifficulty=1.1f;}
        // Lifecycle clock cases must not create unrelated hazards or meals.
        for (auto& Rule:Support->Events) Rule.Weight=0;
        Sequence=NewObject<UMCSingleDayProfile>(World);
        if(SavedPrototypeSlots==1 || SavedPrototypeSlots==2) {
            Sequence->KeyEvents.SetNum(SavedPrototypeSlots);
            Sequence->KeyEvents[0].EventId=TEXT("AuthoredNut");
            Sequence->KeyEvents[0].DirectorSupportSeconds=9;
            if(SavedPrototypeSlots==2) {
                Sequence->KeyEvents[1].EventId=TEXT("AuthoredIce");
                Sequence->KeyEvents[1].DirectorSupportSeconds=17;
            }
        }
        for(auto& Slot:Sequence->KeyEvents) Slot.CompletionExperience=0;
        Loop->Initialize(Plan,Sequence,Support);
        FreezeActors();
    }
    ~FSequenceWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void FreezeActors()
    {
        for (TActorIterator<AActor> It(World);It;++It) {
            It->SetActorTickEnabled(false);It->SetActorEnableCollision(false);
            TArray<UActorComponent*> Components;It->GetComponents(Components);
            for (auto* Component:Components) {
                Component->SetComponentTickEnabled(false);
                if (auto* Primitive=Cast<UPrimitiveComponent>(Component);Primitive && Primitive->IsSimulatingPhysics()) Primitive->SetSimulatePhysics(false);
            }
            if (auto* Status=It->FindComponentByClass<UMCToothStatusComponent>()) {
                Status->State.CoffeeLeft=Status->State.RepairLeft=0;
                Status->State.Health=Status->State.MaxHealth;
            }
        }
    }
    void CompleteNutSlot()
    {
        // Event combat has its own tests. This fixture supplies its terminal signal
        // to the real sequence tick, rather than calling the private handoff.
        Loop->Stage=EMCSingleDayStage::Nuts;
        Loop->NutEvent=World->SpawnActor<AMCNutRainEvent>();
        Loop->NutEvent->Stage=EMCNutRainStage::Complete;
        Loop->NutEvent->EnemiesLeft=Loop->NutEvent->BossesLeft=0;
        Loop->Tick(.1f);
        FreezeActors();
    }
    void CompleteIceSlot()
    {
        Loop->KeyEventIndex=1;
        Loop->Stage=EMCSingleDayStage::Ice;
        Loop->IceEvent=World->SpawnActor<AMCIceEvent>();
        Loop->IceEvent->Stage=EMCIceEventStage::Complete;
        Loop->IceEvent->CandyHealth=0;
        Loop->Tick(.1f);
        FreezeActors();
    }
    void CompleteFogSlot()
    {
        Loop->KeyEventIndex=2;
        Loop->Stage=EMCSingleDayStage::FogBrawl;
        Loop->FogEvent=World->SpawnActor<AMCFogBrawlEvent>();
        Loop->FogEvent->Stage=EMCFogBrawlStage::Complete;
        Loop->FogEvent->StrikesResolved=Loop->FogEvent->TotalStrikes;
        Loop->Tick(.1f);
        FreezeActors();
    }
    void RestoreTongueCollision()
    {
        // Dispatch calls the real event Start. Its floor samples need collision,
        // while pawn, hazard and scheduler ticks remain frozen in this fixture.
        for(TActorIterator<AMCTongue> It(World);It;++It) It->SetActorEnableCollision(true);
    }
    void AddFogTarget()
    {
        const FTransform Placement(FRotator::ZeroRotator,FVector(220,0,28));
        auto* Target=World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Placement);
        FMCArenaToothSettings Defaults;Defaults.InitialCalculusEveryNthTooth=0;
        Target->Initialize(1,Defaults);Target->FinishSpawning(Placement);
        FreezeActors();
    }
    void AdvanceClockTo(float Seconds)
    {
        // TimeOnly advances the engine clock without executing pawn/hazard ticks
        // or stepping physics. Raising this fixture's clamp avoids thousands of
        // artificial frames while testing the removed 30s/day/run deadlines.
        World->GetWorldSettings()->MaxUndilatedFrameTime=3600.f;
        ++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,FMath::Max(.01f,Seconds-float(World->GetTimeSeconds())));
    }
    void AddWorkerAndFoodGeometry()
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
        if (Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        const FTransform Floor(FRotator::ZeroRotator,FVector(0,0,-50),FVector(10,10,1));
        auto* Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor);
        Tongue->SourceMesh=Mesh;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Floor);
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Mode->DefaultPawnClass,FVector(0,0,150),FRotator::ZeroRotator,Params);
        auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Hero);
        auto* Menu=NewObject<UDataTable>(World);Menu->RowStruct=FMCFoodRow::StaticStruct();
        FMCFoodRow Food;Food.Health=10;Food.Fragments=1;Food.Resistance=EMCFoodResistance::Soft;
        Food.WholeMeshes={Mesh};Food.FragmentMeshes={Mesh};
        Menu->AddRow(TEXT("SequenceFood"),Food);Plan->Menu=Menu;
        auto* Cola=NewObject<UMCColdColaProfile>(World);Cola->ColdSeconds=35;Cola->ThawSeconds=5;Plan->ColdColaProfile=Cola;
        FreezeActors();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFragmentSupportTest,"MessControl.SingleDay.Sequence.CompletedFogSlotOpensSupportWithoutFinalBoss",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFragmentSupportTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    TestFalse(TEXT("The new sequence defaults away from a timed finale"),F.Loop->IsLegacyTimedFinale());
    F.CompleteFogSlot();
    TestEqual(TEXT("The terminal fog signal hands off to ordinary support"),F.Loop->Stage,EMCSingleDayStage::Director);
    TestTrue(TEXT("The last saved slot marks the authored fragment complete"),F.Loop->bAuthoredFragmentComplete);
    TestEqual(TEXT("Fragment completion keeps the actual run working"),F.State->Phase,EMCShiftPhase::Working);
    TestNotNull(TEXT("A real adaptive scheduler owns arena support"),F.Mode->GameDirector.Get());
    TestNull(TEXT("The fragment does not create a Zombie finale"),F.Loop->FinalBoss.Get());
    TestNull(TEXT("Completing fog removes its event and restores visibility"),F.Loop->FogEvent.Get());
    TestEqual(TEXT("Open support publishes no event countdown"),F.State->PhaseEndsAt,0.);
    TestEqual(TEXT("Open support publishes no director deadline"),F.State->DirectorState.DayEndAt,0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayNoDeadlineTest,"MessControl.SingleDay.Sequence.SupportSurvivesOldFinaleDayAndRunTargets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayNoDeadlineTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.CompleteFogSlot();
    if (!TestNotNull(TEXT("The clock regression has a real support scheduler"),F.Mode->GameDirector.Get())) return false;
    for (const float Seconds:{31.f,361.f,2101.f}) {
        F.AdvanceClockTo(Seconds);
        TestTrue(TEXT("The fixture advanced actual server world time beyond the target"),F.State->GetServerWorldTimeSeconds()>=Seconds-.01f);
        F.Mode->GameDirector->Tick(.25f);F.Loop->Tick(.1f);
        TestEqual(TEXT("Old finale/day/full-run targets cannot end the new fragment"),F.State->Phase,EMCShiftPhase::Working);
        TestEqual(TEXT("The sequence keeps its support stage"),F.Loop->Stage,EMCSingleDayStage::Director);
        TestTrue(TEXT("Support remains managed after every old deadline"),F.Mode->GameDirector->IsManagingEvents());
        TestNull(TEXT("Elapsed orientation time cannot spawn a final boss"),F.Loop->FinalBoss.Get());
        TestEqual(TEXT("No expired clock is published"),F.State->DirectorState.DayEndAt,0.);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayIceDispatchTest,"MessControl.SingleDay.Sequence.NutIntervalDispatchesIceAndStopsSupport",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayIceDispatchTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry();
    TestEqual(TEXT("New sequence has exactly three implemented key events"),F.Sequence->KeyEvents.Num(),3);
    TestEqual(TEXT("Ice follows the nut encounter"),F.Sequence->KeyEvents[1].Kind,EMCSingleDayKeyEventKind::IceEvent);
    TestEqual(TEXT("Fog follows ice"),F.Sequence->KeyEvents[2].Kind,EMCSingleDayKeyEventKind::FogBrawl);
    F.CompleteNutSlot();
    TestFalse(TEXT("Completing nuts leaves the ice slot outstanding"),F.Loop->bAuthoredFragmentComplete);
    if(!TestNotNull(TEXT("The interval uses the real support scheduler"),F.Mode->GameDirector.Get())) return false;
    auto* OldSupport=F.Mode->GameDirector.Get();
    const double Deadline=F.State->DirectorState.DayEndAt;
    TestTrue(TEXT("The nut slot sets a bounded interval before ice"),Deadline>F.State->GetServerWorldTimeSeconds());
    F.AdvanceClockTo(float(Deadline)-.2f);F.Loop->Tick(.1f);
    TestEqual(TEXT("Ice waits until the authored interval elapses"),F.Loop->Stage,EMCSingleDayStage::Director);
    TestNull(TEXT("No ice actor exists before the interval ends"),F.Loop->IceEvent.Get());
    F.RestoreTongueCollision();F.AdvanceClockTo(float(Deadline)+.2f);F.Loop->Tick(.1f);
    TestEqual(TEXT("The interval dispatches the second key-event kind"),F.Loop->Stage,EMCSingleDayStage::Ice);
    TestEqual(TEXT("The sequence advances exactly once"),F.Loop->KeyEventIndex,1);
    if(!TestNotNull(TEXT("Ice dispatch creates the real event"),F.Loop->IceEvent.Get())) return false;
    TestFalse(TEXT("Ice starts with the fixture's valid tongue"),F.Loop->IceEvent->bFailed);
    TestNull(TEXT("Ordinary support is detached during the key event"),F.Mode->GameDirector.Get());
    TestTrue(TEXT("The previous support actor is destroyed"),OldSupport->IsActorBeingDestroyed());
    TestNull(TEXT("The previous nut actor is removed before ice"),F.Loop->NutEvent.Get());
    TestTrue(TEXT("The HUD publishes ice completion progress"),F.State->TasksTotal>0 && F.State->TasksLeft>0);
    F.Loop->Stop();
    TestNull(TEXT("Stopping the sequence removes the ice actor"),F.Loop->IceEvent.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDaySavedNutCompatibilityTest,"MessControl.SingleDay.Sequence.SavedNutOnlyProfileAppendsIceAndFogWithoutChangingSavedSettings",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDaySavedNutCompatibilityTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F(1);F.AddWorkerAndFoodGeometry();F.AddFogTarget();
    F.CompleteNutSlot();
    TestEqual(TEXT("Runtime compatibility leaves the saved slot count intact"),F.Sequence->KeyEvents.Num(),1);
    TestEqual(TEXT("The saved event identity is preserved"),F.Sequence->KeyEvents[0].EventId,FName(TEXT("AuthoredNut")));
    TestFalse(TEXT("Runtime compatibility adds an outstanding ice event"),F.Loop->bAuthoredFragmentComplete);
    const double Deadline=F.State->DirectorState.DayEndAt;
    TestTrue(TEXT("Runtime compatibility preserves the authored nine-second interval"),
        FMath::IsNearlyEqual(Deadline-F.State->GetServerWorldTimeSeconds(),9.,.01));
    F.RestoreTongueCollision();F.AdvanceClockTo(float(Deadline)+.2f);F.Loop->Tick(.1f);
    TestEqual(TEXT("The extended runtime sequence dispatches ice"),F.Loop->Stage,EMCSingleDayStage::Ice);
    if(!TestNotNull(TEXT("The old profile now runs a real ice event"),F.Loop->IceEvent.Get())) return false;
    TestFalse(TEXT("The compatible ice event starts successfully"),F.Loop->IceEvent->bFailed);
    TestEqual(TEXT("Dispatch never mutates the original saved profile"),F.Sequence->KeyEvents.Num(),1);
    F.Loop->IceEvent->Stage=EMCIceEventStage::Complete;F.Loop->IceEvent->CandyHealth=0;F.Loop->Tick(.1f);
    TestFalse(TEXT("The oldest prototype also receives an outstanding fog slot"),F.Loop->bAuthoredFragmentComplete);
    const double FogDeadline=F.State->DirectorState.DayEndAt;
    TestTrue(TEXT("The new ice slot uses the normal 120-second support interval"),
        FMath::IsNearlyEqual(FogDeadline-F.State->GetServerWorldTimeSeconds(),120.,.01));
    F.AdvanceClockTo(float(FogDeadline)+.2f);F.Loop->Tick(.1f);
    TestEqual(TEXT("The oldest prototype dispatches fog after ice support"),F.Loop->Stage,EMCSingleDayStage::FogBrawl);
    if(!TestNotNull(TEXT("Oldest prototype creates the real fog event"),F.Loop->FogEvent.Get())) return false;
    TestFalse(TEXT("The compatible fog event starts successfully"),F.Loop->FogEvent->bFailed);
    TestEqual(TEXT("Adding both new events leaves the saved profile untouched"),F.Sequence->KeyEvents.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFogDispatchTest,"MessControl.SingleDay.Sequence.IceIntervalDispatchesFogAndStopsSupport",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFogDispatchTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry();F.AddFogTarget();F.CompleteIceSlot();
    TestFalse(TEXT("Completing ice leaves the fog slot outstanding"),F.Loop->bAuthoredFragmentComplete);
    TestNull(TEXT("Ice is removed during support so the arena can thaw"),F.Loop->IceEvent.Get());
    if(!TestNotNull(TEXT("Ice completion starts the real support scheduler"),F.Mode->GameDirector.Get())) return false;
    auto* OldSupport=F.Mode->GameDirector.Get();
    const double Deadline=F.State->DirectorState.DayEndAt;
    TestTrue(TEXT("Ice uses its 120-second authored interval before fog"),
        FMath::IsNearlyEqual(Deadline-F.State->GetServerWorldTimeSeconds(),120.,.01));
    F.AdvanceClockTo(float(Deadline)-.2f);F.Loop->Tick(.1f);
    TestEqual(TEXT("Fog waits for the whole ice support interval"),F.Loop->Stage,EMCSingleDayStage::Director);
    TestNull(TEXT("Fog cannot start before its interval ends"),F.Loop->FogEvent.Get());
    F.RestoreTongueCollision();F.AdvanceClockTo(float(Deadline)+.2f);F.Loop->Tick(.1f);
    TestEqual(TEXT("The elapsed interval dispatches fog"),F.Loop->Stage,EMCSingleDayStage::FogBrawl);
    TestEqual(TEXT("Fog is exactly the third key-event slot"),F.Loop->KeyEventIndex,2);
    if(!TestNotNull(TEXT("Dispatch creates the real fog event"),F.Loop->FogEvent.Get())) return false;
    TestFalse(TEXT("Fog starts with the fixture tongue and controlled worker"),F.Loop->FogEvent->bFailed);
    TestNull(TEXT("Fog detaches ordinary support"),F.Mode->GameDirector.Get());
    TestTrue(TEXT("Fog destroys the preceding support actor"),OldSupport->IsActorBeingDestroyed());
    TestEqual(TEXT("The fog HUD starts with all strikes outstanding"),F.State->TasksLeft,F.Loop->FogEvent->TotalStrikes);
    F.AdvanceClockTo(float(F.Loop->FogEvent->StageEndsAt)+.2f);F.Loop->FogEvent->Tick(.1f);
    TestEqual(TEXT("The actual fog actor enters its first warning"),F.Loop->FogEvent->Stage,EMCFogBrawlStage::Warning);
    if(!TestNotNull(TEXT("The warning selects an existing reachable reserve tooth"),F.Loop->FogEvent->ActiveTarget.Get())) return false;
    F.Loop->FogEvent->StrikesResolved=2;F.Loop->Tick(.1f);
    TestEqual(TEXT("The HUD counts unresolved strikes"),F.State->TasksLeft,F.Loop->FogEvent->TotalStrikes-2);
    TestEqual(TEXT("The HUD retains the total strike count"),F.State->TasksTotal,F.Loop->FogEvent->TotalStrikes);
    TestTrue(TEXT("Resolving strikes advances the published completion progress"),F.State->DirectorState.CompletionProgress>0);
    auto* Fog=F.Loop->FogEvent.Get();F.Loop->Stop();
    TestNull(TEXT("Stopping the sequence removes fog"),F.Loop->FogEvent.Get());
    TestTrue(TEXT("Stopping destroys the actual fog actor"),Fog->IsActorBeingDestroyed());
    F.AdvanceClockTo(float(Deadline)+600.f);F.Loop->Tick(.1f);
    TestNull(TEXT("A stopped sequence cannot start another fog event"),F.Loop->FogEvent.Get());
    TestNull(TEXT("A stopped sequence cannot restart ordinary support"),F.Mode->GameDirector.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDaySavedIceCompatibilityTest,"MessControl.SingleDay.Sequence.SavedNutIceProfileAppendsFogWithoutChangingSavedSettings",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDaySavedIceCompatibilityTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F(2);F.AddWorkerAndFoodGeometry();F.AddFogTarget();F.CompleteIceSlot();
    TestEqual(TEXT("Runtime compatibility preserves the two saved slots"),F.Sequence->KeyEvents.Num(),2);
    TestEqual(TEXT("Runtime compatibility preserves the saved ice identity"),F.Sequence->KeyEvents[1].EventId,FName(TEXT("AuthoredIce")));
    TestFalse(TEXT("The two-slot prototype receives an outstanding fog event"),F.Loop->bAuthoredFragmentComplete);
    const double Deadline=F.State->DirectorState.DayEndAt;
    TestTrue(TEXT("Compatibility preserves the authored 17-second ice interval"),
        FMath::IsNearlyEqual(Deadline-F.State->GetServerWorldTimeSeconds(),17.,.01));
    F.RestoreTongueCollision();F.AdvanceClockTo(float(Deadline)+.2f);F.Loop->Tick(.1f);
    TestEqual(TEXT("The extended two-slot prototype dispatches fog"),F.Loop->Stage,EMCSingleDayStage::FogBrawl);
    if(!TestNotNull(TEXT("Two-slot prototype creates the real fog event"),F.Loop->FogEvent.Get())) return false;
    TestFalse(TEXT("The compatible fog event starts successfully"),F.Loop->FogEvent->bFailed);
    TestEqual(TEXT("Dispatch leaves the original profile's ice interval intact"),F.Sequence->KeyEvents[1].DirectorSupportSeconds,17.f);
    TestEqual(TEXT("Dispatch leaves the original profile's slot count intact"),F.Sequence->KeyEvents.Num(),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFailedFogTest,"MessControl.SingleDay.Sequence.FailedFogSetupTerminatesAndCleansEvent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFailedFogTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    F.Loop->KeyEventIndex=2;F.Loop->Stage=EMCSingleDayStage::FogBrawl;
    F.Loop->FogEvent=F.World->SpawnActor<AMCFogBrawlEvent>();
    F.Loop->FogEvent->Stage=EMCFogBrawlStage::Complete;F.Loop->FogEvent->bFailed=true;
    AddExpectedError(TEXT("MC_SINGLE_DAY FAILED:"),EAutomationExpectedErrorFlags::Contains,1);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Fog failure takes precedence over a terminal event signal"),F.State->Phase,EMCShiftPhase::Lost);
    TestNull(TEXT("Failure removes the fog actor"),F.Loop->FogEvent.Get());
    TestNull(TEXT("Failure cannot start ordinary support"),F.Mode->GameDirector.Get());
    TestFalse(TEXT("Failure never marks the authored fragment complete"),F.Loop->bAuthoredFragmentComplete);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFailedIceTest,"MessControl.SingleDay.Sequence.FailedIceSetupTerminatesAndCleansEvent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFailedIceTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    F.Loop->KeyEventIndex=1;F.Loop->Stage=EMCSingleDayStage::Ice;
    F.Loop->IceEvent=F.World->SpawnActor<AMCIceEvent>();
    F.Loop->IceEvent->Stage=EMCIceEventStage::Complete;F.Loop->IceEvent->bFailed=true;
    AddExpectedError(TEXT("MC_SINGLE_DAY FAILED:"),EAutomationExpectedErrorFlags::Contains,1);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("A failed ice setup ends the run instead of advancing"),F.State->Phase,EMCShiftPhase::Lost);
    TestNull(TEXT("Failure cleans up the ice actor"),F.Loop->IceEvent.Get());
    TestNull(TEXT("Failure cannot start ordinary support"),F.Mode->GameDirector.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDaySupportPoolTest,"MessControl.SingleDay.Sequence.SupportAdmitsFoodColaAndRepairWithoutDayOrDeadlineGates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDaySupportPoolTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry();
    for (auto& Day:F.Support->Days) {
        Day.PressureLimit=3;Day.TargetPressureMin=1;Day.TargetPressureMax=2;
        Day.FoodWorkPerPlayer=120;Day.MaxWholeFood=4;Day.MaxFragments=24;
    }
    for (auto& Rule:F.Support->Events) {
        if (Rule.Kind==EMCGameDirectorEvent::Food || Rule.Kind==EMCGameDirectorEvent::ColdCola || Rule.Kind==EMCGameDirectorEvent::LooseTooth) {
            Rule.Weight=1;Rule.FirstDay=7;Rule.MinimumDifficulty=0;
        }
    }
    auto* Director=F.World->SpawnActor<AMCGameDirector>();F.Mode->GameDirector=Director;
    Director->InitializeRun(F.Plan,F.Support);Director->BeginSupport();F.FreezeActors();F.AdvanceClockTo(2);
    TestEqual(TEXT("This is really day one, below the authored event gates"),F.State->Day,1);
    TestEqual(TEXT("The support snapshot has no deadline"),Director->Observe().DayEndAt,0.);
    const auto Seen=Director->Observe();
    if (!TestTrue(TEXT("The candidate fixture has a living available worker and food surface"),Seen.AvailablePlayers>0 && !Seen.bUrgent && !Seen.bGlobalMovement)) return false;
    const auto Candidates=Director->EvaluateCandidates(Seen,F.State->GetServerWorldTimeSeconds());
    for (const auto Kind:{EMCGameDirectorEvent::Food,EMCGameDirectorEvent::ColdCola,EMCGameDirectorEvent::LooseTooth}) {
        const auto* Candidate=Candidates.FindByPredicate([Kind](const auto& Value){return Value.Kind==Kind;});
        if (!TestNotNull(TEXT("Every ordinary event remains in the runtime candidate pool"),Candidate)) return false;
        TestTrue(FString::Printf(TEXT("Ordinary kind %d can be chosen on day one: %s"),int32(Kind),*Candidate->BlockReason),
            Candidate->BlockReason.IsEmpty() && Candidate->EffectiveWeight>0 && Candidate->Probability>0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDaySharedHistoryTest,"MessControl.SingleDay.Sequence.SharedHistoryIsBoundedAndSurvivesKeyEventPublish",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDaySharedHistoryTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    F.State->DirectorDecisionLog.Reset();
    for (int32 Index=0;Index<140;++Index) F.State->RecordDirectorDecision(FString::Printf(TEXT("SequenceHistory%03d"),Index));
    if (!TestEqual(TEXT("Run history keeps exactly its last 128 decisions"),F.State->DirectorDecisionLog.Num(),128)) return false;
    TestEqual(TEXT("Bounded history evicts only the oldest decisions"),F.State->DirectorDecisionLog[0],FString(TEXT("SequenceHistory012")));
    const auto Before=F.State->DirectorDecisionLog;
    F.Loop->Stage=EMCSingleDayStage::Nuts;F.Loop->NutEvent=F.World->SpawnActor<AMCNutRainEvent>();
    F.Loop->NutEvent->SetActorTickEnabled(false);F.Loop->NutEvent->Stage=EMCNutRainStage::Enemies;F.Loop->NutEvent->EnemiesLeft=3;
    F.Loop->Tick(.1f);
    TestTrue(TEXT("Replacing the key-event HUD card preserves every retained decision"),F.State->DirectorDecisionLog==Before);
    TestTrue(TEXT("The current HUD snapshot still exposes shared history"),F.State->DirectorState.DecisionLog==Before);
    TestEqual(TEXT("The latest decision is not replaced by transient key-event text"),F.State->DirectorState.LastDecision,Before.Last());
    F.State->RecordDirectorDecision(TEXT("AfterKeyEvent"));
    TestEqual(TEXT("Later scheduler writes keep the same bound"),F.State->DirectorDecisionLog.Num(),128);
    TestEqual(TEXT("Later decisions append to the shared history"),F.State->DirectorDecisionLog.Last(),FString(TEXT("AfterKeyEvent")));
    return true;
}
#endif
