#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCPlaytestBotController.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCPlaytestBotTowerTestsPrivate
{
/** Actual E pickup cadence, decision timers, Q throws and throat intake.
 * The worker starts at a supported throwing stance. No navmesh or authored
 * mouth-geometry/pathfinding coverage is claimed by this stationary fixture. */
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCPlaytestBotController* Bot=nullptr;
    AMCThroat* Throat=nullptr;
    TArray<TWeakObjectPtr<AMCFoodActor>> Fragments;
    UStaticMesh* Cube=nullptr;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Floor=World->SpawnActor<AActor>();
        auto* Collision=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Collision);
        Collision->SetBoxExtent(FVector(3000,2000,20));Collision->SetCollisionProfileName(TEXT("BlockAll"));
        Collision->RegisterComponent();Floor->SetActorLocation(FVector(0,5000,-20));
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,5000,100),FRotator::ZeroRotator);
        Hero->SetActorLocation(FVector(0,5000,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4.f));
        Hero->GetCharacterMovement()->DisableMovement();
        const FTransform IntakePose(FVector(1000,5000,0));
        Throat=World->SpawnActorDeferred<AMCThroat>(AMCThroat::StaticClass(),IntakePose);
        Throat->AuthoredMouth->SetSkeletalMesh(nullptr);
        Throat->GateCenter=FVector(0,0,100);Throat->SuctionPullSpeed=0;
        Throat->FinishSpawning(IntakePose);
        Bot=World->SpawnActor<AMCPlaytestBotController>();
        Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41);Bot->Possess(Hero);
        Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        if(Cube) FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
    }
    ~FFixture()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void AddFood(int32 Count)
    {
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;Row.WholeMeshes={Cube};Row.FragmentMeshes={Cube};
        Row.Scale=Row.FragmentScale=FVector(.12f);Row.Collision.bAutoOptimize=false;Row.SpoilSeconds=300;
        FRandomStream Random(41);
        for(int32 I=0;I<Count;++I) {
            const FTransform Pose(FVector(70+(I%2)*25,5000+(I/2-1)*27,12));
            auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Food->ConfigureItem(TEXT("BotTowerFragment"),Row,Random,true);Food->FinishSpawning(Pose);
            Fragments.Add(Food);
        }
    }
    void Step() {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestFullTowerDelivery,
    "MessControl.PlaytestBots.Adaptation.FullTowerUsesRealPickupAndSequentialThrows",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestFullTowerDelivery::RunTest(const FString&)
{
    MCPlaytestBotTowerTestsPrivate::FFixture F;
    if(!TestNotNull(TEXT("Real fragment mesh is available"),F.Cube)) return false;
    F.AddFood(6);
    int32 MaxTower=0;bool FirstThrow=false;int32 TowerAtFirstThrow=0;bool SawFlight=false;
    for(int32 Frame=0;Frame<600;++Frame) {
        const int32 Before=F.Hero->FoodCollection->Pieces.Num();F.Step();
        MaxTower=FMath::Max(MaxTower,F.Hero->FoodCollection->Pieces.Num());
        for(const auto& Food:F.Fragments) if(Food.IsValid()) {
            if(!FirstThrow && Food->WasManuallyThrown()) {FirstThrow=true;TowerAtFirstThrow=Before;}
            SawFlight|=Food->WasManuallyThrown() && !Food->StackCarrier && !Food->IsDisposed();
        }
        if(F.Throat->FoodSwallowed==6) break;
    }
    TestEqual(TEXT("Ordinary pickup builds the available six-piece tower"),MaxTower,6);
    TestEqual(TEXT("The first Q waits for the full useful batch"),TowerAtFirstThrow,6);
    TestTrue(TEXT("Food leaves through real manual flight, without direct delivery"),SawFlight);
    TestEqual(TEXT("The actual intake receives all six separate throws"),F.Throat->FoodSwallowed,6);
    TestTrue(TEXT("The retained stack eventually empties"),F.Hero->FoodCollection->Pieces.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestPartialTowerDelivery,
    "MessControl.PlaytestBots.Adaptation.PartialLastTowerDoesNotWaitForMissingPieces",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestPartialTowerDelivery::RunTest(const FString&)
{
    MCPlaytestBotTowerTestsPrivate::FFixture F;
    if(!TestNotNull(TEXT("Real fragment mesh is available"),F.Cube)) return false;
    F.AddFood(2);
    int32 MaxTower=0;double FirstThrowAt=-1;
    for(int32 Frame=0;Frame<480;++Frame) {
        F.Step();MaxTower=FMath::Max(MaxTower,F.Hero->FoodCollection->Pieces.Num());
        for(const auto& Food:F.Fragments) if(Food.IsValid() && Food->WasManuallyThrown() && FirstThrowAt<0) FirstThrowAt=F.World->GetTimeSeconds();
        if(F.Throat->FoodSwallowed==2) break;
    }
    TestEqual(TEXT("The last two available pieces form one real partial tower"),MaxTower,2);
    TestTrue(TEXT("Missing pieces cannot stall the final batch"),FirstThrowAt>0 && FirstThrowAt<4.);
    TestEqual(TEXT("Normal intake completes both remaining fragments"),F.Throat->FoodSwallowed,2);
    return true;
}
#endif
