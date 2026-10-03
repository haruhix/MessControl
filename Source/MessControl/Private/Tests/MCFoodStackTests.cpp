#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCFoodStackSettings.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FFoodStackWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    TArray<AMCFoodActor*> Foods;
    FFoodStackWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();
    }
    ~FFoodStackWorld()
    { World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false); }
    void Step(float Seconds,float Dt=1.f/60)
    { for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,Dt);} }
    AMCFoodActor* Food(FVector Position,EMCFoodKind Kind=EMCFoodKind::Food,FVector Scale=FVector(.36))
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        FMCFoodRow Row;Row.Kind=Kind;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};
        Row.Scale=Row.FragmentScale=Scale;Row.Mass=9;Row.Fragments=3;Row.SpoilSeconds=300; // Each fragment weighs 3 kg.
        const FTransform T(Position);
        auto* F=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(7);F->ConfigureItem(TEXT("StackFixture"),Row,Random,true);F->FinishSpawning(T);
        Foods.Add(F);return F;
    }
    bool Collect(int32 Count=3)
    {
        Hero->FoodCollection->Toggle();
        for(int32 I=0;I<Count;++I)
            if(!Hero->FoodCollection->Collect(Food(Hero->GetActorLocation()+FVector(110,(I-Count*.5f)*38,-35)))) return false;
        return true;
    }
    AActor* Wall(FVector Position,FVector Extent)
    {
        auto* A=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(A);
        A->SetRootComponent(Box);Box->SetBoxExtent(Extent);Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();A->SetActorLocation(Position);return A;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCarrierPlayerContact,"MessControl.Food.Stack.PlayerContactKnocksCarrierOnly",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCarrierPlayerContact::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/120}) for(bool Pickup:{false,true}) {
        FFoodStackWorld T;
        const FVector Origin=T.Hero->GetActorLocation();
        auto* Other=T.World->SpawnActor<AMCToothCharacter>(Origin+FVector(500,0,0),FRotator::ZeroRotator);
        if(!TestNotNull(TEXT("Second player spawns away from the pickup slot"),Other)) return false;
        Other->GetCharacterMovement()->DisableMovement();
        T.Step(.8f,Dt); // Neither player retains spawn hit immunity.
        if(!TestTrue(TEXT("Carrier collects one piece"),T.Collect(1))) return false;
        if(!Pickup) T.Step(.7f,Dt);
        // A real swept food contact during the hop, or a swept carrier capsule contact.
        Other->SetActorLocation(Origin+FVector(Pickup?100:70,0,0),false,nullptr,ETeleportType::TeleportPhysics);
        const float Health=Other->Status->State.Health;
        if(Pickup) T.Step(.32f,Dt);
        else {
            FHitResult Hit;T.Hero->SetActorLocation(Origin+FVector(25,0,0),true,&Hit);
            TestTrue(TEXT("Carrier capsule actually contacts the other player"),Hit.bBlockingHit && Hit.GetActor()==Other);
        }
        TestEqual(TEXT("Only the carrier is knocked down"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
        TestTrue(TEXT("Contact spills the whole load once"),T.Hero->FoodCollection->Pieces.IsEmpty() && T.Hero->FoodCollection->FallenPieces==1);
        T.Step(.3f,Dt);
        TestEqual(TEXT("The empty-handed player stays standing"),Other->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
        TestEqual(TEXT("Dropped food cannot damage the bystander"),Other->Status->State.Health,Health);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNearbyRecovery,"MessControl.Physics.Recovery.NearbyGroundedSpace",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNearbyRecovery::RunTest(const FString&)
{
    FFoodStackWorld T;const FVector Origin=T.Hero->GetActorLocation();
    const float Half=T.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    auto* Ground=T.Wall(Origin-FVector(0,0,Half+5),FVector(800,800,5));
    T.Step(.8f);
    T.Hero->ToothPhysics->ApplyHit(FVector(250,0,0),Origin);
    T.Step(.8f);
    const FVector Center=T.Hero->ToothPhysics->PhysicalLocation();
    // The ragdoll fits below low shelves, but none of the old five get-up probes do.
    TArray<AActor*> Shelves;
    for(FVector Offset:{FVector::ZeroVector,FVector(80,0,0),FVector(-80,0,0),FVector(0,80,0),FVector(0,-80,0)})
        Shelves.Add(T.Wall(Center+Offset+FVector(0,0,70),FVector(24,24,4)));
    TestTrue(TEXT("Living ragdoll finds nearby diagonal standing room"),T.Hero->ToothPhysics->TryRecover());
    T.Step(1.2f);
    TestTrue(TEXT("Recovery restores walking and actions"),T.Hero->ToothPhysics->CanAct() && T.Hero->GetCharacterMovement()->IsWalking());
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCTestRecovery),false,T.Hero);
    TestFalse(TEXT("Recovered capsule does not intersect shelves or floor"),T.World->OverlapBlockingTestByProfile(T.Hero->GetActorLocation(),FQuat::Identity,TEXT("Pawn"),FCollisionShape::MakeCapsule(T.Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()-.5f,Half-.5f),Q));
    Ground->Destroy();
    for(auto* Shelf:Shelves) Shelf->Destroy();
    T.Step(.8f);T.Hero->ToothPhysics->ApplyHit(FVector(250,0,0),T.Hero->GetActorLocation());
    TestFalse(TEXT("A player without supporting ground cannot stand in midair"),T.Hero->ToothPhysics->TryRecover());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRepeatedContactRecovery,"MessControl.Physics.Recovery.RepeatedContactCannotExtendStunForever",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRepeatedContactRecovery::RunTest(const FString&)
{
    FFoodStackWorld T;
    T.Wall(T.Hero->GetActorLocation()-FVector(0,0,T.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5),FVector(2000,2000,5));
    T.Step(.8f);const float Health=T.Hero->Status->State.Health;
    T.Hero->ToothPhysics->ApplyHit(FVector(250,0,0),T.Hero->GetActorLocation());
    for(int32 I=0;I<24;++I) {
        T.Hero->ToothPhysics->ApplyHit(FVector(5,0,0),T.Hero->ToothPhysics->PhysicalLocation());
        T.Step(.3f);
    }
    TestTrue(TEXT("A living player regains control despite repeated small contacts"),T.Hero->ToothPhysics->CanAct());
    TestEqual(TEXT("Contact recovery preserves health"),T.Hero->Status->State.Health,Health);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackReleaseSafety,"MessControl.Food.Stack.DropProtectsBystandersWithoutDisablingFoodHazards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackReleaseSafety::RunTest(const FString&)
{
    FFoodStackWorld T;const FVector Origin=T.Hero->GetActorLocation();
    auto* Other=T.World->SpawnActor<AMCToothCharacter>(Origin+FVector(500,0,0),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("Bystander spawns outside the pickup area"),Other)) return false;
    Other->GetCharacterMovement()->DisableMovement();T.Step(.8f);
    if(!T.Collect(1)) return false;
    T.Step(.12f);auto* Food=T.Foods[0];
    TestTrue(TEXT("Drop happens while the piece is mid-hop"),Food->IsStackPickupActive());
    T.Hero->FoodCollection->Stop();
    TestTrue(TEXT("Ordinary drop does not retain the hop's projectile velocity"),Food->Body->GetPhysicsLinearVelocity().Size()<50);
    const float Health=Other->Status->State.Health;
    const FVector Position=Other->GetActorLocation()-FVector(100,0,0);
    Food->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    Food->Body->SetEnableGravity(false);Food->Body->SetPhysicsLinearVelocity(FVector(450,0,0));
    T.Step(.25f);
    TestEqual(TEXT("Even an incoming dropped piece cannot injure a bystander during settling"),Other->Status->State.Health,Health);
    TestTrue(TEXT("Settling contact leaves the bystander in control"),Other->ToothPhysics->CanAct());
    T.Step(.8f);
    Food->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);Food->Body->SetPhysicsLinearVelocity(FVector(450,0,0));
    T.Step(.25f);
    TestTrue(TEXT("Real incoming food still damages players after the short drop protection expires"),Other->Status->State.Health<Health);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackStableSway,"MessControl.Food.Stack.StableSwayAtDifferentFrameRates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackStableSway::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60,1.f/120})
    {
        FFoodStackWorld T;if(!TestTrue(TEXT("A full six-piece stack is collectable"),T.Collect(6))) return false;
        T.Step(.7f,Dt);
        const FVector Origin=T.Hero->GetActorLocation();float MaxTilt=0;
        for(int32 I=0;I<FMath::CeilToInt(6/Dt);++I) {
            const float Time=I*Dt;
            T.Hero->SetActorLocationAndRotation(Origin+FVector(80*FMath::Sin(Time*1.6),140*FMath::Sin(Time*2.2),20*FMath::Sin(Time*3)),FRotator(0,35*FMath::Sin(Time),0),false,nullptr,ETeleportType::TeleportPhysics);
            T.Step(Dt,Dt);
            MaxTilt=FMath::Max(MaxTilt,float(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(T.Foods.Last()->GetActorUpVector().Z,-1.,1.)))));
        }
        TestEqual(TEXT("Walking, reversing and turning retain all six pieces"),T.Hero->FoodCollection->Pieces.Num(),6);
        TestEqual(TEXT("Movement alone spills no pieces"),T.Hero->FoodCollection->FallenPieces,0);
        TestTrue(TEXT("The held load visibly sways within its bounded tilt"),MaxTilt>.5 && MaxTilt<=12.1);
        for(auto* F:T.Foods) {
            TestTrue(TEXT("Held pieces keep their identity and disable free-body gravity"),F->StackCarrier==T.Hero && !F->IsDisposed() && !F->Body->IsSimulatingPhysics());
            TestTrue(TEXT("Pickup preserves the actual food scale"),F->Visual->GetRelativeScale3D().Equals(F->FoodData.FragmentScale,.001));
        }
        T.Hero->FoodCollection->Stop(true);
        TestTrue(TEXT("Throw clears the hand stack"),T.Hero->FoodCollection->Pieces.IsEmpty() && !T.Hero->FoodCollection->bCollecting);
        T.Step(Dt,Dt); // Chaos applies the queued throw impulse on its next step.
        for(auto* F:T.Foods) TestTrue(TEXT("Thrown food resumes physics with forward momentum"),!F->StackCarrier && F->Body->IsSimulatingPhysics() && FVector::DotProduct(F->Body->GetPhysicsLinearVelocity(),T.Hero->GetActorForwardVector())>200);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackHitRelease,"MessControl.Food.Stack.DamageAndFoodHitsRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackHitRelease::RunTest(const FString&)
{
    for(int32 Event=0;Event<3;++Event) {
        FFoodStackWorld T;if(!TestTrue(TEXT("Hit fixture collects three pieces"),T.Collect())) return false;
        T.Step(.8f); // Spawn/recovery impact immunity ends before the hit fixture.
        if(Event==0) T.Hero->Status->Damage(5,FVector::RightVector);
        if(Event==1) T.Hero->ToothPhysics->ApplyHit(FVector(120,0,40),T.Hero->GetActorLocation());
        if(Event==2) T.Foods[1]->HitFood(5,FVector::RightVector);
        TestTrue(TEXT("A received hit immediately clears collection"),T.Hero->FoodCollection->Pieces.IsEmpty() && !T.Hero->FoodCollection->bCollecting);
        TestEqual(TEXT("The whole load is spilled once"),T.Hero->FoodCollection->FallenPieces,3);
        for(auto* F:T.Foods) TestTrue(TEXT("Spilled pieces remain alive, independent and simulated"),!F->StackCarrier && !F->IsDisposed() && F->Body->IsSimulatingPhysics());
        TestFalse(TEXT("Recently spilled food cannot immediately return to the hand"),T.Hero->FoodCollection->CanCollect(T.Foods[0]));
        T.Step(.4f);TestTrue(TEXT("A dropped load cannot injure its own carrier during release"),T.Hero->ToothPhysics->CanAct());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackRealCollision,"MessControl.Food.Stack.WorldAndIncomingBodyCollisions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackRealCollision::RunTest(const FString&)
{
    {
        FFoodStackWorld T;if(!TestTrue(TEXT("Wall fixture collects three pieces"),T.Collect())) return false;
        T.Wall(T.Hero->GetActorLocation()+FVector(0,0,-64),FVector(800,800,6));
        T.Hero->GetCharacterMovement()->bRunPhysicsWithNoController=true;T.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        T.Step(.3f);TestEqual(TEXT("Normal floor contact keeps the stack"),T.Hero->FoodCollection->Pieces.Num(),3);
        T.Wall(T.Hero->GetActorLocation()+FVector(160,0,100),FVector(8,400,250));
        for(int32 I=0;I<90;++I) {T.Hero->AddMovementInput(FVector::ForwardVector,1);T.Step(1.f/60);}
        TestTrue(TEXT("A real swept wall collision releases the load"),T.Hero->FoodCollection->Pieces.IsEmpty() && !T.Hero->FoodCollection->bCollecting);
        TestEqual(TEXT("Wall collision releases all three pieces"),T.Hero->FoodCollection->FallenPieces,3);
    }
    {
        FFoodStackWorld T;if(!TestTrue(TEXT("Incoming body fixture collects three pieces"),T.Collect())) return false;
        T.Step(.7f);
        auto* Incoming=T.Food(T.Foods[1]->GetActorLocation()+FVector(250,0,0),EMCFoodKind::ForeignObject);
        Incoming->Body->SetEnableGravity(false);Incoming->Body->SetPhysicsLinearVelocity(FVector(-450,0,0));
        T.Step(1.f);
        TestTrue(TEXT("A real Chaos impact into a held piece releases the stack"),T.Hero->FoodCollection->Pieces.IsEmpty() && T.Hero->FoodCollection->FallenPieces==3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackPickupAnimation,"MessControl.Food.Stack.PickupArcAndInterruptions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackPickupAnimation::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60,1.f/120}) {
        FFoodStackWorld T;
        auto* F=T.Food(T.Hero->GetActorLocation()+FVector(125,-55,-35));
        const FVector Start=F->GetActorLocation(),Extent=F->Body->GetScaledBoxExtent();
        const FVector BaseScale=F->Visual->GetRelativeScale3D();
        T.Hero->FoodCollection->Toggle();
        if(!TestTrue(TEXT("Food starts its collection animation"),T.Hero->FoodCollection->Collect(F))) return false;
        TestTrue(TEXT("Pickup reserves the piece at its starting position"),F->GetActorLocation().Equals(Start,.01) && F->StackCarrier==T.Hero);
        TestFalse(TEXT("The same piece cannot be collected a second time during flight"),T.Hero->FoodCollection->Collect(F));
        bool Squash=false,Stretch=false,Arc=false;double MaxStep=0;FVector Previous=Start;
        const FVector HeroStart=T.Hero->GetActorLocation();
        for(int32 I=0;I<FMath::CeilToInt(.85f/Dt);++I) {
            T.Hero->SetActorLocation(HeroStart+FVector(0,I*Dt*45,0),false,nullptr,ETeleportType::TeleportPhysics);
            T.Step(Dt,Dt);
            const FVector Scale=F->Visual->GetRelativeScale3D()/BaseScale;
            Squash|=Scale.Z<.95;Stretch|=Scale.Z>1.1;
            TestTrue(TEXT("Pickup squash/stretch preserves visual volume"),FMath::IsNearlyEqual(Scale.X*Scale.Y*Scale.Z,1.,.01));
            const FVector Goal=T.Hero->FoodCollection->StackPose(F).GetLocation();
            Arc|=F->GetActorLocation().Z>FMath::Max(Start.Z,Goal.Z)+10;
            MaxStep=FMath::Max(MaxStep,FVector::Dist(Previous,F->GetActorLocation()));Previous=F->GetActorLocation();
        }
        TestTrue(TEXT("Anticipation, aerial stretch and a visible arc were sampled"),Squash && Stretch && Arc);
        TestTrue(TEXT("Every phase of the pickup is exactly twice as fast"),FMath::IsNearlyEqual(F->StackPickupDuration()*2,.075f+F->StackPickup.FlightSeconds+.18f,.001f));
        TestTrue(TEXT("Pickup settles into the moving hand without a teleport"),!F->IsStackPickupActive() && FVector::Dist(F->GetActorLocation(),T.Hero->FoodCollection->StackPose(F).GetLocation())<1 && MaxStep<70);
        TestTrue(TEXT("Authored mesh scale and collision bounds are restored"),F->Visual->GetRelativeScale3D().Equals(BaseScale,.001) && F->Body->GetScaledBoxExtent().Equals(Extent,.001));
        TestEqual(TEXT("A completed hop cannot spill itself"),T.Hero->FoodCollection->FallenPieces,0);
    }
    for(bool Damage:{false,true}) {
        FFoodStackWorld T;T.Step(.8f);
        if(!TestTrue(TEXT("An interruptible piece can be collected"),T.Collect(1))) return false;
        T.Step(.1f);auto* F=T.Foods[0];
        TestTrue(TEXT("The interrupted piece is still mid-hop"),F->IsStackPickupActive());
        if(Damage) T.Hero->Status->Damage(1,FVector::RightVector);else T.Hero->FoodCollection->Stop(true);
        TestTrue(TEXT("A hit or throw immediately cancels the hop and restores physics"),!F->IsStackPickupActive() && !F->StackCarrier && F->Body->IsSimulatingPhysics());
        T.Step(.4f);
        TestTrue(TEXT("Cancelled pickup never snaps back to the hand"),T.Hero->FoodCollection->Pieces.IsEmpty() && !F->StackCarrier && !F->IsStackPickupActive());
        // Released food legitimately stretches in free fall. Remove that input
        // before checking that no cancelled pickup deformation remains.
        F->Body->SetEnableGravity(false);F->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);F->UpdateReaction(1.f/60);
        TestTrue(TEXT("Cancellation leaves no pickup deformation"),F->Visual->GetRelativeScale3D().Equals(F->FoodData.FragmentScale,.001));
    }
    {
        FFoodStackWorld T;auto* F=T.Food(T.Hero->GetActorLocation()+FVector(125,0,-35));
        T.Hero->FoodCollection->Toggle();T.Hero->FoodCollection->Collect(F);
        T.Step(.05f);
        T.Wall(T.Hero->FoodCollection->HandPoint()+FVector(35,0,30),FVector(7,150,200));
        T.Step(.6f);
        TestTrue(TEXT("A blocked pickup releases at the obstacle without registering a strong impact"),!F->StackCarrier && F->Body->IsSimulatingPhysics() && T.Hero->FoodCollection->FallenPieces==0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackPileContacts,"MessControl.Food.Stack.PilePickupAndContactThreshold",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackPileContacts::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60,1.f/120}) {
        FFoodStackWorld T;const FVector P=T.Hero->GetActorLocation();
        T.Wall(P+FVector(0,0,-70),FVector(800,800,6));
        for(int32 I=0;I<6;++I) T.Food(P+FVector(100+(I%2)*36,((I/2)%2?1:-1)*18,-44+(I/4)*36));
        T.Step(.5f,Dt);
        for(auto* F:T.Foods) TestTrue(TEXT("Stacked loose ingredients do not hide one another from pickup"),T.Hero->FoodCollection->CanCollect(F));
        T.Hero->FoodCollection->Toggle();T.Step(1.9f,Dt);
        TestEqual(TEXT("A dense six-piece heap is collected automatically at the faster cadence"),T.Hero->FoodCollection->Pieces.Num(),6);
        TestEqual(TEXT("Resting heap contacts do not register an impact"),T.Hero->FoodCollection->FallenPieces,0);
        for(auto* F:T.Foods) TestTrue(TEXT("Every heap piece finishes the hop and stays held"),F->StackCarrier==T.Hero && !F->IsStackPickupActive());
    }
    {
        FFoodStackWorld T;auto* C=T.Hero->FoodCollection.Get();
        auto* A=T.Food(T.Hero->GetActorLocation()+FVector(110,-45,-35));
        auto* B=T.Food(C->StackPose(A->Body->GetScaledBoxExtent().Z).GetLocation());B->Body->SetEnableGravity(false);
        C->Toggle();TestTrue(TEXT("Loose food overlapping the landing slot cannot reject pickup"),C->Collect(A));
        T.Step(.7f);
        TestTrue(TEXT("Both pieces reach the stack without pushing or spilling each other"),C->Pieces.Num()==2 && C->FallenPieces==0);
    }
    for(float IncomingMass:{3.f,20.f}) {
        FFoodStackWorld T;if(!TestTrue(TEXT("Contact fixture holds three pieces"),T.Collect())) return false;
        T.Step(.7f);
        auto* Incoming=T.Food(T.Foods[1]->GetActorLocation()+FVector(100,0,0),EMCFoodKind::ForeignObject);
        Incoming->Body->SetEnableGravity(false);Incoming->Body->SetMassOverrideInKg(NAME_None,IncomingMass,true);
        Incoming->Body->SetLinearDamping(0);
        Incoming->Body->SetPhysicsLinearVelocity(FVector(-50,0,0));
        T.Step(1.5f);
        if(IncomingMass==3) TestTrue(TEXT("A real light slow contact keeps the load held"),T.Hero->FoodCollection->Pieces.Num()==3 && T.Hero->FoodCollection->FallenPieces==0);
        else TestTrue(TEXT("The same speed with a heavy body exceeds the impulse threshold and spills"),T.Hero->FoodCollection->Pieces.IsEmpty() && T.Hero->FoodCollection->FallenPieces==3);
    }
    {
        FFoodStackWorld T;if(!T.Collect()) return false;T.Step(.7f);
        T.Wall(T.Hero->GetActorLocation()+FVector(0,0,-64),FVector(800,800,6));
        T.Wall(T.Hero->GetActorLocation()+FVector(160,0,100),FVector(8,400,250));
        auto* Move=CastChecked<UMCToothMovementComponent>(T.Hero->GetCharacterMovement());Move->bRunPhysicsWithNoController=true;Move->WalkSpeed=35;Move->SetMovementMode(MOVE_Walking);
        for(int32 I=0;I<180;++I) {T.Hero->AddMovementInput(FVector::ForwardVector,1);T.Step(1.f/60);}
        TestTrue(TEXT("Slow contact with a solid wall keeps the stack without passing through the wall"),T.Hero->FoodCollection->Pieces.Num()==3 && T.Hero->FoodCollection->FallenPieces==0 && T.Foods[0]->GetActorLocation().X<160);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackHorizontalAxes,"MessControl.Food.Stack.ForcedHorizontalAxesAndSpacing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackHorizontalAxes::RunTest(const FString&)
{
    FFoodStackWorld T;auto* C=T.Hero->FoodCollection.Get();C->Toggle();
    const FVector ActorScale(.8,1.2,1.1);
    const FVector Scales[]={FVector(.12,.54,.5),FVector(.52,.13,.46),FVector(.6,.5,.09)};
    TArray<FVector> BodyExtents,VisualScales;
    for(int32 I=0;I<3;++I) {
        auto* F=T.Food(T.Hero->GetActorLocation()+FVector(110,(I-1)*65,-35),EMCFoodKind::Food,Scales[I]);
        F->SetActorScale3D(ActorScale);
        BodyExtents.Add(F->Body->GetScaledBoxExtent());VisualScales.Add(F->Visual->GetRelativeScale3D());
        if(!TestTrue(TEXT("Slices imported on each of the three thin axes can be collected"),C->Collect(F))) return false;
    }
    T.Step(.8f);
    const FQuat Base=C->StackPose(0.f).GetRotation();
    for(int32 I=0;I<T.Foods.Num();++I) {
        auto* F=T.Foods[I];const FVector Axis=F->FoodData.Stack.VerticalAxis(F->ItemMesh,BodyExtents[I]);
        TestTrue(TEXT("The selected local flat-face normal is aligned with the stack up axis"),FVector::DotProduct(F->GetActorQuat().RotateVector(Axis),Base.RotateVector(FVector::UpVector))>.9999);
        TestTrue(TEXT("Flat poses preserve actor scale, item scale and authored collision dimensions"),F->GetActorScale3D().Equals(ActorScale,.001) && F->Visual->GetRelativeScale3D().Equals(VisualScales[I],.001) && F->Body->GetScaledBoxExtent().Equals(BodyExtents[I],.001));
        TestTrue(TEXT("Vertical spacing uses the rotated thin extent"),FMath::IsNearlyEqual(F->StackHalfHeight(),BodyExtents[I].GetMin(),.001));
        TestTrue(TEXT("The small horizontal offset stays inside its support footprint"),FVector(F->StackPickup.SlotOffset).Size2D()<=F->FoodData.Stack.HorizontalOffset+.001);
        if(I>0) {
            const auto* Below=T.Foods[I-1];
            const float Separation=FVector::DotProduct(F->GetActorLocation()-Below->GetActorLocation(),Base.RotateVector(FVector::UpVector));
            TestTrue(TEXT("Mixed-axis layers maintain the configured gap without clipping"),FMath::IsNearlyEqual(Separation,F->StackHalfHeight()+Below->StackHalfHeight()+Below->FoodData.Stack.SafeLayerGap(),.01));
        }
    }
    TestTrue(TEXT("Upper layers have deterministic small lateral offsets"),FVector(T.Foods[1]->StackPickup.SlotOffset).Size2D()>.1);
    T.Foods[1]->SetActorRotation(FRotator(37,15,22),ETeleportType::TeleportPhysics);
    C->TickComponent(1.f/60,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Every update enforces the flat pose after rotation drift"),T.Foods[1]->GetActorQuat().Equals(C->StackPose(T.Foods[1]).GetRotation(),.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackDeliveryHandoff,"MessControl.Food.Stack.AtomicDeliveryKeepsRemainingPieces",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackDeliveryHandoff::RunTest(const FString&)
{
    FFoodStackWorld T;if(!T.Collect(3)) return false;T.Step(.8f);
    auto* C=T.Hero->FoodCollection.Get();auto* Delivered=T.Foods[1];
    const float PreviousTopHeight=T.Foods[2]->StackPickup.SlotHeight;
    TestFalse(TEXT("An ordinary free piece cannot be detached as a delivery"),C->DetachForDelivery(T.Foods[0]));
    TestTrue(TEXT("BeginSwallow atomically accepts a selected carried stack piece"),Delivered->BeginSwallow());
    TestTrue(TEXT("Delivered food stays unsimulated with collision disabled"),!Delivered->StackCarrier && Delivered->Phase==EMCFoodPhase::Swallowing && !Delivered->Body->IsSimulatingPhysics() && Delivered->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Delivery removes only its selected layer and packs the remaining heights"),C->Pieces.Num()==2 && C->bCollecting && C->FallenPieces==0 && T.Foods[0]->StackCarrier==T.Hero && T.Foods[2]->StackCarrier==T.Hero && T.Foods[2]->StackPickup.SlotHeight<PreviousTopHeight);
    TestFalse(TEXT("Delivered food can neither be reacquired nor delivered twice"),C->CanCollect(Delivered) || Delivered->BeginSwallow());
    TestTrue(TEXT("Remaining food can join the same delivery"),T.Foods[0]->BeginSwallow() && T.Foods[2]->BeginSwallow());
    TestTrue(TEXT("The empty delivered stack stops collecting and never records a spill"),C->Pieces.IsEmpty() && !C->bCollecting && C->FallenPieces==0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackSavedMenuPose,"MessControl.Food.Stack.SavedMenuFlatPosesAndRowValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackSavedMenuPose::RunTest(const FString&)
{
    auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    if(!TestNotNull(TEXT("Stack settings use the existing saved breakfast menu"),Menu)) return false;
    int32 Checked=0;
    for(FName Name:Menu->GetRowNames()) if(const auto* Row=Menu->FindRow<FMCFoodRow>(Name,TEXT("Horizontal stack validation"))) {
        if(Row->Kind!=EMCFoodKind::Food) continue;
        for(const auto& Choice:Row->FragmentMeshes) if(!Choice.IsNull()) {
            auto* Mesh=Choice.LoadSynchronous();if(!TestNotNull(TEXT("Saved ordinary fragment mesh loads"),Mesh)) continue;
#if WITH_EDITOR
            FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
            const FVector Extent=Mesh->GetBounds().BoxExtent*Row->FragmentScale.GetAbs();
            for(int32 Slot=0;Slot<6;++Slot) {
                const FQuat Rotation=Row->Stack.RestRotation(Mesh,Extent,Slot);
                TestTrue(TEXT("Every saved ordinary fragment has a flat enforced stack orientation"),FVector::DotProduct(Rotation.RotateVector(Row->Stack.VerticalAxis(Mesh,Extent)),FVector::UpVector)>.99999);
                TestTrue(TEXT("Every saved fragment gets finite rotated spacing"),!FMCFoodStackSettings::RotatedExtent(Extent,Rotation).ContainsNaN() && FMCFoodStackSettings::RotatedExtent(Extent,Rotation).Z>0);
            }
            ++Checked;
        }
#if WITH_EDITOR
        FDataValidationContext RowContext;
        TestTrue(TEXT("Every ordinary menu row validates its stack tuning"),Row->IsDataValid(RowContext)==EDataValidationResult::Valid);
#endif
    }
    TestTrue(TEXT("The check covers actual saved menu fragments"),Checked>=10);
#if WITH_EDITOR
    FMCFoodRow Invalid;Invalid.Stack.HorizontalOffset=-1;
    FDataValidationContext InvalidContext;
    TestTrue(TEXT("The menu row rejects unsafe stack offsets before save"),Invalid.IsDataValid(InvalidContext)==EDataValidationResult::Invalid && InvalidContext.GetNumErrors()>0);
    FMCFoodStackSettings Overrides;FMCFoodStackPoseOverride Entry;
    Entry.Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));Entry.VerticalAxis=EMCFoodStackAxis::X;
    Overrides.PoseOverrides.Add(Entry);
    TestTrue(TEXT("A per-mesh table override takes precedence over automatic bounds"),Overrides.RestRotation(Entry.Mesh.Get(),FVector(20,10,2),0).RotateVector(FVector::ForwardVector).Equals(FVector::UpVector,.001));
    Overrides.PoseOverrides.Add(Entry);FDataValidationContext DuplicateContext;
    TestFalse(TEXT("The menu row rejects conflicting normals for the same mesh"),Overrides.Validate(DuplicateContext));
#endif
    return true;
}
#endif
