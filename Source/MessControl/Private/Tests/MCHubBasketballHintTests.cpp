#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCHubBasketball.h"
#include "MCHubGameState.h"
#include "MCHubGrillStation.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
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
    TestEqual(TEXT("The held ball becomes the throw hint target"),AMCHubBasketball::LocalHintBall(T.Hero),T.Ball);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubBasketballHitRelease,
    "MessControl.Hub.Basketball.AcceptedHitsReleaseOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubBasketballHitRelease::RunTest(const FString&)
{
    FHubBallHintWorld T;
    if(!TestTrue(TEXT("Actual pickup starts the hit fixture"),T.Ball->TryPickup(T.Hero))) return false;
    T.Hero->ServerSetThrowHeld(true,FVector(1,0,0));
    TestEqual(TEXT("A food/Q RPC cannot throw an authoritative held basketball"),T.Ball->Holder.Get(),T.Hero);
    T.Hero->ServerSetThrowHeld(false,FVector(1,0,0));
    // BeginPlay enters Standing with a short hit-protection window. Let that
    // real lifecycle elapse before exercising accepted hits; this fixture has
    // no floor, so hold its capsule in place while the world ticks normally.
    T.Hero->GetCharacterMovement()->DisableMovement();
    for(int32 Frame=0;Frame<45;++Frame) {++GFrameCounter;T.World->Tick(LEVELTICK_All,1.f/60.f);}
    T.Hero->ToothPhysics->Settings.FallThreshold=1400.f;
    T.Hero->ToothPhysics->ApplyHit(FVector::ZeroVector,T.Hero->GetActorLocation());
    TestEqual(TEXT("A zero impulse does not steal the carried ball"),T.Ball->Holder.Get(),T.Hero);
    T.Hero->ToothPhysics->ApplyHit(FVector(120,0,40),T.Hero->GetActorLocation());
    TestNull(TEXT("A hit below the knockdown threshold still releases the ball"),T.Ball->Holder.Get());
    TestTrue(TEXT("Released ball resumes real physics"),T.Ball->Body->IsSimulatingPhysics());
    TestNull(TEXT("Released ball clears its network owner"),T.Ball->GetOwner());
    TestTrue(TEXT("The small hit keeps the player standing"),T.Hero->ToothPhysics->CanAct());
    if(!TestTrue(TEXT("The loose ball can be picked up again"),T.Ball->TryPickup(T.Hero))) return false;
    TestTrue(TEXT("A real accepted health hit is applied"),T.Hero->Status->Damage(1.f,FVector(1,0,0)));
    TestNull(TEXT("Damage without a physics impulse also releases ownership"),T.Ball->Holder.Get());
    return true;
}
#endif
