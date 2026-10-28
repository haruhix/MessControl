#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Algo/Count.h"
#include "MCGameState.h"
#include "MCHubEconomyComponent.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include <limits>

namespace MCBatch1CatalogTests
{
const TArray<FName>& ApprovedIDs()
{
    static const TArray<FName> IDs={TEXT("B1_HeavyHit"),TEXT("B1_LightFoot"),TEXT("B1_DeepBreath"),
        TEXT("B1_BigPortion"),TEXT("B1_ConfidentThrow"),TEXT("B1_FreshDelivery"),TEXT("B1_GreedyHands"),
        TEXT("B1_WideBristles"),TEXT("B1_UnderPressure"),
        TEXT("B2_ReturnTrip"),TEXT("B2_CleanBreath"),TEXT("B2_NeatFreak"),TEXT("B2_LastStroke"),TEXT("B2_CleanAcceleration"),TEXT("B2_RefreshingWork"),
        TEXT("B2_ThickEnamel"),TEXT("B2_Shield"),TEXT("B2_SecondWind"),TEXT("B2_HoudiniDash"),
        TEXT("B2_MightyTooth"),TEXT("B2_StartFromDash"),TEXT("B2_ThunderGod"),TEXT("B2_EchoSwing"),TEXT("B2_Billiards"),
        TEXT("B2_Softener"),TEXT("B2_Antiseptic"),TEXT("B2_LastingFormula")};
    return IDs;
}

UDataTable* CanonicalTable()
{
    return LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
}


FName OldID(const UDataTable* Table,EMCPerkKind Kind)
{
    if(Table) for(FName ID:Table->GetRowNames()) {
        const auto* Row=Table->FindRow<FMCPerkDefinition>(ID,TEXT("Batch 1 legacy fixture"),false);
        if(Row && !Row->bEnabled && UMCPerkComponent::ResolvePerkKind(*Row,ID)==Kind) return ID;
    }
    return NAME_None;
}

/** Real canonical definitions, native authority actors, no viewport/profile writes or package edits. */
struct FWorld
{
    UWorld* World=nullptr;
    AMCGameState* State=nullptr;
    UDataTable* Rules=nullptr;

