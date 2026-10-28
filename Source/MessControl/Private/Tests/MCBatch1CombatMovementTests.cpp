#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "MCInventoryComponent.h"
#include "MCNutEnemy.h"
#include "MCColdCola.h"
#include "MCSmallMintCandy.h"
#include "../MCMeleeCombo.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace MCBatch1CombatMovementTests
{
constexpr float StepSeconds=1.f/60.f;
const FName HeavyHit(TEXT("B1_HeavyHit"));
const FName LightFoot(TEXT("B1_LightFoot"));
const FName DeepBreath(TEXT("B1_DeepBreath"));

struct FWorldFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    APlayerController* Controller=nullptr;
    AMCPlayerState* Player=nullptr;
    AMCToothCharacter* Hero=nullptr;
    UMCToothMovementComponent* Move=nullptr;
    UMCPerkComponent* Perks=nullptr;
    FWorldFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Game=World->SpawnActor<AMCGameState>();World->SetGameState(Game);
        Game->Phase=EMCShiftPhase::Working;Game->bLobbyWaiting=false;
        auto* Floor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Shape);Shape->SetBoxExtent(FVector(5000,5000,20));
        Shape->SetCollisionProfileName(TEXT("BlockAll"));Shape->RegisterComponent();Floor->SetActorLocation(FVector(0,0,10000));
        // A generic local controller provides real possession/PlayerState
        // without a viewport, frontend widgets or a loading-screen barrier.
        Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();
        Player=World->SpawnActor<AMCPlayerState>();Player->SetOwner(Controller);
        Controller->SetPlayerState(Player);Game->AddPlayerState(Player);Controller->bShowMouseCursor=false;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10082),FRotator::ZeroRotator);
        Controller->Possess(Hero);Hero->Status->Initialize(100);Perks=Player->Perks;
        Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
        Tick(.2f);
    }
    ~FWorldFixture()
    {
        MCMeleeCombo::Reset(Hero);
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    void Tick(float Seconds)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds/StepSeconds);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,StepSeconds);}
    }
    bool Ready() const
    {return Hero->CanWork() && Hero->IsLocallyControlled() && Move->IsMovingOnGround() && Perks && Perks->GetPerkTable();}
    void Stamina(float Value,float RecoveryDelay=0)
    {FMCStaminaPredictionState State;State.Value=Value;State.RecoveryDelay=RecoveryDelay;Move->RestoreStaminaPrediction(State);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1LightFootTest,"MessControl.Passives.Batch1.LightFootGroundSpeedAndRealStaminaCosts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1LightFootTest::RunTest(const FString&)
{
    using namespace MCBatch1CombatMovementTests;
    FWorldFixture F;
    if(!TestTrue(TEXT("Native player and saved catalog are ready on real support"),F.Ready())) return false;
    const float Walk=F.Move->GetMaxSpeed();
    F.Move->SetSprinting(true);const float Sprint=F.Move->GetMaxSpeed();F.Move->SetSprinting(false);
    FMCBraceMovementState Load;Load.LoadMass=35.f;F.Move->RestoreBraceForMove(Load);
    const float Loaded=F.Move->GetMaxSpeed();F.Move->RestoreBraceForMove(FMCBraceMovementState());
    F.Stamina(60);F.Move->AdvanceStaminaPrediction(.5f,false,false);const float Recovery=F.Hero->GetStamina();
    FMCSwimPredictionState Swim;Swim.Value=60;F.Move->RestoreSwimPrediction(Swim);
    F.Move->AdvanceSwimPrediction(1.f,true,false,.5f);const float SwimAfter=F.Move->GetSwimEnergy();
    if(!TestTrue(TEXT("The actual new LightFoot row can be acquired"),F.Perks->TryAcquirePerk(LightFoot))) return false;
    TestTrue(TEXT("Walk limit gains eight percent"),FMath::IsNearlyEqual(F.Move->GetMaxSpeed(),Walk*1.08f,.01f));
    F.Move->SetSprinting(true);
    TestTrue(TEXT("Run limit gains eight percent"),FMath::IsNearlyEqual(F.Move->GetMaxSpeed(),Sprint*1.08f,.01f));
    F.Move->SetSprinting(false);F.Move->RestoreBraceForMove(Load);
    TestTrue(TEXT("Load penalties do not suppress the proportional bonus"),FMath::IsNearlyEqual(F.Move->GetMaxSpeed(),Loaded*1.08f,.01f));
    F.Move->RestoreBraceForMove(FMCBraceMovementState());
    F.Stamina(100);F.Move->AdvanceStaminaPrediction(1.f,true,false);
    TestTrue(TEXT("The real sprint ledger spends twelve times 1.08, not extra recovery"),
        FMath::IsNearlyEqual(F.Hero->GetStamina(),100.f-F.Move->SprintStaminaPerSecond*1.08f,.001f));
    F.Stamina(60);F.Move->AdvanceStaminaPrediction(.5f,false,false);
    TestTrue(TEXT("Ordinary regeneration is unchanged"),FMath::IsNearlyEqual(F.Hero->GetStamina(),Recovery,.001f));
    F.Move->RestoreSwimPrediction(Swim);F.Move->AdvanceSwimPrediction(1.f,true,false,.5f);
    TestTrue(TEXT("Separate swimming energy is unchanged"),FMath::IsNearlyEqual(F.Move->GetSwimEnergy(),SwimAfter,.001f));
    const float Cost=F.Move->DashStaminaCost*1.08f;
    TestTrue(TEXT("The effective dash cost uses the same penalty"),FMath::IsNearlyEqual(F.Move->EffectiveDashStaminaCost(),Cost,.001f));
    F.Stamina(Cost-.1f);
    TestFalse(TEXT("A charge cannot be accepted with only the unmodified stamina amount"),F.Move->CanDash());
    F.Stamina(100);const int32 Charges=F.Move->GetDashCharges();
    F.Hero->RequestDashInput();F.Tick(StepSeconds);
    TestTrue(TEXT("Fresh Ctrl starts a real movement dash"),F.Hero->IsDashing());
    TestEqual(TEXT("That movement spends one dash charge"),F.Move->GetDashCharges(),Charges-1);
    TestTrue(TEXT("The accepted dash spends the checked adjusted cost once"),FMath::IsNearlyEqual(F.Hero->GetStamina(),100.f-Cost,.01f));
    F.Hero->CancelGameplayInput();F.Perks->RemovePerk(LightFoot);
    TestTrue(TEXT("Removing the new passive restores the base cost"),FMath::IsNearlyEqual(F.Move->EffectiveDashStaminaCost(),F.Move->DashStaminaCost,.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1DeepBreathTest,"MessControl.Passives.Batch1.DeepBreathPreservesCurrentAmountAndClampsRemoval",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1DeepBreathTest::RunTest(const FString&)
{
    using namespace MCBatch1CombatMovementTests;
    FWorldFixture F;if(!TestTrue(TEXT("The saved runtime fixture is ready"),F.Ready())) return false;
    const float BaseMaximum=F.Move->GetMaxStamina();F.Stamina(80);
    if(!TestTrue(TEXT("DeepBreath can be acquired through normal ownership"),F.Perks->TryAcquirePerk(DeepBreath))) return false;
    TestTrue(TEXT("Capacity grows twenty percent"),FMath::IsNearlyEqual(F.Move->GetMaxStamina(),BaseMaximum*1.2f,.001f));
    TestTrue(TEXT("Receiving capacity does not refill existing stamina"),FMath::IsNearlyEqual(F.Hero->GetStamina(),80.f,.001f));
    TestFalse(TEXT("The same passive cannot stack by duplicate acquisition"),F.Perks->TryAcquirePerk(DeepBreath));
    F.Stamina(F.Move->GetMaxStamina());
    if(!TestTrue(TEXT("Removing a capacity passive succeeds"),F.Perks->RemovePerk(DeepBreath))) return false;
    TestTrue(TEXT("The public amount is capped immediately"),FMath::IsNearlyEqual(F.Hero->GetStamina(),BaseMaximum,.001f));
    F.Move->AdvanceStaminaPrediction(.25f,true,false);
    const float Expected=BaseMaximum-F.Move->SprintStaminaPerSecond*.25f;
    TestTrue(TEXT("No invisible over-cap energy absorbs the next real expenditure"),FMath::IsNearlyEqual(F.Hero->GetStamina(),Expected,.001f));
    TestTrue(TEXT("The movement snapshot is capped too"),FMath::IsNearlyEqual(F.Move->GetStaminaPrediction().Value,Expected,.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1HeavyHitTest,"MessControl.Passives.Batch1.HeavyHitDamageAndAcceptedComboCadence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1HeavyHitTest::RunTest(const FString&)
{
    using namespace MCBatch1CombatMovementTests;
    FWorldFixture F;if(!TestTrue(TEXT("The saved runtime fixture is ready"),F.Ready())) return false;
    F.Hero->Inventory->Selected=EMCToolSlot::Knife;
    MCMeleeCombo::BeginSwing(F.Hero,true);const float BaseDuration=MCMeleeCombo::Duration(F.Hero);MCMeleeCombo::Reset(F.Hero);
    if(!TestTrue(TEXT("The actual new HeavyHit row can be acquired"),F.Perks->TryAcquirePerk(HeavyHit))) return false;
    TestTrue(TEXT("Attack frequency is thirty percent lower"),FMath::IsNearlyEqual(F.Hero->Inventory->RogueAttackRate(),.7f,.001f));
    auto* Enemy=F.World->SpawnActor<AMCNutEnemy>(F.Hero->GetActorLocation()+FVector(900,0,0),FRotator::ZeroRotator);
    Enemy->Health=1000;
    TestTrue(TEXT("Actual enemy HP loses a single forty-percent-boosted hit"),FMath::IsNearlyEqual(Enemy->ReceiveToolDamage(100,F.Hero),140.f,.01f));
    TestTrue(TEXT("The receiver applies the multiplier exactly once"),FMath::IsNearlyEqual(Enemy->Health,860.f,.01f));
    Enemy->Destroy();
    const int32 BeforeSwings=F.Hero->ValidatedSwingCount;
    F.Hero->SwingBrush();const float Slower=MCMeleeCombo::Duration(F.Hero);
    if(!TestEqual(TEXT("The first real press starts one accepted swing"),F.Hero->ValidatedSwingCount,BeforeSwings+1)) return false;
    TestTrue(TEXT("The animation duration is divided by frequency, not merely increased thirty percent"),FMath::IsNearlyEqual(Slower,BaseDuration/.7f,.001f));
    // Tick owns buffer consumption in the live character. Observe accepted
    // swings instead of consuming the same pending press a second time here.
    F.Hero->SwingBrush();F.Tick(BaseDuration+.01f);
    TestEqual(TEXT("A buffered second hit is still rejected at the old interval"),F.Hero->ValidatedSwingCount,BeforeSwings+1);
    F.Tick(Slower-BaseDuration+.02f);
    TestEqual(TEXT("The character accepts the buffered hit after the adjusted interval"),F.Hero->ValidatedSwingCount,BeforeSwings+2);
    F.Tick(MCMeleeCombo::Duration(F.Hero)+.45f);
    TestEqual(TEXT("One queued press never repeats into a third swing"),F.Hero->ValidatedSwingCount,BeforeSwings+2);
    MCMeleeCombo::Reset(F.Hero);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1HeavyDestructiblesTest,"MessControl.Passives.Batch1.HeavyHitScalesRealIceAndMintHealthOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1HeavyDestructiblesTest::RunTest(const FString&)
{
    using namespace MCBatch1CombatMovementTests;
    FWorldFixture F;if(!TestTrue(TEXT("The saved runtime fixture is ready"),F.Ready())) return false;
    F.Hero->Inventory->Selected=EMCToolSlot::Pickaxe;
    if(!TestTrue(TEXT("HeavyHit can be acquired"),F.Perks->TryAcquirePerk(HeavyHit))) return false;
    const FTransform Pose(F.Hero->GetActorLocation()+FVector(130,0,0));
    auto* Ice=F.World->SpawnActorDeferred<AMCIceBlock>(AMCIceBlock::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Ice->Health=Ice->MaxHealth=1000;Ice->Size=FVector(50);Ice->FinishSpawning(Pose);
    if(!TestTrue(TEXT("The same real nearby ice contact is accepted"),Ice->HitWithPickaxe(F.Hero,100))) return false;
    TestTrue(TEXT("Ice HP receives one forty-percent bonus"),FMath::IsNearlyEqual(Ice->Health,860.f,.01f));
    Ice->Destroy();
    auto* Mint=F.World->SpawnActor<AMCSmallMintCandy>(Pose.GetLocation(),FRotator::ZeroRotator);
    Mint->Stage=EMCSmallMintStage::Active;Mint->Health=Mint->MaxHealth=1000;
    if(!TestTrue(TEXT("The native mint contact is accepted"),Mint->HitWithPickaxe(F.Hero,100))) return false;
    TestTrue(TEXT("Mint HP receives one forty-percent bonus"),FMath::IsNearlyEqual(Mint->Health,860.f,.01f));
    return true;
}
#endif
