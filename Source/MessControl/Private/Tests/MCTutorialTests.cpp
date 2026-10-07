#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCTutorialTypes.h"
#include "MCTutorialDirector.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace MCTutorialTestsPrivate
{
    FMCTutorialPlayerProgress NewLearner()
    {
        FMCTutorialPlayerProgress Player;
        Player.bLoaded=true;
        Player.StageRequired=1;
        return Player;
    }

    struct FWorldFixture
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        AMCGameMode* Mode=nullptr;
        AMCGameState* State=nullptr;
        explicit FWorldFixture(bool bTutorial=false)
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL; URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
            if (bTutorial) URL.AddOption(TEXT("MCTutorial=1"));
            World->SetGameMode(URL);
            Mode=World->GetAuthGameMode<AMCGameMode>();
            // These tutorial fixtures retain the authored day-one/legacy handoff.
            Mode->bUseAdaptiveDirector=false;
            World->InitializeActorsForPlay(URL); World->BeginPlay();
            State=World->GetGameState<AMCGameState>();
            Mode->SetActorTickEnabled(false);
        }
        ~FWorldFixture()
        { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
        APlayerController* AddLearner(float Y)
        {
            auto* PC=World->SpawnActor<APlayerController>();
            auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,Y,120),FRotator::ZeroRotator);
            Hero->SetActorTickEnabled(false); Hero->GetCharacterMovement()->DisableMovement(); PC->Possess(Hero);
            return PC;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTutorialPersonalCreditTest,"MessControl.Tutorial.PersonalCreditAndWrongActions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTutorialPersonalCreditTest::RunTest(const FString&)
{
    TArray<FMCTutorialPlayerProgress> Team={MCTutorialTestsPrivate::NewLearner(),MCTutorialTestsPrivate::NewLearner()};
    auto& First=Team[0];
    auto& Second=Team[1];
    TestFalse(TEXT("Tongue cleaning cannot pass the tooth lesson"),
        FMCTutorialProgressRules::CreditAction(First,EMCTutorialStage::BrushTooth,EMCTutorialAction::BrushTongue,true));
    TestFalse(TEXT("Cleaning another learner's target cannot pass a personal lesson"),
        FMCTutorialProgressRules::CreditAction(First,EMCTutorialStage::BrushTooth,EMCTutorialAction::BrushTooth,false));
    TestEqual(TEXT("Wrong actions preserve progress"),First.StageProgress,0);
    TestEqual(TEXT("Wrong actions do not record a learned skill"),First.CompletedActions,0);
    TestTrue(TEXT("Authoritative completion of an owned tooth target passes"),
        FMCTutorialProgressRules::CreditAction(First,EMCTutorialStage::BrushTooth,EMCTutorialAction::BrushTooth,true));
    TestFalse(TEXT("Repeated completion is not counted twice"),
        FMCTutorialProgressRules::CreditAction(First,EMCTutorialStage::BrushTooth,EMCTutorialAction::BrushTooth,true));
    TestEqual(TEXT("The experienced player contributes only their personal completion"),FMCTutorialProgressRules::CompletedPlayers(Team),1);
    TestEqual(TEXT("The second learner still needs to try the action"),Second.StageProgress,0);
    TestEqual(TEXT("The second learner does not inherit another player's skill"),Second.CompletedActions,0);
    TestTrue(TEXT("The second learner can finish their own target"),
        FMCTutorialProgressRules::CreditAction(Second,EMCTutorialStage::BrushTooth,EMCTutorialAction::BrushTooth,true));
    TestEqual(TEXT("The team can advance only after both personal completions"),FMCTutorialProgressRules::CompletedPlayers(Team),Team.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTutorialSortingTest,"MessControl.Tutorial.SortingRequiresCorrectDestination",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTutorialSortingTest::RunTest(const FString&)
{
    auto Player=MCTutorialTestsPrivate::NewLearner();
    TestFalse(TEXT("Discarding fresh food does not pass delivery"),
        FMCTutorialProgressRules::CreditAction(Player,EMCTutorialStage::FreshSort,EMCTutorialAction::TrashDiscarded,true));
    TestTrue(TEXT("Delivering fresh food passes"),
        FMCTutorialProgressRules::CreditAction(Player,EMCTutorialStage::FreshSort,EMCTutorialAction::FoodDelivered,true));
    const int32 FreshSkill=Player.CompletedActions;
    Player.StageActions=0; Player.StageProgress=0;
    TestFalse(TEXT("Sending spoiled food down the throat does not pass sorting"),
        FMCTutorialProgressRules::CreditAction(Player,EMCTutorialStage::SpoiledSort,EMCTutorialAction::FoodDelivered,true));
    TestFalse(TEXT("A foreign object is not the required spoiled-food example"),
        FMCTutorialProgressRules::CreditAction(Player,EMCTutorialStage::SpoiledSort,EMCTutorialAction::TrashDiscarded,true));
    TestEqual(TEXT("Wrong disposal preserves the previously learned fresh-food skill"),Player.CompletedActions,FreshSkill);
    TestTrue(TEXT("Discarding the spoiled example passes"),
        FMCTutorialProgressRules::CreditAction(Player,EMCTutorialStage::SpoiledSort,EMCTutorialAction::SpoiledDiscarded,true));
    Player.StageActions=0; Player.StageProgress=0;
    TestFalse(TEXT("The retired lesson cannot award delivery credit"),
        FMCTutorialProgressRules::CreditAction(Player,EMCTutorialStage::TrashSort,EMCTutorialAction::FoodDelivered,true));
    TestFalse(TEXT("The retired tool-disposal lesson cannot award credit"),
        FMCTutorialProgressRules::CreditAction(Player,EMCTutorialStage::TrashSort,EMCTutorialAction::TrashDiscarded,true));
    TestEqual(TEXT("The retired lesson has no required action"),FMCTutorialProgressRules::RequiredActions(EMCTutorialStage::TrashSort),0);
    const int32 SortingSkills=FMCTutorialProgressRules::ActionBit(EMCTutorialAction::FoodDelivered)
        |FMCTutorialProgressRules::ActionBit(EMCTutorialAction::SpoiledDiscarded);
    TestEqual(TEXT("Both food-sorting skills survive stage changes"),Player.CompletedActions & SortingSkills,SortingSkills);
    TestEqual(TEXT("Legacy trash credit is not recorded"),Player.CompletedActions & FMCTutorialProgressRules::ActionBit(EMCTutorialAction::TrashDiscarded),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTutorialLoadingAndDisconnectTest,"MessControl.Tutorial.LoadingAndDisconnectGate",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTutorialLoadingAndDisconnectTest::RunTest(const FString&)
{
    TArray<FMCTutorialPlayerProgress> Team;
    TestFalse(TEXT("An empty room does not start training"),FMCTutorialProgressRules::AllLoaded(Team));
    TestFalse(TEXT("An empty room does not start day one"),FMCTutorialProgressRules::AllReady(Team));
    Team.Add(MCTutorialTestsPrivate::NewLearner());
    Team.Add(FMCTutorialPlayerProgress());
    TestFalse(TEXT("A loaded host cannot start before the second participant loads"),FMCTutorialProgressRules::AllLoaded(Team));
    Team[1].bLoaded=true;
    TestTrue(TEXT("Training can start when every active participant is loaded"),FMCTutorialProgressRules::AllLoaded(Team));
    Team[0].StageProgress=1; Team[0].StageRequired=1;
    Team[1].StageRequired=1;
    TestEqual(TEXT("An unfinished participant blocks personal-stage completion"),FMCTutorialProgressRules::CompletedPlayers(Team),1);
    Team.RemoveAt(1);
    TestTrue(TEXT("A disconnected participant does not keep loading blocked"),FMCTutorialProgressRules::AllLoaded(Team));
    TestEqual(TEXT("A disconnected participant does not keep a lesson blocked"),FMCTutorialProgressRules::CompletedPlayers(Team),Team.Num());
    TestTrue(TEXT("The remaining participant can confirm completion"),
        FMCTutorialProgressRules::SetReady(Team[0],EMCTutorialStage::Complete,true));
    TestTrue(TEXT("Ready gate follows the active roster"),FMCTutorialProgressRules::AllReady(Team));
    Team.Reset();
    TestFalse(TEXT("The last disconnect cannot silently launch day one"),FMCTutorialProgressRules::AllReady(Team));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTutorialReadyTest,"MessControl.Tutorial.ReadyRequiresCompletionAndWholeTeam",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTutorialReadyTest::RunTest(const FString&)
{
    TArray<FMCTutorialPlayerProgress> Team={MCTutorialTestsPrivate::NewLearner(),MCTutorialTestsPrivate::NewLearner()};
    TestFalse(TEXT("A client cannot mark ready during the introduction"),
        FMCTutorialProgressRules::SetReady(Team[0],EMCTutorialStage::Intro,true));
    TestFalse(TEXT("A client cannot skip a practical stage with ready"),
        FMCTutorialProgressRules::SetReady(Team[0],EMCTutorialStage::Calculus,true));
    TestFalse(TEXT("Premature requests preserve readiness"),Team[0].bReady);
    TestTrue(TEXT("The first finished learner can mark ready"),
        FMCTutorialProgressRules::SetReady(Team[0],EMCTutorialStage::Complete,true));
    TestFalse(TEXT("Duplicate ready requests have no side effects"),
        FMCTutorialProgressRules::SetReady(Team[0],EMCTutorialStage::Complete,true));
    TestFalse(TEXT("One ready player does not launch the next day"),FMCTutorialProgressRules::AllReady(Team));
    TestTrue(TEXT("The second learner can confirm"),
        FMCTutorialProgressRules::SetReady(Team[1],EMCTutorialStage::Complete,true));
    TestTrue(TEXT("Only the whole active team opens the ready gate"),FMCTutorialProgressRules::AllReady(Team));
    TestTrue(TEXT("A learner may cancel readiness before the transition"),
        FMCTutorialProgressRules::SetReady(Team[1],EMCTutorialStage::Complete,false));
    TestFalse(TEXT("Cancelling readiness closes the gate"),FMCTutorialProgressRules::AllReady(Team));
    TestFalse(TEXT("Finished tutorial state cannot be reopened by ready"),
        FMCTutorialProgressRules::SetReady(Team[1],EMCTutorialStage::Finished,true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTutorialFixedToolsTest,"MessControl.Tutorial.FixedToolsAndSkippedToolDisposal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTutorialFixedToolsTest::RunTest(const FString&)
{
    MCTutorialTestsPrivate::FWorldFixture Fixture;
    Fixture.State->Phase=EMCShiftPhase::Working; Fixture.State->bTutorialActive=true;
    // Saved legacy flags must not turn permanent inventory slots into world pickups.
    Fixture.State->bPhysicalBrushes=true;
    auto* PC=Fixture.AddLearner(0);
    auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn());
    auto* Player=PC->GetPlayerState<AMCPlayerState>();
    if (!TestNotNull(TEXT("The learner has a player state"),Player)
        || !TestNotNull(TEXT("The learner has an inventory"),Hero->Inventory.Get())) return false;
    TestNull(TEXT("The default brush needs no equipped pickup actor"),Hero->EquippedBrush.Get());
    TestEqual(TEXT("A new learner starts in the brush slot"),Hero->Inventory->Selected,EMCToolSlot::Brush);
    TestTrue(TEXT("The default brush is usable despite the legacy physical-brush flag"),Hero->HasBrush());
    const EMCToolSlot Slots[]={EMCToolSlot::Pickaxe,EMCToolSlot::Knife,EMCToolSlot::Spray,EMCToolSlot::Brush};
    for (const auto Slot:Slots)
    {
        Hero->Inventory->ServerSelect(Slot);
        TestEqual(TEXT("Every default tool can be selected without a pickup"),Hero->Inventory->Selected,Slot);
        TestEqual(TEXT("Cleaning follows the selected slot"),Hero->HasBrush(),Slot==EMCToolSlot::Brush);
    }
    Hero->ServerThrowItem();
    TestTrue(TEXT("Q with empty hands preserves the inventory brush"),Hero->HasBrush());
    TestNull(TEXT("Q does not create a disposable brush actor"),Hero->EquippedBrush.Get());
    auto* Director=Fixture.World->SpawnActor<AMCTutorialDirector>();
    if (!TestNotNull(TEXT("The real tutorial director spawns"),Director)) return false;
    Director->SetActorTickEnabled(false); Director->Start(); Director->SetLoaded(Player); Director->Tick(.01f);
    TestEqual(TEXT("Loaded learners enter the introduction"),Director->Stage,EMCTutorialStage::Intro);
    auto HasArenaBrush=[&]()
    {
        for (TActorIterator<AMCFoodActor> It(Fixture.World);It;++It)
            if (It->bBrushTool && !It->IsActorBeingDestroyed()) return true;
        return false;
    };
    TestFalse(TEXT("The introduction does not spawn tool pickups"),HasArenaBrush());
    Director->StageEndsAt=Fixture.State->GetServerWorldTimeSeconds()+.001;
    ++GFrameCounter; Fixture.World->Tick(LEVELTICK_TimeOnly,.02f); Director->Tick(.01f);
    TestEqual(TEXT("The introduction leads directly to brush practice"),Director->Stage,EMCTutorialStage::BrushTooth);
    TestTrue(TEXT("Brush practice starts with the existing usable tool"),Hero->HasBrush());
    TestFalse(TEXT("Brush practice does not spawn tool pickups"),HasArenaBrush());
    // Set up the completed gate; sorting action ownership is tested separately.
    Director->Stage=EMCTutorialStage::SpoiledSort;
    Director->Players[0].StageRequired=1; Director->Players[0].StageProgress=1;
    Director->Tick(.01f);
    TestEqual(TEXT("Completed spoiled-food sorting skips tool disposal"),Director->Stage,EMCTutorialStage::BreakfastRain);
    TestFalse(TEXT("The skipped lesson does not create an old brush"),HasArenaBrush());
    Director->Stop();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTutorialDirectorIsolationTest,"MessControl.Tutorial.DirectorRosterTargetsAndNormalLoopIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTutorialDirectorIsolationTest::RunTest(const FString&)
{
    MCTutorialTestsPrivate::FWorldFixture Fixture;
    auto* Plan=NewObject<UMCDayPlan>(Fixture.World);
    Fixture.State->Day=3; Fixture.State->DayPlan=Plan; Fixture.State->StepIndex=4;
    Fixture.State->Phase=EMCShiftPhase::Working; Fixture.State->PhaseEndsAt=42;
    Fixture.State->TasksLeft=3; Fixture.State->TasksTotal=7; Fixture.State->MouthHealth=73;
    Fixture.State->FailedEvents=2; Fixture.State->bDayOneComplete=false; Fixture.State->bTutorialActive=true;
    const float FloodHeight=Plan->FloodHeight,FlowAcceleration=Plan->FlowAcceleration;
    auto* FirstPC=Fixture.AddLearner(-220);
    auto* SecondPC=Fixture.AddLearner(220);
    auto* First=FirstPC->GetPlayerState<AMCPlayerState>();
    auto* Second=SecondPC->GetPlayerState<AMCPlayerState>();
    auto* Hero=Cast<AMCToothCharacter>(FirstPC->GetPawn());
    if (!TestNotNull(TEXT("First learner has the real replicated PlayerState"),First)
        || !TestNotNull(TEXT("Second learner has the real replicated PlayerState"),Second)) return false;
    auto* OrdinaryFood=Fixture.World->SpawnActor<AMCFoodActor>(FVector(400,0,300),FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("An ordinary gameplay food actor exists before training"),OrdinaryFood)) return false;
    OrdinaryFood->Batch=123;
    auto* Director=Fixture.World->SpawnActor<AMCTutorialDirector>();
    if (!TestNotNull(TEXT("Tutorial director spawns"),Director)) return false;
    Director->SetActorTickEnabled(false); Director->Start(Plan);
    Director->SetReady(First,true);
    TestFalse(TEXT("The actual server director rejects premature ready"),Director->GetPlayerProgress(First)->bReady);
    Director->SetLoaded(First); Director->Tick(.01f);
    TestEqual(TEXT("The actual director waits for the second client"),Director->Stage,EMCTutorialStage::Loading);
    Director->SetLoaded(Second); Director->Tick(.01f);
    TestEqual(TEXT("All loaded clients enter the introduction"),Director->Stage,EMCTutorialStage::Intro);
    Director->StageEndsAt=Fixture.State->GetServerWorldTimeSeconds()+.001;
    ++GFrameCounter; Fixture.World->Tick(LEVELTICK_TimeOnly,.02f); Director->Tick(.01f);
    if (!TestEqual(TEXT("The introduction opens the untimed brush lesson"),Director->Stage,EMCTutorialStage::BrushTooth)) return false;
    TestEqual(TEXT("Personal brush practice has no deadline"),Director->StageEndsAt,0.);
    TestTrue(TEXT("Personal brush practice has a usable default brush"),Hero->HasBrush());
    auto* FirstGoal=Cast<AMCArenaTooth>(Director->GetPlayerProgress(First)->GoalTarget.Get());
    auto* SecondGoal=Cast<AMCArenaTooth>(Director->GetPlayerProgress(Second)->GoalTarget.Get());
    if (!TestNotNull(TEXT("First learner gets an actual tooth target"),FirstGoal)
        || !TestNotNull(TEXT("Second learner gets an actual tooth target"),SecondGoal)) return false;
    TestNotSamePtr(TEXT("Each learner gets a distinct target"),FirstGoal,SecondGoal);
    Director->NotifyAction(Hero,EMCTutorialAction::BrushTooth,FirstGoal);
    TestEqual(TEXT("Reporting contact before the tooth is clean cannot pass"),Director->GetPlayerProgress(First)->StageProgress,0);
    Director->NotifyAction(Hero,EMCTutorialAction::BrushTongue,FirstGoal);
    TestEqual(TEXT("Wrong action cannot pass even at the assigned target"),Director->GetPlayerProgress(First)->StageProgress,0);
    while (FirstGoal->Status->NeedsCare(true)) FirstGoal->Status->CareContact(true,Hero);
    TestEqual(TEXT("A real completed care action passes the owner's lesson"),Director->GetPlayerProgress(First)->StageProgress,1);
    TestEqual(TEXT("Training does not award ordinary score"),First->Points,0);
    while (SecondGoal->Status->NeedsCare(true)) SecondGoal->Status->CareContact(true);
    Director->NotifyAction(Hero,EMCTutorialAction::BrushTooth,SecondGoal);
    TestEqual(TEXT("An experienced player cannot complete the other learner's target"),Director->GetPlayerProgress(Second)->StageProgress,0);
    Director->Tick(.01f);
    TestEqual(TEXT("The unfinished active learner blocks the next lesson"),Director->Stage,EMCTutorialStage::BrushTooth);
    SecondPC->Destroy(); Director->Tick(.01f);
    TestEqual(TEXT("The actual roster removes a disconnected controller"),Director->GetRequiredPlayers(),1);
    TestEqual(TEXT("The disconnect allows the remaining completed learner to continue"),Director->Stage,EMCTutorialStage::FoodCut);
    TestTrue(TEXT("Learned actions survive the stage transition"),
        (Director->GetPlayerProgress(First)->CompletedActions & FMCTutorialProgressRules::ActionBit(EMCTutorialAction::BrushTooth))!=0);
    Director->Stop();
    TestEqual(TEXT("Stopped training has no active presentation"),Director->Stage,EMCTutorialStage::Finished);
    TestNull(TEXT("Finished tutorials are not discoverable as active"),AMCTutorialDirector::Find(Fixture.World));
    TestTrue(TEXT("Stopping destroys only tutorial-owned food"),IsValid(OrdinaryFood) && !OrdinaryFood->IsActorBeingDestroyed());
    TestEqual(TEXT("Tutorial leaves the normal day number unchanged"),Fixture.State->Day,3);
    TestSamePtr(TEXT("Tutorial leaves the normal day plan unchanged"),Fixture.State->DayPlan.Get(),Plan);
    TestEqual(TEXT("Tutorial leaves the normal stage index unchanged"),Fixture.State->StepIndex,4);
    TestEqual(TEXT("Tutorial leaves the normal event deadline unchanged"),Fixture.State->PhaseEndsAt,42.);
    TestEqual(TEXT("Tutorial leaves normal remaining tasks unchanged"),Fixture.State->TasksLeft,3);
    TestEqual(TEXT("Tutorial leaves normal task totals unchanged"),Fixture.State->TasksTotal,7);
    TestEqual(TEXT("Tutorial leaves mouth health unchanged"),Fixture.State->MouthHealth,73.f);
    TestEqual(TEXT("Tutorial leaves failure counters unchanged"),Fixture.State->FailedEvents,2);
    TestFalse(TEXT("Tutorial does not mark the ordinary first day complete"),Fixture.State->bDayOneComplete);
    TestEqual(TEXT("Tutorial tuning preserves the authored flood height"),Plan->FloodHeight,FloodHeight);
    TestEqual(TEXT("Tutorial tuning preserves the authored flow strength"),Plan->FlowAcceleration,FlowAcceleration);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTutorialGameModeHandoffTest,"MessControl.Tutorial.GameModeReadyHandoffResumesNormalLoop",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTutorialGameModeHandoffTest::RunTest(const FString&)
{
    MCTutorialTestsPrivate::FWorldFixture Fixture(true);
    auto* FirstPC=Fixture.AddLearner(-220);
    auto* SecondPC=Fixture.AddLearner(220);
    auto* First=FirstPC->GetPlayerState<AMCPlayerState>();
    auto* Second=SecondPC->GetPlayerState<AMCPlayerState>();
    if (!TestNotNull(TEXT("First active learner exists"),First)
        || !TestNotNull(TEXT("Second active learner exists"),Second)) return false;
    TestTrue(TEXT("The tutorial URL selects an isolated training run"),Fixture.State->bTutorialActive);
    TestEqual(TEXT("Training does not consume the first survival day"),Fixture.State->Day,0);
    Fixture.Mode->Tick(.01f);
    auto* Director=Fixture.Mode->TutorialDirector.Get();
    if (!TestNotNull(TEXT("The actual GameMode creates and owns the tutorial director"),Director)) return false;
    Director->SetActorTickEnabled(false);
    Director->SetLoaded(First); Director->SetLoaded(Second);
    // Lesson correctness is covered above. Exercise the real final-state delegate and GameMode handoff here.
    Director->Stage=EMCTutorialStage::Complete;
    Director->SetReady(First,true); Director->Tick(.01f);
    TestTrue(TEXT("The first confirmation leaves the team in training"),Fixture.State->bTutorialActive);
    TestSamePtr(TEXT("The first confirmation cannot replace the director"),Fixture.Mode->TutorialDirector.Get(),Director);
    Director->SetReady(Second,true); Director->Tick(.01f);
    TestFalse(TEXT("The last confirmation disables tutorial protection"),Fixture.State->bTutorialActive);
    TestFalse(TEXT("The completed tutorial does not reopen a lobby"),Fixture.State->bLobbyWaiting);
    TestNull(TEXT("The handoff releases the old tutorial director"),Fixture.Mode->TutorialDirector.Get());
    TestEqual(TEXT("The normal run begins at its existing intermission"),Fixture.State->Phase,EMCShiftPhase::Intermission);
    TestEqual(TEXT("The normal run resets its day counter"),Fixture.State->Day,0);
    TestNull(TEXT("The tutorial plan is not installed as the normal day's plan"),Fixture.State->DayPlan.Get());
    TestEqual(TEXT("The normal run has no tutorial failure penalties"),Fixture.State->FailedEvents,0);
    TestTrue(TEXT("The existing normal intermission deadline is restored"),Fixture.State->PhaseEndsAt>Fixture.State->GetServerWorldTimeSeconds());
    TestTrue(TEXT("The existing authored first-day plan remains enabled"),Fixture.Mode->bUseDayOnePlan);
    // The retained normal event loop also resumes when selected, proving no stale tutorial flag traps its Tick branch.
    Fixture.Mode->bUseDayOnePlan=false;
    Fixture.State->PhaseEndsAt=-1;
    Fixture.Mode->Tick(.01f);
    TestEqual(TEXT("The ordinary loop advances to survival day one"),Fixture.State->Day,1);
    TestEqual(TEXT("The ordinary loop runs a working event"),Fixture.State->Phase,EMCShiftPhase::Working);
    TestNotNull(TEXT("The ordinary event pool is active after the tutorial"),Fixture.State->CurrentEvent.Get());
    return true;
}
#endif
