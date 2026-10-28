#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCSlipperyFood.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCNutRainEvent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace MCSlipperyFoodPopTests
{
constexpr float Step=1.f/60.f;
constexpr float FloorTop=10020.f;
struct FWorldFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCToothCharacter* Other=nullptr;
    IConsoleVariable* PopSpeed=IConsoleManager::Get().FindConsoleVariable(TEXT("MC.Slip.FoodPopSpeed"));
    FString PreviousSpeed;
    EConsoleVariableFlags PreviousPriority=ECVF_SetByConstructor;
    FWorldFixture()
    {
        if(PopSpeed) {
            PreviousSpeed=PopSpeed->GetString();PreviousPriority=static_cast<EConsoleVariableFlags>(PopSpeed->GetFlags()&ECVF_SetByMask);
            PopSpeed->Set(TEXT("500"),PreviousPriority);
        }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Floor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Shape);
        Shape->SetBoxExtent(FVector(3000,3000,20));Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->SetCollisionObjectType(ECC_WorldStatic);Shape->RegisterComponent();Floor->SetActorLocation(FVector(0,0,FloorTop-20));
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,FloorTop+60),FRotator::ZeroRotator,Params);
        Other=World->SpawnActor<AMCToothCharacter>(FVector(140,100,FloorTop+60),FRotator::ZeroRotator,Params);
        // The fixed carriers isolate released Chaos food, not a movement/fall fixture.
        Hero->GetCharacterMovement()->DisableMovement();Other->GetCharacterMovement()->DisableMovement();
        Hero->Status->Initialize(100);Other->Status->Initialize(100);
    }
    ~FWorldFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
        if(PopSpeed) PopSpeed->Set(*PreviousSpeed,PreviousPriority);
    }
    bool Ready() const {return Hero && Other && PopSpeed && FMath::IsNearlyEqual(PopSpeed->GetFloat(),500.f,.001f);}
    void Tick(int32 Frames) {for(int32 I=0;I<Frames;++I){++GFrameCounter;World->Tick(LEVELTICK_All,Step);}}
    AMCFoodActor* Food(float Mass=2.f,FVector Offset=FVector(95,0,-25))
    {
        const FTransform Pose(Hero->GetActorLocation()+Offset);
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};
        Row.Scale=Row.FragmentScale=FVector(.24f);Row.Mass=Mass;Row.Fragments=1;Row.SpoilSeconds=300;
        FRandomStream Random(27);Food->ConfigureItem(TEXT("SlipPopFixture"),Row,Random,true);Food->FinishSpawning(Pose);return Food;
    }
    bool Collect(AMCFoodActor* Food)
    {
        Hero->FoodCollection->bCollecting=true;return Hero->FoodCollection->Collect(Food);
    }
    TSet<AMCFoodActor*> FoodIdentities() const
    {
        TSet<AMCFoodActor*> Result;for(TActorIterator<AMCFoodActor> It(World);It;++It) Result.Add(*It);return Result;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFoodPopFlight,
    "MessControl.SlipperyFood.Pop.OwnedIdentitiesMassIndependentFlightLandingAndRecollect",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFoodPopFlight::RunTest(const FString&)
{
    using namespace MCSlipperyFoodPopTests;
    FWorldFixture T;if(!TestTrue(TEXT("Pop fixture has real carriers and the scoped native speed"),T.Ready())) return false;
    auto* Light=T.Food(2.f,FVector(95,-25,-25));auto* Heavy=T.Food(18.f,FVector(95,25,-25));
    if(!TestTrue(TEXT("Two different real delivery pieces are accepted by native collection"),T.Collect(Light) && T.Collect(Heavy))) return false;
    const auto Before=T.FoodIdentities();
    TestTrue(TEXT("Both original actors are actual owned collection members"),MCSlipperyFood::IsCarrying(T.Hero) && Light->StackCarrier==T.Hero && Heavy->StackCarrier==T.Hero);
    TestTrue(TEXT("The fixture actually supplies substantially different physical masses"),Heavy->Body->GetMass()>Light->Body->GetMass()*4.f);
    const float HeroHP=T.Hero->Status->State.Health,OtherHP=T.Other->Status->State.Health;
    if(!TestEqual(TEXT("One accepted slip releases and launches exactly the two owned pieces"),MCSlipperyFood::ReleaseWithCartoonPop(T.Hero),2)) return false;
    const auto After=T.FoodIdentities();
    TestTrue(TEXT("The pop reuses the exact original food identities, without new actors"),Before.Num()==After.Num() && After.Contains(Light) && After.Contains(Heavy));
    TestTrue(TEXT("Native release clears actual collection ownership and collection mode"),T.Hero->FoodCollection->Pieces.IsEmpty() && !T.Hero->FoodCollection->bCollecting && !MCSlipperyFood::IsCarrying(T.Hero));
    for(auto* Food:{Light,Heavy}) {
        TestTrue(TEXT("Released ordinary food regains native simulation with no remaining holder"),Food->Body->IsSimulatingPhysics() && !Food->StackCarrier && Food->Holders.IsEmpty() && Food->Phase==EMCFoodPhase::Free);
        const FVector Velocity=Food->Body->GetPhysicsLinearVelocity(),Spin=Food->Body->GetPhysicsAngularVelocityInRadians();
        TestTrue(TEXT("Each piece gets a bounded upward cartoon arc and finite fan/spin"),!Velocity.ContainsNaN() && !Spin.ContainsNaN()
            && Velocity.Z>=250.f && Velocity.Z<=650.f && Velocity.Size2D()<=111.f && Spin.Size()>.1f && Spin.Size()<20.f);
    }
    const float LightZ=Light->Body->GetPhysicsLinearVelocity().Z,HeavyZ=Heavy->Body->GetPhysicsLinearVelocity().Z;
    TestTrue(TEXT("Heavy food receives comparable upward speed rather than an inverse-mass impulse"),HeavyZ>LightZ*.75f && HeavyZ<LightZ*1.3f);
    const float StartLight=Light->GetActorLocation().Z,StartHeavy=Heavy->GetActorLocation().Z;
    T.Tick(12);
    TestTrue(TEXT("The light original food actually travels upward under Chaos"),IsValid(Light) && Light->GetActorLocation().Z>StartLight+25.f);
    TestTrue(TEXT("The heavy original food actually travels upward under Chaos"),IsValid(Heavy) && Heavy->GetActorLocation().Z>StartHeavy+25.f);
    T.Tick(180); // Complete the arc and the release grace, rather than forcing a landed state.
    for(auto* Food:{Light,Heavy}) TestTrue(TEXT("Original food lands back on the actual flat collision floor"),IsValid(Food)
        && Food->GetActorLocation().Z>FloorTop-2.f && Food->GetActorLocation().Z<FloorTop+55.f
        && FMath::Abs(Food->Body->GetPhysicsLinearVelocity().Z)<60.f);
    TestEqual(TEXT("Pop and landing do not damage the carrier"),T.Hero->Status->State.Health,HeroHP);
    TestEqual(TEXT("Nearby standing player is not damaged by the released food"),T.Other->Status->State.Health,OtherHP);
    TestEqual(TEXT("Repeating the helper after release cannot create another launch"),MCSlipperyFood::ReleaseWithCartoonPop(T.Hero),0);
    // Diagnostic approach only: collection itself still uses native range/ownership/grace gates.
    T.Hero->SetActorLocation(FVector(Light->GetActorLocation().X-80,Light->GetActorLocation().Y,FloorTop+60),false,nullptr,ETeleportType::TeleportPhysics);
    if(!TestTrue(TEXT("After the real arc and grace, native pickup accepts the same landed food"),T.Collect(Light))) return false;
    TestTrue(TEXT("Recollection owns the original actor rather than a replacement"),Light->StackCarrier==T.Hero && T.Hero->FoodCollection->Contains(Light));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFoodOrdinaryRelease,
    "MessControl.SlipperyFood.Pop.OrdinaryStopAndQThrowKeepTheirOwnMotion",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFoodOrdinaryRelease::RunTest(const FString&)
{
    using namespace MCSlipperyFoodPopTests;
    FWorldFixture T;if(!TestTrue(TEXT("Ordinary-release fixture is available"),T.Ready())) return false;
    auto* Drop=T.Food();if(!TestTrue(TEXT("Ordinary drop uses actual pickup"),T.Collect(Drop))) return false;
    T.Hero->FoodCollection->Stop(false);
    TestTrue(TEXT("Ordinary Stop restores physics without the new upward pop"),Drop->Body->IsSimulatingPhysics() && !Drop->StackCarrier
        && FMath::Abs(Drop->Body->GetPhysicsLinearVelocity().Z)<1.f);
    auto* Throw=T.Food(2.f,FVector(110,45,-25));if(!TestTrue(TEXT("Q baseline uses another actual pickup"),T.Collect(Throw))) return false;
    if(!TestTrue(TEXT("The native Q endpoint still launches one item"),T.Hero->FoodCollection->ThrowOne(FVector::ForwardVector))) return false;
    TestTrue(TEXT("Q keeps its normal aimed horizontal launch, rather than the slip fan"),!Throw->StackCarrier
        && Throw->Body->GetPhysicsLinearVelocity().X>300.f && FMath::Abs(Throw->Body->GetPhysicsLinearVelocity().Z)<250.f);
    TestEqual(TEXT("An empty collection cannot receive a spurious pop after Q"),MCSlipperyFood::ReleaseWithCartoonPop(T.Hero),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFoodPopExclusions,
    "MessControl.SlipperyFood.Pop.NutToolAndStaleEntriesAreNotLaunched",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFoodPopExclusions::RunTest(const FString&)
{
    using namespace MCSlipperyFoodPopTests;
    for(int32 Case=0;Case<3;++Case) {
        FWorldFixture T;if(!TestTrue(TEXT("Excluded-ownership fixture is available"),T.Ready())) return false;
        auto* Food=T.Food();
        if(Case==0) {
            auto* Encounter=T.World->SpawnActor<AMCNutRainEvent>();Encounter->Settings.bBossEncounter=true;Food->SetOwner(Encounter);
            TestTrue(TEXT("Excluded nut has actual encounter ownership"),Food->IsNutEncounterFragment());
            if(!TestTrue(TEXT("Boss ammunition uses its native hand/collection path"),T.Collect(Food))) return false;
        } else if(Case==1) {
            Food->bBrushTool=true;Food->Phase=EMCFoodPhase::Carried;Food->Holders={T.Hero};
            T.Hero->HeldFood=Food;T.Hero->Grip->Frame.Food=Food;
        } else {
            // Stale membership is deliberately restored without matching carrier ownership.
            T.Hero->FoodCollection->Pieces.Add(Food);Food->SetStackCarrier(T.Other);
        }
        TestEqual(TEXT("Boss nuts, tools and somebody else's stale entry never receive a pop"),MCSlipperyFood::ReleaseWithCartoonPop(T.Hero),0);
        TestTrue(TEXT("The excluded original actor survives without upward launch speed"),IsValid(Food) && Food->Body->GetPhysicsLinearVelocity().Z<100.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperySharedLegacyFood,
    "MessControl.SlipperyFood.Pop.SharedLegacyHolderPreventsLaunch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperySharedLegacyFood::RunTest(const FString&)
{
    using namespace MCSlipperyFoodPopTests;
    FWorldFixture T;if(!TestTrue(TEXT("Shared legacy ownership fixture is available"),T.Ready())) return false;
    auto* Food=T.Food();Food->Phase=EMCFoodPhase::Carried;Food->Holders={T.Hero,T.Other};
    T.Hero->HeldFood=Food;T.Hero->Grip->Frame.Food=Food;
    T.Other->HeldFood=Food;T.Other->Grip->Frame.Food=Food;
    TestTrue(TEXT("The restored legacy food genuinely has both registered holders"),MCSlipperyFood::IsCarrying(T.Hero) && MCSlipperyFood::IsCarrying(T.Other));
    TestEqual(TEXT("A food actor with a remaining legacy holder must not be launched"),MCSlipperyFood::ReleaseWithCartoonPop(T.Hero),0);
    TestTrue(TEXT("The other player's actual hold remains present"),Food->Holders.Contains(T.Other) && T.Other->HeldFood==Food);
    TestTrue(TEXT("Shared food receives no added upward launch velocity"),Food->Body->GetPhysicsLinearVelocity().Z<100.f);
    return true;
}
#endif
