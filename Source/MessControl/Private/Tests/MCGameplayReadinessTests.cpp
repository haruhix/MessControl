#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MCGameplayReadinessTestsPrivate
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* State=nullptr;

    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
        State->GameplayLoadId=FGuid::NewGuid();
        State->bLobbyWaiting=false;State->Phase=EMCShiftPhase::Working;
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    AMCPlayerController* Controller(bool WithState=true,bool WithPawn=true)
    {
        auto* PC=World->SpawnActor<AMCPlayerController>();
        if(WithState) Identity(PC);
        if(WithPawn) Pawn(PC);
        return PC;
    }
    AMCPlayerState* Identity(AMCPlayerController* PC)
    {
        const FTransform Pose;
        auto* PS=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),Pose,PC);
        PS->FinishSpawning(Pose);PC->SetPlayerState(PS);return PS;
    }
    AMCToothCharacter* Pawn(AMCPlayerController* PC)
    {
        auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();PC->Possess(Hero);return Hero;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameplayReadinessRetryTest,
    "MessControl.Loading.GameplayAck.RejectedUntilPossessedThenRetry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameplayReadinessRetryTest::RunTest(const FString&)
{
    using namespace MCGameplayReadinessTestsPrivate;
    FWorld F;auto* PC=F.Controller(false,false);
    TestFalse(TEXT("No identity or pawn cannot acknowledge preparation"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    F.Identity(PC);
    TestFalse(TEXT("A connected identity without possession is not loaded"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    TestFalse(TEXT("A rejected request does not release the server barrier"),PC->HasAcknowledgedGameplay());
    F.Pawn(PC);
    TestTrue(TEXT("A retry after possession is accepted"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    TestTrue(TEXT("The accepted identity belongs to the current world"),PC->HasAcknowledgedGameplay());
    TestTrue(TEXT("An identical retry is harmless and idempotent"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameplayReadinessEpochTest,
    "MessControl.Loading.GameplayAck.RejectsForeignWorldAndStaleEpoch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameplayReadinessEpochTest::RunTest(const FString&)
{
    using namespace MCGameplayReadinessTestsPrivate;
    FWorld F,Other;auto* PC=F.Controller();
    TestFalse(TEXT("An empty load identity cannot release the barrier"),PC->AcknowledgeGameplayLoaded(F.State,FGuid()));
    TestFalse(TEXT("An unissued load identity cannot release the barrier"),PC->AcknowledgeGameplayLoaded(F.State,FGuid::NewGuid()));
    Other.State->GameplayLoadId=F.State->GameplayLoadId;
    TestFalse(TEXT("A foreign world's state is rejected even with a matching identity"),PC->AcknowledgeGameplayLoaded(Other.State,F.State->GameplayLoadId));
    TestFalse(TEXT("Rejected stale packets leave the barrier closed"),PC->HasAcknowledgedGameplay());
    const FGuid Previous=F.State->GameplayLoadId;
    TestTrue(TEXT("The actual current destination is accepted"),PC->AcknowledgeGameplayLoaded(F.State,Previous));
    F.State->GameplayLoadId=FGuid::NewGuid();
    TestFalse(TEXT("A new preparation epoch immediately invalidates the previous receipt"),PC->HasAcknowledgedGameplay());
    TestFalse(TEXT("A delayed previous epoch cannot acknowledge the new preparation"),PC->AcknowledgeGameplayLoaded(F.State,Previous));
    TestTrue(TEXT("The new epoch can be acknowledged normally"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameplayReadinessMembershipTest,
    "MessControl.Loading.GameplayAck.InactiveAndSpectatorCannotAcknowledge",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameplayReadinessMembershipTest::RunTest(const FString&)
{
    using namespace MCGameplayReadinessTestsPrivate;
    FWorld F;auto* PC=F.Controller();auto* PS=PC->GetPlayerState<AMCPlayerState>();
    PS->SetIsInactive(true);
    TestFalse(TEXT("An inactive participant cannot acknowledge loading"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    PS->SetIsInactive(false);PS->SetIsSpectator(true);
    TestFalse(TEXT("A spectator cannot acknowledge participant loading"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    PS->SetIsSpectator(false);PS->SetIsOnlyASpectator(true);
    TestFalse(TEXT("A spectator-only identity cannot acknowledge loading"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    PS->SetIsOnlyASpectator(false);
    TestTrue(TEXT("Restoring active membership permits a valid acknowledgement"),PC->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameplayReadinessFourPlayersTest,
    "MessControl.Loading.GameplayAck.FourIndependentReceipts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameplayReadinessFourPlayersTest::RunTest(const FString&)
{
    using namespace MCGameplayReadinessTestsPrivate;
    FWorld F;TArray<AMCPlayerController*> Players;
    for(int32 Index=0;Index<4;++Index) Players.Add(F.Controller());
    auto Loaded=[&]() {int32 Count=0;for(const auto* PC:Players) Count+=PC->HasAcknowledgedGameplay()?1:0;return Count;};
    for(int32 Index=0;Index<3;++Index)
        TestTrue(TEXT("Each of the first three participants receives their own receipt"),Players[Index]->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    TestEqual(TEXT("The fourth participant is not implicitly marked loaded"),Loaded(),3);
    TestTrue(TEXT("The fourth participant can acknowledge independently"),Players[3]->AcknowledgeGameplayLoaded(F.State,F.State->GameplayLoadId));
    TestEqual(TEXT("All four actual participants can be ready"),Loaded(),4);
    F.State->GameplayLoadId=FGuid::NewGuid();
    TestEqual(TEXT("Rotating the destination identity invalidates every old receipt"),Loaded(),0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameplayPresentationOwnershipTest,
    "MessControl.Loading.GameplayAck.DefersUIUntilLocalPlayerOwnsController",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameplayPresentationOwnershipTest::RunTest(const FString&)
{
    using namespace MCGameplayReadinessTestsPrivate;
    FWorld F;auto* PC=F.Controller();
    PC->SetAsLocalPlayerController();
    TestNull(TEXT("The travelling controller has no attached local player yet"),PC->GetLocalPlayer());
    TestFalse(TEXT("A local designation alone is insufficient to create or focus UI"),PC->HasLocalPresentationOwner());
    PC->UpdateInputMode();
    PC->PostSeamlessTravel();
    TestNull(TEXT("Destination reset defers the gameplay HUD until local ownership is attached"),PC->PrototypeWidget.Get());
    TestNull(TEXT("Destination reset defers the boss HUD as well"),PC->BossHealthWidget.Get());
    TestFalse(TEXT("A controller without local presentation ownership cannot acknowledge ready UI"),PC->IsWorldPresentationReady());
    return true;
}
#endif
