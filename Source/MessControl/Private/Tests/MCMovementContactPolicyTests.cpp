#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCMovementContactPolicy.h"
#include "MCNetworkProtocol.h"
#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/NetworkVersion.h"
#include "Serialization/CustomVersion.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCFoodPreparedCutTests { UStaticMesh* MakeBox(); }

namespace MCMovementContactTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    UPackage* Package=World->GetOutermost();
    TStrongObjectPtr<UStaticMesh> FoodMesh;
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
    AMCFoodActor* Food(FVector Point,FVector Dimensions)
    {
        if(!FoodMesh.IsValid())FoodMesh.Reset(MCFoodPreparedCutTests::MakeBox());
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;Row.Resistance=EMCFoodResistance::Soft;
        Row.WholeMeshes={FoodMesh.Get()};Row.FragmentMeshes={FoodMesh.Get()};
        Row.Scale=Row.FragmentScale=Dimensions/100.;Row.Mass=1.4f;Row.Health=100;Row.SpoilSeconds=300;
        const FTransform Pose(FQuat::Identity,Point);
        auto* Piece=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(929901);Piece->ConfigureItem(TEXT("WalkingCollisionFixture"),Row,Random,true);
        Piece->FinishSpawning(Pose);Piece->Phase=EMCFoodPhase::Free;
        return Piece;
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
    Policy.Set(Shape,Other,EMCMovementContactReason::StableFood,true);
    Policy.Set(Shape,Other,EMCMovementContactReason::CarriedFood,true);
    Policy.Set(Shape,Other,EMCMovementContactReason::ScriptedFlight,true);
    Policy.Set(Shape,Other,EMCMovementContactReason::StableFood,false);
    TestTrue(TEXT("Publishing a walking obstacle cannot release independent carry or scripted-flight exclusions"),
        Shape->GetMoveIgnoreActors().Contains(Other)
        && Policy.HasReason(Other,EMCMovementContactReason::CarriedFood)
        && Policy.HasReason(Other,EMCMovementContactReason::ScriptedFlight));
    Policy.Set(Shape,Other,EMCMovementContactReason::CarriedFood,false);
    TestTrue(TEXT("Releasing carry leaves the independent scripted-flight reason"),Shape->GetMoveIgnoreActors().Contains(Other));
    Policy.Set(Shape,Other,EMCMovementContactReason::ScriptedFlight,false);
    TestFalse(TEXT("Releasing the last remaining reason restores queries"),Shape->GetMoveIgnoreActors().Contains(Other));
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
    FWorld T;auto* Hero=T.Player(FVector(-240,0,10085));T.Step(.3f);
    auto* Food=T.Food(FVector(0,0,10050),FVector(100));
    auto* Capsule=Hero->GetCapsuleComponent();auto* Move=Hero->GetCharacterMovement();
    TestTrue(TEXT("A newly spawned moving food is initially excluded until its stable obstacle is ready"),Capsule->GetMoveIgnoreActors().Contains(Food));
    T.Step(.7f);
    if(!TestTrue(TEXT("A substantial settled fragment publishes its real walking obstacle"),Food->HasWalkingObstacle()))return false;
    TestEqual(TEXT("The walking shape participates in queries without Chaos contacts"),Food->WalkingBlocker->GetCollisionEnabled(),ECollisionEnabled::QueryOnly);
    TestEqual(TEXT("The proxy uses the dedicated walking channel"),Food->WalkingBlocker->GetCollisionObjectType(),AMCFoodActor::WalkingObstacleChannel);
    TestTrue(TEXT("The query proxy shares the already prepared food shape rather than preparing another convex"),Food->WalkingBlocker->GetBodySetup()==Food->Body->GetBodySetup());
    TestFalse(TEXT("The fixed walking proxy never simulates a second physics body"),Food->WalkingBlocker->IsSimulatingPhysics());
    TestFalse(TEXT("A published obstacle releases only the stable-food movement exclusion"),Capsule->GetMoveIgnoreActors().Contains(Food));
    const FTransform Anchor=Food->WalkingBlocker->GetComponentTransform();
    const float Health=Hero->Status->State.Health;
    const int32 Knockdowns=Hero->ToothPhysics->KnockdownCount;
    Hero->SetBase(Food->Body);
    FHitResult OldFloor(Food,Food->Body,Food->GetActorLocation()+FVector(0,0,50),FVector::UpVector);
    OldFloor.bBlockingHit=true;Move->CurrentFloor.SetFromSweep(OldFloor,0,true);
    Hero->RefreshFoodMovementContact(Food);
    TestTrue(TEXT("The raw simulated food body cannot remain a cached movement base or floor"),
        Hero->GetMovementBase()!=Food->Body && !Move->CurrentFloor.IsWalkableFloor());
    for(int32 Frame=0;Frame<72;++Frame) {Hero->AddMovementInput(FVector::ForwardVector);T.Step(1.f/60);}
    const FBox Bounds=Food->GetWalkingCollisionBounds();
    const double Stop=Bounds.Min.X-Capsule->GetScaledCapsuleRadius();
    TestTrue(TEXT("Real CMC walking advances to the large piece but cannot cross its surface"),
        Hero->GetActorLocation().X>-180 && Hero->GetActorLocation().X<=Stop+3 && Move->IsMovingOnGround());
    TestTrue(TEXT("Walking against food retains ordinary world support"),Hero->GetMovementBase()!=Food->Body && Hero->GetMovementBase()!=Food->WalkingBlocker);
    TestEqual(TEXT("A resting food obstacle does not injure the walker"),Hero->Status->State.Health,Health);
    TestEqual(TEXT("A resting food obstacle does not knock the walker down"),Hero->ToothPhysics->KnockdownCount,Knockdowns);
    TestTrue(TEXT("The anchored query collider did not follow or shove its physical food"),Food->WalkingBlocker->GetComponentTransform().Equals(Anchor,.01));
    TestEqual(TEXT("The fragment's raw Chaos body continues to ignore Pawn"),Food->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
    TestEqual(TEXT("Character keeps world blocking"),Capsule->GetCollisionResponseToChannel(ECC_WorldStatic),ECR_Block);
    FHitResult Aim;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCStableFoodAim),false,Hero);
    const bool Hit=T.World->LineTraceSingleByChannel(Aim,FVector(-200,0,10050),FVector(200,0,10050),ECC_Visibility,Query);
    TestTrue(TEXT("Independent targeting still hits the actual physical fragment rather than a walking-only proxy"),Hit && Aim.GetActor()==Food && Aim.GetComponent()==Food->Body);
    Hero->SetMovementContactReason(Food,EMCMovementContactReason::ScriptedFlight,true);
    Hero->RefreshFoodMovementContact(Food);
    TestTrue(TEXT("The independently owned scripted-flight reason still excludes a published obstacle"),Capsule->GetMoveIgnoreActors().Contains(Food));
    Hero->SetMovementContactReason(Food,EMCMovementContactReason::ScriptedFlight,false);
    TestFalse(TEXT("Ending scripted transit restores the already-published walking obstacle"),Capsule->GetMoveIgnoreActors().Contains(Food));
    Food->Destroy();
    TestFalse(TEXT("Food lifetime cleanup removes its owned exclusions"),Capsule->GetMoveIgnoreActors().Contains(Food));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSmallFoodMovementContacts,
    "MessControl.Networking.MovementContacts.SmallFoodYieldsWithoutPlayerImpulse",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSmallFoodMovementContacts::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;auto* Hero=T.Player(FVector(-220,0,10085));T.Step(.3f);
    auto* Food=T.Food(FVector(0,0,10015),FVector(30));T.Step(.6f);
    TestFalse(TEXT("A small physical fragment does not publish a blocking proxy"),Food->HasWalkingObstacle());
    TestTrue(TEXT("The small fragment remains excluded from movement sweeps"),Hero->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Food));
    const FVector Start=Food->GetActorLocation();const FBox OriginalBounds=Food->GetWalkingCollisionBounds();
    const float Health=Hero->Status->State.Health;
    const int32 Knockdowns=Hero->ToothPhysics->KnockdownCount;
    float MaximumFoodSpeed=0;
    for(int32 Frame=0;Frame<72;++Frame) {
        Hero->AddMovementInput(FVector::ForwardVector);T.Step(1.f/60);
        MaximumFoodSpeed=FMath::Max(MaximumFoodSpeed,float(Food->Body->GetPhysicsLinearVelocity().Size2D()));
    }
    AddInfo(FString::Printf(TEXT("Small debris walking: hero=%s grounded=%d speed=%.2f maxSpeed=%.2f foodStart=%s foodEnd=%s maxFoodSpeed=%.2f"),
        *Hero->GetActorLocation().ToString(),Hero->GetCharacterMovement()->IsMovingOnGround(),
        Hero->GetVelocity().Size2D(),Hero->GetCharacterMovement()->GetMaxSpeed(),
        *Start.ToString(),*Food->GetActorLocation().ToString(),MaximumFoodSpeed));
    TestTrue(TEXT("Real walking carries the entire capsule beyond the small debris footprint with ordinary ground support"),
        Hero->GetActorLocation().X-Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()>OriginalBounds.Max.X+5
        && Hero->GetCharacterMovement()->IsMovingOnGround());
    TestTrue(TEXT("Small physical debris receives an outward physical response to walking"),Food->GetActorLocation().X>Start.X+.5 && MaximumFoodSpeed>10);
    TestTrue(TEXT("The cosmetic yielding impulse remains bounded rather than launching debris"),MaximumFoodSpeed<300);
    TestEqual(TEXT("Yielding debris does not change player health"),Hero->Status->State.Health,Health);
    TestEqual(TEXT("Yielding debris does not knock the player down"),Hero->ToothPhysics->KnockdownCount,Knockdowns);
    TestFalse(TEXT("Small debris never becomes the player's physical movement base"),Hero->GetMovementBase()==Food->Body);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodWalkingAnchorLifecycle,
    "MessControl.Networking.MovementContacts.FoodWalkingAnchorAndSafeRearm",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodWalkingAnchorLifecycle::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;auto* Hero=T.Player(FVector(-240,0,10085));T.Step(.3f);
    auto* Food=T.Food(FVector(0,0,10050),FVector(100));T.Step(.7f);
    if(!TestTrue(TEXT("The large fixture has settled before ledge validation"),Food->HasWalkingObstacle()))return false;
    auto* Move=Hero->GetCharacterMovement();const FTransform Anchor=Food->WalkingBlocker->GetComponentTransform();
    const FBox Bounds=Food->GetWalkingCollisionBounds();
    Hero->SetActorLocation(FVector(Bounds.GetCenter().X,Bounds.GetCenter().Y,Bounds.Max.Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+35),false,nullptr,ETeleportType::TeleportPhysics);
    Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Falling);T.Step(.65f);
    if(!TestTrue(TEXT("A real falling character can stand on the fixed query ledge"),Move->IsMovingOnGround() && Hero->GetMovementBase()==Food->WalkingBlocker))return false;
    const FVector HeroBefore=Hero->GetActorLocation();
    Food->SetActorLocation(Food->GetActorLocation()+FVector(3,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    T.Step(.1f);
    TestTrue(TEXT("Sub-tolerance physical-body jitter leaves the walking anchor fixed"),Food->HasWalkingObstacle() && Food->WalkingBlocker->GetComponentTransform().Equals(Anchor,.01));
    TestTrue(TEXT("The walker is not carried horizontally by raw food jitter"),FVector::Dist2D(Hero->GetActorLocation(),HeroBefore)<.5);
    TestTrue(TEXT("Refreshing food contact preserves a valid fixed-proxy floor"),Hero->GetMovementBase()==Food->WalkingBlocker);
    Food->SetActorLocation(Food->GetActorLocation()+FVector(20,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    Food->UpdateWalkingCollision(1.f/60);
    TestFalse(TEXT("A real displacement beyond tolerance immediately retires the old obstacle"),Food->HasWalkingObstacle());
    TestTrue(TEXT("Retiring an anchor restores the ordinary movement exclusion"),Hero->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Food));
    Hero->SetActorLocation(FVector(-240,0,10085),false,nullptr,ETeleportType::TeleportPhysics);Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Falling);
    T.Step(.2f);
    TestFalse(TEXT("A displaced food cannot skip the fresh settlement delay even with a clear footprint"),Food->HasWalkingObstacle());
    T.Step(.5f);
    TestTrue(TEXT("A clear newly settled footprint can publish a fresh anchor"),Food->HasWalkingObstacle());
    Food->SetActorLocation(Food->GetActorLocation()+FVector(20,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    Food->UpdateWalkingCollision(1.f/60);
    Hero->SetActorLocation(FVector(Food->GetActorLocation().X,Food->GetActorLocation().Y,10060),false,nullptr,ETeleportType::TeleportPhysics);
    Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Falling);T.Step(.65f);
    TestFalse(TEXT("A settled food cannot arm its query blocker through a worker occupying the footprint"),Food->HasWalkingObstacle());
    Hero->SetActorLocation(FVector(-240,0,10085),false,nullptr,ETeleportType::TeleportPhysics);Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Falling);
    T.Step(.5f);
    TestTrue(TEXT("Moving clear allows the settled food to arm safely"),Food->HasWalkingObstacle());
    Food->Dispose();
    TestFalse(TEXT("Disposal immediately removes the walking obstacle"),Food->HasWalkingObstacle());
    TestEqual(TEXT("Disposed food has no stale blocking query shape"),Food->WalkingBlocker->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodWalkingBoundsClassification,
    "MessControl.Networking.MovementContacts.FoodWalkingUsesRemainingBounds",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodWalkingBoundsClassification::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;
    auto* Short=T.Food(FVector(-800,0,10100),FVector(100,100,44));
    auto* Narrow=T.Food(FVector(-600,0,10100),FVector(100,49,100));
    auto* Boundary=T.Food(FVector(-400,0,10100),FVector(50,50,45));
    auto* Scaled=T.Food(FVector(-200,0,10100),FVector(40));
    TestFalse(TEXT("A fragment below the real height threshold is small"),Short->IsSubstantialWalkingObstacle());
    TestFalse(TEXT("A fragment below the minimum horizontal span is small"),Narrow->IsSubstantialWalkingObstacle());
    TestTrue(TEXT("Both horizontal spans and height qualify at the authored boundary"),Boundary->IsSubstantialWalkingObstacle());
    TestFalse(TEXT("The unscaled small source is initially below the walking threshold"),Scaled->IsSubstantialWalkingObstacle());
    Scaled->SetActorScale3D(FVector(1.5));
    TestTrue(TEXT("Actor world scale participates in the walking dimensions"),Scaled->IsSubstantialWalkingObstacle());
    TestTrue(TEXT("World scale is applied once to the source dimensions"),Scaled->GetWalkingCollisionBounds().GetSize().Equals(FVector(60),.05));

    auto* Source=T.Food(FVector(0,600,10125),FVector(400,300,250));Source->SetActorTickEnabled(false);
    Source->Body->SetSimulatePhysics(false);
    auto* Hero=T.Player(FVector(-350,600,10085));
    const FVector Point=Source->GetActorLocation()+FVector(152,0,0);
    if(!TestTrue(TEXT("A real off-centre plane creates two unfinished physical fragments"),Source->HitFood(1,FVector::ForwardVector,Hero,Point,FVector::ForwardVector)))return false;
    AMCFoodActor* Thin=nullptr;AMCFoodActor* Large=nullptr;int32 Remaining=0;
    for(TActorIterator<AMCFoodActor> It(T.World);It;++It)if(*It!=Source && It->bFragment && !It->IsDisposed()
        && It->ItemName==Source->ItemName && It->GetActorLocation().Y>500) {
        ++Remaining;
        if(It->GetWalkingCollisionBounds().GetSize().X<100)Thin=*It;else Large=*It;
    }
    if(!TestEqual(TEXT("Both non-ready pieces remain physical after this cut"),Remaining,2)
        || !TestNotNull(TEXT("The narrow off-centre child exists"),Thin)
        || !TestNotNull(TEXT("The substantial remaining child exists"),Large))return false;
    TestFalse(TEXT("The narrow child is still above automatic volume readiness"),Thin->FitsAutomaticIntakePortion());
    TestFalse(TEXT("The large child is still above automatic volume readiness"),Large->FitsAutomaticIntakePortion());
    TestTrue(TEXT("The narrow child reports its actual 48cm remaining span"),Thin->GetWalkingCollisionBounds().GetSize().Equals(FVector(48,300,250),.1));
    TestTrue(TEXT("The remaining child reports the complementary actual dimensions"),Large->GetWalkingCollisionBounds().GetSize().Equals(FVector(352,300,250),.1));
    TestFalse(TEXT("The narrow child does not inherit a blocker from its large original mesh"),Thin->IsSubstantialWalkingObstacle());
    TestTrue(TEXT("The substantial child qualifies independently"),Large->IsSubstantialWalkingObstacle());
    TestTrue(TEXT("Off-centre remaining bounds retain their measured centre"),Thin->GetWalkingCollisionBounds().GetCenter().X>Source->GetActorLocation().X+150);
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
