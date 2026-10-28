#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCFoodPerkMath.h"
#include "MCGameState.h"
#include "MCNutRainEvent.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCRoguePerkEffect.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCFoodPassiveTests
{
constexpr double GroundZ=10000.;
constexpr float Dt=1.f/120.f;
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCPlayerState* Player=nullptr;
    AMCToothCharacter* Hero=nullptr;
    UMCPerkComponent* Perks=nullptr;
    UDataTable* Table=nullptr;
    UStaticMesh* Cube=nullptr;
    TArray<TWeakObjectPtr<AActor>> Fixtures;
    FWorld(double HeroHeight=80.)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;
        Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        Table=NewObject<UDataTable>(World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        Add(TEXT("B1_BigPortion"),EMCRogueStat::CarryCapacity,2,true);
        Add(TEXT("B1_ConfidentThrow"),EMCRogueStat::FoodThrowRange,.2f);
        Add(TEXT("B1_FreshDelivery"),EMCRogueStat::SpoilRate,-.1f);
        Add(TEXT("B1_GreedyHands"),EMCRogueStat::FoodPickupRange,.3f);
        auto* Controller=World->SpawnActor<AMCPlayerController>();
        Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,Controller);
        Player->Perks->PerkTable=Table;Player->FinishSpawning(FTransform::Identity);
        Controller->SetPlayerState(Player);Player->SetOwner(Controller);State->AddPlayerState(Player);Perks=Player->Perks;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,GroundZ+HeroHeight),FRotator::ZeroRotator);
        Controller->Possess(Hero);Hero->GetCharacterMovement()->DisableMovement();Hero->Status->Initialize(100);
        Controller->bShowMouseCursor=false;
    }
    ~FWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Add(FName ID,EMCRogueStat Stat,float Value,bool Flat=false)
    {
        FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Passive;Row.bRoguePerk=true;Row.MaxStacks=1;
        Row.EffectClass=UMCRoguePerkEffect::StaticClass();Row.ChoiceFamily=ID;
        FMCRogueModifier Modifier;Modifier.Stat=Stat;Modifier.Value=Value;Modifier.bFlat=Flat;Row.Modifiers={Modifier};
        Table->AddRow(ID,Row);
    }
    void Step(double Seconds)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,Dt);}
    }
    AActor* Box(FVector Center,FVector HalfExtent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape);Actor->SetActorLocation(Center);Shape->SetBoxExtent(HalfExtent);
        Shape->SetCollisionProfileName(TEXT("BlockAll"));Shape->RegisterComponent();Fixtures.Add(Actor);return Actor;
    }
    AMCFoodActor* Food(FVector Position,bool Fragment=true)
    {
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;Row.WholeMeshes={Cube};Row.FragmentMeshes={Cube};
        Row.Scale=Row.FragmentScale=FVector(.12f);Row.Mass=.6f;Row.Fragments=3;Row.Health=100;Row.SpoilSeconds=300;
        const FTransform Pose(Position);
        auto* Item=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(57);Item->ConfigureItem(TEXT("BatchOneFoodFixture"),Row,Random,Fragment);Item->FinishSpawning(Pose);
        Fixtures.Add(Item);return Item;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBigPortionAutomaticPickup,
    "MessControl.Passives.Batch1.Food.BigPortionUsesActualAutomaticCapacity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBigPortionAutomaticPickup::RunTest(const FString&)
{
    using namespace MCFoodPassiveTests;
    FWorld T;auto* Collection=T.Hero->FoodCollection.Get();
    TestEqual(TEXT("Ordinary carrying capacity is six"),Collection->EffectiveCapacity(),6);
    if(!TestTrue(TEXT("The new passive can be acquired"),T.Perks->ServerGrantPerk(TEXT("B1_BigPortion")))) return false;
    TestEqual(TEXT("The additive bonus adds exactly two places"),Collection->EffectiveCapacity(),8);
    Collection->MaxPieces=10;TestEqual(TEXT("An authored base ten keeps its two extra places"),Collection->EffectiveCapacity(),12);
    Collection->MaxPieces=6;
    T.Box(FVector(0,0,GroundZ-20),FVector(1000,1000,20));
    for(int32 I=0;I<9;++I) {
        const double Angle=2*PI*I/9.;T.Food(FVector(FMath::Cos(Angle)*105,FMath::Sin(Angle)*105,GroundZ+12));
    }
    Collection->Toggle();
    // This is the normal timed E collector, not eight direct Collect calls.
    T.Step(5.);
    TestEqual(TEXT("Ordinary collector tick actually fills both added places"),Collection->Pieces.Num(),8);
    T.Step(1.);
    TestEqual(TEXT("A ninth nearby piece is not collected"),Collection->Pieces.Num(),8);
    TestFalse(TEXT("The same passive cannot add another two places"),T.Perks->ServerGrantPerk(TEXT("B1_BigPortion")));
    Collection->Stop();T.Perks->RemovePerk(TEXT("B1_BigPortion"));
    TestEqual(TEXT("Removing the passive restores base capacity"),Collection->EffectiveCapacity(),6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGreedyHandsRange,
    "MessControl.Passives.Batch1.Food.GreedyHandsFragmentRangeAndWalls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGreedyHandsRange::RunTest(const FString&)
{
    using namespace MCFoodPassiveTests;
    FWorld T;auto* Collection=T.Hero->FoodCollection.Get();
    const FVector Origin=T.Hero->GetActorLocation();
    auto* Piece=T.Food(Origin+FVector(210,0,0));
    TestFalse(TEXT("The ordinary 180cm reach excludes a 210cm fragment"),Collection->CanCollect(Piece));
    if(!TestTrue(TEXT("The new pickup passive is granted"),T.Perks->ServerGrantPerk(TEXT("B1_GreedyHands")))) return false;
    TestTrue(TEXT("Fragment reach becomes exactly 234cm"),FMath::IsNearlyEqual(Collection->EffectiveCollectionReach(Piece),234.f,.001f));
    TestTrue(TEXT("The real pickup eligibility accepts the farther fragment"),Collection->CanCollect(Piece));
    auto* Beyond=T.Food(Origin+FVector(235,80,0));
    TestFalse(TEXT("The reach bonus is bounded"),Collection->CanCollect(Beyond));
    auto* Whole=T.Food(Origin+FVector(210,-70,0),false);
    TestEqual(TEXT("Small whole food keeps baseline reach"),Collection->EffectiveCollectionReach(Whole),180.f);
    TestFalse(TEXT("The passive does not extend whole-food pickup"),Collection->CanCollect(Whole));
    auto* Encounter=T.World->SpawnActor<AMCNutRainEvent>();Encounter->Settings.bBossEncounter=true;
    auto* Nut=T.Food(Origin+FVector(210,70,0));Nut->SetOwner(Encounter);
    TestTrue(TEXT("The exclusion uses actual encounter ammunition ownership"),Nut->IsNutEncounterFragment());
    TestEqual(TEXT("Boss ammunition retains its original interaction radius"),Collection->EffectiveCollectionReach(Nut),180.f);
    TestFalse(TEXT("The fragment passive does not reach farther ammunition"),Collection->CanCollect(Nut));
    T.Box(Origin+FVector(100,0,0),FVector(5,30,80));
    TestFalse(TEXT("The longer reach does not pick through a structural wall"),Collection->CanCollect(Piece));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreshDeliveryOwnership,
    "MessControl.Passives.Batch1.Food.FreshDeliveryActualHoldingAndExpiredFood",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreshDeliveryOwnership::RunTest(const FString&)
{
    using namespace MCFoodPassiveTests;
    FWorld T;auto* Collection=T.Hero->FoodCollection.Get();
    if(!TestTrue(TEXT("The fresh-delivery passive is granted"),T.Perks->ServerGrantPerk(TEXT("B1_FreshDelivery")))) return false;
    auto* Held=T.Food(T.Hero->GetActorLocation()+FVector(80,0,0));Collection->Toggle();
    if(!TestTrue(TEXT("Food is really acquired into the hand stack"),Collection->Collect(Held))) return false;
    const double Start=T.World->GetTimeSeconds();Held->SpoilAt=Start+30;
    const double InitialDeadline=Held->SpoilAt;T.Step(1.);
    const double Elapsed=T.World->GetTimeSeconds()-Start;
    TestTrue(TEXT("Actual carried-food ticks consume freshness at 0.9 rate"),FMath::IsNearlyEqual(Held->SpoilAt-InitialDeadline,Elapsed*.1,.003));
    TestTrue(TEXT("The passive slows decay rather than restoring elapsed freshness"),Held->SpoilAt-T.World->GetTimeSeconds()<30.);
    Collection->Stop();const double ReleasedDeadline=Held->SpoilAt;T.Step(.5);
    TestEqual(TEXT("Released food receives no further spoil-time credit"),Held->SpoilAt,ReleasedDeadline);
    auto* Stale=T.Food(T.Hero->GetActorLocation()+FVector(80,60,0));Stale->SpoilAt=T.World->GetTimeSeconds()+30;
    Stale->SetStackCarrier(T.Hero);const double StaleDeadline=Stale->SpoilAt;Stale->UpdateAbsorption(.5f);
    TestEqual(TEXT("A stale carrier pointer outside the actual collection gives no bonus"),Stale->SpoilAt,StaleDeadline);
    Stale->SetStackCarrier(nullptr);Stale->Holders.Add(T.Hero);Stale->EquippedBy=T.Hero;Stale->UpdateAbsorption(.5f);
    TestEqual(TEXT("Stale equipped/holder pointers without a real grip give no bonus"),Stale->SpoilAt,StaleDeadline);
    Stale->Holders.Reset();Stale->EquippedBy=nullptr;
    auto* Expired=T.Food(T.Hero->GetActorLocation()+FVector(80,-60,0));Collection->Toggle();
    if(!TestTrue(TEXT("Overdue food can still be physically picked up for disposal"),Collection->Collect(Expired))) return false;
    Expired->SpoilAt=T.World->GetTimeSeconds()-.0001;const double ExpiredDeadline=Expired->SpoilAt;
    Expired->UpdateAbsorption(.5f);
    TestTrue(TEXT("A pickup cannot revive an overdue spoil deadline"),Expired->bSpoiled);
    TestEqual(TEXT("Overdue deadline is not pushed into the future"),Expired->SpoilAt,ExpiredDeadline);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCConfidentThrowPhysicalRange,
    "MessControl.Passives.Batch1.Food.ConfidentThrowPhysicalPlaneRange",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCConfidentThrowPhysicalRange::RunTest(const FString&)
{
    using namespace MCFoodPassiveTests;
    const auto Flight=[&](double Height,bool Bonus,double& OutRange)->bool {
        FWorld T(Height-20.);auto* Collection=T.Hero->FoodCollection.Get();
        // The calibration floor is under the release only. The flight crosses
        // its mathematical plane outside the small box, without a collision
        // bounce or sphere radius being mistaken for the landing center.
        T.Box(FVector(0,0,GroundZ-20),FVector(100,500,20));
        if(Bonus && !T.Perks->ServerGrantPerk(TEXT("B1_ConfidentThrow"))) {AddError(TEXT("ConfidentThrow grant failed"));return false;}
        Collection->Toggle();auto* Food=T.Food(FVector(50,0,GroundZ+Height));
        if(!Collection->Collect(Food)) {AddError(TEXT("The trajectory fixture did not actually acquire food"));return false;}
        const FVector Launch=Food->GetActorLocation();const float Speed=Collection->EffectiveThrowSpeed();
        if(!Collection->ThrowOne(FVector(1,0,1).GetSafeNormal())) {AddError(TEXT("Native ThrowOne rejected the fixture"));return false;}
        TestTrue(TEXT("The native body uses the shared camera/launch speed"),FMath::IsNearlyEqual(float(Food->Body->GetPhysicsLinearVelocity().Size()),Speed,.1f));
        FVector Previous=Food->Body->GetComponentLocation();
        for(int32 I=0;I<900;++I) {
            T.Step(Dt);const FVector Current=Food->Body->GetComponentLocation();
            if(Previous.Z>GroundZ && Current.Z<=GroundZ) {
                const double Alpha=(Previous.Z-GroundZ)/FMath::Max(double(SMALL_NUMBER),Previous.Z-Current.Z);
                const FVector Crossing=FMath::Lerp(Previous,Current,Alpha);OutRange=FVector2D(Crossing-Launch).Size();return true;
            }
            Previous=Current;
        }
        AddError(TEXT("Real Chaos flight never crossed the reference plane"));return false;
    };
    for(double Height:{80.,250.,500.}) {
        double Baseline=0,Improved=0;
        if(!Flight(Height,false,Baseline) || !Flight(Height,true,Improved)) return false;
        const double Ratio=Improved/Baseline;
        AddInfo(FString::Printf(TEXT("release height %.1fcm: real horizontal reach %.2f -> %.2fcm (x%.5f)"),Height,Baseline,Improved,Ratio));
        TestTrue(TEXT("The actual horizontal fallback reach grows 20%, not 44% from multiplying speed"),FMath::IsNearlyEqual(Ratio,1.2,.02));
    }
    FWorld T;auto* Collection=T.Hero->FoodCollection.Get();
    if(!TestTrue(TEXT("The exclusion fixture owns the new throw passive"),T.Perks->ServerGrantPerk(TEXT("B1_ConfidentThrow")))) return false;
    const float OrdinaryBaseline=NewObject<UMCFoodCollectionComponent>()->EffectiveThrowSpeed();
    auto* Foreign=T.Food(T.Hero->GetActorLocation()+FVector(70,0,0),false);
    Foreign->FoodData.Kind=EMCFoodKind::ForeignObject;T.Hero->HeldFood=Foreign;
    TestEqual(TEXT("Foreign-object throw calibration does not receive the food passive"),Collection->EffectiveThrowSpeed(),OrdinaryBaseline);
    T.Hero->HeldFood=nullptr;Foreign->Destroy();
    Collection->Toggle();auto* Nut=T.Food(T.Hero->GetActorLocation()+FVector(70,0,0));
    auto* Encounter=T.World->SpawnActor<AMCNutRainEvent>();Encounter->Settings.bBossEncounter=true;Nut->SetOwner(Encounter);
    if(!TestTrue(TEXT("A true encounter fragment can be picked up"),Collection->Collect(Nut))) return false;
    TestEqual(TEXT("The passive does not boost the separate aimed nut shot"),Collection->EffectiveThrowSpeed(),Collection->NutShotSpeed);
    TestTrue(TEXT("Nut ammunition remains a single hand slot"),Collection->EffectiveCapacity()==1);
    return true;
}
#endif
