#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCHubEconomyComponent.h"
#include "MCHubGameState.h"
#include "MCHubGrillStation.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"

namespace MCHubEconomyTestFixtures
{
/** A rules-only authority world: no map travel, real asset edits, viewport or profile write. */
struct FWorld
{
    UWorld* World=nullptr;
    UDataTable* Catalog=nullptr;
    UDataTable* Rewards=nullptr;
    UDataTable* Rules=nullptr;
    AMCHubGameState* Hub=nullptr;
    FWorld()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hub=World->SpawnActor<AMCHubGameState>();World->SetGameState(Hub);
        Hub->SetPhase(EMCHubPhase::Gathering);
        Catalog=NewObject<UDataTable>(World);Catalog->RowStruct=FMCHubCatalogRow::StaticStruct();
        Rewards=NewObject<UDataTable>(World);Rewards->RowStruct=FMCGrillRewardRow::StaticStruct();
        Rules=NewObject<UDataTable>(World);Rules->RowStruct=FMCHubEconomyRulesRow::StaticStruct();
        Rules->AddRow(TEXT("Default"),FMCHubEconomyRulesRow());
        FMCHubCatalogRow Calcium;Calcium.Kind=EMCHubCatalogKind::Consumable;
        Calcium.ConsumableID=TEXT("CalciumSelfGrenade");Calcium.Price=12;Calcium.MaximumQueuedStacks=3;
        Catalog->AddRow(TEXT("CalciumSelfGrenade"),Calcium);
        FMCGrillRewardRow Coins;Coins.MinimumCoins=Coins.MaximumCoins=9;
        Coins.ReelSymbols={TEXT("Cherry"),TEXT("Lemon"),TEXT("Watermelon")};
        Rewards->AddRow(TEXT("FixedCoins"),Coins);
    }
    AMCPlayerState* Player()
    {
        auto* Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Player->Economy->CatalogTable=Catalog;Player->Economy->GrillRewardsTable=Rewards;Player->Economy->RulesTable=Rules;
        Player->FinishSpawning(FTransform::Identity);
        return Player;
    }
    void MakeRealRun()
    {
        auto* State=World->SpawnActor<AMCGameState>();State->bTutorialActive=false;
        World->SetGameState(State);World->URL.AddOption(TEXT("MCHubRun=1"));
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubGrillReceiptTest,"MessControl.Hub.Economy.FishReceiptIsExactlyOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubGrillReceiptTest::RunTest(const FString&)
{
    MCHubEconomyTestFixtures::FWorld Fixture;
    auto* Player=Fixture.Player();auto* Economy=Player->Economy.Get();Player->FishCaught=2;
    FGuid Receipt;FMCGrillReward Preview;FText Error;
    if(!TestTrue(TEXT("An authority fish starts a valid weighted attempt"),Economy->BeginGrillRoll(Receipt,Preview,Error))) return false;
    TestEqual(TEXT("Exactly one fish is reserved before animation"),Player->FishCaught,1);
    TestEqual(TEXT("Coins are not granted at reservation time"),Economy->Coins,0);
    TestEqual(TEXT("The authoritative outcome includes exactly three physical symbols"),Preview.ReelSymbols.Num(),3);
    FGuid Other;FMCGrillReward OtherPreview;
    TestFalse(TEXT("A reentrant/double click cannot reserve a second attempt"),Economy->BeginGrillRoll(Other,OtherPreview,Error));
    TestEqual(TEXT("The rejected attempt leaves the fish untouched"),Player->FishCaught,1);
    TestFalse(TEXT("An unrelated receipt cannot complete another roll"),Economy->CompleteGrillRoll(FGuid::NewGuid()));
    TestTrue(TEXT("The accepted receipt grants its preselected result"),Economy->CompleteGrillRoll(Receipt));
    TestEqual(TEXT("Only the resolved nine personal coins are awarded"),Economy->Coins,9);
    TestFalse(TEXT("Duplicate completion is harmless"),Economy->CompleteGrillRoll(Receipt));
    TestFalse(TEXT("Completed attempts cannot be refunded"),Economy->CancelGrillRoll(Receipt));
    TestEqual(TEXT("Coins remain exactly nine"),Economy->Coins,9);
    TestTrue(TEXT("The remaining fish may start a new roll"),Economy->BeginGrillRoll(Receipt,Preview,Error));
    TestTrue(TEXT("Cancellation refunds the reserved fish"),Economy->CancelGrillRoll(Receipt));
    TestFalse(TEXT("A repeated cancellation cannot mint fish"),Economy->CancelGrillRoll(Receipt));
    TestEqual(TEXT("One fish remains after exactly one cancellation"),Player->FishCaught,1);
    TestEqual(TEXT("The second cancelled roll adds no coins"),Economy->Coins,9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubInvalidEconomyDataTest,"MessControl.Hub.Economy.InvalidDataNeverConsumesFish",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubInvalidEconomyDataTest::RunTest(const FString&)
{
    MCHubEconomyTestFixtures::FWorld Fixture;
    auto* Player=Fixture.Player();Player->FishCaught=3;
    FGuid Receipt;FMCGrillReward Preview;FText Error;
    Fixture.Rewards->EmptyTable();
    TestFalse(TEXT("An empty roll pool rejects the attempt"),Player->Economy->BeginGrillRoll(Receipt,Preview,Error));
    FMCGrillRewardRow Invalid;Invalid.Weight=1;Invalid.MinimumCoins=10;Invalid.MaximumCoins=5;
    Invalid.ReelSymbols={TEXT("Cherry"),TEXT("Cherry"),TEXT("Cherry")};Fixture.Rewards->AddRow(TEXT("InvalidRange"),Invalid);
    Invalid.MinimumCoins=Invalid.MaximumCoins=10;Invalid.Weight=-1;Fixture.Rewards->AddRow(TEXT("InvalidWeight"),Invalid);
    Invalid.Weight=1;Invalid.ReelSymbols.Pop();Fixture.Rewards->AddRow(TEXT("MissingSymbol"),Invalid);
    Invalid.ReelSymbols={TEXT("Star"),TEXT("Star"),TEXT("Star")};Invalid.Kind=EMCGrillRewardKind::CatalogItem;
    Invalid.CatalogItem=TEXT("NotAnAuthoredItem");Fixture.Rewards->AddRow(TEXT("MissingCatalogItem"),Invalid);
    Invalid.Kind=EMCGrillRewardKind::Coins;Invalid.MinimumCoins=0;Invalid.MaximumCoins=2;
    Fixture.Rewards->AddRow(TEXT("AmbiguousWinLossRange"),Invalid);
    TestFalse(TEXT("Malformed ranges, weights, symbols and catalog references are excluded"),Player->Economy->BeginGrillRoll(Receipt,Preview,Error));
    TestEqual(TEXT("Invalid data leaves every fish owned"),Player->FishCaught,3);
    TestFalse(TEXT("Rejected data leaves no receipt"),Player->Economy->HasPendingGrillRoll());
    Fixture.Rewards->EmptyTable();Invalid=FMCGrillRewardRow();Invalid.ReelSymbols={TEXT("Bell"),TEXT("Bell"),TEXT("Bell")};
    Fixture.Rewards->AddRow(TEXT("Valid"),Invalid);Fixture.Rules->EmptyTable();
    TestFalse(TEXT("A missing Default rules row also rejects before debit"),Player->Economy->BeginGrillRoll(Receipt,Preview,Error));
    TestEqual(TEXT("Missing rules cannot lose fish"),Player->FishCaught,3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubExplicitGrillLossTest,"MessControl.Hub.Economy.ExplicitLossConsumesFishAndSurvivesSave",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubExplicitGrillLossTest::RunTest(const FString&)
{
    MCHubEconomyTestFixtures::FWorld Fixture;
    Fixture.Rewards->EmptyTable();FMCGrillRewardRow Loss;
    Loss.MinimumCoins=Loss.MaximumCoins=0;Loss.ReelSymbols={TEXT("Cherry"),TEXT("Lemon"),TEXT("Watermelon")};
    Fixture.Rewards->AddRow(TEXT("NoPrize_Mixed"),Loss);
    auto* Player=Fixture.Player();auto* Economy=Player->Economy.Get();Player->FishCaught=1;Economy->Coins=17;
    FGuid Receipt;FMCGrillReward Preview;FText Error;
    if(!TestTrue(TEXT("The table may author a no-prize mixed combination"),Economy->BeginGrillRoll(Receipt,Preview,Error))) return false;
    TestEqual(TEXT("The losing attempt still costs exactly one fish"),Player->FishCaught,0);
    TestEqual(TEXT("The server publishes a zero-coin result"),Preview.Coins,0);
    const auto Saved=Economy->ExportSave();
    TestFalse(TEXT("Saving settles the accepted losing receipt"),Economy->HasPendingGrillRoll());
    TestEqual(TEXT("Settlement grants no consolation coins"),Economy->Coins,17);
    TestEqual(TEXT("The final display keeps the authoritative zero outcome"),Economy->LastGrillReward.Coins,0);
    TestFalse(TEXT("A saved losing roll cannot complete twice"),Economy->CompleteGrillRoll(Receipt));
    TestFalse(TEXT("A completed losing roll cannot refund its fish"),Economy->CancelGrillRoll(Receipt));
    TestEqual(TEXT("The saved wallet keeps its original balance"),Saved.Coins,17);
    // Serialized-receipt fixture: a profile captured by a reentrant save callback
    // may contain an accepted pending outcome, including an explicit loss.
    auto PendingSave=Saved;PendingSave.PendingGrillReward=Preview;
    auto* Restored=Fixture.Player();
    TestTrue(TEXT("A serialized accepted zero outcome is valid on restore"),Restored->Economy->ImportSave(PendingSave,Error));
    TestEqual(TEXT("Restoring a losing receipt never mints currency"),Restored->Economy->Coins,17);
    TestTrue(TEXT("Restore retains the exact receipt identity"),Restored->Economy->LastGrillReward.RollID==Receipt);
    TestFalse(TEXT("The restored loss is settled exactly once"),Restored->Economy->CompleteGrillRoll(Receipt));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubPersonalShopAndRunTest,"MessControl.Hub.Economy.PurchasesArePersonalAndNextRunOnly",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubPersonalShopAndRunTest::RunTest(const FString&)
{
    MCHubEconomyTestFixtures::FWorld Fixture;
    // Retained future feature behavior is exercised only with an explicit opt-in.
    auto Rules=*Fixture.Rules->FindRow<FMCHubEconomyRulesRow>(TEXT("Default"),TEXT("test"));
    Rules.bCalciumEnabled=true; Fixture.Rules->AddRow(TEXT("Default"),Rules);
    auto* Player=Fixture.Player();auto* Other=Fixture.Player();auto* Economy=Player->Economy.Get();FText Error;
    Economy->Coins=11;
    TestFalse(TEXT("Insufficient funds do not enqueue a consumable"),Economy->TryPurchase(TEXT("CalciumSelfGrenade"),Error));
    TestEqual(TEXT("Failed purchase keeps the balance"),Economy->Coins,11);
    TestTrue(TEXT("Failed purchase keeps the queue empty"),Economy->PendingBoosters.IsEmpty());
    Economy->Coins=60;
    for(int32 Index=0;Index<3;++Index) TestTrue(TEXT("Three affordable charges can be bought"),Economy->TryPurchase(TEXT("CalciumSelfGrenade"),Error));
    TestEqual(TEXT("The server charges the table price per unit"),Economy->Coins,24);
    TestFalse(TEXT("A fourth charge cannot exceed the authored maximum"),Economy->TryPurchase(TEXT("CalciumSelfGrenade"),Error));
    TestEqual(TEXT("Full inventory does not debit money"),Economy->Coins,24);
    TestEqual(TEXT("No charge leaks into the current Hub"),Economy->RunCalciumCharges,0);
    TestTrue(TEXT("Another player receives no purchase"),Other->Economy->PendingBoosters.IsEmpty());
    const FGuid RunId=FGuid::NewGuid();
    TestFalse(TEXT("A real run ID cannot apply boosters while still in Hub"),Economy->ApplyPendingBoostersForRun(RunId,Error));
    TestFalse(TEXT("A Hub input cannot consume the next-run grenade"),Economy->TryConsumeCalciumCharge());
    TestEqual(TEXT("All purchased charges remain queued"),Economy->QueuedStacks(TEXT("CalciumSelfGrenade")),3);
    Fixture.MakeRealRun();
    TestFalse(TEXT("Invalid run identity cannot consume the queue"),Economy->ApplyPendingBoostersForRun(FGuid(),Error));
    TestTrue(TEXT("The actual new run transfers purchased charges"),Economy->ApplyPendingBoostersForRun(RunId,Error));
    TestEqual(TEXT("Exactly three owner charges enter that run"),Economy->RunCalciumCharges,3);
    TestTrue(TEXT("The transferred queue is cleared"),Economy->PendingBoosters.IsEmpty());
    TestTrue(TEXT("Successful owner grenade spawn may consume one charge"),Economy->TryConsumeCalciumCharge());
    TestEqual(TEXT("One charge is consumed"),Economy->RunCalciumCharges,2);
    TestTrue(TEXT("A same-run respawn is idempotent"),Economy->ApplyPendingBoostersForRun(RunId,Error));
    TestEqual(TEXT("Respawn cannot refill consumed charges"),Economy->RunCalciumCharges,2);
    Economy->RefundCalciumCharge();TestEqual(TEXT("A failed spawn can restore its one reserved charge"),Economy->RunCalciumCharges,3);
    TestTrue(TEXT("A different actual run consumes no old queue"),Economy->ApplyPendingBoostersForRun(FGuid::NewGuid(),Error));
    TestEqual(TEXT("Unused old-run charges expire"),Economy->RunCalciumCharges,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubSuspendedCalciumTest,"MessControl.Hub.Economy.SuspendedCalciumPreservesLegacyInventoryAndOtherBoosters",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubSuspendedCalciumTest::RunTest(const FString&)
{
    MCHubEconomyTestFixtures::FWorld Fixture;
    auto* Player=Fixture.Player(); auto* Economy=Player->Economy.Get(); FText Error;
    Economy->Coins=100; Economy->RunCalciumCharges=2;
    TestFalse(TEXT("Calcium is disabled by default in rules"),Economy->IsCalciumEnabled());
    TestFalse(TEXT("Even an affordable calcium purchase is unavailable"),Economy->TryPurchase(TEXT("CalciumSelfGrenade"),Error));
    TestTrue(TEXT("The suspended item is absent from the shop"),Economy->GetCatalog().IsEmpty());
    TestEqual(TEXT("Suspension never debits coins"),Economy->Coins,100);

    Fixture.Rewards->EmptyTable(); FMCGrillRewardRow CalciumReward;
    CalciumReward.Kind=EMCGrillRewardKind::CatalogItem;CalciumReward.CatalogItem=TEXT("CalciumSelfGrenade");
    CalciumReward.ReelSymbols={TEXT("Seven"),TEXT("X"),TEXT("Seven")};
    Fixture.Rewards->AddRow(TEXT("SuspendedOnly"),CalciumReward);
    Player->FishCaught=2; FGuid Receipt; FMCGrillReward Preview;
    TestFalse(TEXT("A suspended-only reward pool cannot consume fish"),Economy->BeginGrillRoll(Receipt,Preview,Error));
    TestEqual(TEXT("Both fish remain owned"),Player->FishCaught,2);

    // Legacy inventory remains valid even though new purchases are unavailable.
    Economy->PendingBoosters.Add(TEXT("CalciumSelfGrenade"));
    const auto Legacy=Economy->ExportSave();
    auto* Restored=Fixture.Player();
    TestTrue(TEXT("Suspended inventory does not invalidate a saved profile"),Restored->Economy->ImportSave(Legacy,Error));
    TestEqual(TEXT("The saved charge remains queued"),Restored->Economy->QueuedStacks(TEXT("CalciumSelfGrenade")),1);
    TestEqual(TEXT("Existing run charges remain stored while inactive"),Restored->Economy->RunCalciumCharges,2);

    FMCHubCatalogRow Speed; Speed.PerkID=TEXT("P01"); Speed.Price=15;
    Fixture.Catalog->AddRow(TEXT("FixtureSpeed"),Speed);
    if(!TestTrue(TEXT("Another enabled booster can still be bought"),Economy->TryPurchase(TEXT("FixtureSpeed"),Error))) return false;
    Fixture.MakeRealRun(); const FGuid Run=FGuid::NewGuid();
    if(!TestTrue(TEXT("A mixed queue applies the enabled booster"),Economy->ApplyPendingBoostersForRun(Run,Error))) return false;
    TestEqual(TEXT("P01 is granted exactly once"),Player->Perks->GetStacks(TEXT("P01")),1);
    TestEqual(TEXT("Only the suspended entry remains queued"),Economy->PendingBoosters.Num(),1);
    TestEqual(TEXT("That retained entry is calcium"),Economy->QueuedStacks(TEXT("CalciumSelfGrenade")),1);
    TestFalse(TEXT("Stored run charges cannot be used while disabled"),Economy->TryConsumeCalciumCharge());
    TestTrue(TEXT("Repeated controller refresh is idempotent"),Economy->ApplyPendingBoostersForRun(Run,Error));
    TestEqual(TEXT("Repeated refresh cannot duplicate P01"),Player->Perks->GetStacks(TEXT("P01")),1);

    auto Accepted=Legacy;
    Accepted.PendingGrillReward.RollID=FGuid::NewGuid();
    Accepted.PendingGrillReward.Kind=EMCGrillRewardKind::CatalogItem;
    Accepted.PendingGrillReward.CatalogItem=TEXT("CalciumSelfGrenade");
    Accepted.PendingGrillReward.Quantity=1;Accepted.PendingGrillReward.ReelSymbols=CalciumReward.ReelSymbols;
    auto* ReceiptOwner=Fixture.Player();
    TestTrue(TEXT("An already accepted legacy prize can be restored"),ReceiptOwner->Economy->ImportSave(Accepted,Error));
    TestFalse(TEXT("Its settlement releases the receipt lock"),ReceiptOwner->Economy->HasPendingGrillRoll());
    TestEqual(TEXT("The accepted prize is retained as inactive inventory"),ReceiptOwner->Economy->QueuedStacks(TEXT("CalciumSelfGrenade")),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubGrillProjectionTest,"MessControl.Hub.Grill.SharedTupleAnglesAndOrientedInteraction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubGrillProjectionTest::RunTest(const FString&)
{
    MCHubEconomyTestFixtures::FWorld Fixture;
    auto* Station=Fixture.World->SpawnActor<AMCHubGrillStation>();auto* Pawn=Fixture.World->SpawnActor<ADefaultPawn>();
    Station->SetActorTransform(FTransform(FRotator(0,37,0),FVector(400,800,200),FVector(1.2,.8,1)));
    Station->InteractionVolume->SetBoxExtent(FVector(100,120,150));
    Pawn->SetActorLocation(Station->GetActorTransform().TransformPosition(FVector(90,100,0)));
    TestTrue(TEXT("A movable rotated OBB contains its actual local interaction area"),Station->ContainsPawn(Pawn));
    Pawn->SetActorLocation(Station->GetActorTransform().TransformPosition(FVector(110,0,0)));
    TestFalse(TEXT("World-aligned bounding boxes cannot widen the interaction zone"),Station->ContainsPawn(Pawn));
    Station->CurrentReward.ReelSymbols={TEXT("Cherry"),TEXT("Bell"),TEXT("Star")};
    Station->SymbolStopAngles.Add(TEXT("Cherry"),15.f);Station->SymbolStopAngles.Add(TEXT("Bell"),160.f);Station->SymbolStopAngles.Add(TEXT("Star"),305.f);
    Station->ReelAngleOffsets={0.f,1.f,2.f};Station->Stage=EMCHubGrillStage::Revealing;
    const auto Stop=Station->GetReelDisplayAngles();
    TestEqual(TEXT("The common angle source resolves first tuple symbol"),Stop[0],15.f);
    TestEqual(TEXT("The common angle source includes calibrated second offset"),Stop[1],161.f);
    TestEqual(TEXT("The common angle source resolves the third identical presentation tuple"),Stop[2],307.f);
    Station->RollStartAngles={25.f,55.f,85.f};Station->Stage=EMCHubGrillStage::Closing;
    const auto Start=Station->GetReelDisplayAngles();
    TestEqual(TEXT("Closing preserves the previous reel orientation without a zero snap"),Start[1],55.f);
    TestTrue(TEXT("The shared station cannot accept parallel rolls while closing"),Station->IsBusy());
    Station->Stage=EMCHubGrillStage::Idle;TestFalse(TEXT("Idle releases the shared interaction lock"),Station->IsBusy());
    return true;
}
#endif
