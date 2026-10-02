#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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
    AMCFoodActor* Food(FVector Position,EMCFoodKind Kind=EMCFoodKind::Food)
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        FMCFoodRow Row;Row.Kind=Kind;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};
        Row.Scale=Row.FragmentScale=FVector(.36);Row.Mass=3;Row.SpoilSeconds=300;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStackStableSway,"MessControl.Food.Stack.StableSwayAtDifferentFrameRates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStackStableSway::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60,1.f/120})
    {
        FFoodStackWorld T;if(!TestTrue(TEXT("A full six-piece stack is collectable"),T.Collect(6))) return false;
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
        T.Step(.2f);
        auto* Incoming=T.Food(T.Foods[1]->GetActorLocation()+FVector(250,0,0),EMCFoodKind::ForeignObject);
        Incoming->Body->SetEnableGravity(false);Incoming->Body->SetPhysicsLinearVelocity(FVector(-450,0,0));
        T.Step(1.f);
        TestTrue(TEXT("A real Chaos impact into a held piece releases the stack"),T.Hero->FoodCollection->Pieces.IsEmpty() && T.Hero->FoodCollection->FallenPieces==3);
    }
    return true;
}
#endif
