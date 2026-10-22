#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCSingleDayDirector.h"
#include "MCTutorialDirector.h"
#include "MCProgressionComponent.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCPerkComponent.h"
#include "MCToothCharacter.h"
#include "MCBossCharacter.h"
#include "MCNutRainEvent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"

namespace MCSingleDayTestsPrivate
{
struct FWorldFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;
    FWorldFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
        World->SetGameMode(URL); Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseSingleDayLoop=true;
        World->InitializeActorsForPlay(URL); World->BeginPlay();
        State=World->GetGameState<AMCGameState>(); Mode->SetActorTickEnabled(false);
    }
    ~FWorldFixture() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayHandoffTest,"MessControl.SingleDay.TutorialHandoffHasNoStartingPerk",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayHandoffTest::RunTest(const FString&)
{
    MCSingleDayTestsPrivate::FWorldFixture F;
    F.World->SpawnActor<APlayerStart>(FVector(0,5000,130),FRotator::ZeroRotator);
    F.Mode->DefaultPawnClass=AMCToothCharacter::StaticClass();
    auto* PC=F.World->SpawnActor<AMCPlayerController>();
    auto* Player=PC->GetPlayerState<AMCPlayerState>();
    if(!Player) {
        Player=F.World->SpawnActor<AMCPlayerState>(); Player->SetOwner(PC); PC->SetPlayerState(Player);
    }
    F.State->AddPlayerState(Player);
    auto* Hero=F.World->SpawnActor<AMCToothCharacter>(); PC->Possess(Hero);
    auto* Table=NewObject<UDataTable>(); Table->RowStruct=FMCPerkDefinition::StaticStruct();
    for(int32 I=0;I<3;++I) {FMCPerkDefinition Row;Row.MaxStacks=10;Table->AddRow(FName(*FString::Printf(TEXT("Test%d"),I)),Row);}
    Player->Perks->PerkTable=Table;
    F.Mode->TutorialDirector=F.World->SpawnActor<AMCTutorialDirector>();
    F.State->bTutorialActive=true;
    F.Mode->AwardTaskToPlayerState(Player,EMCScoreTask::Food);
    TestEqual(TEXT("Practice tasks award no run XP"),F.State->Progression->TotalExperience,int64(0));
    F.Mode->FinishTutorial();
    TestFalse(TEXT("Finished lesson releases tutorial safety"),F.State->bTutorialActive);
    TestNotNull(TEXT("The restarted normal run supplies a worker"),Cast<AMCToothCharacter>(PC->GetPawn()));
    TestEqual(TEXT("Tutorial completion awards no XP"),F.State->Progression->TotalExperience,int64(0));
    TestEqual(TEXT("Tutorial completion never advances a compatibility level"),F.State->Progression->TeamLevel,1);
    TestFalse(TEXT("Tutorial completion grants no initial cards"),Player->LevelUpOffer.IsValid());
    TestTrue(TEXT("Tutorial completion grants no initial perk"),Player->Perks->ActivePerks.IsEmpty());
    TestEqual(TEXT("New run is configured as one day"),F.State->RunSettings.DaysToSurvive,1);
    TestEqual(TEXT("The restarted run retains its ordinary loading barrier"),F.State->SingleDayDirector->Stage,EMCSingleDayStage::Training);
    APawn* ReadyPawn=PC->GetPawn().Get();
    F.State->SingleDayDirector->BeginFirstPerk();
    TestEqual(TEXT("The compatibility entry point starts the opening pause directly"),F.State->SingleDayDirector->Stage,EMCSingleDayStage::OpeningPause);
    TestTrue(TEXT("Beginning the actual sequence preserves its ready pawn"),PC->GetPawn()==ReadyPawn);
    TestNull(TEXT("The opening meals cannot be bypassed by a removed perk stage"),F.State->SingleDayDirector->NutEvent.Get());
    TestFalse(TEXT("No removed XP choice can block the first meal"),F.State->Progression->HasPendingChoices());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayBossEndTest,"MessControl.SingleDay.LegacyFinalBossDeathEndsRun",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayBossEndTest::RunTest(const FString&)
{
    MCSingleDayTestsPrivate::FWorldFixture F;
    auto* Loop=F.State->SingleDayDirector.Get(); F.State->bTutorialActive=false; F.State->Phase=EMCShiftPhase::Working;
    auto* Legacy=NewObject<UMCSingleDayProfile>(F.World); Legacy->bLegacyTimedFinale=true;
    Loop->Initialize(F.Mode->FirstDayPlan.LoadSynchronous(),Legacy,nullptr);
    TestTrue(TEXT("The timed finale requires explicit legacy opt-in"),Loop->IsLegacyTimedFinale());
    Loop->Stage=EMCSingleDayStage::Boss;
    Loop->FinalBoss=F.World->SpawnActor<AMCBossCharacter>();
    Loop->Tick(.1f);
    TestEqual(TEXT("A living boss does not end the day"),F.State->Phase,EMCShiftPhase::Working);
    Loop->FinalBoss->Runtime.Health=0;
    Loop->Tick(.1f);
    TestEqual(TEXT("Boss death is the terminal success event"),F.State->Phase,EMCShiftPhase::Won);
    TestEqual(TEXT("The sequence is complete"),Loop->Stage,EMCSingleDayStage::Complete);
    TestEqual(TEXT("Victory never increments to day two"),F.State->Day,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFailedNutTest,"MessControl.SingleDay.FailedNutSetupTerminatesRun",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFailedNutTest::RunTest(const FString&)
{
    MCSingleDayTestsPrivate::FWorldFixture F;
    auto* Loop=F.State->SingleDayDirector.Get(); F.State->bTutorialActive=false;
    Loop->Stage=EMCSingleDayStage::Nuts;
    Loop->NutEvent=F.World->SpawnActor<AMCNutRainEvent>();
    Loop->NutEvent->Stage=EMCNutRainStage::Complete; Loop->NutEvent->bFailed=true;
    AddExpectedError(TEXT("MC_SINGLE_DAY FAILED:"),EAutomationExpectedErrorFlags::Contains,1);
    Loop->Tick(.1f);
    TestEqual(TEXT("A failed setup cannot leave players waiting forever"),F.State->Phase,EMCShiftPhase::Lost);
    TestNull(TEXT("Failed event actors are cleaned up"),Loop->NutEvent.Get());
    return true;
}
#endif
