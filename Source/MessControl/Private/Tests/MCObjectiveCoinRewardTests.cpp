#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCHubEconomyComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCPlaytestBotController.h"
#include "MCPerkComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCSingleDayDirector.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

namespace MCObjectiveCoinFixtures
{
/** Real authoritative mode, controllers, wallets and purchase logic; no viewport,
 * network transport or persistent profile writes. Profiles are seeded in memory. */
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameMode* Mode=nullptr;
    AMCGameState* Game=nullptr;
    UDataTable* Catalog=nullptr;
    UDataTable* Rewards=nullptr;
    UDataTable* Rules=nullptr;
    UDataTable* Perks=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        // Deliberately no GameInstance::Init: no platform subsystem/profile I/O.
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
        World->SetGameMode(URL);Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseSingleDayLoop=Mode->bUseDayOnePlan=Mode->bUseAdaptiveDirector=false;
        auto* RunRules=NewObject<UMCRunRules>(World);RunRules->Settings.InitialArenaTeeth=1;
        Mode->RunRulesProfile=RunRules;
        World->InitializeActorsForPlay(URL);World->BeginPlay();Mode->SetActorTickEnabled(false);
        Game=World->GetGameState<AMCGameState>();
        Game->bTutorialActive=Game->bLobbyWaiting=Game->bDevManualEvents=false;
        Game->Phase=EMCShiftPhase::Working;
        // Test the currency path with no legacy chest director available.
        Mode->RoguelikeDirector=nullptr;
        Catalog=NewObject<UDataTable>(World);Catalog->RowStruct=FMCHubCatalogRow::StaticStruct();
        Rewards=NewObject<UDataTable>(World);Rewards->RowStruct=FMCGrillRewardRow::StaticStruct();
        Rules=NewObject<UDataTable>(World);Rules->RowStruct=FMCHubEconomyRulesRow::StaticStruct();
        Rules->AddRow(TEXT("Default"),FMCHubEconomyRulesRow());
        Perks=NewObject<UDataTable>(World);Perks->RowStruct=FMCPerkDefinition::StaticStruct();
        FMCPerkDefinition Perk;Perk.MaxStacks=10;Perk.Polarity=EMCPerkPolarity::Positive;
        Perks->AddRow(TEXT("CoinTestPerk"),Perk);
        FMCHubCatalogRow Booster;Booster.Kind=EMCHubCatalogKind::NextRunBooster;
        Booster.PerkID=TEXT("CoinTestPerk");Booster.Price=7;Booster.MaximumQueuedStacks=10;
        Catalog->AddRow(TEXT("CoinTestBooster"),Booster);
    }
    ~FWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCPlayerState* SeedState(AController* Controller)
    {
        if(auto* Old=Controller->GetPlayerState<AMCPlayerState>()) {Controller->SetPlayerState(nullptr);Old->Destroy();}
        auto* State=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,Controller);
        State->Economy->CatalogTable=Catalog;State->Economy->GrillRewardsTable=Rewards;State->Economy->RulesTable=Rules;
        State->Perks->PerkTable=Perks;State->FinishSpawning(FTransform::Identity);
        State->bHubProfileLoaded=true;Controller->SetPlayerState(State);
        return State;
    }
    AMCPlayerController* Human(bool Local=false)
    {
        auto* PC=World->SpawnActor<AMCPlayerController>();PC->SetActorTickEnabled(false);
        if(Local) PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));
        SeedState(PC);return PC;
    }
    AMCPlayerState* Bot()
    {
        auto* Controller=World->SpawnActor<AMCPlaytestBotController>();Controller->SetActorTickEnabled(false);
        return SeedState(Controller);
    }
    void CoinCap(int32 Maximum)
    {
        auto Row=*Rules->FindRow<FMCHubEconomyRulesRow>(TEXT("Default"),TEXT("Objective coin tests"));
        Row.MaximumCoins=Maximum;Rules->AddRow(TEXT("Default"),Row);
    }
    void Teeth(int32 Count)
    {
        for(AMCArenaTooth* Tooth:Game->ArenaTeeth) if(IsValid(Tooth)) Tooth->Destroy();
        Game->ArenaTeeth.Reset();
        for(int32 I=0;I<Count;++I) {
            const FTransform Pose(FVector(I*200,5000,10000));
            auto* Tooth=World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose);
            Tooth->Initialize(I+1,FMCArenaToothSettings());Tooth->FinishSpawning(Pose);
            Tooth->SetActorTickEnabled(false);Game->ArenaTeeth.Add(Tooth);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCObjectiveCoinTeamOnceTest,"MessControl.Economy.ObjectiveCoins.PersonalOnceAndSpendable",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCObjectiveCoinTeamOnceTest::RunTest(const FString&)
{
    MCObjectiveCoinFixtures::FWorld F;
    auto* Host=F.Human(true);auto* Remote=F.Human();auto* Spectator=F.Human();auto* Bot=F.Bot();
    auto* A=Host->GetPlayerState<AMCPlayerState>();auto* B=Remote->GetPlayerState<AMCPlayerState>();
    Spectator->GetPlayerState<AMCPlayerState>()->SetIsSpectator(true);
    TestTrue(TEXT("Fixture owns real human PlayerStates in the authoritative game mode"),F.Mode==F.World->GetAuthGameMode()
        && A->GetOwner()==Host && B->GetOwner()==Remote && AMCGameMode::IsGameplayParticipant(Remote));
    TestTrue(TEXT("The second participant has no LocalPlayer; payout must not be host-only"),Remote->GetLocalPlayer()==nullptr);
    TestEqual(TEXT("First personal starting balance is credited through the real authority API"),A->Economy->CreditObjectiveCoins(17),17);
    TestEqual(TEXT("Second personal balance is different"),B->Economy->CreditObjectiveCoins(5),5);
    auto* Dead=F.World->SpawnActor<AMCToothCharacter>(FVector(0,0,3000),FRotator::ZeroRotator);
    Remote->Possess(Dead);Dead->Status->Initialize(100);
    if(!TestTrue(TEXT("Second real human is awaiting replacement after a real lethal hit"),Dead->Status->Damage(1000)
        && !Dead->Status->IsAlive() && Remote->GetPawn()==Dead)) return false;
    TestTrue(TEXT("A completed shared task accepts one coin receipt"),F.Mode->AwardObjectiveCoinsOnce(TEXT("OpeningPortion_1")));
    TestEqual(TEXT("Host receives ten personal purchase coins"),A->Economy->Coins,27);
    TestEqual(TEXT("Dead remote participant also receives ten personal purchase coins"),B->Economy->Coins,15);
    TestEqual(TEXT("Spectator receives no objective money"),Spectator->GetPlayerState<AMCPlayerState>()->Economy->Coins,0);
    TestEqual(TEXT("Diagnostic allied bot receives no persistent economy money"),Bot->Economy->Coins,0);
    TestFalse(TEXT("A repeated completion ID is rejected"),F.Mode->AwardObjectiveCoinsOnce(TEXT("OpeningPortion_1")));
    TestEqual(TEXT("Duplicate callback cannot mint another reward for host"),A->Economy->Coins,27);
    TestEqual(TEXT("Duplicate callback cannot mint another reward for remote"),B->Economy->Coins,15);
    F.Mode->NotifyObjectiveCompleted(TEXT("LegacyDay_1"));
    TestEqual(TEXT("Generic objective currency works without RoguelikeDirector"),A->Economy->Coins,37);
    TestEqual(TEXT("Generic objective still fans out to every human"),B->Economy->Coins,25);
    F.Mode->NotifyObjectiveCompleted(TEXT("LegacyDay_1"));
    TestEqual(TEXT("Generic repeated notification is idempotent"),A->Economy->Coins,37);
    TestEqual(TEXT("Profile/export data contains that same spendable wallet"),A->Economy->ExportSave().Coins,37);
    // Purchase is deliberately performed in the actual Hub context, using the
    // same wallet credited during the arena, without enabling suspended calcium.
    auto* Hub=F.World->SpawnActor<AMCHubGameState>();Hub->SetPhase(EMCHubPhase::Gathering);F.World->SetGameState(Hub);
    FText Error;
    TestTrue(TEXT("Earned objective coins buy a real queued booster through normal purchase validation"),A->Economy->TryPurchase(TEXT("CoinTestBooster"),Error));
    TestEqual(TEXT("Purchase spends the table price from the credited personal wallet"),A->Economy->Coins,30);
    TestEqual(TEXT("A teammate's wallet is unaffected by the purchase"),B->Economy->Coins,25);
    TestEqual(TEXT("Bought booster is queued rather than given to everyone"),A->Economy->QueuedStacks(TEXT("CoinTestBooster")),1);
    TestEqual(TEXT("No teammate gets the purchase"),B->Economy->QueuedStacks(TEXT("CoinTestBooster")),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCObjectiveCoinGateAndCapTest,"MessControl.Economy.ObjectiveCoins.GatesCapsAndRunReset",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCObjectiveCoinGateAndCapTest::RunTest(const FString&)
{
    MCObjectiveCoinFixtures::FWorld F;
    auto* PC=F.Human(true);auto* Other=F.Human();
    auto* A=PC->GetPlayerState<AMCPlayerState>();auto* B=Other->GetPlayerState<AMCPlayerState>();
    TestFalse(TEXT("An empty ID can never be a rewarded objective"),F.Mode->AwardObjectiveCoinsOnce(NAME_None));
    F.Game->bTutorialActive=true;TestFalse(TEXT("Tutorial callbacks cannot create purchase money"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Suppressed")));F.Game->bTutorialActive=false;
    F.Game->bLobbyWaiting=true;TestFalse(TEXT("Lobby/loading callbacks cannot create purchase money"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Suppressed")));F.Game->bLobbyWaiting=false;
    F.Game->bDevManualEvents=true;TestFalse(TEXT("Manual F3 events cannot farm the wallet"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Suppressed")));F.Game->bDevManualEvents=false;
    F.Game->Phase=EMCShiftPhase::Lost;TestFalse(TEXT("A stopped/lost run cannot accept completions"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Suppressed")));F.Game->Phase=EMCShiftPhase::Working;
    TestEqual(TEXT("Every excluded callback preserves the first wallet"),A->Economy->Coins,0);
    TestEqual(TEXT("Every excluded callback preserves the other wallet"),B->Economy->Coins,0);
    TestTrue(TEXT("A new real completion remains eligible after excluded diagnostics"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Suppressed")));
    F.Mode->ObjectiveCoinReward=0;
    TestTrue(TEXT("A disabled reward still seals the completed ID"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Disabled")));
    F.Mode->ObjectiveCoinReward=10;
    TestFalse(TEXT("Enabling rewards later cannot replay a task completed while disabled"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Disabled")));
    TestEqual(TEXT("A negative direct credit is rejected"),A->Economy->CreditObjectiveCoins(-1),0);
    TestEqual(TEXT("An oversized direct credit is rejected"),A->Economy->CreditObjectiveCoins(1001),0);
    A->SetRole(ROLE_SimulatedProxy);TestEqual(TEXT("A non-authority wallet cannot grant purchase currency"),A->Economy->CreditObjectiveCoins(1),0);A->SetRole(ROLE_Authority);
    F.CoinCap(20);
    TestEqual(TEXT("Bring one personal wallet to one coin below cap"),A->Economy->CreditObjectiveCoins(9),9);
    TestEqual(TEXT("Bring the other wallet exactly to cap"),B->Economy->CreditObjectiveCoins(10),10);
    TestTrue(TEXT("Capped objective is accepted once"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Capped")));
    TestEqual(TEXT("A cap clips the real increase to one coin"),A->Economy->Coins,20);
    TestEqual(TEXT("The tooth bonus basis contains only actual task credits, including a clipped one"),A->RunTaskCoins,11);
    TestEqual(TEXT("A full wallet adds nothing to the tooth bonus basis"),B->RunTaskCoins,10);
    TestEqual(TEXT("Full teammate wallet never overflows"),B->Economy->Coins,20);
    TestFalse(TEXT("A fully settled capped ID cannot repeat"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Capped")));
    // No pawn is required for eligibility. Mark both human identities spectator
    // only during the real RestartShift call to avoid unrelated arrival fixtures.
    A->SetIsSpectator(true);B->SetIsSpectator(true);
    F.Mode->RestartShift();
    A->SetIsSpectator(false);B->SetIsSpectator(false);
    F.Game->bTutorialActive=F.Game->bLobbyWaiting=F.Game->bDevManualEvents=false;F.Game->Phase=EMCShiftPhase::Working;
    F.CoinCap(100);
    TestEqual(TEXT("Starting another run does not reset purchase currency"),A->Economy->Coins,20);
    TestEqual(TEXT("Restart clears the old run's multiplier basis"),A->RunTaskCoins,0);
    TestTrue(TEXT("The next run can reward the same authored objective ID once"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Capped")));
    TestEqual(TEXT("New run reward reaches the existing personal balance"),A->Economy->Coins,30);
    TestEqual(TEXT("Only the new task contributes to the new run's multiplier"),A->RunTaskCoins,10);
    TestEqual(TEXT("Both current human participants receive the new run reward"),B->Economy->Coins,30);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCObjectiveCoinPortionAndCoffeeTest,"MessControl.Economy.ObjectiveCoins.MealAndCoffeeCompletions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCObjectiveCoinPortionAndCoffeeTest::RunTest(const FString&)
{
    MCObjectiveCoinFixtures::FWorld F;auto* PC=F.Human(true);auto* Player=PC->GetPlayerState<AMCPlayerState>();
    auto* Director=F.World->SpawnActor<AMCSingleDayDirector>();Director->SetActorTickEnabled(false);
    Director->Settings=NewObject<UMCSingleDayProfile>(Director);
    Director->Stage=EMCSingleDayStage::FirstMeal;
    TestTrue(TEXT("A finished food portion enters its real stage reward path"),Director->BeginStageReward(TEXT("Meal_0_0")));
    TestEqual(TEXT("Food completion grants ten spendable coins alongside its booster"),Player->Economy->Coins,10);
    TestTrue(TEXT("A pending chest placement can be retried"),Director->BeginStageReward(TEXT("Meal_0_0")));
    TestEqual(TEXT("Placement retry never repeats the coin grant"),Player->Economy->Coins,10);
    Director->ResetStageRewards();Director->Stage=EMCSingleDayStage::BetweenMealsPause;
    TestTrue(TEXT("Finished coffee cleanup enters the same real reward path"),Director->BeginStageReward(TEXT("MealCoffee_0")));
    TestEqual(TEXT("Coffee completion grants another ten spendable coins"),Player->Economy->Coins,20);
    TestEqual(TEXT("Both task types are included in this round's bonus basis"),Player->RunTaskCoins,20);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCObjectiveCoinSavedTeethTest,"MessControl.Economy.ObjectiveCoins.SavedTeethVictoryBonus",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCObjectiveCoinSavedTeethTest::RunTest(const FString&)
{
    MCObjectiveCoinFixtures::FWorld F;auto* PC=F.Human(true);auto* Player=PC->GetPlayerState<AMCPlayerState>();
    Player->Economy->CreditObjectiveCoins(500); // Savings brought from the Hub.
    F.Mode->AwardObjectiveCoinsOnce(TEXT("Food"));F.Mode->AwardObjectiveCoinsOnce(TEXT("Coffee"));
    F.Teeth(5);
    F.Game->ArenaTeeth[1]->State.Health=65; // A damaged surviving tooth still counts.
    F.Game->ArenaTeeth[3]->State.bLost=true;
    F.Game->ArenaTeeth[4]->State.bConsumed=true;
    TestFalse(TEXT("An unfinished round cannot pay a tooth bonus"),F.Mode->AwardSavedTeethCoinsOnce());
    F.Game->Phase=EMCShiftPhase::Lost;
    TestFalse(TEXT("Defeat cannot pay the success bonus"),F.Mode->AwardSavedTeethCoinsOnce());
    F.Game->Phase=EMCShiftPhase::Won;F.Mode->FinalizeRunLoadout();
    TestEqual(TEXT("Three surviving arena teeth add 30% of twenty earned coins"),Player->RunSavedToothBonusCoins,6);
    TestEqual(TEXT("Lost/consumed teeth do not count; chipped teeth do"),Player->RunSavedTeeth,3);
    TestEqual(TEXT("Old savings are not multiplied"),Player->Economy->Coins,526);
    TestFalse(TEXT("A second finalization receipt is rejected"),F.Mode->AwardSavedTeethCoinsOnce());
    F.Mode->FinalizeRunLoadout();
    TestEqual(TEXT("Repeated tick/menu finalization cannot mint more currency"),Player->Economy->Coins,526);
    TestEqual(TEXT("The actual credited bonus is part of the persistent wallet"),Player->Economy->ExportSave().Coins,526);
    auto* Replacement=F.World->SpawnActor<AMCPlayerState>();Player->CopyProperties(Replacement);
    TestEqual(TEXT("PlayerState replacement preserves this run's earning receipt"),Replacement->RunTaskCoins,20);
    TestEqual(TEXT("PlayerState replacement preserves the receipt epoch"),Replacement->CoinRewardRunId,F.Game->GameplayLoadId);
    Player->ResetMatchScore();
    TestEqual(TEXT("Starting a new round clears only the multiplier basis"),Player->RunTaskCoins,0);
    TestEqual(TEXT("Round reset keeps the spendable wallet"),Player->Economy->Coins,526);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCObjectiveCoinBonusCapTest,"MessControl.Economy.ObjectiveCoins.SavedTeethBonusGatesAndCap",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCObjectiveCoinBonusCapTest::RunTest(const FString&)
{
    MCObjectiveCoinFixtures::FWorld F;auto* PC=F.Human(true);auto* Player=PC->GetPlayerState<AMCPlayerState>();
    F.Teeth(12);F.Mode->ObjectiveCoinReward=1000;
    F.Mode->AwardObjectiveCoinsOnce(TEXT("A"));F.Mode->AwardObjectiveCoinsOnce(TEXT("B"));
    F.Game->Phase=EMCShiftPhase::Won;
    F.Game->bTutorialActive=true;TestFalse(TEXT("Tutorial victory never pays"),F.Mode->AwardSavedTeethCoinsOnce());F.Game->bTutorialActive=false;
    F.Game->bDevManualEvents=true;TestFalse(TEXT("F3 victory never pays"),F.Mode->AwardSavedTeethCoinsOnce());F.Game->bDevManualEvents=false;
    F.Game->bLobbyWaiting=true;TestFalse(TEXT("Loading/lobby state never pays"),F.Mode->AwardSavedTeethCoinsOnce());F.Game->bLobbyWaiting=false;
    F.CoinCap(4300);
    TestTrue(TEXT("A genuine victory accepts the aggregate bonus"),F.Mode->AwardSavedTeethCoinsOnce());
    TestEqual(TEXT("A 2400-coin bonus can exceed one objective's limit but is clipped by the wallet cap"),Player->RunSavedToothBonusCoins,2300);
    TestEqual(TEXT("The wallet reaches its cap without overflow"),Player->Economy->Coins,4300);
    TestFalse(TEXT("A clipped bonus is still settled exactly once"),F.Mode->AwardSavedTeethCoinsOnce());
    Player->SetRole(ROLE_SimulatedProxy);
    TestEqual(TEXT("A client cannot directly issue an aggregate coin grant"),Player->Economy->CreditRunCompletionCoins(5000),0);
    Player->SetRole(ROLE_Authority);
    return true;
}
#endif
