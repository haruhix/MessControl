#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "MCGripComponent.h"
#include "MCGameState.h"
#include "MCFoodActor.h"
#include "MCCoffeeFlood.h"
#include "MCDayPlan.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
    // No procedural arena or shift director: these contacts and floor are the
    // whole fixture, so unrelated spawn geometry cannot decide the nearest hand.
    struct FBraceWorld
    {
        UWorld* World;
        FBraceWorld()
        {
            World=UWorld::CreateWorld(EWorldType::Game,false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
            auto* State=World->SpawnActor<AMCGameState>(); State->Phase=EMCShiftPhase::Working;
            World->SetGameState(State);
            Box(FVector(0,0,-10),FVector(2500,2500,10));
        }
        ~FBraceWorld()
        { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
        AActor* Box(FVector Center,FVector Extent)
        {
            auto* Actor=World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Shape); Shape->SetBoxExtent(Extent);
            Shape->SetCollisionProfileName(TEXT("BlockAll")); Shape->RegisterComponent();
            Actor->SetActorLocation(Center); return Actor;
        }
        AMCToothCharacter* Worker(FVector XY)
        {
            XY.Z=61;
            auto* Hero=World->SpawnActor<AMCToothCharacter>(XY,FRotator::ZeroRotator);
            auto* Move=Hero->GetCharacterMovement(); Move->bRunPhysicsWithNoController=true;
            Move->SetMovementMode(MOVE_Walking); return Hero;
        }
        void Step(float Seconds)
        { for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); } }
        AMCFoodActor* LooseFood(FVector Location)
        {
            auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
            if(Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
            const FTransform Transform(Location);
            auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
            FMCFoodRow Row; Row.Kind=EMCFoodKind::Food; Row.Mass=8; Row.HalfExtent=FVector(20);
            Row.SpoilSeconds=300; Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(Mesh));
            FRandomStream Random(1); Food->ConfigureItem(TEXT("BraceFixture"),Row,Random);
            Food->FinishSpawning(Transform); Food->Phase=EMCFoodPhase::Free;
            Food->Body->SetSimulatePhysics(true); Food->Body->SetEnableGravity(false);
            Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector); return Food;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBraceChainTest,"MessControl.Grip.Brace.ContactChain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBraceChainTest::RunTest(const FString&)
{
    FBraceWorld W;
    auto* FloorOnly=W.Worker(FVector(0,700,0));
    auto* ComplexFloor=W.World->SpawnActor<AActor>();
    auto* FloorMesh=NewObject<UProceduralMeshComponent>(ComplexFloor); ComplexFloor->SetRootComponent(FloorMesh);
    FloorMesh->bUseComplexAsSimpleCollision=true; FloorMesh->bUseAsyncCooking=false;
    FloorMesh->SetCollisionProfileName(TEXT("BlockAll")); FloorMesh->SetCollisionObjectType(ECC_WorldStatic); FloorMesh->RegisterComponent();
    // The distant 45cm patch enlarges the same component's bounds. Its AABB
    // projects a fictitious point just below the character centre, while the
    // actual floor beneath this worker remains flat at Z=0, as on the tongue.
    const TArray<FVector> FloorVertices={FVector(-400,-400,0),FVector(400,-400,0),FVector(400,400,0),FVector(-400,400,0),
        FVector(600,-80,45),FVector(700,-80,45),FVector(700,80,45),FVector(600,80,45)};
    const TArray<int32> FloorTriangles={0,1,2,0,2,3,4,5,6,4,6,7};
    TArray<FVector> FloorNormals; FloorNormals.Init(FVector::UpVector,FloorVertices.Num());
    FloorMesh->CreateMeshSection(0,FloorVertices,FloorTriangles,FloorNormals,TArray<FVector2D>(),TArray<FColor>(),TArray<FProcMeshTangent>(),true);
    ComplexFloor->SetActorLocation(FVector(0,700,0));
    FHitResult ActualFloor;
    TestTrue(TEXT("The complex triangle floor really supports the worker at Z=0"),FloorMesh->LineTraceComponent(ActualFloor,FVector(0,700,200),FVector(0,700,-20),FCollisionQueryParams(SCENE_QUERY_STAT(MCBraceComplexFloor),true)) && FMath::IsNearlyZero(float(ActualFloor.ImpactPoint.Z),.1f));
    TestTrue(TEXT("The distant raised surface puts the fictitious bounds point above the floor rejection height"),FloorMesh->Bounds.GetBox().GetClosestPointTo(FloorOnly->GetActorLocation()+FVector(0,0,20)).Z>FloorOnly->GetActorLocation().Z-20);
    FloorOnly->SetBraceInputHeld(true);
    TestFalse(TEXT("Standing on a complex tongue-like floor does not become a hand anchor"),FloorOnly->Grip->IsBracing());
    FloorOnly->SetBraceInputHeld(false);

    auto* Wall=W.Box(FVector(270,0,110),FVector(30,110,110));
    auto* B=W.Worker(FVector(130,0,0)); W.Step(.05f);
    auto* MoveB=CastChecked<UMCToothMovementComponent>(B->GetCharacterMovement());
    const float SpeedBefore=MoveB->GetMaxSpeed(),AccelerationBefore=MoveB->GetMaxAcceleration();
    B->SetBraceInputHeld(true);
    if(!TestTrue(TEXT("RMB chooses a nearby physical wall"),B->Grip->IsBracing() && B->Grip->BraceTarget()==Wall)) return false;
    TestTrue(TEXT("A fixed hand contact anchors to the world"),B->Grip->IsWorldAnchored());
    const float BMass=B->Grip->TotalChainMass();
    auto* A=W.Worker(FVector(0,0,0)); A->SetBraceInputHeld(true);
    if(!TestTrue(TEXT("RMB can hold another conscious player"),A->Grip->IsBracing() && A->Grip->BraceTarget()==B)) return false;
    const float AMass=A->Grip->TotalChainMass();
    W.Step(.05f);
    TestTrue(TEXT("A player chain reaches its terminal wall"),A->Grip->IsWorldAnchored());
    TestTrue(TEXT("The supported player carries the incoming subtree mass"),FMath::IsNearlyEqual(B->Grip->TotalChainMass(),BMass+AMass,.1f));
    TestTrue(TEXT("Incoming mass slows both walking and acceleration"),MoveB->GetMaxSpeed()<SpeedBefore && MoveB->GetMaxAcceleration()<AccelerationBefore);
    TestTrue(TEXT("A reverse edge would create a brace cycle"),B->Grip->WouldCreateBraceCycle(A));
    B->SetBraceInputHeld(false);
    TestFalse(TEXT("Releasing the terminal contact releases world anchoring throughout the chain"),A->Grip->IsWorldAnchored());
    Wall->Destroy(); B->SetActorRotation(FRotator(0,180,0)); B->SetBraceInputHeld(true);
    TestFalse(TEXT("The server refuses the cyclic player contact"),B->Grip->IsBracing());
    B->SetBraceInputHeld(false);
    A->CancelGameplayInput(); W.Step(.05f);
    TestFalse(TEXT("Gameplay cancellation clears held intent and the contact"),A->Grip->IsBracing() || A->Grip->Brace.bHeld);
    TestTrue(TEXT("Release removes the incoming mass and restores walking speed"),B->Grip->IncomingChainMass()<.1f && FMath::IsNearlyEqual(MoveB->GetMaxSpeed(),SpeedBefore,.1f));

    auto* Loaded=W.Worker(FVector(130,1100,0));
    auto* Passenger=W.Worker(FVector(0,1100,0));
    auto* Solo=W.Worker(FVector(130,1500,0)); W.Step(.05f);
    Passenger->SetBraceInputHeld(true);
    if(!TestTrue(TEXT("The drag fixture holds its sprinting teammate"),Passenger->Grip->BraceTarget()==Loaded)) return false;
    Loaded->SetSprintInputHeld(true); Solo->SetSprintInputHeld(true);
    const FVector LoadedStart=Loaded->GetActorLocation(),SoloStart=Solo->GetActorLocation(),PassengerStart=Passenger->GetActorLocation();
    for(int32 I=0;I<60;++I) {
        Loaded->AddMovementInput(FVector::ForwardVector,1,true);
        Solo->AddMovementInput(FVector::ForwardVector,1,true); W.Step(1.f/60);
    }
    const float LoadedTravel=float(Loaded->GetActorLocation().X-LoadedStart.X);
    const float SoloTravel=float(Solo->GetActorLocation().X-SoloStart.X);
    const float PassengerTravel=float(Passenger->GetActorLocation().X-PassengerStart.X);
    UE_LOG(LogTemp,Display,TEXT("MC_BRACE_SPRINT_DRAG loaded=%.1f solo=%.1f passenger=%.1f incoming=%.1f"),LoadedTravel,SoloTravel,PassengerTravel,Loaded->Grip->IncomingChainMass());
    TestTrue(TEXT("A held teammate can still sprint but travels less than a solo runner"),LoadedTravel>100 && SoloTravel-LoadedTravel>80);
    TestTrue(TEXT("The running teammate physically pulls the idle holder along"),PassengerTravel>30 && Passenger->Grip->IsBracing());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBraceRiverTest,"MessControl.Grip.Brace.RiverAndReplay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBraceRiverTest::RunTest(const FString&)
{
    FBraceWorld W;
    W.Box(FVector(270,0,110),FVector(30,110,110));
    auto* B=W.Worker(FVector(130,0,0)); B->SetBraceInputHeld(true);
    auto* A=W.Worker(FVector(0,0,0)); A->SetBraceInputHeld(true);
    auto* Free=W.Worker(FVector(0,-600,0)); W.Step(.05f);
    if(!TestTrue(TEXT("The fixture starts with a wall anchored two-player chain"),A->Grip->IsWorldAnchored() && B->Grip->IsWorldAnchored())) return false;
    auto* Plan=NewObject<UMCDayPlan>(); Plan->ArenaCenter=FVector::ZeroVector;
    Plan->ArenaHalfSize=FVector(1000,1000,200); Plan->CoffeeProfile.Reset(); Plan->FloodHeight=155;
    auto* Flood=W.World->SpawnActor<AMCCoffeeFlood>(); Flood->Start(Plan);
    // Keep a bulk section over the test track; only character and physics time advances.
    auto WetStep=[&](float Seconds) {
        for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) {
            Flood->StartedAt=W.World->GetTimeSeconds()-2.2; W.Step(1.f/60);
        }
    };
    Flood->StartedAt=W.World->GetTimeSeconds()-2.2;
    const FVector Sole=Free->GetActorLocation()-FVector(0,0,Free->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-10);
    if(!TestTrue(TEXT("The unanchored worker stands in the real moving river band"),Flood->Contains(Sole))) return false;
    const FVector ChainStart=A->GetActorLocation(),FreeStart=Free->GetActorLocation(); WetStep(.3f);
    TestTrue(TEXT("Bulk current moves an idle unanchored character more than 200cm"),FVector::Dist2D(FreeStart,Free->GetActorLocation())>200);
    TestTrue(TEXT("A chain terminating at a wall resists the same current"),FVector::Dist2D(ChainStart,A->GetActorLocation())<15);
    TestTrue(TEXT("The tsunami keeps conscious workers walking"),Free->GetCharacterMovement()->IsMovingOnGround() && A->GetCharacterMovement()->IsMovingOnGround());

    // Catch removal of an already running additive river source, rather than only
    // pre-wave acquisition. The side wall leaves downstream motion unobstructed.
    const FVector P=Free->GetActorLocation();
    auto* SideWall=W.Box(P+FVector(0,110,50),FVector(250,20,110));
    Free->SetActorRotation(FRotator(0,90,0)); Free->SetBraceInputHeld(true);
    if(!TestTrue(TEXT("A drifting worker can acquire the side wall"),Free->Grip->BraceTarget()==SideWall && Free->Grip->IsWorldAnchored())) return false;
    const FVector HoldStart=Free->GetActorLocation(); WetStep(.35f);
    TestTrue(TEXT("Grabbing during drift removes the previous river velocity"),FVector::Dist2D(HoldStart,Free->GetActorLocation())<20);
    auto* Move=CastChecked<UMCToothMovementComponent>(Free->GetCharacterMovement());
    TestTrue(TEXT("The anchored movement sample has no river force"),Move->CaptureRiverForMove().IsNearlyZero());
    const FMCBraceMovementState Saved=Move->CaptureBraceForMove();
    Free->SetBraceInputHeld(false); Move->RestoreBraceForMove(Saved);
    TestTrue(TEXT("A saved move keeps its contact geometry after live release"),!Free->Grip->IsBracing() && Saved.bTether && Saved.bWorldAnchored && Saved.RestLength>0);
    const FVector Out=(HoldStart-Saved.Anchor).GetSafeNormal2D();
    const FVector Tangent=FVector::CrossProduct(FVector::UpVector,Out);
    const FVector Input=Out*500+Tangent*120+FVector(0,0,33);
    const FVector Clamped=Saved.ConstrainVelocity(Input,Saved.Anchor+Out*(Saved.RestLength+5),.016f);
    TestTrue(TEXT("The saved tether removes only outward separation"),FVector::DotProduct(Clamped,Out)<1 && FMath::IsNearlyEqual(FVector::DotProduct(Clamped,Tangent),120.f,.1f) && FMath::IsNearlyEqual(float(Clamped.Z),33.f,.1f));
    Move->RestoreBraceForMove(FMCBraceMovementState()); Flood->Stop();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBraceFoodTest,"MessControl.Grip.Brace.DynamicFoodLoad",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBraceFoodTest::RunTest(const FString&)
{
    FBraceWorld W;
    auto* Food=W.LooseFood(FVector(155,0,120)); auto* A=W.Worker(FVector(0,0,0));
    TestFalse(TEXT("Ordinary loose food does not use the old object grip"),Food->UsesLegacyGrip());
    A->SetBraceInputHeld(true);
    if(!TestTrue(TEXT("RMB holds the actual simulated body of ordinary food"),A->Grip->BraceTarget()==Food && A->Grip->IsBracing())) return false;
    TestFalse(TEXT("A loose rigid body is not a fixed world anchor"),A->Grip->IsWorldAnchored());
    const float AMass=A->Grip->TotalChainMass(),OneLoad=UMCGripComponent::AttachedMassFor(Food);
    auto* B=W.Worker(FVector(-130,0,0)); B->SetBraceInputHeld(true);
    if(!TestTrue(TEXT("A second player can extend the food-bearing chain"),B->Grip->BraceTarget()==A)) return false;
    const float BMass=B->Grip->TotalChainMass();
    TestTrue(TEXT("Food receives the mass of all attached players exactly once"),FMath::IsNearlyEqual(UMCGripComponent::AttachedMassFor(Food),OneLoad+BMass,.1f) && FMath::IsNearlyEqual(A->Grip->TotalChainMass(),AMass+BMass,.1f));
    W.Step(.1f);
    TestTrue(TEXT("The hand load pulls the body downward even with body gravity disabled"),Food->Body->GetPhysicsLinearVelocity().Z<-5);
    A->SetBraceInputHeld(false); B->SetBraceInputHeld(false);
    TestTrue(TEXT("Releasing contacts removes the food's attached player mass"),UMCGripComponent::AttachedMassFor(Food)<.1f);
    return true;
}
#endif
