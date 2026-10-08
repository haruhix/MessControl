#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCInventoryComponent.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCNutRainEvent.h"
#include "MCGameState.h"
#include "MCSingleDayDirector.h"
#include "MCPlaytestBotController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCPlaytestNutCombatTestsPrivate
{
    struct FWorldFixture
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        AMCPlaytestBotController* Bot=nullptr;
        AMCToothCharacter* Hero=nullptr;

        FWorldFixture()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
            auto* Floor=World->SpawnActor<AActor>();
            auto* Collision=NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Collision); Collision->SetBoxExtent(FVector(1000,1000,20));
            Collision->SetCollisionProfileName(TEXT("BlockAll")); Collision->RegisterComponent();
            Floor->SetActorLocation(FVector(0,5000,-20));
            Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,5000,130),FRotator::ZeroRotator);
            Hero->GetCharacterMovement()->DisableMovement();
            Bot=World->SpawnActor<AMCPlaytestBotController>();
            Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41); Bot->Possess(Hero);
        }
        ~FWorldFixture()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
        void Step(float Seconds)
        {
            for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) {
                ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60);
            }
        }
        AMCNutEnemy* AddNut()
        {
            auto* Nut=World->SpawnActor<AMCNutEnemy>(Hero->GetActorLocation()+FVector(120,0,-50),FRotator::ZeroRotator);
            // Isolate controller decisions and real weapon contact; enemy motion
            // and bite targeting have their own nut lifecycle and live-map tests.
            Nut->SetActorTickEnabled(false);
            return Nut;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestNutCombatTest,"MessControl.PlaytestBots.NutThreatInterruptsCareAndUsesWeaponContact",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestNutCombatTest::RunTest(const FString&)
{
    MCPlaytestNutCombatTestsPrivate::FWorldFixture T;
    T.Hero->Status->Settings.ContactSeconds=4;
    T.Hero->Status->Settings.bAllowSelfCare=true;
    T.Hero->Status->Damage(T.Hero->Status->State.MaxHealth*.25f);
    const float DamagedHealth=T.Hero->Status->State.Health;
    T.Step(.2f);
    TestTrue(TEXT("The bot was holding an ordinary care action"),T.Hero->IsPrimaryHeld());
    TestEqual(TEXT("The initial task is self repair"),T.Bot->GetActivity(),FString(TEXT("repair")));

    auto* Nut=T.AddNut();
    if(!TestNotNull(TEXT("A nearby hostile nut exists"),Nut)) return false;
    const float InitialHealth=Nut->Health;
    T.Step(.3f);
    TestTrue(TEXT("A nearby threat interrupts an already-held care action"),T.Bot->GetTaskTarget()==Nut);
    TestEqual(TEXT("The bot selects the ordinary pickaxe"),T.Hero->Inventory->Selected,EMCToolSlot::Pickaxe);
    TestTrue(TEXT("A real weapon swing was validated"),T.Hero->ValidatedSwingCount>0);
    TestEqual(TEXT("Choosing combat cannot damage before weapon contact"),Nut->Health,InitialHealth);
    TestEqual(TEXT("The interrupted care cannot manufacture healing"),T.Hero->Status->State.Health,DamagedHealth);
    T.Step(1.1f);
    TestTrue(TEXT("The delayed ordinary weapon contact damages the nut"),Nut->Health<InitialHealth);
    TestTrue(TEXT("The hit is counted by production swing resolution"),T.Hero->ConfirmedHitCount>0);
    T.Step(2.5f);
    TestTrue(TEXT("Repeated normal swings defeat the nut"),!IsValid(Nut) || !Nut->CanReceiveToolHit());
    TestTrue(TEXT("A defeated hostile is released as a work target"),T.Bot->GetTaskTarget()!=Nut);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestNutCarryInterruptionTest,"MessControl.PlaytestBots.NutThreatDropsFoodThroughToolSelection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestNutCarryInterruptionTest::RunTest(const FString&)
{
    MCPlaytestNutCombatTestsPrivate::FWorldFixture T;
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("The food fixture has a compiled mesh"),Cube)) return false;
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
    const FTransform Pose(FVector(70,5000,130));
    auto* Food=T.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose);
    if(!TestNotNull(TEXT("An ordinary collectable piece exists"),Food)) return false;
    FMCFoodRow Row; Row.WholeMeshes={Cube}; Row.FragmentMeshes={Cube}; Row.Scale=Row.FragmentScale=FVector(.2);
    Row.SpoilSeconds=600; FRandomStream Random(41);
    Food->ConfigureItem(TEXT("CarryFixture"),Row,Random,true); Food->FinishSpawning(Pose);
    T.Hero->SetPrimaryInputHeld(true);
    T.Step(.2f);
    if(!TestTrue(TEXT("Normal collection input picks up food before the threat"),T.Hero->FoodCollection->Contains(Food))) return false;
    auto* Nut=T.AddNut();
    T.Step(.3f);
    TestTrue(TEXT("Delivery intent yields to the visible nearby hostile"),T.Bot->GetTaskTarget()==Nut);
    TestEqual(TEXT("Normal tool selection arms the carrier"),T.Hero->Inventory->Selected,EMCToolSlot::Pickaxe);
    TestFalse(TEXT("Switching tools releases the ordinary collection state"),T.Hero->FoodCollection->bCollecting);
    TestTrue(TEXT("Switching tools empties the carried pile"),T.Hero->FoodCollection->Pieces.IsEmpty());
    TestNull(TEXT("Released food is no longer attached to the worker"),Food->StackCarrier.Get());
    TestFalse(TEXT("Combat preparation cannot dispose food for free"),Food->IsDisposed());
    const float InitialHealth=Nut->Health;
    T.Step(1.1f);
    TestTrue(TEXT("A former carrier attacks through actual weapon contact"),Nut->Health<InitialHealth);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestNutContactRangeTest,"MessControl.PlaytestBots.NutOutsideContactNeedsApproach",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestNutContactRangeTest::RunTest(const FString&)
{
    MCPlaytestNutCombatTestsPrivate::FWorldFixture T;
    auto* Nut=T.AddNut();
    Nut->SetActorLocation(T.Hero->GetActorLocation()+FVector(160,0,-50));
    if (!TestFalse(TEXT("This visible hostile is outside the real weapon contact range"),T.Hero->CanContact(Nut))) return false;
    const float Health=Nut->Health;
    T.Step(.3f);
    TestFalse(TEXT("An out-of-contact bot must approach rather than hold ineffective attack input"),T.Hero->IsPrimaryHeld());
    TestEqual(TEXT("Approach planning cannot manufacture damage"),Nut->Health,Health);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestNutCommitmentTest,"MessControl.PlaytestBots.NutEncounterKeepsCombatButAllowsCriticalCare",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestNutCommitmentTest::RunTest(const FString&)
{
    MCPlaytestNutCombatTestsPrivate::FWorldFixture T;
    auto* State=T.World->SpawnActor<AMCGameState>(); T.World->SetGameState(State);
    State->Phase=EMCShiftPhase::Working; State->bSingleDayLoop=true;
    auto* Director=T.World->SpawnActor<AMCSingleDayDirector>(); Director->SetActorTickEnabled(false);
    State->SingleDayDirector=Director; Director->Stage=EMCSingleDayStage::Nuts;
    auto* Event=T.World->SpawnActor<AMCNutRainEvent>(); Event->SetActorTickEnabled(false);
    Director->NutEvent=Event; Event->Settings.bBossEncounter=true; Event->Stage=EMCNutRainStage::Enemies;
    const FTransform Pose(FRotator(0,180,0),T.Hero->GetActorLocation()+FVector(120,0,-50));
    auto* Boss=T.World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose);
    if(!TestNotNull(TEXT("A contact-range tank exists"),Boss)) return false;
    Boss->State=EMCNutBossState::Idle; Boss->Health=1000; Boss->Settings.MaxHealth=1000;
    Boss->FinishSpawning(Pose); Boss->SetActorTickEnabled(false);
    T.Hero->Status->Settings.bAllowSelfCare=true;
    T.Hero->Status->Settings.ContactSeconds=4;
    T.Step(1.5f); // Beyond the skilled bot's 1.2-second ordinary task commitment.
    if(!TestTrue(TEXT("The bot acquired the boss through an ordinary decision"),T.Bot->GetTaskTarget()==Boss)) return false;
    TestTrue(TEXT("A blocked flank still permits real shield-front weapon hits"),T.Hero->ConfirmedHitCount>0);
    const float BossHealth=Boss->Health;
    T.Hero->Status->Damage(T.Hero->Status->State.MaxHealth*.25f);
    const float WorkerHealth=T.Hero->Status->State.Health;
    // Exercise the next decision with attack released, as during an approach or
    // between contacts. Ordinary self repair now has a higher score than tank combat.
    T.Hero->SetPrimaryInputHeld(false);
    T.Step(.3f);
    TestTrue(TEXT("The active encounter retains a valid boss after task commitment expires"),T.Bot->GetTaskTarget()==Boss);
    TestEqual(TEXT("Combat keeps its ordinary weapon selected"),T.Hero->Inventory->Selected,EMCToolSlot::Pickaxe);
    T.Step(.9f);
    TestTrue(TEXT("Retained combat produces another actual tool contact"),Boss->Health<BossHealth);
    TestEqual(TEXT("Retaining combat does not silently heal the worker"),T.Hero->Status->State.Health,WorkerHealth);
    T.Hero->Status->Damage(T.Hero->Status->State.MaxHealth*.5f);
    T.Step(.3f);
    TestTrue(TEXT("Critical health still interrupts boss commitment for normal self care"),T.Bot->GetTaskTarget()==T.Hero);
    TestEqual(TEXT("Critical care selects the existing repair action"),T.Bot->GetActivity(),FString(TEXT("repair")));
    return true;
}
#endif
