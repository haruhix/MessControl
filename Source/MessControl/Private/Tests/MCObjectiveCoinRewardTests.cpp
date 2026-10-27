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
    TestTrue(TEXT("The next run can reward the same authored objective ID once"),F.Mode->AwardObjectiveCoinsOnce(TEXT("Capped")));
    TestEqual(TEXT("New run reward reaches the existing personal balance"),A->Economy->Coins,30);
    TestEqual(TEXT("Both current human participants receive the new run reward"),B->Economy->Coins,30);
    return true;
}
#endif
