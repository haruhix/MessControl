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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayHandoffTest,"MessControl.SingleDay.TutorialExperiencePreservesPawnAndChoices",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayHandoffTest::RunTest(const FString&)
{
    MCSingleDayTestsPrivate::FWorldFixture F;
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
    TestTrue(TEXT("The taught player keeps their original pawn"),PC->GetPawn()==Hero);
    TestEqual(TEXT("The first XP grants a shared level"),F.State->Progression->TeamLevel,2);
    TestTrue(TEXT("The first level offers personal cards"),Player->LevelUpOffer.IsValid());
    TestEqual(TEXT("New run is configured as one day"),F.State->RunSettings.DaysToSurvive,1);
    TestEqual(TEXT("Main sequence waits for the first choice"),F.State->SingleDayDirector->Stage,EMCSingleDayStage::FirstPerk);
    F.State->SingleDayDirector->Tick(.1f);
    TestEqual(TEXT("Pending personal cards gate nut rain"),F.State->SingleDayDirector->Stage,EMCSingleDayStage::FirstPerk);
    TestTrue(TEXT("An independent choice consumes the first offer"),Player->TryChooseLevelUpPerk(Player->LevelUpOffer.OfferId,0));
    TestFalse(TEXT("The team is ready after its choice"),F.State->Progression->HasPendingChoices());
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