    FWorld()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;State->bTutorialActive=false;
        Rules=NewObject<UDataTable>(World);Rules->RowStruct=FMCHubEconomyRulesRow::StaticStruct();
        FMCHubEconomyRulesRow Settings;
        Settings.CommonShopAvailability=Settings.RareShopAvailability=Settings.EpicShopAvailability=Settings.LegendaryShopAvailability=1;
        Rules->AddRow(TEXT("Default"),Settings);
    }
    AMCPlayerState* Player(UDataTable* OverridePerks=nullptr)
    {
        auto* Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(OverridePerks) Player->Perks->PerkTable=OverridePerks;
        Player->Economy->RulesTable=Rules;
        Player->FinishSpawning(FTransform::Identity);State->AddPlayerState(Player);
        return Player;
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
};

FName CatalogIDFor(UDataTable* Table,FName PerkID)
{
    if(Table) for(FName ID:Table->GetRowNames()) {
        const auto* Row=Table->FindRow<FMCHubCatalogRow>(ID,TEXT("Batch 1 purchase fixture"),false);
        if(Row && Row->Kind!=EMCHubCatalogKind::Cosmetic && Row->PerkID==PerkID) return ID;
    }
    return NAME_None;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1SavedCatalogTest,"MessControl.Passives.Batch1.Catalog.SavedCurrentCatalog",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1SavedCatalogTest::RunTest(const FString&)
{
    using namespace MCBatch1CatalogTests;
    auto* Table=CanonicalTable();
    if(!TestNotNull(TEXT("The production canonical perk package loads"),Table)) return false;
    if(!TestTrue(TEXT("Canonical table has the expected reflected row type"),Table->GetRowStruct()==FMCPerkDefinition::StaticStruct())) return false;
    int32 Enabled=0,RetainedOld=0;
    for(FName ID:Table->GetRowNames()) {
        const auto* Row=Table->FindRow<FMCPerkDefinition>(ID,TEXT("Batch 1 saved catalog"),false);
        if(!TestNotNull(*ID.ToString(),Row)) continue;
        if(ApprovedIDs().Contains(ID)) {
            TestTrue(FString::Printf(TEXT("%s is enabled"),*ID.ToString()),Row->bEnabled);
            TestTrue(TEXT("Each new passive participates in chest choices"),Row->bAvailableForLevelChoice);
            TestTrue(TEXT("Each new passive has a finite positive reward weight"),FMath::IsFinite(Row->Weight) && Row->Weight>0);
            TestTrue(TEXT("New definitions resolve as passive skills"),UMCPerkComponent::ResolvePerkKind(*Row,ID)==EMCPerkKind::Passive);
            TestTrue(TEXT("The dangerous residual coating is negative; other current skills are positive"),
                Row->Polarity==(ID==TEXT("B2_LastingFormula")?EMCPerkPolarity::Negative:EMCPerkPolarity::Positive));
            TestEqual(TEXT("New passives are unique rather than stack upgrades"),Row->MaxStacks,1);
            TestTrue(TEXT("Current passives use authored modifiers or explicit event behavior"),Row->bRoguePerk
                && (!Row->Modifiers.IsEmpty() || !Row->Parameters.IsEmpty() || ID==TEXT("B2_MightyTooth")));
            for(const auto& Parameter:Row->Parameters) TestTrue(TEXT("Authored event parameters are finite"),FMath::IsFinite(Parameter.Value));
            TestTrue(TEXT("New passives do not replace an ordinary tool"),Row->ToolUpgrade==EMCToolUpgrade::None);
        } else {
            ++RetainedOld;
            TestFalse(FString::Printf(TEXT("Legacy definition %s remains disabled"),*ID.ToString()),Row->bEnabled);
            TestEqual(TEXT("Legacy definitions have no reward sampling weight"),Row->Weight,0.f);
        }
        Enabled+=Row->bEnabled?1:0;
    }
    TestEqual(TEXT("Exactly the approved current production rows are enabled"),Enabled,ApprovedIDs().Num());
    TestTrue(TEXT("Legacy saved row identities remain available for migration"),RetainedOld>0);
    for(FName ID:ApprovedIDs()) TestNotNull(FString::Printf(TEXT("Approved row %s exists"),*ID.ToString()),
        Table->FindRow<FMCPerkDefinition>(ID,TEXT("Batch 1 approved IDs"),false));

    FWorld W;auto* Player=W.Player();
    TSet<FName> ShopPerks;
    for(const auto& Entry:Player->Economy->GetCatalog()) if(Entry.Definition.Kind!=EMCHubCatalogKind::Cosmetic) {
        TestTrue(TEXT("The saved shop resolves only the new gameplay definitions"),ApprovedIDs().Contains(Entry.Definition.PerkID));
        TestTrue(TEXT("The current gameplay assortment contains passives only"),Entry.PerkKind==EMCPerkKind::Passive);
        ShopPerks.Add(Entry.Definition.PerkID);
    }
    TestEqual(TEXT("All current passives are available to shop rotation"),ShopPerks.Num(),ApprovedIDs().Num());
    TestTrue(TEXT("Old rare tool rows cannot enter team tool rewards"),Player->Perks->GetToolRewardIDs().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1OfferGrantTest,"MessControl.Passives.Batch1.Catalog.GrantsAndOffersAreUnique",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1OfferGrantTest::RunTest(const FString&)
{
    using namespace MCBatch1CatalogTests;
    FWorld W;auto* Player=W.Player();auto* Perks=Player->Perks.Get();
    if(!TestNotNull(TEXT("Perks loaded the production table"),Perks->GetPerkTable())) return false;
    for(int32 Seed=0;Seed<32;++Seed) {
        TArray<FName> Offer;
        TestTrue(TEXT("Fresh player receives a nonempty real offer"),Perks->BuildRogueOffer(EMCPerkPolarity::Positive,Seed,Offer));
        TestTrue(TEXT("A card offer contains at most three entries"),Offer.Num()>0 && Offer.Num()<=3);
        TSet<FName> Seen;
        for(FName ID:Offer) {
            TestTrue(TEXT("No old ID is sampled by a normal chest"),ApprovedIDs().Contains(ID));
            TestFalse(TEXT("One offer cannot repeat the same passive"),Seen.Contains(ID));Seen.Add(ID);
        }
    }
    for(FName ID:Perks->GetPerkTable()->GetRowNames()) if(!ApprovedIDs().Contains(ID)) {
        TestFalse(TEXT("Disabled old row is not a grantable reward"),Perks->ServerGrantPerk(ID));
        TestEqual(TEXT("Rejected legacy grant has no owned stack"),Perks->GetStacks(ID),0);
    }
    TestTrue(TEXT("Grant one new passive through the real server entry point"),Perks->ServerGrantPerk(TEXT("B1_DeepBreath")));
    TestFalse(TEXT("An owned new passive cannot stack"),Perks->ServerGrantPerk(TEXT("B1_DeepBreath")));
    TestFalse(TEXT("An owned passive is excluded from further offer eligibility"),Perks->CanOfferPerk(TEXT("B1_DeepBreath")));
    for(int32 Seed=0;Seed<12;++Seed) {
        TArray<FName> Offer;Perks->BuildRogueOffer(EMCPerkPolarity::Positive,Seed,Offer);
        TestFalse(TEXT("Ownership exclusion survives weighted offer selection"),Offer.Contains(TEXT("B1_DeepBreath")));
    }
    for(FName ID:ApprovedIDs()) if(ID!=TEXT("B1_DeepBreath"))
        TestTrue(TEXT("Distinct passives may coexist beyond the eight-icon HUD estimate"),Perks->ServerGrantPerk(ID));
    TestEqual(TEXT("All current distinct passive sources are retained"),Perks->PassivePerkIDs.Num(),ApprovedIDs().Num());
    for(const auto& Slot:Perks->ActiveSkillSlots) TestTrue(TEXT("Passives never occupy a 1/2/3/4 active slot"),Slot.PerkID.IsNone());
    TArray<FName> Offer;
    TestFalse(TEXT("Exhausting this catalog creates no duplicate fallback reward"),Perks->BuildRogueOffer(EMCPerkPolarity::Positive,51,Offer));
    TestTrue(TEXT("Exhausted offer is empty"),Offer.IsEmpty());
    TestTrue(TEXT("Removing a passive is supported"),Perks->RemovePerk(TEXT("B1_DeepBreath")));
    TestTrue(TEXT("Removed passive is eligible again rather than permanently blacklisted"),Perks->BuildRogueOffer(EMCPerkPolarity::Positive,51,Offer));
    TestEqual(TEXT("Only the now-unowned passive returns"),Offer.Num(),1);
    if(Offer.Num()==1) TestEqual(TEXT("Correct returning identity"),Offer[0],FName(TEXT("B1_DeepBreath")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1LegacyCopyTest,"MessControl.Passives.Batch1.Catalog.DisabledOwnedStateStaysInertOnCopy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1LegacyCopyTest::RunTest(const FString&)
{
    using namespace MCBatch1CatalogTests;
    FWorld W;auto* Source=W.Player()->Perks.Get();auto* Target=W.Player()->Perks.Get();
    if(!TestTrue(TEXT("A new valid modifier is present before copying"),Source->ServerGrantPerk(TEXT("B1_DeepBreath")))) return false;
    TArray<float> Before;
    for(uint8 Stat=0;Stat<=uint8(EMCRogueStat::SprayWidth);++Stat) Before.Add(Source->RogueValue(EMCRogueStat(Stat),1));
    TArray<FName> OldCanonical;
    for(FName ID:Source->GetPerkTable()->GetRowNames()) {
        const auto* Row=Source->FindDefinition(ID);
        if(!Row || Row->bEnabled) continue;
        const auto Kind=UMCPerkComponent::ResolvePerkKind(*Row,ID);
        if(Kind!=EMCPerkKind::Passive && Kind!=EMCPerkKind::ToolUpgrade) continue;
        (Kind==EMCPerkKind::Passive?Source->PassivePerkIDs:Source->OwnedToolPerkIDs).AddUnique(ID);
        FMCActivePerk Stale;Stale.PerkID=ID;Stale.Stacks=7;Source->ActivePerks.Add(Stale);OldCanonical.Add(ID);
    }
    TestTrue(TEXT("Fixture contains real retained legacy owned definitions"),!OldCanonical.IsEmpty());
    const FName OldConsumable=OldID(Source->GetPerkTable(),EMCPerkKind::Consumable);
    if(!TestFalse(TEXT("A legacy consumable definition remains addressable"),OldConsumable.IsNone())) return false;
    auto& StaleSlot=Source->ActiveSkillSlots[0];StaleSlot.PerkID=OldConsumable;StaleSlot.InstanceID=FGuid::NewGuid();StaleSlot.Charges=3;
    W.State->TeamToolUpgrades=0x0f;
    for(FName ID:OldCanonical) TestEqual(TEXT("A stale canonical ID cannot drive GetStacks hardcoded behavior"),Source->GetStacks(ID),0);
    for(uint8 Stat=0;Stat<=uint8(EMCRogueStat::SprayWidth);++Stat)
        TestEqual(TEXT("Every stale legacy modifier is masked before migration rebuild"),Source->RogueValue(EMCRogueStat(Stat),1),Before[Stat],.0001f);
    for(EMCToolUpgrade Kind:{EMCToolUpgrade::MeshaBrush,EMCToolUpgrade::Chainsaw,EMCToolUpgrade::Watergun})
        TestFalse(TEXT("Stale team tool bits do not revive disabled tools"),Source->HasToolUpgrade(Kind));

    Source->CopyPerksTo(Target);
    TestEqual(TEXT("The current passive survives pawn/travel handoff"),Target->GetStacks(TEXT("B1_DeepBreath")),1);
    TestEqual(TEXT("Only the new canonical passive remains live"),Target->PassivePerkIDs.Num(),1);
    TestTrue(TEXT("Old tool ownership is removed from live state"),Target->OwnedToolPerkIDs.IsEmpty());
    for(FName ID:OldCanonical) {
        TestFalse(TEXT("Old canonical ownership is quarantined after handoff"),Target->OwnsPerk(ID));
        TestTrue(TEXT("Quarantined ID is retained as explicit migration data"),Target->LegacyUnappliedBoosterIDs.Contains(ID));
    }
    TestEqual(TEXT("Every unconsumed disabled consumable charge is retained"),
        int32(Algo::Count(Target->LegacyUnappliedBoosterIDs,OldConsumable)),3);
    for(const auto& Slot:Target->ActiveSkillSlots) TestTrue(TEXT("Disabled consumable is absent from live skill slots"),Slot.PerkID.IsNone());
    for(uint8 Stat=0;Stat<=uint8(EMCRogueStat::SprayWidth);++Stat)
        TestEqual(TEXT("Handoff restores only enabled modifier totals"),Target->RogueValue(EMCRogueStat(Stat),1),Before[Stat],.0001f);
    for(EMCToolUpgrade Kind:{EMCToolUpgrade::MeshaBrush,EMCToolUpgrade::Chainsaw,EMCToolUpgrade::Watergun})
        TestFalse(TEXT("Copied player cannot use stale shared tool bits"),Target->HasToolUpgrade(Kind));

    // A consumed old grenade can leave no loadout IDs, so pruning owned rows alone cannot catch it.
    auto* ConsumedSource=W.Player()->Perks.Get();auto* ConsumedTarget=W.Player()->Perks.Get();
    ConsumedSource->bGrenadeArmed=true;ConsumedSource->CopyPerksTo(ConsumedTarget);
    TestFalse(TEXT("An armed legacy grenade without an enabled adapter is not copied"),ConsumedTarget->bGrenadeArmed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1PaidMigrationTest,"MessControl.Passives.Batch1.Catalog.PaidLegacyRetainedAndStockRepaired",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1PaidMigrationTest::RunTest(const FString&)
{
    using namespace MCBatch1CatalogTests;
    FWorld W;auto* Player=W.Player();auto* Economy=Player->Economy.Get();
    auto* Catalog=Economy->CatalogTable.LoadSynchronous();
    if(!TestNotNull(TEXT("The production hub catalog loads"),Catalog)) return false;
    FName OldPerk,OldCatalog;
    for(FName ID:Catalog->GetRowNames()) {
        const auto* Item=Catalog->FindRow<FMCHubCatalogRow>(ID,TEXT("Old paid catalog entry"),false);
        const auto* Definition=Item?Player->Perks->FindDefinition(Item->PerkID):nullptr;
        if(Definition && !Definition->bEnabled) {OldPerk=Item->PerkID;OldCatalog=ID;break;}
    }
    if(!TestFalse(TEXT("A real retired paid catalog identity is retained"),OldPerk.IsNone())) return false;
    const FName NewPerk=TEXT("B1_DeepBreath"),NewCatalog=CatalogIDFor(Catalog,NewPerk);
    if(!TestFalse(TEXT("A new passive has a production shop entry"),NewCatalog.IsNone())) return false;
    FMCHubPendingPurchase OldPurchase;OldPurchase.InstanceID=FGuid::NewGuid();OldPurchase.CatalogID=OldCatalog;
    OldPurchase.PerkID=OldPerk;OldPurchase.DestinationSlot=2;OldPurchase.Charges=3;
    FMCHubPendingPurchase NewPurchase;NewPurchase.InstanceID=FGuid::NewGuid();NewPurchase.CatalogID=NewCatalog;NewPurchase.PerkID=NewPerk;
    FMCHubEconomySaveData Save;Save.Coins=73;Save.PendingPurchases={OldPurchase,NewPurchase};Save.PendingBoosters={OldCatalog};
    Save.bShopStockGenerated=true;Save.ShopRotationId=FGuid::NewGuid();Save.ShopRotationSeed=19;Save.ShopStockIDs={OldCatalog};
    FText Error;
    if(!TestTrue(TEXT("Import accepts and preserves paid retired entries"),Economy->ImportSave(Save,Error))) return false;
    TestEqual(TEXT("Import does not throw away either paid purchase"),Economy->PendingPurchases.Num(),2);
    TestFalse(TEXT("Retired catalog item is not purchasable"),Economy->CanPurchase(OldCatalog,Error));
    TestTrue(TEXT("Obsolete stock is repaired in the same run epoch"),Economy->EnsureShopStock(Save.ShopRotationId,Save.ShopRotationSeed));
    const auto Stock=Economy->ShopStockIDs;
    TestTrue(TEXT("Repaired stock contains real new choices"),!Stock.IsEmpty());
    TestFalse(TEXT("Repair removes the retired stock item"),Stock.Contains(OldCatalog));
    TestTrue(TEXT("Repair preserves the current rotation epoch"),Economy->ShopRotationId==Save.ShopRotationId);
    TestEqual(TEXT("Repair preserves the original deterministic seed"),Economy->ShopRotationSeed,Save.ShopRotationSeed);
    for(const auto& Entry:Economy->GetShopCatalog()) TestTrue(TEXT("Repaired stock resolves only the new gameplay catalog"),ApprovedIDs().Contains(Entry.Definition.PerkID));
    TestTrue(TEXT("Reopening valid stock succeeds"),Economy->EnsureShopStock(Save.ShopRotationId,999));
    TestTrue(TEXT("Reopening never rerolls already repaired stock"),Economy->ShopStockIDs==Stock);
    TestEqual(TEXT("A later caller cannot replace the persisted seed"),Economy->ShopRotationSeed,Save.ShopRotationSeed);

    W.World->URL.AddOption(TEXT("MCHubRun=1"));
    const FGuid RunID=FGuid::NewGuid();
    TestTrue(TEXT("Valid prepared purchases apply for the next real run"),Economy->ApplyPendingBoostersForRun(RunID,Error));
    TestEqual(TEXT("The new paid passive becomes owned"),Player->Perks->GetStacks(NewPerk),1);
    TestEqual(TEXT("Only the disabled purchase remains pending"),Economy->PendingPurchases.Num(),1);
    if(Economy->PendingPurchases.Num()==1) {
        const auto& Retained=Economy->PendingPurchases[0];
        TestTrue(TEXT("Retained purchase keeps its receipt identity"),Retained.InstanceID==OldPurchase.InstanceID);
        TestEqual(TEXT("Retained purchase keeps its catalog identity"),Retained.CatalogID,OldPurchase.CatalogID);
        TestEqual(TEXT("Retained purchase keeps its canonical ID"),Retained.PerkID,OldPurchase.PerkID);
        TestEqual(TEXT("Retained purchase keeps its destination metadata"),Retained.DestinationSlot,OldPurchase.DestinationSlot);
        TestEqual(TEXT("Retained purchase keeps its unconsumed paid charges"),Retained.Charges,OldPurchase.Charges);
    }
    TestEqual(TEXT("The old legacy queue receipt also remains untouched"),Economy->PendingBoosters.Num(),1);
    TestTrue(TEXT("The user receives a retained-purchase notice"),!Economy->MigrationNotice.IsEmpty());
    TestEqual(TEXT("Migration never spends/refunds/mints wallet coins"),Economy->Coins,73);
    TestEqual(TEXT("Disabled paid ID never becomes a live effect"),Player->Perks->GetStacks(OldPerk),0);
    TestTrue(TEXT("Repeat readiness callback is idempotent"),Economy->ApplyPendingBoostersForRun(RunID,Error));
    TestEqual(TEXT("Idempotency preserves the quarantined purchase"),Economy->PendingPurchases.Num(),1);
    const auto RoundTrip=Economy->ExportSave();
    TestEqual(TEXT("Future saves retain the old paid purchase"),RoundTrip.PendingPurchases.Num(),1);
    TestEqual(TEXT("Future saves retain the old legacy queue receipt"),RoundTrip.PendingBoosters.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1NoHardcodedGateTest,"MessControl.Passives.Batch1.Catalog.EnabledCustomDefinitionsStillWork",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1NoHardcodedGateTest::RunTest(const FString&)
{
    using namespace MCBatch1CatalogTests;
    FWorld W;
    auto* Table=NewObject<UDataTable>(W.World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
    FMCPerkDefinition Passive;Passive.Kind=EMCPerkKind::Passive;Passive.bRoguePerk=true;Passive.MaxStacks=1;
    FMCRogueModifier Modifier;Modifier.Stat=EMCRogueStat::NormalSpeed;Modifier.Value=.05f;Passive.Modifiers={Modifier};
    Table->AddRow(TEXT("Fixture_EnabledSkill"),Passive);
    auto* Perks=W.Player(Table)->Perks.Get();
    TArray<FName> Offer;
    TestTrue(TEXT("The current catalog is authored data, not an engine-wide ID allowlist"),
        Perks->BuildRogueOffer(EMCPerkPolarity::Positive,42,Offer));
    TestTrue(TEXT("An unrelated enabled definition participates in its own table"),Offer.Contains(TEXT("Fixture_EnabledSkill")));
    TestTrue(TEXT("A custom table retains the generic acquisition extension point"),Perks->ServerGrantPerk(TEXT("Fixture_EnabledSkill")));
    TestEqual(TEXT("Custom authored modifiers remain executable"),Perks->RogueValue(EMCRogueStat::NormalSpeed),1.05f,.0001f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventPassiveParameterTest,"MessControl.Passives.Batch2.Catalog.ParameterFallbacks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventPassiveParameterTest::RunTest(const FString&)
{
    using namespace MCBatch1CatalogTests;
    FWorld World;
    auto* Table=NewObject<UDataTable>(World.World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
    FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Passive;Row.bRoguePerk=true;
    Row.Parameters.Add(TEXT("Chance"),.47f);
    Row.Parameters.Add(TEXT("Invalid"),std::numeric_limits<float>::quiet_NaN());
    Table->AddRow(TEXT("PrivateParameterFixture"),Row);
    Row.bEnabled=false;Table->AddRow(TEXT("DisabledParameterFixture"),Row);
    auto* Perks=World.Player(Table)->Perks.Get();
    TestEqual(TEXT("Event tuning comes from the current reflected row"),Perks->PerkParameter(TEXT("PrivateParameterFixture"),TEXT("Chance"),.2f),.47f);
    TestEqual(TEXT("Missing key retains the explicit versioned fallback"),Perks->PerkParameter(TEXT("PrivateParameterFixture"),TEXT("Missing"),.2f),.2f);
    TestEqual(TEXT("Nonfinite authored data never reaches gameplay arithmetic"),Perks->PerkParameter(TEXT("PrivateParameterFixture"),TEXT("Invalid"),.2f),.2f);
    TestEqual(TEXT("Disabled definitions cannot supply live event tuning"),Perks->PerkParameter(TEXT("DisabledParameterFixture"),TEXT("Chance"),.2f),.2f);
    TestEqual(TEXT("Missing row and nonfinite fallback are a finite zero"),Perks->PerkParameter(TEXT("Missing"),TEXT("Chance"),std::numeric_limits<float>::infinity()),0.f);
    return true;
}

#endif
