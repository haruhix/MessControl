#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCGameDirector.h"
#include "MCGameDirectorProfile.h"
#include "MCSingleDayDirector.h"
#include "MCTutorialDirector.h"
#include "MCNutRainEvent.h"
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
    FSequenceWorld()
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
        auto* Sequence=NewObject<UMCSingleDayProfile>(World);
        Sequence->KeyEvents[0].CompletionExperience=0;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFragmentSupportTest,"MessControl.SingleDay.Sequence.CompletedNutSlotOpensSupportWithoutFinalBoss",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFragmentSupportTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    TestFalse(TEXT("The new sequence defaults away from a timed finale"),F.Loop->IsLegacyTimedFinale());
    F.CompleteNutSlot();
    TestEqual(TEXT("The terminal nut signal hands off to ordinary support"),F.Loop->Stage,EMCSingleDayStage::Director);
    TestTrue(TEXT("The last saved slot marks the authored fragment complete"),F.Loop->bAuthoredFragmentComplete);
    TestEqual(TEXT("Fragment completion keeps the actual run working"),F.State->Phase,EMCShiftPhase::Working);
    TestNotNull(TEXT("A real adaptive scheduler owns arena support"),F.Mode->GameDirector.Get());
    TestNull(TEXT("The fragment does not create a Zombie finale"),F.Loop->FinalBoss.Get());
    TestEqual(TEXT("Open support publishes no event countdown"),F.State->PhaseEndsAt,0.);
    TestEqual(TEXT("Open support publishes no director deadline"),F.State->DirectorState.DayEndAt,0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayNoDeadlineTest,"MessControl.SingleDay.Sequence.SupportSurvivesOldFinaleDayAndRunTargets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayNoDeadlineTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.CompleteNutSlot();
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
