#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCSlipperyFood.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCHubBasketball.h"
#include "MCNutRainEvent.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MCSlipperyFoodTests
{
struct FWorldFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCToothCharacter* Other=nullptr;

    FWorldFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator,Params);
        Other=World->SpawnActor<AMCToothCharacter>(FVector(1500,0,10000),FRotator::ZeroRotator,Params);
        Hero->GetCharacterMovement()->DisableMovement();Other->GetCharacterMovement()->DisableMovement();
    }
    ~FWorldFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    AMCFoodActor* Food()
    {
        const FTransform Pose(Hero->GetActorLocation()+FVector(95,0,-25));
        auto* Result=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};Row.Scale=Row.FragmentScale=FVector(.24f);
        Row.Mass=3.f;Row.SpoilSeconds=300.f;FRandomStream Random(27);
        Result->ConfigureItem(TEXT("SlipFoodFixture"),Row,Random,true);Result->FinishSpawning(Pose);
        return Result;
    }
    void Stack(AMCFoodActor* Food)
    {
        Hero->FoodCollection->Pieces={Food};Food->SetStackCarrier(Hero);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFoodStackOwnershipTest,"MessControl.SlipperyFood.StackOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFoodStackOwnershipTest::RunTest(const FString&)
{
    using namespace MCSlipperyFoodTests;
    FWorldFixture T;auto* Collection=T.Hero->FoodCollection.Get();
    TestFalse(TEXT("Null tooth has no delivery load"),MCSlipperyFood::IsCarrying(nullptr));
    Collection->bCollecting=true;Collection->MaxPieces=10;
    TestFalse(TEXT("Empty collection mode and capacity are not food"),MCSlipperyFood::IsCarrying(T.Hero));
    auto* Food=T.Food();
    TestFalse(TEXT("Nearby loose food does not count"),MCSlipperyFood::IsCarrying(T.Hero));
    TestTrue(TEXT("Normal native collection accepts the fixture"),Collection->Collect(Food));
    TestTrue(TEXT("Actual normal pickup assigns its carrier"),Food->StackCarrier==T.Hero);
    TestTrue(TEXT("Native stack pickup counts even in its Free phase"),MCSlipperyFood::IsCarrying(T.Hero));
    TestFalse(TEXT("A different tooth is not carrying this load"),MCSlipperyFood::IsCarrying(T.Other));
    Collection->Stop(false);
    TestFalse(TEXT("Native release removes the carried food classification"),MCSlipperyFood::IsCarrying(T.Hero));
    Collection->Pieces.Add(Food);
    TestFalse(TEXT("A stale piece entry without its carrier is ignored"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->SetStackCarrier(T.Other);
    TestFalse(TEXT("An entry carried by another tooth is ignored"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->SetStackCarrier(T.Hero);Collection->Pieces.Reset();
    TestFalse(TEXT("A carrier pointer without collection membership is ignored"),MCSlipperyFood::IsCarrying(T.Hero));
    Collection->Pieces.Add(Food);Food->Destroy();
    TestFalse(TEXT("A destroyed actor in a stale collection entry is ignored"),MCSlipperyFood::IsCarrying(T.Hero));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFoodKindsTest,"MessControl.SlipperyFood.DeliveryKindsAndTerminalPhases",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFoodKindsTest::RunTest(const FString&)
{
    using namespace MCSlipperyFoodTests;
    FWorldFixture T;auto* Food=T.Food();T.Stack(Food);
    Food->Phase=EMCFoodPhase::Free;
    TestTrue(TEXT("Owned ordinary delivery piece counts"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->bSpoiled=true;
    TestTrue(TEXT("Spoiled delivery food is still a physical food load"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->bBrushTool=true;
    TestFalse(TEXT("Brush tools are not delivery food"),MCSlipperyFood::IsCarrying(T.Hero));Food->bBrushTool=false;
    for(auto Kind:{EMCFoodKind::ForeignObject,EMCFoodKind::Spicy}) {
        Food->FoodData.Kind=Kind;
        TestFalse(TEXT("Foreign/spicy legacy objects are outside the agreed delivery predicate"),MCSlipperyFood::IsCarrying(T.Hero));
    }
    Food->FoodData.Kind=EMCFoodKind::Food;
    for(auto Phase:{EMCFoodPhase::Disposed,EMCFoodPhase::Swallowing,EMCFoodPhase::Equipped,EMCFoodPhase::Absorbing,EMCFoodPhase::Stuck}) {
        Food->Phase=Phase;
        TestFalse(TEXT("Terminal/noncarried phases cannot authorize a stale load"),MCSlipperyFood::IsCarrying(T.Hero));
    }
    Food->Phase=EMCFoodPhase::Free;
    auto* Encounter=T.World->SpawnActor<AMCNutRainEvent>();Encounter->Settings.bBossEncounter=true;
    Food->SetOwner(Encounter);
    TestTrue(TEXT("Fixture is actual boss nut ammunition by production identity"),Food->IsNutEncounterFragment());
    TestFalse(TEXT("Boss nut ammunition in the food slot is excluded"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->SetOwner(nullptr);T.Hero->FoodCollection->Pieces.Reset();
    auto* Ball=T.World->SpawnActor<AMCHubBasketball>();Ball->Holder=T.Hero;
    TestFalse(TEXT("Basketball ownership is not a delivery load"),MCSlipperyFood::IsCarrying(T.Hero));
    T.Hero->Grip->GrabbedPlayer=T.Other;
    TestFalse(TEXT("Holding another player is not a delivery load"),MCSlipperyFood::IsCarrying(T.Hero));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFoodGripOwnershipTest,"MessControl.SlipperyFood.PrimaryAndSecondaryGripOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFoodGripOwnershipTest::RunTest(const FString&)
{
    using namespace MCSlipperyFoodTests;
    FWorldFixture T;auto* Food=T.Food();Food->Phase=EMCFoodPhase::Carried;
    T.Hero->HeldFood=Food;T.Hero->Grip->Frame.Food=Food;
    TestFalse(TEXT("Stale held/frame pointers without a holder do not count"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->Holders.Add(T.Hero);
    TestTrue(TEXT("Owned primary grip counts"),MCSlipperyFood::IsCarrying(T.Hero));
    T.Hero->HeldFood=nullptr;
    TestTrue(TEXT("Primary frame works without its compatibility mirror"),MCSlipperyFood::IsCarrying(T.Hero));
    T.Hero->Grip->Frame.Food=nullptr;T.Hero->HeldFood=Food;
    TestTrue(TEXT("Owned compatibility HeldFood counts"),MCSlipperyFood::IsCarrying(T.Hero));
    T.Hero->HeldFood=nullptr;T.Hero->Grip->Secondary.Food=Food;
    TestTrue(TEXT("Owned secondary grip counts on its own"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->Holders={T.Other};
    TestFalse(TEXT("A secondary pointer actually held by someone else is ignored"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->Holders={T.Hero};Food->Phase=EMCFoodPhase::Swallowing;
    TestFalse(TEXT("A swallowing item no longer counts even with stale grip ownership"),MCSlipperyFood::IsCarrying(T.Hero));
    Food->Phase=EMCFoodPhase::Carried;T.Hero->Grip->Secondary.Food=nullptr;
    TestFalse(TEXT("An unrelated food claiming a holder is not enough without a held slot"),MCSlipperyFood::IsCarrying(T.Hero));
    T.Hero->HeldFood=Food;T.Hero->Destroy();
    TestFalse(TEXT("A destroyed carrier is rejected"),MCSlipperyFood::IsCarrying(T.Hero));
    return true;
}
#endif
