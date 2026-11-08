#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Package.h"

namespace MCPeerMovementCollisionTests
{
struct FPlayer
{
    AMCToothCharacter* Hero=nullptr;
    APlayerController* Controller=nullptr;
    AMCPlayerState* State=nullptr;
};
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    UPackage* Package=World->GetOutermost();
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;
        Box(FVector(0,0,9980),FVector(3000,3000,20));
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
        if(IsValid(Package) && Package->GetName().StartsWith(TEXT("/Temp/"))) Package->SetDirtyFlag(false);
    }
    AActor* Box(FVector Point,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape);Shape->SetBoxExtent(Extent);Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->RegisterComponent();Actor->SetActorLocation(Point);return Actor;
    }
    FPlayer Player(FVector Point,bool Identity=true)
    {
        FPlayer P;P.Controller=World->SpawnActor<APlayerController>();P.Controller->SetAsLocalPlayerController();
        P.State=World->SpawnActor<AMCPlayerState>();P.State->SetOwner(P.Controller);
        if(Identity) P.Controller->SetPlayerState(P.State);
        P.Hero=World->SpawnActor<AMCToothCharacter>(Point,FRotator::ZeroRotator);
        P.Controller->Possess(P.Hero);P.Hero->Status->Initialize(100);return P;
    }
    void Step(float Seconds)
    {
        for(float At=0;At<Seconds-UE_SMALL_NUMBER;At+=1.f/60) {
            ++GFrameCounter;World->Tick(LEVELTICK_All,FMath::Min(1.f/60,Seconds-At));
        }
    }
};
bool Ignores(const AMCToothCharacter* A,const AMCToothCharacter* B)
{return A->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(B);}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPeerCapsuleMovementQueries,
    "MessControl.Networking.PeerCapsules.PassThroughPreservesQueriesAndBlockers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPeerCapsuleMovementQueries::RunTest(const FString&)
{
    using namespace MCPeerMovementCollisionTests;
    FWorld T;auto A=T.Player(FVector(-140,0,10085)),B=T.Player(FVector(140,0,10085));T.Step(.2f);
    auto* Capsule=A.Hero->GetCapsuleComponent();auto* Other=B.Hero->GetCapsuleComponent();
    const auto PawnResponse=Capsule->GetCollisionResponseToChannel(ECC_Pawn);
    const auto WorldResponse=Capsule->GetCollisionResponseToChannel(ECC_WorldStatic);
    const auto MeshWorld=A.Hero->GetMesh()->GetCollisionResponseToChannel(ECC_WorldStatic);
    TestTrue(TEXT("Both actual player capsules ignore only each other's movement sweeps"),Ignores(A.Hero,B.Hero) && Ignores(B.Hero,A.Hero));
    for(int32 Frame=0;Frame<72;++Frame) {
        A.Hero->AddMovementInput(FVector::ForwardVector);B.Hero->AddMovementInput(-FVector::ForwardVector);T.Step(1.f/60);
    }
    TestTrue(TEXT("Real CMC movement crosses the other player's original position in both directions"),
        A.Hero->GetActorLocation().X>140 && B.Hero->GetActorLocation().X< -140);
    TestEqual(TEXT("Pawn channel response remains unchanged"),Capsule->GetCollisionResponseToChannel(ECC_Pawn),PawnResponse);
    TestEqual(TEXT("World response remains unchanged"),Capsule->GetCollisionResponseToChannel(ECC_WorldStatic),WorldResponse);
    TestEqual(TEXT("Skeletal world collision remains unchanged"),A.Hero->GetMesh()->GetCollisionResponseToChannel(ECC_WorldStatic),MeshWorld);

    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPeerQueryWitness),false,A.Hero);
    const FVector Target=B.Hero->GetActorLocation(),Start=Target-FVector(140,0,0),End=Target+FVector(140,0,0);
    TestTrue(TEXT("Hook-style direct real capsule trace still hits the peer"),Other->LineTraceComponent(Hit,Start,End,Query));
    TestTrue(TEXT("Independent projectile pawn-object sweep still hits the peer"),
        T.World->SweepSingleByObjectType(Hit,Start,End,FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),FCollisionShape::MakeSphere(3),Query)
        && Hit.GetActor()==B.Hero);
    // The native Pawn profile ignores Visibility. A fixture-only blocking
    // response verifies that movement ignores never leak into world queries.
    const auto Visibility=Other->GetCollisionResponseToChannel(ECC_Visibility);
    Other->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    TestTrue(TEXT("Visibility query can still hit a peer configured to block that query"),
        T.World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Query) && Hit.GetActor()==B.Hero);
    Other->SetCollisionResponseToChannel(ECC_Visibility,Visibility);

    auto* Enemy=T.World->SpawnActor<APawn>();auto* EnemyCapsule=NewObject<UCapsuleComponent>(Enemy);
    Enemy->SetRootComponent(EnemyCapsule);EnemyCapsule->InitCapsuleSize(34,58);EnemyCapsule->SetCollisionProfileName(TEXT("Pawn"));
    EnemyCapsule->RegisterComponent();Enemy->SetActorLocation(FVector(500,300,10060));
    auto* Move=A.Hero->GetCharacterMovement();
    A.Hero->SetActorLocation(FVector(350,300,10060),false,nullptr,ETeleportType::TeleportPhysics);
    Move->SafeMoveUpdatedComponent(FVector(300,0,0),FQuat::Identity,true,Hit);
    TestTrue(TEXT("A nonplayer enemy Pawn still blocks the player's CMC sweep"),Hit.bBlockingHit && Hit.GetActor()==Enemy);
    auto* Wall=T.Box(FVector(500,600,10060),FVector(15,80,90));
    A.Hero->SetActorLocation(FVector(350,600,10060),false,nullptr,ETeleportType::TeleportPhysics);
    Move->SafeMoveUpdatedComponent(FVector(300,0,0),FQuat::Identity,true,Hit);
    TestTrue(TEXT("World geometry still blocks the same CMC sweep"),Hit.bBlockingHit && Hit.GetActor()==Wall);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPeerCapsuleLifecycle,
    "MessControl.Networking.PeerCapsules.IdentityLifecycleAndOwnedCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPeerCapsuleLifecycle::RunTest(const FString&)
{
    using namespace MCPeerMovementCollisionTests;
    FWorld T;auto A=T.Player(FVector(-300,0,10085)),B=T.Player(FVector(300,0,10085),false);T.Step(.2f);
    TestFalse(TEXT("An unassigned player-state pawn is not exempted"),Ignores(A.Hero,B.Hero));
    B.Controller->SetPlayerState(B.State);B.Hero->SetPlayerState(B.State);
    TestTrue(TEXT("Late PlayerState assignment installs reciprocal movement ignores"),Ignores(A.Hero,B.Hero) && Ignores(B.Hero,A.Hero));
    B.Hero->SetPlayerState(nullptr);
    TestFalse(TEXT("Identity removal releases the entries in both directions"),Ignores(A.Hero,B.Hero) || Ignores(B.Hero,A.Hero));
    B.Hero->SetPlayerState(B.State);
    B.Hero->GetCharacterMovement()->DisableMovement();T.Step(1.f/60);
    TestFalse(TEXT("A movement-locked pawn is outside ordinary walking/jump"),Ignores(A.Hero,B.Hero) || Ignores(B.Hero,A.Hero));
    B.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);T.Step(1.f/60);
    TestTrue(TEXT("Returning to ordinary movement restores the pair"),Ignores(A.Hero,B.Hero) && Ignores(B.Hero,A.Hero));
    B.Hero->bPreviewAnimation=true;T.Step(1.f/60);
    TestFalse(TEXT("Showcase/preview characters are excluded even with a player state"),Ignores(A.Hero,B.Hero));
    B.Hero->bPreviewAnimation=false;T.Step(1.f/60);
    B.Controller->UnPossess();
    A.Hero->GetCapsuleComponent()->IgnoreActorWhenMoving(B.Hero,true);
    B.Controller->Possess(B.Hero);
    TestTrue(TEXT("The policy coexists with a pre-existing external movement ignore"),Ignores(B.Hero,A.Hero));
    B.Controller->UnPossess();
    TestTrue(TEXT("Cleanup does not remove an entry this policy did not add"),Ignores(A.Hero,B.Hero));
    TestFalse(TEXT("The reciprocal policy-owned entry is still removed"),Ignores(B.Hero,A.Hero));
    A.Hero->GetCapsuleComponent()->IgnoreActorWhenMoving(B.Hero,false);B.Controller->Possess(B.Hero);
    auto* Old=B.Hero;Old->Destroy();
    TestFalse(TEXT("Destroy/end-play clears the surviving capsule's old pawn reference"),A.Hero->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Old));
    B.Hero=T.World->SpawnActor<AMCToothCharacter>(FVector(300,0,10085),FRotator::ZeroRotator);B.Controller->Possess(B.Hero);T.Step(.1f);
    TestTrue(TEXT("A respawned pawn acquires a fresh reciprocal pair"),Ignores(A.Hero,B.Hero) && Ignores(B.Hero,A.Hero));
    TestTrue(TEXT("Public authoritative death API accepts the live gameplay pawn"),B.Hero->Status->KillFromArenaBoundary());
    TestFalse(TEXT("The normal death notification leaves the pawn dead"),B.Hero->Status->IsAlive());
    TestFalse(TEXT("Death immediately clears both ordinary-movement exemptions"),Ignores(A.Hero,B.Hero) || Ignores(B.Hero,A.Hero));
    TestEqual(TEXT("The corpse mesh retains world blocking"),B.Hero->GetMesh()->GetCollisionResponseToChannel(ECC_WorldStatic),ECR_Block);
    return true;
}
#endif
