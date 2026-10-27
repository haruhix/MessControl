#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCHubGameMode.h"
#include "MCHubGameState.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"

namespace
{
struct FHubLoadingWorld
{
    UWorld* World = nullptr;
    FHubLoadingWorld()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;
        URL.AddOption(TEXT("game=/Script/MessControl.MCHubGameMode"));
        World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);
        World->BeginPlay();
    }
    ~FHubLoadingWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
    AMCPlayerController* AddPlayer(bool bPawn)
    {
        auto* Player = World->SpawnActor<AMCPlayerController>();
        // A rules-only fixture has no net driver. Explicitly supply the local
        // designation instead of pretending this verifies transport/Steam.
        Player->SetAsLocalPlayerController();
        Player->SetPlayerState(World->SpawnActor<AMCPlayerState>());
        if (bPawn) Player->Possess(World->SpawnActor<ADefaultPawn>(
            FVector(World->GetNumPlayerControllers() * 300.f, 0.f, 200.f), FRotator::ZeroRotator));
        return Player;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubFourPlayerLoadBarrierTest,
    "MessControl.Hub.Loading.FourParticipantsDelayedPawnAndDisconnect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCHubFourPlayerLoadBarrierTest::RunTest(const FString&)
{
    FHubLoadingWorld Fixture;
    auto* Mode = Fixture.World->GetAuthGameMode<AMCHubGameMode>();
    auto* State = Fixture.World->GetGameState<AMCHubGameState>();
    if (!TestNotNull(TEXT("Hub rules are initialized"), Mode)
        || !TestNotNull(TEXT("Hub load state is initialized"), State)) return false;
    TArray<AMCPlayerController*> Players;
    for (int32 Index = 0; Index < 3; ++Index) Players.Add(Fixture.AddPlayer(true));
    FString LoginError;
    Mode->PreLogin(TEXT(""), TEXT("127.0.0.1"), FUniqueNetIdRepl(), LoginError);
    TestTrue(TEXT("The hub admits a fourth player when three are connected"), LoginError.IsEmpty());
    Players.Add(Fixture.AddPlayer(false));
    Mode->PreLogin(TEXT(""), TEXT("127.0.0.1"), FUniqueNetIdRepl(), LoginError);
    TestFalse(TEXT("A fifth login is rejected after four participants are connected"), LoginError.IsEmpty());
    Mode->RefreshReadiness();
    TestEqual(TEXT("All four connected participants are counted before they load"), State->GetParticipantCount(), 4);
    TestEqual(TEXT("Connection alone is not a load acknowledgement"), State->GetLoadedCount(), 0);
    for (auto* Player : Players) Mode->AcknowledgeSceneLoaded(Player, State, State->SceneGeneration);
    TestEqual(TEXT("An early fourth ACK without a pawn is rejected"), State->GetLoadedCount(), 3);
    TestFalse(TEXT("Three loaded players do not release a four-player barrier"), State->AreAllLoaded());
    TestFalse(TEXT("The fourth player's own entry remains unacknowledged"), State->HasLoadedScene(Players[3]->PlayerState));
    Players[3]->Possess(Fixture.World->SpawnActor<ADefaultPawn>(FVector(1500.f, 0.f, 200.f), FRotator::ZeroRotator));
    Mode->AcknowledgeSceneLoaded(Players[3], State, State->SceneGeneration);
    TestEqual(TEXT("A retried fourth ACK after possession is accepted"), State->GetLoadedCount(), 4);
    TestTrue(TEXT("Four actual current ACKs release loading"), State->AreAllLoaded());
    TestFalse(TEXT("Loading all four never substitutes for standing in READY zones"), State->AreAllReady());

    State->SetPhase(EMCHubPhase::Selecting);
    for (int32 Index = 0; Index < 3; ++Index) Mode->AcknowledgeSceneLoaded(Players[Index], State, State->SceneGeneration);
    TestFalse(TEXT("The last participant still loading holds its own barrier"), State->AreAllLoaded());
    Players[3]->Destroy();
    Mode->RefreshReadiness();
    TestEqual(TEXT("Disconnected controllers do not remain in the membership snapshot"), State->GetParticipantCount(), 3);
    TestTrue(TEXT("Disconnecting a pending loader releases the remaining loaded team"), State->AreAllLoaded());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubWorldBoundLoadAckTest,
    "MessControl.Hub.Loading.RejectsOtherWorldAndPreviousScene",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCHubWorldBoundLoadAckTest::RunTest(const FString&)
{
    FHubLoadingWorld Fixture;
    auto* Mode = Fixture.World->GetAuthGameMode<AMCHubGameMode>();
    auto* State = Fixture.World->GetGameState<AMCHubGameState>();
    auto* Player = Fixture.AddPlayer(true);
    FHubLoadingWorld PreviousWorld;
    const auto* PreviousState = PreviousWorld.World->GetGameState<AMCHubGameState>();
    if (!TestNotNull(TEXT("Current hub state"), State)
        || !TestNotNull(TEXT("Previous hub state"), PreviousState)) return false;
    TestEqual(TEXT("Different worlds may legitimately reuse a scene number"), PreviousState->SceneGeneration, State->SceneGeneration);
    Mode->AcknowledgeSceneLoaded(Player, PreviousState, State->SceneGeneration);
    TestFalse(TEXT("An ACK for a previous world cannot acknowledge this hub"), State->AreAllLoaded());
    Mode->AcknowledgeSceneLoaded(Player, nullptr, State->SceneGeneration);
    TestFalse(TEXT("A missing/destroyed world identity cannot acknowledge this hub"), State->AreAllLoaded());
    Mode->AcknowledgeSceneLoaded(Player, State, State->SceneGeneration);
    TestTrue(TEXT("The same request with the correct world identity works"), State->AreAllLoaded());
    const int32 OldGeneration = State->SceneGeneration;
    State->SetPhase(EMCHubPhase::Selecting);
    Mode->AcknowledgeSceneLoaded(Player, State, OldGeneration);
    TestFalse(TEXT("A correct world still rejects the previous presentation generation"), State->AreAllLoaded());
    Mode->AcknowledgeSceneLoaded(Player, State, State->SceneGeneration);
    TestTrue(TEXT("A fresh current-scene ACK restores readiness"), State->AreAllLoaded());
    return true;
}
#endif
