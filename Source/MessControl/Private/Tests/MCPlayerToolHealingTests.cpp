#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCExpressionComponent.h"
#include "MCGameState.h"
#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MCPlayerToolHealingTestsPrivate
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Worker=nullptr;
    AMCToothCharacter* Teammate=nullptr;

    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        auto* State=World->SpawnActor<AMCGameState>();
        State->Phase=EMCShiftPhase::Working; State->bLobbyWaiting=false;
        World->SetGameState(State);
        Worker=SpawnWorker(FVector(0,5000,10000));
        Teammate=SpawnWorker(FVector(160,5000,10000));
        Step(.1f);
    }
    ~FFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    AMCToothCharacter* SpawnWorker(FVector Position)
    {
        auto* Controller=World->SpawnActor<AMCPlayerController>();
        Controller->SetPlayerState(World->SpawnActor<AMCPlayerState>());
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Position,FRotator::ZeroRotator);
        Controller->Possess(Hero); Hero->GetCharacterMovement()->DisableMovement();
        return Hero;
    }
    void Step(float Seconds)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I)
        { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDefaultPlayerCareContactTest,
    "MessControl.PlayerCare.DefaultSelfAndAllyToolContactsCannotHeal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDefaultPlayerCareContactTest::RunTest(const FString&)
{
    MCPlayerToolHealingTestsPrivate::FFixture F;
    auto* Profile=F.Worker->Status->Profile.LoadSynchronous();
    if(!TestNotNull(TEXT("The ordinary character loads its real saved tooth care profile"),Profile)) return false;
    TestEqual(TEXT("Spawned health comes from the saved care profile"),
        F.Worker->Status->State.MaxHealth,Profile->Settings.PlayerHealth);
    for(auto* Target:{F.Worker,F.Teammate})
    {
        if(!TestTrue(TEXT("Fixture receives real nonlethal damage"),
            Target->Status->Damage(Target->Status->State.MaxHealth*.7f))) return false;
        const FMCToothStatus Before=Target->Status->State;
        TestTrue(TEXT("Fixture includes the ordinary loose/repair condition"),Before.RepairLeft>0);
        TestFalse(TEXT("A damaged character is not a default tool-healing target"),Target->Status->NeedsCare(false));
        TestFalse(TEXT("Self contact cannot restore character HP"),Target->Status->CareContact(false,Target));
        auto* Other=Target==F.Worker?F.Teammate:F.Worker;
        TestFalse(TEXT("A teammate contact cannot restore character HP"),Target->Status->CareContact(false,Other));
        TestFalse(TEXT("A caller without a worker cannot bypass the default rule"),Target->Status->CareContact(false,nullptr));
        TestEqual(TEXT("Rejected tool care preserves real damaged HP"),Target->Status->State.Health,Before.Health);
        TestEqual(TEXT("Rejected tool care cannot clear the repair condition"),Target->Status->State.RepairLeft,Before.RepairLeft);
        TestEqual(TEXT("Rejected tool care produces no fake reaction"),Target->Status->State.ReactionAt,Before.ReactionAt);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerSelfBrushKeepsHealthTest,
    "MessControl.PlayerCare.TimedSelfBrushCleansWithoutHealing",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerSelfBrushKeepsHealthTest::RunTest(const FString&)
{
    MCPlayerToolHealingTestsPrivate::FFixture F;
    F.Teammate->SetActorLocation(FVector(1000,5000,10000));
    if(!TestTrue(TEXT("The worker receives ordinary damage"),
        F.Worker->Status->Damage(F.Worker->Status->State.MaxHealth*.7f))) return false;
    F.Worker->Status->ApplyCoffee();
    const int32 Layers=F.Worker->Status->State.CoffeeLeft;
    const float Health=F.Worker->Status->State.Health;
    const int32 Repair=F.Worker->Status->State.RepairLeft;
    const float ContactSeconds=F.Worker->Status->Settings.ContactSeconds;
    if(!TestTrue(TEXT("The saved profile allows self-cleaning of actual coffee layers"),
        Layers>0 && F.Worker->Status->Settings.bAllowSelfCare)) return false;
    F.Worker->SetSelfCareInput(true);
    F.Worker->SetPrimaryInputHeld(true);
    F.Step(ContactSeconds*.4f);
    TestEqual(TEXT("A partial held brush contact cannot instantly clear coffee"),F.Worker->Status->State.CoffeeLeft,Layers);
    F.Step(ContactSeconds*(Layers+.5f));
    TestEqual(TEXT("Ordinary held input and elapsed contacts remove personal coffee"),F.Worker->Status->State.CoffeeLeft,0);
    TestEqual(TEXT("Self-cleaning cannot restore player HP"),F.Worker->Status->State.Health,Health);
    TestEqual(TEXT("The brush cannot repair damage while removing coffee"),F.Worker->Status->State.RepairLeft,Repair);
    TestFalse(TEXT("Completing self-cleaning returns the still-held tool to the environment"),F.Worker->bSelfCare);
    F.Worker->SetPrimaryInputHeld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerSprayAndWatergunCareTest,
    "MessControl.PlayerCare.AimedSprayAndWatergunDoNotRestoreCharacterHP",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerSprayAndWatergunCareTest::RunTest(const FString&)
{
    MCPlayerToolHealingTestsPrivate::FFixture F;
    for(auto* Target:{F.Worker,F.Teammate})
        if(!TestTrue(TEXT("Both workers have real nonlethal damage before treatment"),
            Target->Status->Damage(Target->Status->State.MaxHealth*.35f))) return false;
    const float WorkerHealth=F.Worker->Status->State.Health,TeammateHealth=F.Teammate->Status->State.Health;
    auto* Inventory=F.Worker->Inventory.Get();
    Inventory->ServerSelect(EMCToolSlot::Spray);
    auto HoldAimedCare=[&](const TCHAR* Context)
    {
        // A controller without a LocalPlayer uses the same authoritative body
        // facing fallback as playtest AI. Keep normal aim resolution and verify
        // actual nozzle contact below instead of injecting a private RPC view.
        F.Worker->SetActorRotation((F.Teammate->GetActorLocation()-F.Worker->GetActorLocation()).Rotation());
        F.Worker->SetPrimaryInputHeld(true);
        F.Step(2.f);
        TestTrue(*FString::Printf(TEXT("%s exercises held care input with the visible selected tool"),Context),
            F.Worker->IsPrimaryHeld() && Inventory->ShouldPresentTool() && Inventory->Selected==EMCToolSlot::Spray && !Inventory->bPressureMode);
        TestEqual(*FString::Printf(TEXT("%s physically reaches the teammate and causes the ordinary social response"),Context),
            F.Teammate->Expression->SprayState.Source.Get(),F.Worker);
        TestEqual(*FString::Printf(TEXT("%s cannot restore the user's HP"),Context),F.Worker->Status->State.Health,WorkerHealth);
        TestEqual(*FString::Printf(TEXT("%s cannot restore the teammate's HP"),Context),F.Teammate->Status->State.Health,TeammateHealth);
        F.Worker->SetPrimaryInputHeld(false);
    };
    HoldAimedCare(TEXT("Default spray"));
    auto* Perks=F.Worker->GetPlayerState<AMCPlayerState>()->Perks.Get();
    if(!TestTrue(TEXT("The watergun is granted through the ordinary tool reward path"),
        Perks->ServerGrantReward(TEXT("Tool_Watergun_Rare")))) return false;
    if(!TestTrue(TEXT("The worker really owns the watergun upgrade"),Inventory->HasUpgrade(EMCToolUpgrade::Watergun))) return false;
    // The authored watergun has a much longer muzzle than the spray can.
    // Keep its target beyond the nozzle, inside its ordinary ten-metre care
    // reach; the social-contact assertion still proves real beam convergence.
    F.Teammate->SetActorLocation(F.Worker->GetActorLocation()+FVector(600,0,0));
    F.Teammate->Expression->SprayState.Source=nullptr;
    HoldAimedCare(TEXT("Upgraded watergun care mode"));
    return true;
}
#endif
