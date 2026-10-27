#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCArenaTooth.h"
#include "MCHubGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCWorldStatusComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MCToothCareRepairTests
{
/** Authority rules only: no viewport, input, profile write, spawned effects or world-time override. */
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Worker=nullptr;
    int32 NextTooth=1;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Hub=World->SpawnActor<AMCHubGameState>();World->SetGameState(Hub);Hub->Phase=EMCHubPhase::Gathering;
        Worker=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        Worker->Status->Initialize(100);Worker->GetCharacterMovement()->DisableMovement();
    }
    AMCArenaTooth* Tooth()
    {
        const int32 Id=NextTooth++;const FTransform Pose(FVector(Id*1000,0,10000));
        auto* Result=World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose,
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Result->Initialize(Id,FMCArenaToothSettings());Result->FinishSpawning(Pose);return Result;
    }
    ~FWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrokenToothCycleTest,"MessControl.Care.Arena.BrokenCycleKeepsFivefoldDuration",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrokenToothCycleTest::RunTest(const FString&)
{
    MCToothCareRepairTests::FWorld F;
    if(!TestTrue(TEXT("The authority worker dispatched real BeginPlay and can perform care"),F.Worker->HasActorBegunPlay() && F.Worker->CanWork())) return false;
    auto* Baseline=F.Tooth();auto* Slow=F.Tooth();
    TestEqual(TEXT("Existing care profiles inherit the approved fivefold duration"),Slow->Status->Settings.BrokenRepairDurationMultiplier,5.f);
    Baseline->Status->Settings.BrokenRepairDurationMultiplier=1.f;
    for(auto* Tooth:{Baseline,Slow})
        if(!TestTrue(TEXT("A real arena hit creates a live Broken crown"),Tooth->ReceiveArenaHit(70,FVector::ForwardVector)
            && Tooth->DamageStage()==EMCArenaToothDamageStage::Broken)) return false;
    int32 BaselineSteps=0,SlowSteps=0;bool SawSlowChipped=false;
    while(Baseline->Status->NeedsCare(false) && BaselineSteps<400)
    {if(!Baseline->Status->RepairWithTool(2.5f,F.Worker)) return TestTrue(TEXT("Baseline repair remains accepted"),false);++BaselineSteps;}
    while(Slow->Status->NeedsCare(false) && SlowSteps<400)
    {
        if(!Slow->Status->RepairWithTool(2.5f,F.Worker)) return TestTrue(TEXT("Broken-cycle repair remains accepted"),false);
        ++SlowSteps;
        if(Slow->DamageStage()==EMCArenaToothDamageStage::Chipped) {
            SawSlowChipped=true;
            TestTrue(TEXT("Crossing into Chipped retains the ongoing broken-repair contract"),Slow->Status->State.bBrokenRepairCycle);
        }
    }
    TestTrue(TEXT("The repair actually crossed the Broken-to-Chipped visual stage"),SawSlowChipped);
    TestEqual(TEXT("Equal accepted tool-work pulses take five times as many steps through the whole cycle"),SlowSteps,BaselineSteps*5);
    TestEqual(TEXT("The broken crown reaches its real full HP"),Slow->Status->State.Health,Slow->Status->State.MaxHealth);
    TestFalse(TEXT("Full restoration releases the broken-cycle latch"),Slow->Status->State.bBrokenRepairCycle);
    TestFalse(TEXT("The completed repair leaves no outstanding repair contacts"),Slow->Status->NeedsCare(false));

    auto* Original=F.Tooth();Original->ReceiveArenaHit(70,FVector::ForwardVector);
    for(int32 I=0;I<3;++I) Original->Status->RepairWithTool(25,F.Worker);
    if(!TestEqual(TEXT("The saved partial cycle is already visually Chipped"),Original->DamageStage(),EMCArenaToothDamageStage::Chipped)) return false;
    auto* Restored=F.Tooth();Restored->Status->Restore(Original->Status->State);
    const float Before=Restored->Status->State.Health;Restored->Status->RepairWithTool(25,F.Worker);
    TestEqual(TEXT("Restoring the status snapshot preserves the unfinished slower cycle"),Restored->Status->State.Health-Before,5.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToothRepairGuardTest,"MessControl.Care.Arena.ChippedLostAndPlayerGuards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToothRepairGuardTest::RunTest(const FString&)
{
    MCToothCareRepairTests::FWorld F;auto* Chipped=F.Tooth();
    if(!TestTrue(TEXT("An ordinary hit produces only a chipped tooth"),Chipped->ReceiveArenaHit(25,FVector::ForwardVector)
        && Chipped->DamageStage()==EMCArenaToothDamageStage::Chipped)) return false;
    TestTrue(TEXT("Ordinary chipped care still accepts an unchanged tool pulse"),Chipped->Status->RepairWithTool(10,F.Worker));
    TestEqual(TEXT("A chipped-only repair is not slowed"),Chipped->Status->State.Health,85.f);
    TestFalse(TEXT("A chipped-only job never enters the broken cycle"),Chipped->Status->State.bBrokenRepairCycle);
    const float Before=Chipped->Status->State.Health;
    TestFalse(TEXT("Invalid repair amounts cannot damage or complete a tooth"),Chipped->Status->RepairWithTool(-20,F.Worker));
    TestEqual(TEXT("Rejected work leaves real HP unchanged"),Chipped->Status->State.Health,Before);
    auto* Lost=F.Tooth();Lost->ReceiveArenaHit(100,FVector::ForwardVector);
    TestFalse(TEXT("Repair does not resurrect a lost reserve tooth"),Lost->Status->RepairWithTool(100,F.Worker));
    TestEqual(TEXT("A lost tooth retains zero HP"),Lost->Status->State.Health,0.f);
    auto* Consumed=F.Tooth();Consumed->ConsumeForRespawn();
    TestFalse(TEXT("Repair cannot replace a consumed reserve"),Consumed->Status->RepairWithTool(100,F.Worker));
    F.Worker->Status->Damage(20,FVector::ForwardVector);
    const float PlayerHP=F.Worker->Status->State.Health;
    TestFalse(TEXT("Arena repair never enables baseline player HP healing"),F.Worker->Status->RepairWithTool(10,F.Worker));
    TestEqual(TEXT("The rejected arena-repair path leaves player HP unchanged"),F.Worker->Status->State.Health,PlayerHP);
    F.Worker->Status->Heal(10);
    TestEqual(TEXT("Explicit player/perk healing keeps its original amount"),F.Worker->Status->State.Health,PlayerHP+10.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToothHPStatusSourceTest,"MessControl.Care.Arena.HealthStatusOnlyForIncompleteLivingTooth",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToothHPStatusSourceTest::RunTest(const FString&)
{
    MCToothCareRepairTests::FWorld F;auto* Tooth=F.Tooth();
    auto* Indicator=Tooth->WorldStatus.Get();
    if(!TestTrue(TEXT("Arena teeth use the existing ordinary-enemy status presenter"),Indicator && Indicator->SupportsOwner())) return false;
    FMCWorldStatusSnapshot Value;FVector Anchor;
    TestFalse(TEXT("A full-health tooth has no HP presentation source"),Indicator->ReadSnapshot(Value,Anchor));
    Tooth->SetCoffee(.5f);
    TestTrue(TEXT("The fixture has real coffee needing cleaning"),Tooth->Status->NeedsCare(true));
    TestFalse(TEXT("Dirt alone at full HP does not turn on a health bar"),Indicator->ReadSnapshot(Value,Anchor));
    Tooth->ReceiveArenaHit(20,FVector::ForwardVector);
    if(!TestTrue(TEXT("An available damaged tooth supplies a health snapshot"),Indicator->ReadSnapshot(Value,Anchor))) return false;
    TestTrue(TEXT("The snapshot is an HP-only ordinary status pill"),Value.bShowHealth && !Value.bHasTimer);
    TestEqual(TEXT("The displayed ratio follows actual tooth HP"),Value.HealthFraction,.8f,.001f);
    Tooth->Status->Heal(20);
    TestFalse(TEXT("Returning to full HP removes the presentation source"),Indicator->ReadSnapshot(Value,Anchor));
    Tooth->ReceiveArenaHit(100,FVector::ForwardVector);
    TestFalse(TEXT("A lost tooth cannot retain a world HP bar"),Indicator->ReadSnapshot(Value,Anchor));
    auto* Consumed=F.Tooth();Consumed->ReceiveArenaHit(20,FVector::ForwardVector);Consumed->ConsumeForRespawn();
    TestFalse(TEXT("A consumed reserve cannot retain a world HP bar"),Consumed->WorldStatus->ReadSnapshot(Value,Anchor));
    return true;
}
#endif
