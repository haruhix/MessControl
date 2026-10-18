#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
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
    auto* Controller=World->SpawnActor<APlayerController>();Mode->DefaultPawnClass=AMCToothCharacter::StaticClass();
    const FVector Buried(10000,0,1450);
    Mode->RestartPlayerAtTransform(Controller,FTransform(Buried));
    auto* Hero=Cast<AMCToothCharacter>(Controller->GetPawn());
    TestNotNull(TEXT("A buried player start still produces a pawn"),Hero);
    if(Hero) {
        FHitResult Floor;TestTrue(TEXT("Spawn has tissue underneath"),Tongue->SurfacePoint(Hero->GetActorLocation(),Floor));
        const float Bottom=Hero->GetActorLocation().Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        TestTrue(FString::Printf(TEXT("Capsule starts above tissue with settling clearance (bottom=%.3f floor=%.3f half=%.3f normal=%.3f)"),Bottom,Floor.ImpactPoint.Z,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),Floor.ImpactNormal.Z),Bottom>=Floor.ImpactPoint.Z+29.f);
        TestTrue(TEXT("Spawn does not shift the player horizontally"),FVector::DistSquared2D(Hero->GetActorLocation(),Buried)<1.f);
    }
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
