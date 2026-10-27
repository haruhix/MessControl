#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCHubBasketball.h"
#include "MCHubGameState.h"
#include "MCHubGrillStation.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
struct FHubBallHintWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCHubGameState* Hub=nullptr;
    AMCToothCharacter* Hero=nullptr;
    APlayerController* Player=nullptr;
    AMCHubBasketball* Ball=nullptr;
    FHubBallHintWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hub=World->SpawnActor<AMCHubGameState>();World->SetGameState(Hub);
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator,Params);
        Player=World->SpawnActor<APlayerController>();Player->SetAsLocalPlayerController();Player->Possess(Hero);
        Ball=World->SpawnActor<AMCHubBasketball>(Hero->GetActorLocation()+FVector(180,0,0),FRotator::ZeroRotator,Params);
    }
    ~FHubBallHintWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubBasketballHintAvailability,
    "MessControl.Hub.Basketball.ContextHintsFollowActionAvailability",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubBasketballHintAvailability::RunTest(const FString&)
{
    FHubBallHintWorld T;
    if(!TestTrue(TEXT("Fixture has an actionable local Hub player"),T.Hero->CanWork() && T.Player->IsLocalController())) return false;
    TestEqual(TEXT("A reachable free ball is the E hint target"),AMCHubBasketball::LocalHintBall(T.Hero),T.Ball);
    TestTrue(TEXT("The ball reuses the existing screen-space prompt"),T.Ball->InteractionPrompt && T.Ball->InteractionPrompt->GetWidgetSpace()==EWidgetSpace::Screen);
    T.Ball->SetActorLocation(T.Hero->GetActorLocation()+FVector(400,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    TestNull(TEXT("Walking away clears the pickup hint"),AMCHubBasketball::LocalHintBall(T.Hero));
    T.Ball->SetActorLocation(T.Hero->GetActorLocation()+FVector(180,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    auto* Wall=T.World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Shape);Shape->SetBoxExtent(FVector(5,100,100));Shape->SetCollisionProfileName(TEXT("BlockAll"));
    Shape->RegisterComponent();Wall->SetActorLocation(T.Hero->GetActorLocation()+FVector(90,0,0));
    TestNull(TEXT("A wall blocks the same candidate test used by pickup"),AMCHubBasketball::LocalHintBall(T.Hero));
    Wall->Destroy();
    auto* Grill=T.World->SpawnActor<AMCHubGrillStation>(T.Hero->GetActorLocation(),FRotator::ZeroRotator);
    TestNull(TEXT("The higher-priority grill E interaction suppresses pickup guidance"),AMCHubBasketball::LocalHintBall(T.Hero));
    Grill->Destroy();
    TestTrue(TEXT("The actual pickup succeeds"),T.Ball->TryPickup(T.Hero));
    TestEqual(TEXT("The held ball becomes the Q hint target"),AMCHubBasketball::LocalHintBall(T.Hero),T.Ball);
    T.Player->bShowMouseCursor=true;
    TestNull(TEXT("A modal cursor hides held-ball guidance"),AMCHubBasketball::LocalHintBall(T.Hero));
    T.Player->bShowMouseCursor=false;T.Player->SetIgnoreMoveInput(true);
    TestNull(TEXT("Blocked gameplay input hides the keycap and footer target"),AMCHubBasketball::LocalHintBall(T.Hero));
    T.Player->ResetIgnoreMoveInput();
    T.Player->UnPossess();
    TestNull(TEXT("Losing possession clears the old player's hint"),AMCHubBasketball::LocalHintBall(T.Hero));
    T.Player->Possess(T.Hero);
    T.Hub->Phase=EMCHubPhase::Selecting;
    TestNull(TEXT("Location selection never shows basketball onboarding"),AMCHubBasketball::LocalHintBall(T.Hero));
    T.Hub->Phase=EMCHubPhase::Gathering;
    T.Ball->Release(FVector::ZeroVector);
    auto* Other=T.World->SpawnActor<AMCToothCharacter>(T.Ball->GetActorLocation()+FVector(100,0,0),FRotator::ZeroRotator);
    if(!TestTrue(TEXT("Another player can acquire the free ball"),T.Ball->TryPickup(Other))) return false;
    TestNull(TEXT("Another player's held ball supplies neither pickup nor throw hint"),AMCHubBasketball::LocalHintBall(T.Hero));
    T.World->SetGameState(nullptr);
    TestNull(TEXT("Arena and other non-Hub worlds never show these prompts"),AMCHubBasketball::LocalHintBall(T.Hero));
    return true;
}
#endif
