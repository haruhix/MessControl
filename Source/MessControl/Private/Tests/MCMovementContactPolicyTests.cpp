#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCMovementContactPolicy.h"
#include "MCNetworkProtocol.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/NetworkVersion.h"
#include "Serialization/CustomVersion.h"
#include "UObject/Package.h"

namespace MCMovementContactTests
{
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
        State->GameplayLoadId=FGuid::NewGuid();
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
    AMCToothCharacter* Player(FVector Point)
    {
        auto* Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();
        auto* State=World->SpawnActor<AMCPlayerState>();State->SetOwner(Controller);Controller->SetPlayerState(State);
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Point,FRotator::ZeroRotator);
        Controller->Possess(Hero);Hero->Status->Initialize(100);return Hero;
    }
    void Step(float Seconds)
    {
        for(float At=0;At<Seconds-UE_SMALL_NUMBER;At+=1.f/60) {
            ++GFrameCounter;World->Tick(LEVELTICK_All,FMath::Min(1.f/60,Seconds-At));
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMovementContactReasons,
    "MessControl.Networking.MovementContacts.ReasonUnionAndForeignOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMovementContactReasons::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;auto* Owner=T.Box(FVector(0,0,11000),FVector(10));
    auto* Shape=Cast<UPrimitiveComponent>(Owner->GetRootComponent());
    auto* Other=T.Box(FVector(100,0,11000),FVector(10));
    FMCMovementContactPolicy Policy;
    Policy.Set(Shape,Other,EMCMovementContactReason::StableFood,true);
    Policy.Set(Shape,Other,EMCMovementContactReason::CarriedFood,true);
    Policy.Set(Shape,Other,EMCMovementContactReason::ScriptedFlight,true);
    Policy.Set(Shape,Other,EMCMovementContactReason::CarriedFood,false);
    Policy.Set(Shape,Other,EMCMovementContactReason::ScriptedFlight,false);
    TestTrue(TEXT("Dropping a carried object or ending transit cannot undo the standing food policy"),
        Shape->GetMoveIgnoreActors().Contains(Other) && Policy.HasReason(Other,EMCMovementContactReason::StableFood));
    Policy.Set(Shape,Other,EMCMovementContactReason::StableFood,false);
    TestFalse(TEXT("The final owned reason releases its exclusion"),Shape->GetMoveIgnoreActors().Contains(Other));
    Shape->IgnoreActorWhenMoving(Other,true);
    Policy.Set(Shape,Other,EMCMovementContactReason::StableFood,true);Policy.Release();
    TestTrue(TEXT("A pre-existing foreign exclusion survives policy cleanup"),Shape->GetMoveIgnoreActors().Contains(Other));
    Shape->IgnoreActorWhenMoving(Other,false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStableFoodMovementContacts,
    "MessControl.Networking.MovementContacts.StableFoodPreservesQueriesAndWorld",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStableFoodMovementContacts::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;auto* Hero=T.Player(FVector(-160,0,10085));T.Step(.3f);
    auto* Food=T.World->SpawnActor<AMCFoodActor>(FVector(0,0,10065),FRotator::ZeroRotator);
    Food->SetActorTickEnabled(false);Food->Body->SetSimulatePhysics(false);
    Food->Body->SetBoxExtent(FVector(35));Food->Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Food->Body->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
    Food->Body->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    auto* Capsule=Hero->GetCapsuleComponent();auto* Move=Hero->GetCharacterMovement();
    TestTrue(TEXT("A newly spawned food joins the shared policy without a per-tick world scan"),Capsule->GetMoveIgnoreActors().Contains(Food));
    Hero->SetBase(Food->Body);
    FHitResult OldFloor(Food,Food->Body,Food->GetActorLocation()+FVector(0,0,35),FVector::UpVector);
    OldFloor.bBlockingHit=true;Move->CurrentFloor.SetFromSweep(OldFloor,0,true);
    Hero->RefreshFoodMovementContact(Food);
    TestTrue(TEXT("Food cannot remain a cached movement base/floor after policy activation"),
        Hero->GetMovementBase()!=Food->Body && !Move->CurrentFloor.IsWalkableFloor());
    for(int32 Frame=0;Frame<72;++Frame) {Hero->AddMovementInput(FVector::ForwardVector);T.Step(1.f/60);}
    TestTrue(TEXT("Real CMC locomotion crosses a food collider with stable world support"),Hero->GetActorLocation().X>160 && Move->IsMovingOnGround());
    TestEqual(TEXT("Food keeps its Chaos Pawn response for canonical incoming hits"),Food->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
    TestEqual(TEXT("Character keeps world blocking"),Capsule->GetCollisionResponseToChannel(ECC_WorldStatic),ECR_Block);
    FHitResult Aim;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCStableFoodAim),false,Hero);
    const bool Hit=T.World->LineTraceSingleByChannel(Aim,FVector(-200,0,10065),FVector(200,0,10065),ECC_Visibility,Query);
    TestTrue(TEXT("Independent targeting still hits the real food collider"),Hit && Aim.GetActor()==Food);
    Hero->SetMovementContactReason(Food,EMCMovementContactReason::CarriedFood,true);
    Hero->SetMovementContactReason(Food,EMCMovementContactReason::CarriedFood,false);
    TestTrue(TEXT("A carry/drop transition retains the ordinary stable-food exclusion"),Capsule->GetMoveIgnoreActors().Contains(Food));
    Food->Destroy();
    TestFalse(TEXT("Food lifetime cleanup removes its owned exclusions"),Capsule->GetMoveIgnoreActors().Contains(Food));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNetworkFoundationProtocol,
    "MessControl.Networking.Foundation.ProtocolCompatibility",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNetworkFoundationProtocol::RunTest(const FString&)
{
    const auto* Version=FNetworkVersion::GetNetworkCustomVersions().GetVersion(MCNetworkProtocol::FoundationGuid);
    TestTrue(TEXT("The project registers its custom movement/interaction wire version before connecting"),
        Version && Version->Version==MCNetworkProtocol::FoundationVersion);
    const uint32 Current=FNetworkVersion::GetLocalNetworkVersion();
    TestTrue(TEXT("Matching project versions retain native compatibility"),FNetworkVersion::IsNetworkCompatible(Current,Current));
    TestFalse(TEXT("The native handshake rejects an incompatible checksum"),FNetworkVersion::IsNetworkCompatible(Current,Current^0xA5A5A5A5u));
    return true;
}
#endif
