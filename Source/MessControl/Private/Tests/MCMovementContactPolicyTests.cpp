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
#include "CollisionShape.h"
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
namespace MCFoodCapturedBroccoliTests { UStaticMesh* MakeMesh(FVector& OutFoodPoint); }

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
    Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Falling);
    Food->UpdateWalkingCollision(1.f/60);
    TestFalse(TEXT("A true worker overlap cannot immediately publish a blocking proxy"),Food->HasWalkingObstacle());
    T.Step(.2f);
    TestFalse(TEXT("Food separation cannot skip the initial settlement delay"),Food->HasWalkingObstacle());
    T.Step(2.8f);
    TestTrue(TEXT("A settled Free food moves itself clear before safely publishing a new anchor"),Food->HasWalkingObstacle()
        && !Food->Body->OverlapComponent(Hero->GetActorLocation(),Hero->GetActorQuat(),Hero->GetCapsuleComponent()->GetCollisionShape(5.f)));
    Food->Dispose();
    TestFalse(TEXT("Disposal immediately removes the walking obstacle"),Food->HasWalkingObstacle());
    TestEqual(TEXT("Disposed food has no stale blocking query shape"),Food->WalkingBlocker->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodWalkingOverlapSeparation,
    "MessControl.Networking.MovementContacts.FoodSeparatesFromStationaryWorker",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodWalkingOverlapSeparation::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;auto* Hero=T.Player(FVector(0,0,10085));T.Step(.3f);
    auto* Food=T.Food(FVector(65,0,10050),FVector(100));Food->Body->SetSimulatePhysics(false);
    auto* Capsule=Hero->GetCapsuleComponent();auto* Move=Hero->GetCharacterMovement();
    if(!TestTrue(TEXT("The prepared substantial food genuinely overlaps the stationary worker"),
        Food->Body->OverlapComponent(Capsule->GetComponentLocation(),Capsule->GetComponentQuat(),Capsule->GetCollisionShape(5.f))))return false;
    const FVector HeroStart=Hero->GetActorLocation(),FoodStart=Food->GetActorLocation();
    const float Health=Hero->Status->State.Health;const int32 Knockdowns=Hero->ToothPhysics->KnockdownCount;
    FVector PreviousFood=FoodStart;double MaximumFoodStep=0,MaximumHeroMotion=0,MaximumHeroSpeed=0;bool GroundPreserved=true;
    for(int32 Frame=0;Frame<180 && !Food->HasWalkingObstacle();++Frame) {
        T.Step(1.f/60);
        MaximumFoodStep=FMath::Max(MaximumFoodStep,FVector::Dist(PreviousFood,Food->GetActorLocation()));
        MaximumHeroMotion=FMath::Max(MaximumHeroMotion,FVector::Dist(HeroStart,Hero->GetActorLocation()));
        MaximumHeroSpeed=FMath::Max(MaximumHeroSpeed,Hero->GetVelocity().Size());
        GroundPreserved&=Move->IsMovingOnGround() && Hero->GetMovementBase()!=Food->Body && Hero->GetMovementBase()!=Food->WalkingBlocker;
        PreviousFood=Food->GetActorLocation();
    }
    AddInfo(FString::Printf(TEXT("Food separation: start=%s end=%s armed=%d maxStep=%.3f heroMotion=%.3f heroSpeed=%.3f ground=%d"),
        *FoodStart.ToString(),*Food->GetActorLocation().ToString(),Food->HasWalkingObstacle(),MaximumFoodStep,MaximumHeroMotion,MaximumHeroSpeed,GroundPreserved));
    if(!TestTrue(TEXT("Only the overlapping Free food separates and eventually arms its real walking proxy"),Food->HasWalkingObstacle()))return false;
    TestTrue(TEXT("The food really moved within the bounded horizontal separation budget"),
        FVector::Dist2D(FoodStart,Food->GetActorLocation())>1 && FVector::Dist2D(FoodStart,Food->GetActorLocation())<=400.1
        && FMath::Abs(FoodStart.Z-Food->GetActorLocation().Z)<.01 && MaximumFoodStep<=16.1);
    TestFalse(TEXT("The published food is clear of the actual padded worker capsule"),
        Food->Body->OverlapComponent(Capsule->GetComponentLocation(),Capsule->GetComponentQuat(),Capsule->GetCollisionShape(5.f)));
    TestTrue(TEXT("Separating food never moves or accelerates the stationary grounded worker"),
        MaximumHeroMotion<.1 && MaximumHeroSpeed<.1 && GroundPreserved);
    TestEqual(TEXT("Food-owned separation preserves worker health"),Hero->Status->State.Health,Health);
    TestEqual(TEXT("Food-owned separation cannot knock the worker down"),Hero->ToothPhysics->KnockdownCount,Knockdowns);
    const FVector Direction=(Food->GetWalkingCollisionBounds().GetCenter()-Hero->GetActorLocation()).GetSafeNormal2D();
    const FVector WalkStart=Hero->GetActorLocation();
    FHitResult Contact;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCSeparatedFoodReentry),false,Hero);
    const bool Hit=T.World->SweepSingleByObjectType(Contact,WalkStart,WalkStart+Direction*600,FQuat::Identity,
        FCollisionObjectQueryParams(AMCFoodActor::WalkingObstacleChannel),Capsule->GetCollisionShape(),Query);
    if(!TestTrue(TEXT("A re-entry sweep reaches the exact separated food proxy from outside"),
        Hit && !Contact.bStartPenetrating && Contact.GetActor()==Food && Contact.GetComponent()==Food->WalkingBlocker))return false;
    const double ExpectedTravel=FVector::DotProduct(Contact.Location-WalkStart,Direction);double FurthestTravel=0;
    for(int32 Frame=0;Frame<120;++Frame) {
        Hero->AddMovementInput(Direction);T.Step(1.f/60);
        FurthestTravel=FMath::Max(FurthestTravel,FVector::DotProduct(Hero->GetActorLocation()-WalkStart,Direction));
    }
    const double Travel=FVector::DotProduct(Hero->GetActorLocation()-WalkStart,Direction);
    TestTrue(TEXT("Ordinary re-entry reaches the separated food and cannot cross its contact plane"),
        ExpectedTravel>0 && FMath::Abs(Travel-ExpectedTravel)<=3 && FurthestTravel<=ExpectedTravel+3);
    TestTrue(TEXT("The re-entering worker keeps world ground support"),Move->IsMovingOnGround()
        && Hero->GetMovementBase()!=Food->Body && Hero->GetMovementBase()!=Food->WalkingBlocker);
    TestEqual(TEXT("Re-entry after separation preserves health"),Hero->Status->State.Health,Health);
    TestEqual(TEXT("Re-entry after separation does not knock the worker down"),Hero->ToothPhysics->KnockdownCount,Knockdowns);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodWalkingBlockedSeparation,
    "MessControl.Networking.MovementContacts.FoodSeparationRespectsWallsAndOtherWorker",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodWalkingBlockedSeparation::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;auto* Hero=T.Player(FVector(-65,0,10085));auto* Other=T.Player(FVector(99,0,10085));T.Step(.3f);
    // The narrow corridor permits at most3cm of lateral food travel. The rear
    // wall and second worker leave no route that clears the original overlap.
    AActor* Walls[]={T.Box(FVector(0,-58,10120),FVector(250,5,120)),
        T.Box(FVector(0,58,10120),FVector(250,5,120)),T.Box(FVector(-115,0,10120),FVector(5,53,120))};
    auto* Food=T.Food(FVector(0,0,10050),FVector(100));Food->Body->SetSimulatePhysics(false);
    auto* Capsule=Hero->GetCapsuleComponent();auto* OtherCapsule=Other->GetCapsuleComponent();
    if(!TestTrue(TEXT("The blocked fixture starts in a true overlap with only the first worker"),
        Food->Body->OverlapComponent(Capsule->GetComponentLocation(),Capsule->GetComponentQuat(),Capsule->GetCollisionShape(5.f)))
        || !TestFalse(TEXT("The second worker starts clear and must not be used as an escape destination"),
            Food->Body->OverlapComponent(OtherCapsule->GetComponentLocation(),OtherCapsule->GetComponentQuat(),OtherCapsule->GetCollisionShape(5.f))))return false;
    const FVector HeroStart=Hero->GetActorLocation(),OtherStart=Other->GetActorLocation(),FoodStart=Food->GetActorLocation();
    const float Health=Hero->Status->State.Health,OtherHealth=Other->Status->State.Health;
    const int32 Knockdowns=Hero->ToothPhysics->KnockdownCount,OtherKnockdowns=Other->ToothPhysics->KnockdownCount;
    bool CrossedWall=false,EnteredOther=false,EverArmed=false,GroundPreserved=true;
    double MaximumHeroMotion=0,MaximumOtherMotion=0,MaximumHeroSpeed=0,MaximumOtherSpeed=0,MaximumFoodStep=0;
    FVector PreviousFood=FoodStart;
    for(int32 Frame=0;Frame<180;++Frame) {
        T.Step(1.f/60);
        MaximumHeroMotion=FMath::Max(MaximumHeroMotion,FVector::Dist(HeroStart,Hero->GetActorLocation()));
        MaximumOtherMotion=FMath::Max(MaximumOtherMotion,FVector::Dist(OtherStart,Other->GetActorLocation()));
        MaximumHeroSpeed=FMath::Max(MaximumHeroSpeed,Hero->GetVelocity().Size());
        MaximumOtherSpeed=FMath::Max(MaximumOtherSpeed,Other->GetVelocity().Size());
        MaximumFoodStep=FMath::Max(MaximumFoodStep,FVector::Dist(PreviousFood,Food->GetActorLocation()));
        GroundPreserved&=Hero->GetCharacterMovement()->IsMovingOnGround() && Other->GetCharacterMovement()->IsMovingOnGround();
        EverArmed|=Food->HasWalkingObstacle();
        EnteredOther|=Food->Body->OverlapComponent(OtherCapsule->GetComponentLocation(),OtherCapsule->GetComponentQuat(),OtherCapsule->GetCollisionShape(5.f));
        for(auto* Wall:Walls) {
            const auto* Shape=CastChecked<UBoxComponent>(Wall->GetRootComponent());
            CrossedWall|=Food->Body->OverlapComponent(Shape->GetComponentLocation(),Shape->GetComponentQuat(),Shape->GetCollisionShape(-.05f));
        }
        PreviousFood=Food->GetActorLocation();
    }
    AddInfo(FString::Printf(TEXT("Blocked food separation: start=%s end=%s wall=%d otherWorker=%d armed=%d maxStep=%.3f heroMotion=%.3f otherMotion=%.3f"),
        *FoodStart.ToString(),*Food->GetActorLocation().ToString(),CrossedWall,EnteredOther,EverArmed,MaximumFoodStep,MaximumHeroMotion,MaximumOtherMotion));
    TestFalse(TEXT("A blocked separation never teleports its actual food hull through a world wall"),CrossedWall);
    TestFalse(TEXT("A blocked separation never moves food into the second worker"),EnteredOther);
    TestFalse(TEXT("Food with no clear escape never publishes a blocker through the trapped worker"),EverArmed);
    TestTrue(TEXT("Failed food separation leaves both workers stationary, unaccelerated and grounded"),
        MaximumHeroMotion<.1 && MaximumOtherMotion<.1 && MaximumHeroSpeed<.1 && MaximumOtherSpeed<.1 && GroundPreserved);
    TestTrue(TEXT("Blocked attempts preserve the bounded horizontal food-step policy"),MaximumFoodStep<=16.1
        && FMath::Abs(FoodStart.Z-Food->GetActorLocation().Z)<.01 && FVector::Dist2D(FoodStart,Food->GetActorLocation())<=400.1);
    TestEqual(TEXT("The original worker keeps health when food cannot separate"),Hero->Status->State.Health,Health);
    TestEqual(TEXT("The second worker keeps health when food cannot separate"),Other->Status->State.Health,OtherHealth);
    TestEqual(TEXT("Blocked separation never ejects the original worker into knockdown"),Hero->ToothPhysics->KnockdownCount,Knockdowns);
    TestEqual(TEXT("Blocked separation never ejects the second worker into knockdown"),Other->ToothPhysics->KnockdownCount,OtherKnockdowns);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodSeparationCapturedBroccoli,
    "MessControl.Networking.MovementContacts.FoodSeparationCapturedBroccoli",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodSeparationCapturedBroccoli::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;FVector FoodPoint;
    T.FoodMesh.Reset(MCFoodCapturedBroccoliTests::MakeMesh(FoodPoint));
    if(!TestNotNull(TEXT("The actual captured procedural Broccoli geometry loads without a box substitute"),T.FoodMesh.Get()))return false;
    auto* Hero=T.Player(FVector(0,0,10060));T.Step(.3f);
    auto* Food=T.Food(FoodPoint,FVector(100));Food->Body->SetSimulatePhysics(false);
    auto* Capsule=Hero->GetCapsuleComponent();
    const FVector HeroStart=Hero->GetActorLocation(),FoodStart=Food->GetActorLocation();
    const float Health=Hero->Status->State.Health;const int32 Knockdowns=Hero->ToothPhysics->KnockdownCount;
    if(!TestTrue(TEXT("The saved real Broccoli hull initially encloses the actual worker capsule"),
        Food->Body->OverlapComponent(Capsule->GetComponentLocation(),Capsule->GetComponentQuat(),Capsule->GetCollisionShape(5.f))))return false;
    double MaximumHeroMotion=0,MaximumHeroSpeed=0;bool GroundPreserved=true;
    for(int32 Frame=0;Frame<240;++Frame) {
        T.Step(1.f/60);
        MaximumHeroMotion=FMath::Max(MaximumHeroMotion,FVector::Dist(HeroStart,Hero->GetActorLocation()));
        MaximumHeroSpeed=FMath::Max(MaximumHeroSpeed,Hero->GetVelocity().Size());
        GroundPreserved&=Hero->GetCharacterMovement()->IsMovingOnGround();
    }
    const bool Overlap=Food->Body->OverlapComponent(Capsule->GetComponentLocation(),Capsule->GetComponentQuat(),Capsule->GetCollisionShape(5.f));
    AddInfo(FString::Printf(TEXT("Captured Broccoli separation: food=%s -> %s, capsuleOverlap=%d armed=%d heroMotion=%.4f heroSpeed=%.4f"),
        *FoodStart.ToString(),*Food->GetActorLocation().ToString(),Overlap,Food->HasWalkingObstacle(),MaximumHeroMotion,MaximumHeroSpeed));
    TestTrue(TEXT("The captured Broccoli itself moves clear and publishes a walking blocker"),
        !Overlap && Food->HasWalkingObstacle() && FVector::Dist2D(FoodStart,Food->GetActorLocation())>1);
    TestTrue(TEXT("The original Broccoli overlap is resolved without moving or accelerating the worker"),
        MaximumHeroMotion<.1 && MaximumHeroSpeed<.1 && GroundPreserved);
    TestEqual(TEXT("The worker keeps health during the captured Broccoli correction"),Hero->Status->State.Health,Health);
    TestEqual(TEXT("The captured Broccoli never knocks the worker down"),Hero->ToothPhysics->KnockdownCount,Knockdowns);
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
    auto* Narrow=T.Food(FVector(-600,0,10100),FVector(49,49,100));
    auto* Slab=T.Food(FVector(-600,300,10100),FVector(100,49,100));
    auto* Boundary=T.Food(FVector(-400,0,10100),FVector(50,50,45));
    auto* Scaled=T.Food(FVector(-200,0,10100),FVector(40));
    TestFalse(TEXT("A fragment below the real height threshold is small"),Short->IsSubstantialWalkingObstacle());
    TestFalse(TEXT("A fragment below both horizontal span thresholds is small"),Narrow->IsSubstantialWalkingObstacle());
    TestTrue(TEXT("A broad upright slice qualifies even when its thickness is below 50cm"),Slab->IsSubstantialWalkingObstacle());
    TestTrue(TEXT("The horizontal span and height qualify at the authored boundary"),Boundary->IsSubstantialWalkingObstacle());
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
    TestTrue(TEXT("The thin upright child qualifies from its own broad 300cm face"),Thin->IsSubstantialWalkingObstacle());
    TestTrue(TEXT("The substantial child qualifies independently"),Large->IsSubstantialWalkingObstacle());
    TestTrue(TEXT("Off-centre remaining bounds retain their measured centre"),Thin->GetWalkingCollisionBounds().GetCenter().X>Source->GetActorLocation().X+150);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodWalkingRotatedClearance,
    "MessControl.Networking.MovementContacts.FoodWalkingRotatedBoundsClearance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodWalkingRotatedClearance::RunTest(const FString&)
{
    using namespace MCMovementContactTests;
    FWorld T;auto* Hero=T.Player(FVector(0,-150,10085));T.Step(.3f);
    auto* Food=T.Food(FVector(0,0,10100),FVector(400,70,200));
    Food->Body->SetSimulatePhysics(false);
    Food->SetActorRotation(FRotator(0,45,0),ETeleportType::TeleportPhysics);
    auto* Capsule=Hero->GetCapsuleComponent();auto* Move=Hero->GetCharacterMovement();
    const float Radius=Capsule->GetScaledCapsuleRadius(),HalfHeight=Capsule->GetScaledCapsuleHalfHeight();
    const FBox Expanded=Food->GetWalkingCollisionBounds().ExpandBy(FVector(Radius+5,Radius+5,HalfHeight+5));
    if(!TestTrue(TEXT("The worker occupies a rotated slab's expanded world AABB"),Expanded.IsInsideOrOn(Hero->GetActorLocation()))
        || !TestFalse(TEXT("The prepared slab hull is nevertheless clear of the actual padded capsule"),
            Food->Body->OverlapComponent(Capsule->GetComponentLocation(),Capsule->GetComponentQuat(),Capsule->GetCollisionShape(5.f))))return false;
    T.Step(.7f);
    if(!TestTrue(TEXT("An empty AABB corner cannot prevent a settled slab from arming its walking proxy"),Food->HasWalkingObstacle()))return false;
    const FVector Start=Hero->GetActorLocation();
    const FVector Direction=Food->GetActorRotation().RotateVector(FVector::RightVector);
    FHitResult Contact;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCRotatedFoodWalkingContact),false,Hero);
    const bool Hit=T.World->SweepSingleByObjectType(Contact,Start,Start+Direction*200,FQuat::Identity,
        FCollisionObjectQueryParams(AMCFoodActor::WalkingObstacleChannel),Capsule->GetCollisionShape(),Query);
    if(!TestTrue(TEXT("The actual capsule sweep reaches the exact rotated walking proxy without an initial overlap"),
        Hit && !Contact.bStartPenetrating && Contact.GetActor()==Food && Contact.GetComponent()==Food->WalkingBlocker))return false;
    const double ExpectedTravel=FVector::DotProduct(Contact.Location-Start,Direction);
    const float Health=Hero->Status->State.Health;const int32 Knockdowns=Hero->ToothPhysics->KnockdownCount;
    double FurthestTravel=0;
    for(int32 Frame=0;Frame<72;++Frame) {
        Hero->AddMovementInput(Direction);T.Step(1.f/60);
        FurthestTravel=FMath::Max(FurthestTravel,FVector::DotProduct(Hero->GetActorLocation()-Start,Direction));
    }
    const double Travel=FVector::DotProduct(Hero->GetActorLocation()-Start,Direction);
    TestTrue(TEXT("Real walking reaches the queried slab face and never crosses its capsule contact plane"),
        ExpectedTravel>5 && FMath::Abs(Travel-ExpectedTravel)<=3 && FurthestTravel<=ExpectedTravel+3);
    TestTrue(TEXT("Walking into the rotated slab preserves ordinary world support"),Move->IsMovingOnGround()
        && Hero->GetMovementBase()!=Food->Body && Hero->GetMovementBase()!=Food->WalkingBlocker);
    TestEqual(TEXT("The rotated slab walking contact preserves health"),Hero->Status->State.Health,Health);
    TestEqual(TEXT("The rotated slab walking contact does not knock the worker down"),Hero->ToothPhysics->KnockdownCount,Knockdowns);
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
