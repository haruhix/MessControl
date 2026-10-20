#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCInventoryComponent.h"
#include "MCArenaTooth.h"
#include "MCPlaytestBotController.h"
#include "MCSmallMintCandy.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCPlaytestBotsAdaptationTestsPrivate
{
/** Real controller timers and ordinary pawn contacts, with stationary actors.
 * No navmesh, director or full-level completion is claimed by these fixtures. */
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCPlaytestBotController* Bot=nullptr;
    AMCToothCharacter* Hero=nullptr;

    explicit FFixture(FVector WorkerPosition=FVector(-120,5000,130))
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        auto* Floor=World->SpawnActor<AActor>();
        auto* Collision=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Collision); Collision->SetBoxExtent(FVector(2000,2000,20));
        Collision->SetCollisionProfileName(TEXT("BlockAll")); Collision->RegisterComponent();
        Floor->SetActorLocation(FVector(0,5000,-20));
        Hero=World->SpawnActor<AMCToothCharacter>(WorkerPosition,FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();
        Bot=World->SpawnActor<AMCPlaytestBotController>();
        Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41); Bot->Possess(Hero);
    }
    ~FFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    void Step(float Seconds)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I)
        { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); }
    }
    UStaticMesh* Mesh(const TCHAR* Path)
    {
        auto* Result=LoadObject<UStaticMesh>(nullptr,Path);
#if WITH_EDITOR
        if(Result) FStaticMeshCompilingManager::Get().FinishCompilation({Result});
#endif
        return Result;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestOwnFootRescueTest,
    "MessControl.PlaytestBots.Adaptation.FrozenBotUsesOrdinaryHeldPickaxe",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestOwnFootRescueTest::RunTest(const FString&)
{
    MCPlaytestBotsAdaptationTestsPrivate::FFixture F;
    F.Hero->FreezeLegs(80);
    if(!TestTrue(TEXT("Fixture has real foot ice before the AI decision"),F.Hero->HasFrozenLegs())) return false;
    const int32 Before=F.Hero->ConfirmedHitCount;
    F.Step(.12f);
    TestTrue(TEXT("A decision cannot instantly remove the ice"),F.Hero->HasFrozenLegs());
    TestEqual(TEXT("The AI selects the ordinary rescue tool before contact"),F.Hero->Inventory->Selected,EMCToolSlot::Pickaxe);
    F.Step(3.f);
    TestFalse(TEXT("Real held-input swings free the bot's feet"),F.Hero->HasFrozenLegs());
    TestTrue(TEXT("Rescue requires at least two confirmed tool contacts"),F.Hero->ConfirmedHitCount-Before>=2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestCarriedFragmentTest,
    "MessControl.PlaytestBots.Adaptation.ReplanningPreservesCollectedFragment",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestCarriedFragmentTest::RunTest(const FString&)
{
    MCPlaytestBotsAdaptationTestsPrivate::FFixture F;
    auto* Cube=F.Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("Fragment mesh exists"),Cube)) return false;
    FMCFoodRow Row; Row.Kind=EMCFoodKind::Food; Row.WholeMeshes={Cube}; Row.FragmentMeshes={Cube};
    Row.Scale=Row.FragmentScale=FVector(.2); Row.Collision.bAutoOptimize=false;
    auto* Fragment=F.World->SpawnActor<AMCFoodActor>(F.Hero->GetActorLocation()+FVector(70,0,-20),FRotator::ZeroRotator);
    FRandomStream Random(41); Fragment->ConfigureItem(TEXT("BotFragment"),Row,Random,true);
    F.Hero->SetFoodCollectionInput(true);
    if(!TestTrue(TEXT("Fixture pickup passes ordinary collection validation"),F.Hero->FoodCollection->Collect(Fragment))) return false;
    F.Step(1.f);
    // There is no delivery landmark in this fixture. Replanning must wait with
    // its real load, not discard it or try to chop an indestructible fragment.
    TestTrue(TEXT("An already collected fragment survives task replanning"),F.Hero->FoodCollection->Contains(Fragment));
    TestEqual(TEXT("Fragment remains assigned to the same real carrier"),Fragment->StackCarrier.Get(),F.Hero);
    TestFalse(TEXT("A decision cannot complete food without a real throw/intake"),Fragment->IsDisposed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestArenaSprayRepairTest,
    "MessControl.PlaytestBots.Adaptation.ArenaRepairUsesOrdinaryTimedSpray",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestArenaSprayRepairTest::RunTest(const FString&)
{
    MCPlaytestBotsAdaptationTestsPrivate::FFixture F;
    auto* Cube=F.Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("The dental fixture uses an engine mesh"),Cube)) return false;
    // A live, stationary crown in ordinary tool reach. The worker remains on
    // the fixture's floor; no navmesh or synthetic care contacts are needed.
    const FTransform Pose(FVector(40,5000,130));
    auto* Tooth=F.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose);
    if(!TestNotNull(TEXT("The damaged crown is a real arena tooth"),Tooth)) return false;
    Tooth->Initialize(1,FMCArenaToothSettings());
    Tooth->SetAppearance(Cube,FVector(2));
    Tooth->FinishSpawning(Pose);
    if(!TestTrue(TEXT("The crown receives ordinary nonlethal damage"),Tooth->ReceiveArenaHit(40,FVector::RightVector))) return false;
    const float DamagedHealth=Tooth->Status->State.Health;
    TestTrue(TEXT("The damaged crown is still available for normal repair"),Tooth->IsAvailable() && Tooth->Status->NeedsCare(false));
    F.Step(.05f);
    TestEqual(TEXT("Waiting before the first decision cannot manufacture repair"),Tooth->Status->State.Health,DamagedHealth);
    F.Step(.10f);
    TestTrue(TEXT("Ordinary decisions acquire the damaged crown"),F.Bot->GetTaskTarget()==Tooth);
    TestEqual(TEXT("Dental repair selects the actual spray tool"),F.Hero->Inventory->Selected,EMCToolSlot::Spray);
    TestTrue(TEXT("The bot holds the ordinary treatment input"),F.Hero->IsPrimaryHeld());
    TestTrue(TEXT("Selecting repair cannot instantly restore the crown"),Tooth->Status->State.Health<Tooth->Status->State.MaxHealth);
    F.Step(1.f);
    TestTrue(TEXT("The real spray ray and elapsed tool ticks increase dental health"),Tooth->Status->State.Health>DamagedHealth);
    TestTrue(TEXT("Timed treatment respects ordinary maximum health"),Tooth->Status->State.Health<=Tooth->Status->State.MaxHealth);
    TestEqual(TEXT("The arena health snapshot follows its real status component"),Tooth->State.Health,Tooth->Status->State.Health);
    F.Bot->SetLabControlled(true); // Normal release, then stop autonomous re-presses.
    const float ReleasedHealth=Tooth->Status->State.Health;
    F.Step(.5f);
    TestFalse(TEXT("Stopping the task releases treatment input"),F.Hero->IsPrimaryHeld());
    TestEqual(TEXT("Released treatment cannot continue healing"),Tooth->Status->State.Health,ReleasedHealth);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestSmallMintContactTest,
    "MessControl.PlaytestBots.Adaptation.SmallMintIsChosenAndBrokenThroughWeaponContacts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestSmallMintContactTest::RunTest(const FString&)
{
    // The tall authored candy scales to a 28 cm X half-width at diameter135.
    // This stationary contact fixture must already be inside the normal AI
    // approach radius; navigation itself is exercised by the saved-level probe.
    MCPlaytestBotsAdaptationTestsPrivate::FFixture F(FVector(-100,5000,130));
    auto* Plane=F.Mesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if(!TestNotNull(TEXT("Mint surface mesh exists"),Plane)) return false;
    const FTransform Floor(FRotator::ZeroRotator,FVector(0,5000,0),FVector(30,30,1));
    auto* Tongue=F.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor);
    Tongue->SourceMesh=Plane; Tongue->bAutomaticYawns=false; Tongue->FinishSpawning(Floor);
    Tongue->SetActorTickEnabled(false);
    auto* Mint=F.World->SpawnActor<AMCSmallMintCandy>();
    FMCSmallMintCandySettings Config;
    if(!TestTrue(TEXT("Real mint starts on the ordinary tongue query"),Mint->Start(Tongue,Config,41))) return false;
    const float InitialHealth=Mint->Health;
    F.Step(.2f);
    TestEqual(TEXT("Choosing a falling mint cannot bypass its invulnerable fall"),Mint->Health,InitialHealth);
    const int32 Hits=F.Hero->ConfirmedHitCount;
    F.Step(12.f);
    TestTrue(TEXT("Ordinary AI weapon contacts destroy the live mint"),Mint->IsComplete());
    TestTrue(TEXT("The mint receives multiple confirmed ordinary contacts"),F.Hero->ConfirmedHitCount-Hits>=5);
    TestFalse(TEXT("Finished mint is released as the task target"),F.Bot->GetTaskTarget()==Mint);
    return true;
}
#endif
