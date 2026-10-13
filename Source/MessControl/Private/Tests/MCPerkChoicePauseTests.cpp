#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCRewardChest.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace
{
struct FPerkPauseWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    FPerkPauseWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
    }
    ~FPerkPauseWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    AMCPlayerController* Player(bool bHasChoice)
    {
        auto* PC=World->SpawnActor<AMCPlayerController>();
        auto* State=World->SpawnActor<AMCPlayerState>(); PC->SetPlayerState(State);
        auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        PC->Possess(Hero); Hero->Status->Initialize(100);
        if (bHasChoice) {
            State->LevelUpOffer.OfferId=FGuid::NewGuid();
            State->LevelUpOffer.TeamLevel=2;
            State->LevelUpOffer.PerkIDs={TEXT("P01"),TEXT("P02"),TEXT("P03")};
        }
        return PC;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkChoiceSharedPauseTest,"MessControl.Rewards.ChoiceSharedPause",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkChoiceSharedPauseTest::RunTest(const FString&)
{
    FPerkPauseWorld W;
    auto* First=W.Player(true); auto* Second=W.Player(true); auto* NoChoice=W.Player(false);
    NoChoice->ServerSetPerkChoicePause(true);
    TestFalse(TEXT("A player without a reward cannot pause the shared world"),W.World->IsPaused());
    First->ServerSetPerkChoicePause(true);
    TestTrue(TEXT("A choice pauses gameplay"),W.World->IsPaused());
    const double PausedAt=W.World->GetTimeSeconds();
    for (int32 Frame=0;Frame<120;++Frame) W.World->Tick(LEVELTICK_TimeOnly,1.f/60);
    TestEqual(TEXT("Gameplay time remains frozen while choosing"),W.World->GetTimeSeconds(),PausedAt);
    Second->ServerSetPerkChoicePause(true);
    First->ServerSetPerkChoicePause(true); // Duplicate open notifications must not add another lease.
    First->ServerSetPerkChoicePause(false);
    TestTrue(TEXT("One player finishing cannot resume another player's choice"),W.World->IsPaused());
    Second->ServerSetPerkChoicePause(false);
    TestFalse(TEXT("The last closed choice resumes the world"),W.World->IsPaused());
    W.World->Tick(LEVELTICK_TimeOnly,1.f/60);
    TestTrue(TEXT("Gameplay time advances again after the last choice"),W.World->GetTimeSeconds()>PausedAt);

    First->ServerSetPerkChoicePause(true); Second->ServerSetPerkChoicePause(true);
    First->Destroy();
    TestTrue(TEXT("Disconnecting the first chooser preserves the other chooser's pause"),W.World->IsPaused());
    TestTrue(TEXT("The pause owner moves to a surviving player"),W.World->GetWorldSettings()->GetPauserPlayerState()!=First->PlayerState);
    Second->Destroy();
    TestFalse(TEXT("Disconnecting the last chooser releases the pause"),W.World->IsPaused());

    auto* Chest=W.World->SpawnActor<AMCRewardChest>();
    auto* Hero=CastChecked<AMCToothCharacter>(NoChoice->GetPawn());
    Chest->OpeningPlayer=Hero; Hero->RewardInteraction=Chest;
    Chest->Stage=EMCRewardChestStage::Opening;
    NoChoice->ServerSetPerkChoicePause(true);
    TestFalse(TEXT("The chest opening animation runs before cards appear"),W.World->IsPaused());
    Chest->Stage=EMCRewardChestStage::Open;
    NoChoice->ServerSetPerkChoicePause(true);
    TestTrue(TEXT("An opened chest choice also pauses gameplay"),W.World->IsPaused());
    NoChoice->ServerSetPerkChoicePause(false);
    TestFalse(TEXT("Closing chest cards resumes gameplay"),W.World->IsPaused());
    return true;
}
#endif
