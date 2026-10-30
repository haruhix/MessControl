#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCDefenseSprayPassives.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothMovementComponent.h"
#include "MCInventoryComponent.h"
#include "MCMouthSurface.h"
#include "MCCoffeeWipe.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MCPlayerNameComponent.h"

namespace MCDefenseSprayTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    UDataTable* Table=nullptr;
    AMCToothCharacter* Hero=nullptr;
    AMCPlayerState* Player=nullptr;
    UMCPerkComponent* Perks=nullptr;
    UMCToothMovementComponent* Move=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Game=World->SpawnActor<AMCGameState>();World->SetGameState(Game);
        Game->Phase=EMCShiftPhase::Working;Game->bLobbyWaiting=false;
        Table=NewObject<UDataTable>(GetTransientPackage());Table->RowStruct=FMCPerkDefinition::StaticStruct();
        FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Passive;Row.MaxStacks=1;Row.bRoguePerk=true;
        for(FName ID:{FName(TEXT("B2_ThickEnamel")),FName(TEXT("B2_Shield")),FName(TEXT("B2_SecondWind")),FName(TEXT("B2_HoudiniDash")),
            FName(TEXT("B2_Softener")),FName(TEXT("B2_Antiseptic")),FName(TEXT("B2_LastingFormula"))}) Table->AddRow(ID,Row);
        auto* Controller=World->SpawnActor<AMCPlayerController>();
        Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity);
        Player->Perks->PerkTable=Table;Player->FinishSpawning(FTransform::Identity);
        Controller->SetPlayerState(Player);Player->SetOwner(Controller);Game->AddPlayerState(Player);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        Controller->Possess(Hero);Controller->bShowMouseCursor=false;
        Hero->Status->Initialize(100);Hero->SetActorTickEnabled(false);
        Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());Move->DisableMovement();
        Perks=Player->Perks;Perks->ResetPerks();
    }
    ~FWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Set(FName ID,FName Key,float Value) {Table->FindRow<FMCPerkDefinition>(ID,TEXT("Test tuning"))->Parameters.Add(Key,Value);}
    void Step(float Seconds)
    {
        for(float At=0;At<Seconds-UE_SMALL_NUMBER;At+=.05f) {
            const float Dt=FMath::Min(.05f,Seconds-At);++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,Dt);
            // TimeOnly deliberately omits actor/component/tickable simulation.
            // Exercise these real owners explicitly, without a full gameplay fixture.
            Hero->Inventory->TickComponent(Dt,LEVELTICK_All,nullptr);
            UMCDefenseSprayPassives::ForWorld(Hero)->Tick(Dt);
        }
    }
    AMCMouthSurface* Stain(FVector Point)
    {
        const FTransform Pose(Point);
        auto* Patch=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
        Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=100.f;Patch->FinishSpawning(Pose);Patch->Status->ApplyCoffee(1.f);
        return Patch;
    }
    void Stamina(float Value) {FMCStaminaPredictionState State;State.Value=Value;Move->RestoreStaminaPrediction(State);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDefenseDamageOrderTest,"MessControl.Perks.Remaining.DefenseDamageOrder",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDefenseDamageOrderTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;
    TestTrue(TEXT("Shield is acquired through the real loadout"),F.Perks->ServerGrantPerk(TEXT("B2_Shield")));
    F.Hero->Status->State.Health=25.f;
    TestTrue(TEXT("The hit crossing the threshold is accepted"),F.Hero->Status->Damage(10.f));
    TestEqual(TEXT("A crossing hit keeps its full amount"),F.Hero->Status->State.Health,15.f);
    F.Hero->Status->Damage(10.f);
    TestEqual(TEXT("A hit beginning below20% loses half"),F.Hero->Status->State.Health,10.f);
    F.Hero->Status->Damage(8.f,FVector::ForwardVector,nullptr,EMCRogueDamageKind::Ordinary,false);
    TestEqual(TEXT("Fixed fraction attacks retain new protection"),F.Hero->Status->State.Health,6.f);
    F.Hero->Status->Damage(4.f,FVector::ForwardVector,nullptr,EMCRogueDamageKind::Chainsaw);
    TestEqual(TEXT("A chainsaw crash is ordinary incoming protection"),F.Hero->Status->State.Health,4.f);
    TestTrue(TEXT("Enamel can coexist"),F.Perks->ServerGrantPerk(TEXT("B2_ThickEnamel")));
    F.Set(TEXT("B2_ThickEnamel"),TEXT("Chance"),1.f);
    const double DamageAt=F.Hero->Status->State.DamageAt;
    TestFalse(TEXT("A successful enamel roll rejects before hit reactions"),F.Hero->Status->Damage(50.f));
    TestEqual(TEXT("No health is consumed"),F.Hero->Status->State.Health,4.f);
    TestEqual(TEXT("No damage reaction clock changes"),F.Hero->Status->State.DamageAt,DamageAt);
    F.Set(TEXT("B2_ThickEnamel"),TEXT("Chance"),0.f);
    TestTrue(TEXT("Chance0 never evades"),F.Hero->Status->Damage(2.f));
    TestEqual(TEXT("Shield still resolves after an unsuccessful roll"),F.Hero->Status->State.Health,3.f);
    F.Set(TEXT("B2_ThickEnamel"),TEXT("Chance"),1.f);
    TestTrue(TEXT("System death bypasses both defenses"),F.Hero->Status->Damage(100.f,FVector::UpVector,nullptr,EMCRogueDamageKind::System,false));
    TestFalse(TEXT("System death leaves no live pawn"),F.Hero->Status->IsAlive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDefenseBoundaryTest,"MessControl.Perks.Remaining.DefenseBoundaryBypass",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDefenseBoundaryTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;F.Perks->ServerGrantPerk(TEXT("B2_ThickEnamel"));F.Set(TEXT("B2_ThickEnamel"),TEXT("Chance"),1.f);
    F.Perks->ServerGrantPerk(TEXT("B2_Shield"));F.Hero->Status->State.Health=10.f;
    TestTrue(TEXT("Physical arena failure uses the direct death path"),F.Hero->Status->KillFromArenaBoundary());
    TestFalse(TEXT("No chance or lowHP shield can evade it"),F.Hero->Status->IsAlive());
    TestFalse(TEXT("Repeated boundary samples stay inert"),F.Hero->Status->KillFromArenaBoundary());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEnamelContactLatchTest,"MessControl.Perks.Remaining.EnamelConsumesBossContact",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEnamelContactLatchTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;F.Perks->ServerGrantPerk(TEXT("B2_ThickEnamel"));
    auto* Boss=F.World->SpawnActor<AMCNutBoss>(F.Hero->GetActorLocation()+FVector(80,0,0),FRotator::ZeroRotator);
    Boss->SetActorTickEnabled(false);const FVector Contact=F.Hero->GetActorLocation();
    F.Set(TEXT("B2_ThickEnamel"),TEXT("Chance"),1.f);Boss->DamageChargeSegment(Contact,Contact);
    TestTrue(TEXT("An evaded charge records its physical contact once"),Boss->AttackHits.Contains(F.Hero));
    F.Set(TEXT("B2_ThickEnamel"),TEXT("Chance"),0.f);Boss->DamageChargeSegment(Contact,Contact);
    TestEqual(TEXT("Another charge substep cannot roll that contact again"),F.Hero->Status->State.Health,100.f);
    F.Set(TEXT("B2_ThickEnamel"),TEXT("Chance"),1.f);Boss->DamageRollSegment(Contact,Contact);
    TestTrue(TEXT("An evaded roll starts the normal per-target hit gap"),Boss->RollHitAt.Contains(F.Hero));
    F.Set(TEXT("B2_ThickEnamel"),TEXT("Chance"),0.f);Boss->DamageRollSegment(Contact,Contact);
    TestEqual(TEXT("A repeated roll substep cannot retry inside its gap"),F.Hero->Status->State.Health,100.f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSecondWindReceiptTest,"MessControl.Perks.Remaining.SecondWindConfirmedReceipt",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSecondWindReceiptTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;F.Perks->ServerGrantPerk(TEXT("B2_SecondWind"));F.Move->MaxStamina=200.f;F.Stamina(20.f);
    auto* Source=F.World->SpawnActor<AActor>();
    UMCDefenseSprayPassives::NotifyDashStarted(F.Hero);UMCDefenseSprayPassives::NotifyDodgedAttack(F.Hero,Source,1);
    TestEqual(TEXT("An empty dash never fabricates a threat"),F.Move->GetStamina(),20.f);
    UMCDefenseSprayPassives::RegisterThreat(F.Hero,Source,2);UMCDefenseSprayPassives::NotifyDodgedAttack(F.Hero,Source,2);
    TestEqual(TEXT("Walking out without a dash restores nothing"),F.Move->GetStamina(),20.f);
    UMCDefenseSprayPassives::RegisterThreat(F.Hero,Source,3);UMCDefenseSprayPassives::NotifyDashStarted(F.Hero);
    UMCDefenseSprayPassives::NotifyDodgedAttack(F.Hero,Source,3);UMCDefenseSprayPassives::NotifyDodgedAttack(F.Hero,Source,3);
    TestEqual(TEXT("One confirmed attack restores30% of the actual maximum once"),F.Move->GetStamina(),80.f);
    UMCDefenseSprayPassives::RegisterThreat(F.Hero,Source,4);UMCDefenseSprayPassives::NotifyDashStarted(F.Hero);
    F.Hero->Status->Damage(1.f,FVector::ForwardVector,Source);UMCDefenseSprayPassives::NotifyDodgedAttack(F.Hero,Source,4);
    TestEqual(TEXT("A real hit consumes the outcome before any late miss callback"),F.Move->GetStamina(),80.f);
    UMCDefenseSprayPassives::RegisterThreat(F.Hero,Source,5);UMCDefenseSprayPassives::NotifyDashStarted(F.Hero);
    UMCDefenseSprayPassives::NotifyAttackFinished(Source,5,true);
    TestEqual(TEXT("An interrupted attack is not an evasion"),F.Move->GetStamina(),80.f);
    F.Stamina(190.f);UMCDefenseSprayPassives::RegisterThreat(F.Hero,Source,6);UMCDefenseSprayPassives::NotifyDashStarted(F.Hero);
    UMCDefenseSprayPassives::NotifyAttackFinished(Source,6);
    TestEqual(TEXT("Recovery never exceeds maximum"),F.Move->GetStamina(),200.f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDefenseDashTest,"MessControl.Perks.Remaining.HoudiniIndependentChargesAndVisibility",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDefenseDashTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;
    const float BaseCooldown=F.Move->EffectiveDashCooldown();
    TestEqual(TEXT("The unmodified fixture recharge is exactly3seconds"),BaseCooldown,3.f);
    F.Perks->ServerGrantPerk(TEXT("B2_HoudiniDash"));F.Move->SetMovementMode(MOVE_Walking);
    const float ExpectedCooldown=BaseCooldown*(1.f+.2f);
    TestEqual(TEXT("Houdini increases the effective recharge by exactly20percent"),F.Move->EffectiveDashCooldown(),ExpectedCooldown);
    const bool NameHidden=F.Hero->PlayerNameLabel->bHiddenInGame;
    TestTrue(TEXT("A confirmed native dash starts"),F.Move->StartDash());
    TestTrue(TEXT("Houdini hides the actor during the real root motion dash"),F.Hero->IsHidden());
    TestEqual(TEXT("First charge stores its own increased recharge"),F.Move->GetDashChargeCooldownRemaining(0),ExpectedCooldown);
    F.Move->CancelDash();
    TestFalse(TEXT("Cancellation restores the previous actor visibility"),F.Hero->IsHidden());
    TestEqual(TEXT("The nameplate's own hidden state is preserved"),F.Hero->PlayerNameLabel->bHiddenInGame,NameHidden);
    F.Hero->SetActorHiddenInGame(true);TestTrue(TEXT("A second independent dash starts"),F.Move->StartDash());F.Move->CancelDash();
    TestTrue(TEXT("An already hidden actor stays hidden after the override ends"),F.Hero->IsHidden());
    TestEqual(TEXT("Second charge stores the same independent increased recharge"),F.Move->GetDashChargeCooldownRemaining(1),ExpectedCooldown);
    F.Hero->SetActorHiddenInGame(false);F.Move->RestoreDashPrediction(FMCDashPredictionState());F.Move->StartDash();
    F.Perks->RemovePerk(TEXT("B2_HoudiniDash"));UMCDefenseSprayPassives::UpdateDashVisibility(F.Hero);
    TestFalse(TEXT("Removing the passive restores visibility during a dash"),F.Hero->IsHidden());
    TestEqual(TEXT("An already spent charge is not rewritten by removal"),F.Move->GetDashChargeCooldownRemaining(0),ExpectedCooldown);
    TestEqual(TEXT("New charges return to the ordinary cooldown"),F.Move->EffectiveDashCooldown(),3.f);F.Move->CancelDash();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAntisepticCycleTest,"MessControl.Perks.Remaining.AntisepticWorkingCycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAntisepticCycleTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;auto* Inventory=F.Hero->Inventory.Get();F.Perks->ServerGrantPerk(TEXT("B2_Antiseptic"));
    F.Set(TEXT("B2_Antiseptic"),TEXT("BaseCooldown"),8.f);F.Set(TEXT("B2_Antiseptic"),TEXT("SprayDuration"),1.f);
    Inventory->ServerSelect(EMCToolSlot::Spray);F.Hero->ServerSetPrimary(true);
    TestTrue(TEXT("The actual held input starts an emitting burst"),Inventory->IsSprayCycleEmitting());
    TestTrue(TEXT("A burst reserves a real future readiness deadline"),Inventory->SprayReadyAt>F.World->GetTimeSeconds()+10.f);
    TestEqual(TEXT("The active cooldown applies the30% penalty"),Inventory->CooldownSeconds(),10.4f);
    TestEqual(TEXT("HUD shows no cooldown while the custom burst emits"),Inventory->SpraySecondsLeft(),0.f);
    const double ReservedDeadline=Inventory->SprayReadyAt;
    F.Step(.25f);F.Hero->ServerSetPrimary(false);Inventory->TickComponent(0.f,LEVELTICK_All,nullptr);
    TestFalse(TEXT("Releasing ends emission"),Inventory->IsSprayCycleEmitting());
    const double Deadline=Inventory->SprayReadyAt;
    TestEqual(TEXT("Early release starts cooldown at the actual stop"),Deadline,
        F.World->GetTimeSeconds()+double(Inventory->CooldownSeconds()),.0001);
    TestTrue(TEXT("Early stop does not reserve the unused burst tail"),Deadline<ReservedDeadline);
    TestEqual(TEXT("HUD now shows only the real custom cooldown"),Inventory->SpraySecondsLeft(),Inventory->CooldownSeconds(),.0001f);
    F.Hero->ServerSetPrimary(true);
    TestFalse(TEXT("A fresh press cannot bypass reload"),Inventory->IsSprayCycleEmitting());
    TestEqual(TEXT("A blocked press cannot restart the deadline"),Inventory->SprayReadyAt,Deadline);
    F.Step(.2f);F.Hero->ServerSetPrimary(false);Inventory->TickComponent(0.f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Releasing during cooldown cannot move readiness"),Inventory->SprayReadyAt,Deadline);
    F.Hero->ServerSetPrimary(true);
    TestFalse(TEXT("Repeated tapping still cannot bypass reload"),Inventory->IsSprayCycleEmitting());
    TestEqual(TEXT("Repeated tapping preserves the same deadline"),Inventory->SprayReadyAt,Deadline);
    F.Hero->ServerSetPrimary(false);F.Perks->RemovePerk(TEXT("B2_Antiseptic"));F.Step(.1f);
    TestFalse(TEXT("Removing the passive returns to baseline continuous spray"),Inventory->UsesAntisepticCycle());
    TestEqual(TEXT("Its pending cooldown is cleaned up"),Inventory->SpraySecondsLeft(),0.f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAntisepticHeldCycleTest,"MessControl.Perks.Remaining.AntisepticHeldCycleAndHUD",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAntisepticHeldCycleTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;auto* Inventory=F.Hero->Inventory.Get();
    F.Perks->ServerGrantPerk(TEXT("B2_Antiseptic"));
    // Missing row overrides exercise the native3s/2s production fallbacks.
    // The custom1s/8s case above remains independently designer-authored.
    Inventory->ServerSelect(EMCToolSlot::Spray);const double Started=F.World->GetTimeSeconds();
    F.Hero->ServerSetPrimary(true);Inventory->TickComponent(0.f,LEVELTICK_All,nullptr);
    const double FirstBurstEnd=Inventory->SprayBurstUntil,FirstReady=Inventory->SprayReadyAt;
    TestEqual(TEXT("Native fallback emits for three seconds"),FirstBurstEnd-Started,3.,.0001);
    TestEqual(TEXT("Native fallback cooldown retains the30percent penalty"),Inventory->CooldownSeconds(),2.6f,.0001f);
    TestEqual(TEXT("Full burst reserves end plus cooldown"),FirstReady,FirstBurstEnd+double(Inventory->CooldownSeconds()),.0001);
    TestEqual(TEXT("HUD stays at zero during actual emission"),Inventory->SpraySecondsLeft(),0.f);
    F.Step(2.9f);
    TestTrue(TEXT("Held input still emits near the end of its real burst"),Inventory->IsSprayCycleEmitting());
    TestEqual(TEXT("Late active burst still has no cooldown HUD"),Inventory->SpraySecondsLeft(),0.f);
    F.Step(.2f);
    TestFalse(TEXT("A full held burst stops naturally for cooldown"),Inventory->IsSprayCycleEmitting());
    TestEqual(TEXT("Natural completion keeps its original readiness"),Inventory->SprayReadyAt,FirstReady);
    TestEqual(TEXT("Cooldown HUD excludes the completed burst"),Inventory->SpraySecondsLeft(),
        float(FirstReady-F.World->GetTimeSeconds()),.0001f);
    F.Hero->ServerSetPrimary(false);Inventory->TickComponent(0.f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Release after full completion cannot extend cooldown"),Inventory->SprayReadyAt,FirstReady);
    F.Hero->ServerSetPrimary(true);
    TestFalse(TEXT("Pressing within that cooldown remains blocked"),Inventory->IsSprayCycleEmitting());
    F.Step(float(FirstReady-F.World->GetTimeSeconds())+.1f);
    TestTrue(TEXT("Continuously held input automatically starts the next ready burst"),Inventory->IsSprayCycleEmitting());
    TestTrue(TEXT("The next burst advances its native deadline"),Inventory->SprayBurstUntil>FirstBurstEnd);
    TestEqual(TEXT("A repeated full cycle still reserves end plus cooldown"),Inventory->SprayReadyAt,
        Inventory->SprayBurstUntil+double(Inventory->CooldownSeconds()),.0001);
    TestEqual(TEXT("HUD returns to zero for the new emitting burst"),Inventory->SpraySecondsLeft(),0.f);
    F.Perks->RemovePerk(TEXT("B2_Antiseptic"));F.Step(.1f);
    TestFalse(TEXT("Removing during a repeated burst restores baseline spray"),Inventory->UsesAntisepticCycle());
    TestEqual(TEXT("Removal clears pending burst state"),Inventory->SprayBurstUntil,0.);
    TestEqual(TEXT("Removal clears the HUD cooldown"),Inventory->SpraySecondsLeft(),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHoudiniProxyOwnershipTest,"MessControl.Perks.Remaining.HoudiniProxyDoesNotOwnHiddenFlag",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHoudiniProxyOwnershipTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;F.Perks->ServerGrantPerk(TEXT("B2_HoudiniDash"));F.Move->SetMovementMode(MOVE_Walking);
    F.Hero->SetRole(ROLE_SimulatedProxy);
    F.Hero->SetActorHiddenInGame(true); // Simulate bHidden arriving before the dash timestamp.
    UMCDefenseSprayPassives::UpdateDashVisibility(F.Hero);
    F.Move->DashStartedAt=F.World->GetTimeSeconds();
    F.Move->ProcessEvent(F.Move->FindFunctionChecked(TEXT("OnRep_DashStartedAt")),nullptr);
    UMCDefenseSprayPassives::UpdateDashVisibility(F.Hero);
    TestTrue(TEXT("A proxy leaves the authority's hidden receipt intact"),F.Hero->IsHidden());
    F.Move->DashStartedAt=-100;F.Move->ProcessEvent(F.Move->FindFunctionChecked(TEXT("OnRep_DashStartedAt")),nullptr);
    F.Hero->SetActorHiddenInGame(false); // Simulate the later authoritative visible receipt.
    UMCDefenseSprayPassives::UpdateDashVisibility(F.Hero);
    TestFalse(TEXT("A proxy never restores a stale server-hidden snapshot"),F.Hero->IsHidden());
    F.Hero->SetRole(ROLE_AutonomousProxy);F.Hero->SetActorHiddenInGame(true);
    UMCDefenseSprayPassives::UpdateDashVisibility(F.Hero);
    TestTrue(TEXT("The autonomous owner also leaves hidden authority intact"),F.Hero->IsHidden());
    F.Hero->SetActorHiddenInGame(false);UMCDefenseSprayPassives::UpdateDashVisibility(F.Hero);
    TestFalse(TEXT("The autonomous owner never owns the replicated flag"),F.Hero->IsHidden());
    F.Hero->SetRole(ROLE_Authority);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSoftenerLocalityTest,"MessControl.Perks.Remaining.SoftenerPlaceAndLifetime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSoftenerLocalityTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;F.Perks->ServerGrantPerk(TEXT("B2_Softener"));
    auto* Patch=F.Stain(FVector(300,0,10000));const FVector Point=Patch->GetActorLocation();
    UMCDefenseSprayPassives::ApplySpray(F.Hero,Point+FVector(0,0,100),-FVector::UpVector,235.f,.1f);
    TestEqual(TEXT("The sprayed disk gains40% brush rate"),UMCDefenseSprayPassives::BrushMultiplier(Patch,Point),1.4f);
    TestEqual(TEXT("A different place on the same stain stays ordinary"),UMCDefenseSprayPassives::BrushMultiplier(Patch,Point+FVector(90,0,0)),1.f);
    auto* Other=F.Stain(Point+FVector(300,0,0));
    TestEqual(TEXT("An untreated stain stays ordinary"),UMCDefenseSprayPassives::BrushMultiplier(Other,Other->GetActorLocation()),1.f);
    F.Step(5.1f);TestEqual(TEXT("A five-second treatment expires"),UMCDefenseSprayPassives::BrushMultiplier(Patch,Point),1.f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCLastingFormulaTest,"MessControl.Perks.Remaining.LastingFormulaCleaningAndSelfRisk",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCLastingFormulaTest::RunTest(const FString&)
{
    MCDefenseSprayTests::FWorld F;F.Perks->ServerGrantPerk(TEXT("B2_LastingFormula"));
    auto* Patch=F.Stain(FVector(300,0,10000));const FVector Point=Patch->GetActorLocation();
    F.Hero->SetActorLocation(Point+FVector(0,0,F.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    const float Before=Patch->RemainingLiquid();
    UMCDefenseSprayPassives::ApplySpray(F.Hero,Point+FVector(0,0,100),-FVector::UpVector,235.f,.1f);
    F.Step(1.f);
    TestTrue(TEXT("Residual composition actually removes mask dirt after release"),Patch->RemainingLiquid()<Before);
    TestTrue(TEXT("Standing in one's own coating deals ordinary HP damage"),F.Hero->Status->State.Health<100.f);
    F.Hero->SetActorLocation(Point+FVector(150,0,F.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    const float Safe=F.Hero->Status->State.Health;F.Step(.6f);
    TestEqual(TEXT("Leaving the actual disk stops its damage"),F.Hero->Status->State.Health,Safe);
    F.Step(2.f);const auto AfterResidue=Patch->WipeMask;F.Step(1.f);
    TestTrue(TEXT("The three-second cleaning deadline ends mask work"),Patch->WipeMask==AfterResidue);
    TestTrue(TEXT("The risk visual outlives the cleaning window"),bool(TActorIterator<AMCSprayCoating>(F.World)));
    F.Perks->RemovePerk(TEXT("B2_LastingFormula"));UMCDefenseSprayPassives::PruneOwner(F.Perks);
    TestFalse(TEXT("Removing the passive immediately destroys its temporary coating"),bool(TActorIterator<AMCSprayCoating>(F.World)));
    F.Perks->ServerGrantPerk(TEXT("B2_LastingFormula"));
    TestFalse(TEXT("Immediate regrant cannot resurrect an old coating"),bool(TActorIterator<AMCSprayCoating>(F.World)));return true;
}
#endif
