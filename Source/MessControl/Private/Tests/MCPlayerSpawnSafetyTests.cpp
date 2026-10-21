#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "MCTutorialDirector.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerSpawnClearanceTest,"MessControl.Spawn.PlayerCapsuleClearsTongue",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerSpawnClearanceTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));World->SetGameMode(URL);
    auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    Mode->bUseDayOnePlan=false;Mode->bUseAdaptiveDirector=false;
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
    if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
#endif
    if(!TestNotNull(TEXT("Engine plane fixture is available"),Plane)) {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;
    }
    const FTransform TissuePose(FQuat::Identity,FVector(10000,0,1500),FVector(20,20,1));
    auto* Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),TissuePose);
    Tongue->SourceMesh=Plane;Tongue->FinishSpawning(TissuePose);Tongue->SetActorTickEnabled(false);
    auto* Controller=World->SpawnActor<APlayerController>();
    Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));
    Mode->DefaultPawnClass=AMCToothCharacter::StaticClass();
    const FVector Buried(10000,0,1450);
    Mode->RestartPlayerAtTransform(Controller,FTransform(Buried));
    auto* Hero=Cast<AMCToothCharacter>(Controller->GetPawn());
    TestNotNull(TEXT("A buried player start still produces a pawn"),Hero);
    if(Hero) {
        FHitResult Floor;TestTrue(TEXT("Spawn has tissue underneath"),Tongue->SurfacePoint(Hero->GetActorLocation(),Floor));
        const float Bottom=Hero->GetActorLocation().Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        TestTrue(FString::Printf(TEXT("Capsule starts above tissue with settling clearance (bottom=%.3f floor=%.3f half=%.3f normal=%.3f)"),Bottom,Floor.ImpactPoint.Z,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),Floor.ImpactNormal.Z),Bottom>=Floor.ImpactPoint.Z+29.f);
        TestTrue(TEXT("Spawn does not shift the player horizontally"),FVector::DistSquared2D(Hero->GetActorLocation(),Buried)<1.f);
        TestTrue(TEXT("Initial arrival has a visible drop instead of just a tiny settling gap"),Bottom>=Floor.ImpactPoint.Z+Mode->ArenaArrivalHeight-1.f);
        const FVector HeldAt=Hero->GetActorLocation();
        TestTrue(TEXT("Initial entrance hold is explicit for the owning client"),Hero->IsArenaArrivalHeld());
        Hero->PawnClientRestart();
        TestEqual(TEXT("A client restart cannot bypass the entrance hold"),uint8(Hero->GetCharacterMovement()->MovementMode),uint8(MOVE_None));
        const float Health=Hero->Status->State.Health;
        auto* State=World->GetGameState<AMCGameState>();
        const int32 Reserves=State->AvailableArenaTeeth();State->bLobbyWaiting=true;
        for(int32 I=0;I<60;++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}
        TestTrue(TEXT("Pre-game loading barrier holds the drop above the arena"),Hero->GetActorLocation().Equals(HeldAt,1.f));
        TestEqual(TEXT("The held capsule does not simulate gravity behind the loading cover"),uint8(Hero->GetCharacterMovement()->MovementMode),uint8(MOVE_None));
        State->bLobbyWaiting=false;
        for(int32 I=0;I<120;++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}
        TestTrue(TEXT("Releasing the existing barrier lands the worker on tissue"),Hero->GetCharacterMovement()->IsMovingOnGround());
        TestFalse(TEXT("The initial entrance hold is cleared after the barrier"),Hero->IsArenaArrivalHeld());
        TestEqual(TEXT("Arrival landing does not damage the worker"),Hero->Status->State.Health,Health);
        TestEqual(TEXT("Arrival does not spend an arena tooth life"),State->AvailableArenaTeeth(),Reserves);

        // A second start under a low roof shortens the entrance rather than
        // moving a capsule into geometry or discarding its landing clearance.
        auto* Roof=World->SpawnActor<AActor>();
        auto* Ceiling=NewObject<UBoxComponent>(Roof);Roof->SetRootComponent(Ceiling);
        Ceiling->SetBoxExtent(FVector(250,250,20));Ceiling->SetCollisionProfileName(TEXT("BlockAll"));Ceiling->RegisterComponent();
        Roof->SetActorLocation(FVector(10000,400,1850));
        auto* SecondController=World->SpawnActor<APlayerController>();
        SecondController->SetPlayer(NewObject<ULocalPlayer>(GEngine));
        Mode->RestartPlayerAtTransform(SecondController,FTransform(FVector(10000,400,1450)));
        auto* Second=Cast<AMCToothCharacter>(SecondController->GetPawn());
        if(TestNotNull(TEXT("A low ceiling still allows a safe initial spawn"),Second)) {
            const float Half=Second->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            TestTrue(TEXT("Arrival capsule stays beneath the ceiling"),Second->GetActorLocation().Z+Half<=1830.f);
            TestTrue(TEXT("Low-ceiling arrival still clears the floor"),Second->GetActorLocation().Z-Half>=1529.f);
            TestTrue(TEXT("Ceiling clearance reduces the default high drop"),Second->GetActorLocation().Z-Half<1500.f+Mode->ArenaArrivalHeight);
            // The short tutorial also owns a single-day director, left in
            // Training until the lessons finish. That unrelated stage must
            // not keep a loaded tutorial player frozen above the arena.
            State->bTutorialActive=true;State->bSingleDayLoop=true;
            Mode->TutorialDirector=World->SpawnActor<AMCTutorialDirector>();
            Mode->TutorialDirector->SetActorTickEnabled(false);
            Mode->TutorialDirector->Stage=EMCTutorialStage::Loading;
            ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);
            TestTrue(TEXT("Tutorial loading keeps the visible arrival held"),Second->IsArenaArrivalHeld());
            Mode->TutorialDirector->Stage=EMCTutorialStage::BrushTooth;
            ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);
            TestFalse(TEXT("Loaded short tutorial releases arrival even with single-day Training"),Second->IsArenaArrivalHeld());
            State->bTutorialActive=false;State->bSingleDayLoop=false;
        }
    }
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
