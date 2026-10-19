#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCHubGameMode.h"
#include "MCHubGameState.h"
#include "MCHubReadyZone.h"
#include "MCGameMode.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

namespace
{
struct FHubRulesWorld
{
    UWorld* World = nullptr;
    explicit FHubRulesWorld(bool bSelector = false)
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;
        URL.AddOption(bSelector ? TEXT("game=/Script/MessControl.MCHubGameMode") : TEXT("game=/Script/Engine.GameModeBase"));
        if (bSelector) URL.AddOption(TEXT("MCHubSelection=1"));
        World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);
        World->BeginPlay();
    }
    ~FHubRulesWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
};

FMCHubPlayerReadiness Entry(APlayerState* Player, bool bReady, bool bLoaded)
{
    FMCHubPlayerReadiness Result;
    Result.Player = Player;
    Result.bReady = bReady;
    Result.bLoaded = bLoaded;
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubReadinessRulesTest, "MessControl.Hub.ReadinessRequiresEveryLoadedParticipant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCHubReadinessRulesTest::RunTest(const FString&)
{
    FHubRulesWorld Fixture;
    auto* State = Fixture.World->SpawnActor<AMCHubGameState>();
    auto* First = Fixture.World->SpawnActor<APlayerState>();
    auto* Second = Fixture.World->SpawnActor<APlayerState>();
    auto* Spectator = Fixture.World->SpawnActor<APlayerState>();
    Spectator->SetIsOnlyASpectator(true);
    TestFalse(TEXT("An empty hub is never all ready"), State->AreAllReady());
    TestFalse(TEXT("Zero of zero is not a loaded team"), State->AreAllLoaded());
    State->SetReadiness({Entry(First,true,true), Entry(Second,true,false), Entry(Spectator,false,false)});
    TestEqual(TEXT("Observers do not block the participating team"), State->GetParticipantCount(), 2);
    TestEqual(TEXT("Unloaded players cannot contribute readiness"), State->GetReadyCount(), 1);
    TestFalse(TEXT("One participant still loading blocks the barrier"), State->AreAllLoaded());
    TestFalse(TEXT("Standing in the zone before a load ACK is insufficient"), State->AreAllReady());
    State->SetReadiness({Entry(First,true,true), Entry(Second,false,true)});
    TestTrue(TEXT("Both scene ACKs release only the loading barrier"), State->AreAllLoaded());
    TestFalse(TEXT("Leaving the zone removes ready status"), State->AreAllReady());
    State->SetReadiness({Entry(First,true,true), Entry(Second,true,true)});
    TestTrue(TEXT("The complete loaded participating team is ready"), State->AreAllReady());
    State->SetReadiness({Entry(First,true,true)});
    TestEqual(TEXT("Removing a disconnected member updates the denominator"), State->GetParticipantCount(), 1);
    TestTrue(TEXT("A remaining loaded host can continue solo"), State->AreAllReady());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubDestinationSafetyTest, "MessControl.Hub.DestinationsAndRotatedReadyVolume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCHubDestinationSafetyTest::RunTest(const FString&)
{
    FHubRulesWorld Fixture;
    auto* State = Fixture.World->SpawnActor<AMCHubGameState>();
    State->DestinationTable.Reset();
    State->ReloadDestinations();
    TestEqual(TEXT("All seven presentation days remain available to preview"), State->GetDestinations().Num(), 7);
    TestTrue(TEXT("The native fallback enables day one only"), State->FindDestination(1)->bEnabled);
    for (int32 Day = 2; Day <= 7; ++Day) TestFalse(TEXT("Unavailable days never silently launch day one"), State->FindDestination(Day)->bEnabled);
    TestNull(TEXT("Unknown day cannot resolve a destination"), State->FindDestination(8));
    FMCHubDestinationRow Row = *State->FindDestination(1);
    TestTrue(TEXT("The authored first gameplay map has a valid local map path"), Row.HasValidMapPath());
    Row.Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry")));
    TestFalse(TEXT("An external engine package is not an authored game destination"), Row.HasValidMapPath());
    Row.Map.Reset();
    TestFalse(TEXT("A missing destination cannot launch"), Row.HasValidMapPath());

    auto* Zone = Fixture.World->SpawnActor<AMCHubReadyZone>();
    auto* Pawn = Fixture.World->SpawnActor<ADefaultPawn>();
    Zone->SetZoneExtent(FVector(240,140,160));
    Zone->SetActorTransform(FTransform(FRotator(0,37,0),FVector(1000,2000,160),FVector(1.2,1.5,1)));
    Pawn->SetActorLocation(Zone->Zone->GetComponentTransform().TransformPosition(FVector(200,100,0)));
    TestTrue(TEXT("A rotated and scaled editor volume contains the pawn center"), Zone->ContainsPawn(Pawn));
    Pawn->SetActorLocation(Zone->Zone->GetComponentTransform().TransformPosition(FVector(250,100,0)));
    TestFalse(TEXT("Leaving one actual oriented boundary removes readiness"), Zone->ContainsPawn(Pawn));
    Zone->bEnabled = false;
    Pawn->SetActorLocation(Zone->GetActorLocation());
    TestFalse(TEXT("Disabled volumes cannot claim ready participants"), Zone->ContainsPawn(Pawn));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubHostSelectionRulesTest, "MessControl.Hub.HostOnlySelectionAndTravelBarrier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCHubHostSelectionRulesTest::RunTest(const FString&)
{
    FHubRulesWorld Fixture(true);
    auto* Mode = Fixture.World->GetAuthGameMode<AMCHubGameMode>();
    auto* State = Fixture.World->GetGameState<AMCHubGameState>();
    if (!TestNotNull(TEXT("The selector uses the independent hub GameMode"), Mode)
        || !TestNotNull(TEXT("The selector has shared hub state"), State)) return false;
    TestFalse(TEXT("Hub is not a subclass of the level director GameMode"), Mode->IsA(AMCGameMode::StaticClass()));
    TestEqual(TEXT("Selection is initialized from the level URL before gameplay"), State->Phase, EMCHubPhase::Selecting);
    TestNull(TEXT("Selection does not spawn a gameplay pawn class"), Mode->DefaultPawnClass.Get());

    auto* Player = Fixture.World->SpawnActor<AMCPlayerController>();
    // This rules-only world has no viewport/login path. Mirror the local
    // designation normally supplied by GameModeBase::SpawnPlayerControllerCommon.
    Player->SetAsLocalPlayerController();
    auto* Identity = Fixture.World->SpawnActor<AMCPlayerState>();
    Player->SetPlayerState(Identity);
    Identity->bSessionHost = false;
    Mode->RefreshReadiness();
    TestFalse(TEXT("A participant without the host identity cannot change the common day"), Mode->RequestSelectDay(Player,2));
    TestFalse(TEXT("A participant without host identity cannot launch"), Mode->RequestLaunch(Player));
    TestFalse(TEXT("A participant without host identity cannot return the team"), Mode->RequestReturnToHub(Player));
    Identity->bSessionHost = true;
    TestTrue(TEXT("The fixture explicitly represents a local session host"), State->IsHost(Player));
    Mode->AcknowledgeSceneLoaded(Player);
    TestTrue(TEXT("The selector accepts a scene ACK from the fixture host"), State->AreAllLoaded());
    TestTrue(TEXT("A loaded local host can preview unavailable future locations"), Mode->RequestSelectDay(Player,7));
    TestEqual(TEXT("The shared day records the preview, without changing world"), State->SelectedDay, 7);
    TestFalse(TEXT("An unavailable day cannot start a gameplay map"), Mode->RequestLaunch(Player));
    TestFalse(TEXT("An out of range day cannot be selected"), Mode->RequestSelectDay(Player,8));
    State->SetPhase(EMCHubPhase::Travelling);
    TestFalse(TEXT("A duplicate request cannot launch again while travelling"), Mode->RequestLaunch(Player));
    TestFalse(TEXT("Selection cannot change under an ongoing transition"), Mode->RequestSelectDay(Player,1));
    TestFalse(TEXT("Back cannot compete with an ongoing transition"), Mode->RequestReturnToHub(Player));
    TestTrue(TEXT("Rejected requests never schedule ServerTravel"), Fixture.World->NextURL.IsEmpty());
    return true;
}
#endif
