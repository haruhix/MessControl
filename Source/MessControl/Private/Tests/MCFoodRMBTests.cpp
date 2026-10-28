#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGameState.h"
#include "MCGripComponent.h"
#include "MCHubBasketball.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCFoodRMBTests
{
constexpr double GroundZ=10000.;
constexpr float Dt=1.f/120.f;
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    UStaticMesh* Cube=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;
        Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        if(Cube) FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        Box(FVector(0,0,GroundZ-20),FVector(1000,1000,20));
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,GroundZ+80),FRotator::ZeroRotator);
        auto* Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();
        Controller->Possess(Hero);Hero->GetCharacterMovement()->DisableMovement();Hero->Status->Initialize(100);
    }
    ~FWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AActor* Box(FVector Center,FVector HalfExtent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape);Shape->SetBoxExtent(HalfExtent);
        Shape->SetCollisionProfileName(TEXT("BlockAll"));Shape->RegisterComponent();Actor->SetActorLocation(Center);
        return Actor;
    }
    AMCFoodActor* Food(FVector Offset)
    {
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;Row.WholeMeshes={Cube};Row.FragmentMeshes={Cube};
        Row.Scale=Row.FragmentScale=FVector(.12f);Row.Mass=.6f;Row.Fragments=3;Row.Health=100;Row.SpoilSeconds=300;
        const FTransform Pose(Hero->GetActorLocation()+Offset);
        auto* Item=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(57);Item->ConfigureItem(TEXT("FoodRMBFixture"),Row,Random,true);Item->FinishSpawning(Pose);
        return Item;
    }
    void Step(double Seconds)
    {for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,Dt);}}
    bool Collect(AMCFoodActor* Food)
    {
        Hero->SetFoodCollectionInput(true);
        return Hero->FoodCollection->Collect(Food);
    }
    // Exercise the reflected server entry point without changing its access or
    // invoking a second copy of its implementation in the test.
    bool ServerBrace(bool Held,uint16 RequestId)
    {
        UFunction* Function=Hero->Grip->FindFunction(TEXT("ServerSetBraceHeld"));
        if(!Function) return false;
        auto* HeldProperty=FindFProperty<FBoolProperty>(Function,TEXT("Held"));
        auto* RequestProperty=FindFProperty<FUInt16Property>(Function,TEXT("RequestId"));
        if(!HeldProperty || !RequestProperty) return false;
        FStructOnScope Parameters(Function);
        HeldProperty->SetPropertyValue_InContainer(Parameters.GetStructMemory(),Held);
        RequestProperty->SetPropertyValue_InContainer(Parameters.GetStructMemory(),RequestId);
        Hero->Grip->ProcessEvent(Function,Parameters.GetStructMemory());return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodRMBPreservesCarry,
    "MessControl.Food.RMB.PreservesActualCarriedStack",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodRMBPreservesCarry::RunTest(const FString&)
{
    using namespace MCFoodRMBTests;
    FWorld T;auto* Collection=T.Hero->FoodCollection.Get();auto* Grip=T.Hero->Grip.Get();
    auto* First=T.Food(FVector(90,-25,0));
    if(!TestTrue(TEXT("E starts the real collector and accepts the first fragment"),T.Collect(First))) return false;
    auto* Second=T.Food(FVector(90,40,0));
    if(!TestTrue(TEXT("The real collector accepts a second fragment"),Collection->Collect(Second))) return false;
    T.Step(1.);
    if(!TestTrue(TEXT("Both fragments have finished pickup into the active load"),Collection->Pieces.Num()==2
        && First->StackCarrier==T.Hero && Second->StackCarrier==T.Hero
        && !First->IsStackPickupActive() && !Second->IsStackPickupActive())) return false;
    const auto Pieces=Collection->Pieces;
    const FTransform HeroPose=T.Hero->GetActorTransform(),FirstPose=First->GetActorTransform(),SecondPose=Second->GetActorTransform();
    for(int32 I=0;I<3;++I) {
        T.Hero->SetBraceInputHeld(true);T.Hero->SetBraceInputHeld(true);T.Hero->SetBraceInputHeld(false);
        TestTrue(TEXT("Press, held repeat and release retain the same stack identities"),Collection->Pieces==Pieces);
        TestTrue(TEXT("RMB retains the collection pose's active state and both carriers"),Collection->bCollecting
            && First->StackCarrier==T.Hero && Second->StackCarrier==T.Hero);
        TestFalse(TEXT("RMB never converts the player's own food into a brace target"),Grip->IsBracing() || Grip->IsBraceInputHeld());
        TestTrue(TEXT("The input transition does not teleport the body or either carried fragment"),HeroPose.Equals(T.Hero->GetActorTransform(),.001f)
            && FirstPose.Equals(First->GetActorTransform(),.001f) && SecondPose.Equals(Second->GetActorTransform(),.001f));
    }
    T.Step(.25);
    TestTrue(TEXT("The real collector tick keeps the same load after the input sequence"),Collection->Pieces==Pieces
        && Collection->bCollecting && First->StackCarrier==T.Hero && Second->StackCarrier==T.Hero);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodRMBServerRace,
    "MessControl.Food.RMB.ServerRejectsCarryAndLatePickup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodRMBServerRace::RunTest(const FString&)
{
    using namespace MCFoodRMBTests;
    FWorld T;auto* Collection=T.Hero->FoodCollection.Get();auto* Grip=T.Hero->Grip.Get();
    auto* Food=T.Food(FVector(90,0,0));
    if(!TestTrue(TEXT("The authoritative fixture has a real carried fragment"),T.Collect(Food))) return false;
    T.Step(1.);const FTransform FoodPose=Food->GetActorTransform();
    if(!TestTrue(TEXT("The actual reflected server RPC accepts the test request"),T.ServerBrace(true,73))) return false;
    TestEqual(TEXT("A rejected delayed request acknowledges its original serial"),Grip->Brace.RequestId,uint16(73));
    TestFalse(TEXT("The server rejects held intent and a contact while food occupies the hands"),Grip->Brace.bHeld || Grip->IsBracing());
    TestTrue(TEXT("Rejecting the request preserves food ownership, pose and collector state"),Collection->bCollecting
        && Collection->Pieces.Num()==1 && Collection->Contains(Food) && Food->StackCarrier==T.Hero
        && FoodPose.Equals(Food->GetActorTransform(),.001f));
    Grip->SetBraceHeld(true);
    TestFalse(TEXT("The component entry point also refuses carry without starting a predicted intent"),Grip->IsBraceInputHeld());
    TestTrue(TEXT("The component path leaves the actual load untouched"),Collection->Contains(Food) && Food->StackCarrier==T.Hero && Collection->bCollecting);

    T.Hero->SetFoodCollectionInput(false);Food->SetActorLocation(T.Hero->GetActorLocation()+FVector(-500,0,0));
    auto* Wall=T.Box(T.Hero->GetActorLocation()+FVector(145,0,30),FVector(20,90,110));
    T.Hero->SetBraceInputHeld(true);
    if(!TestTrue(TEXT("Empty hands acquire a real wall before the late pickup race"),Grip->IsBracing() && Grip->BraceTarget()==Wall)) return false;
    auto* LateFood=T.Food(FVector(80,-35,0));
    // Model pickup arriving after brace intent. The normal E entry point also
    // releases the brace, so go through the real component pickup in this race.
    Collection->Toggle();
    if(!TestTrue(TEXT("A late real pickup can complete after the brace request"),Collection->Collect(LateFood))) return false;
    T.Step(Dt);
    TestFalse(TEXT("The actual grip tick discards the stale wall intent when the hands fill"),Grip->IsBracing() || Grip->Brace.bHeld);
    TestTrue(TEXT("Invalidating a stale brace does not discard the newly acquired food"),Collection->Contains(LateFood)
        && LateFood->StackCarrier==T.Hero && Collection->bCollecting);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodRMBExplicitRelease,
    "MessControl.Food.RMB.ExplicitReleaseRestoresBraceAndBallAim",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodRMBExplicitRelease::RunTest(const FString&)
{
    using namespace MCFoodRMBTests;
    FWorld T;auto* Collection=T.Hero->FoodCollection.Get();auto* Grip=T.Hero->Grip.Get();
    auto* Food=T.Food(FVector(90,-25,0));
    if(!TestTrue(TEXT("The release fixture starts with an actual load"),T.Collect(Food))) return false;
    T.Step(1.);T.Hero->SetFoodCollectionInput(false);
    TestTrue(TEXT("Explicit E releases ownership and exits the carry pose"),Collection->Pieces.IsEmpty()
        && !Collection->bCollecting && !Food->StackCarrier && !T.Hero->HasFoodToThrow());
    Food->SetActorLocation(T.Hero->GetActorLocation()+FVector(-500,0,0));
    auto* Wall=T.Box(T.Hero->GetActorLocation()+FVector(145,0,30),FVector(20,90,110));
    T.Hero->SetBraceInputHeld(true);
    TestTrue(TEXT("After E release normal RMB can acquire an external wall"),Grip->IsBracing() && Grip->BraceTarget()==Wall);
    T.Hero->SetBraceInputHeld(false);

    auto* Thrown=T.Food(FVector(80,-35,0));
    if(!TestTrue(TEXT("A new food fragment can be collected beside the brace wall"),T.Collect(Thrown))) return false;
    T.Step(1.);T.Hero->ThrowItem();
    TestTrue(TEXT("Explicit Q still throws the final fragment and frees the hands"),Collection->Pieces.IsEmpty()
        && !Collection->bCollecting && !Thrown->StackCarrier && !T.Hero->HasFoodToThrow());
    Thrown->SetActorLocation(T.Hero->GetActorLocation()+FVector(-500,200,0));
    T.Hero->SetBraceInputHeld(true);
    TestTrue(TEXT("After Q release normal RMB can acquire an external wall"),Grip->IsBracing() && Grip->BraceTarget()==Wall);
    T.Hero->SetBraceInputHeld(false);Wall->Destroy();

    // Use the side lane so pickup LOS is independent of deferred wall teardown.
    auto* Ball=T.World->SpawnActor<AMCHubBasketball>(T.Hero->GetActorLocation()+FVector(0,180,0),FRotator::ZeroRotator);
    if(!TestTrue(TEXT("An empty-handed local player can pick up the actual basketball"),Ball && Ball->TryPickup(T.Hero))) return false;
    T.Hero->SetBraceInputHeld(true);
    TestTrue(TEXT("Basketball RMB keeps its aim special case and does not start bracing"),Ball->IsAiming() && !Grip->IsBracing());
    T.Hero->SetBraceInputHeld(false);
    TestFalse(TEXT("Releasing RMB ends basketball aim"),Ball->IsAiming());
    return true;
}
#endif
