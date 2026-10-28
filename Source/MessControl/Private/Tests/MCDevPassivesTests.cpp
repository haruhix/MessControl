#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "MCDevCommands.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCHubEconomyComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace MCDevPassivesTests
{
const FName HeavyHit(TEXT("B1_HeavyHit"));
const FName DeepBreath(TEXT("B1_DeepBreath"));

struct FPlayer
{
    APlayerController* Controller=nullptr;
    AMCPlayerState* State=nullptr;
    UMCPerkComponent* Perks=nullptr;
};

/** Actual host command dispatcher, no viewport, profile writes or network driver. */
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;

    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));URL.AddOption(TEXT("Seed=41"));
        World->SetGameMode(URL);Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseDayOnePlan=false;Mode->bUseAdaptiveDirector=false;Mode->bUseSingleDayLoop=false;
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        State=World->GetGameState<AMCGameState>();
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;State->bTutorialActive=false;
    }
    FPlayer Player(bool Local=true,UDataTable* Table=nullptr)
    {
        FPlayer P;P.Controller=World->SpawnActor<APlayerController>();
        if(Local) P.Controller->SetAsLocalPlayerController();
        P.State=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,
            P.Controller,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(Table) P.State->Perks->PerkTable=Table;
        P.State->FinishSpawning(FTransform::Identity);P.Controller->SetPlayerState(P.State);
        State->AddPlayerState(P.State);P.Perks=P.State->Perks;return P;
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDevPassivesGrantRemoveTest,
    "MessControl.Passives.Batch1.DevPanel.GrantRemoveUsesRealLoadoutWithoutReset",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDevPassivesGrantRemoveTest::RunTest(const FString&)
{
    using namespace MCDevPassivesTests;
    FWorld F;const auto Host=F.Player();
    if(!TestTrue(TEXT("A designated standalone host passes the production dev guard"),F.Mode->CanUseDevPanel(Host.Controller))
        || !TestNotNull(TEXT("The current saved canonical table is loaded"),Host.Perks->GetPerkTable())) return false;
    const TArray<FName> IDs=MCDevPassiveIDs(Host.Perks);
    const TArray<FName> Expected={HeavyHit,TEXT("B1_LightFoot"),DeepBreath,TEXT("B1_BigPortion"),TEXT("B1_ConfidentThrow"),
        TEXT("B1_FreshDelivery"),TEXT("B1_GreedyHands"),TEXT("B1_WideBristles"),TEXT("B1_UnderPressure")};
    if(!TestEqual(TEXT("F3 lists the nine current passives, including unowned ones"),int32(IDs.Num()),int32(Expected.Num()))) return false;
    TSet<int32> Tokens;
    for(int32 I=0;I<IDs.Num();++I) {
        const auto* Row=Host.Perks->FindDefinition(IDs[I]);
        TestTrue(TEXT("Every listed ID is a current expected passive"),Expected.Contains(IDs[I]));
        TestTrue(TEXT("Every listed row has a name and readable description"),Row && !Row->DisplayName.IsEmpty() && !Row->Description.IsEmpty());
        TestTrue(TEXT("Every listed row is enabled and passive"),Row && Row->bEnabled && UMCPerkComponent::ResolvePerkKind(*Row,IDs[I])==EMCPerkKind::Passive);
        if(I>0) TestTrue(TEXT("The presentation order is stable lexical order"),IDs[I-1].LexicalLess(IDs[I]));
        Tokens.Add(MCDevPassiveToken(IDs[I]));
    }
    TestEqual(TEXT("Every current row has a distinct command identity"),int32(Tokens.Num()),int32(IDs.Num()));
    if(!TestTrue(TEXT("An existing different passive is prepared before F3 mutation"),Host.Perks->TryAcquirePerk(DeepBreath))) return false;

    auto* Economy=Host.State->Economy.Get();Economy->Coins=123;
    FMCHubPendingPurchase Purchase;Purchase.InstanceID=FGuid::NewGuid();Purchase.PerkID=TEXT("P01");Purchase.CatalogID=TEXT("RetainedPaidLegacy");
    Economy->PendingPurchases.Add(Purchase);Economy->PendingBoosters.Add(TEXT("RetainedLegacyQueue"));
    Economy->ShopRotationId=FGuid::NewGuid();Economy->ShopRotationSeed=91;Economy->bShopStockGenerated=true;
    Economy->ShopStockIDs.Add(TEXT("Passive_B1_DeepBreath"));
    const FGuid Rotation=Economy->ShopRotationId,RunID=F.Mode->GetHubBoosterRunId();
    const auto Director=F.Mode->RoguelikeDirector;const auto DayDirector=F.Mode->DayDirector;const auto GameDirector=F.Mode->GameDirector;
    const auto Phase=F.State->Phase;const int32 Step=F.State->StepIndex,Day=F.State->Day;
    const int32 Tasks=F.State->TasksLeft,Total=F.State->TasksTotal;const double PhaseEndsAt=F.State->PhaseEndsAt;

    F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::GrantPassive,MCDevPassiveToken(HeavyHit));
    TestEqual(TEXT("The typed F3 grant acquires one real passive"),Host.Perks->GetStacks(HeavyHit),1);
    TestEqual(TEXT("The granted row affects the actual stat consumer"),Host.Perks->RogueValue(EMCRogueStat::WeaponDamage,100.f),140.f,.001f);
    F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::GrantPassive,MCDevPassiveToken(HeavyHit));
    TestEqual(TEXT("Repeated F3 grant does not create duplicate passive entries"),int32(Host.Perks->PassivePerkIDs.Num()),2);
    TestEqual(TEXT("Repeated F3 grant never stacks the effect"),Host.Perks->RogueValue(EMCRogueStat::WeaponDamage,100.f),140.f,.001f);
    TestTrue(TEXT("Ownership changes do not hide a row or change its command identity"),MCDevPassiveIDs(Host.Perks)==IDs);
    F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::RemovePassive,MCDevPassiveToken(HeavyHit));
    TestFalse(TEXT("The typed F3 removal removes the selected canonical ID"),Host.Perks->OwnsPerk(HeavyHit));
    TestEqual(TEXT("Removal restores the real base stat"),Host.Perks->RogueValue(EMCRogueStat::WeaponDamage,100.f),100.f,.001f);
    F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::RemovePassive,MCDevPassiveToken(HeavyHit));
    TestEqual(TEXT("Removing an absent row keeps the unrelated passive"),Host.Perks->GetStacks(DeepBreath),1);
    TestEqual(TEXT("Unrelated ownership was never reset"),int32(Host.Perks->PassivePerkIDs.Num()),1);
    TestEqual(TEXT("All four active slots remain present"),int32(Host.Perks->ActiveSkillSlots.Num()),4);
    TestTrue(TEXT("F3 passive commands never populate active slots"),!Host.Perks->ActiveSkillSlots.ContainsByPredicate([](const auto& Slot){return !Slot.PerkID.IsNone();}));
    TestTrue(TEXT("No old tool or booster was granted"),Host.Perks->OwnedToolPerkIDs.IsEmpty() && Host.Perks->BoosterQueue.IsEmpty());
    TestEqual(TEXT("Commands never debit or reward the wallet"),Economy->Coins,123);
    TestTrue(TEXT("Paid pending purchases are preserved byte-for-field"),Economy->PendingPurchases.Num()==1
        && Economy->PendingPurchases[0].InstanceID==Purchase.InstanceID && Economy->PendingPurchases[0].PerkID==Purchase.PerkID
        && Economy->PendingPurchases[0].CatalogID==Purchase.CatalogID && Economy->PendingPurchases[0].DestinationSlot==Purchase.DestinationSlot
        && Economy->PendingPurchases[0].Charges==Purchase.Charges);
    TestTrue(TEXT("The legacy paid queue remains intact"),Economy->PendingBoosters==TArray<FName>{TEXT("RetainedLegacyQueue")});
    TestTrue(TEXT("F3 does not reroll the shop"),Economy->ShopRotationId==Rotation && Economy->ShopRotationSeed==91
        && Economy->bShopStockGenerated && Economy->ShopStockIDs==TArray<FName>{TEXT("Passive_B1_DeepBreath")});
    TestTrue(TEXT("F3 does not reset the run or replace directors"),F.Mode->GetHubBoosterRunId()==RunID
        && F.Mode->RoguelikeDirector==Director && F.Mode->DayDirector==DayDirector && F.Mode->GameDirector==GameDirector);
    TestTrue(TEXT("F3 does not skip or restart an objective"),F.State->Phase==Phase && F.State->StepIndex==Step && F.State->Day==Day
        && F.State->TasksLeft==Tasks && F.State->TasksTotal==Total && F.State->PhaseEndsAt==PhaseEndsAt);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDevPassivesValidationTest,
    "MessControl.Passives.Batch1.DevPanel.HostGuardAndStaleDisabledTokens",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDevPassivesValidationTest::RunTest(const FString&)
{
    using namespace MCDevPassivesTests;
    FWorld F;const auto Host=F.Player();
    if(!TestNotNull(TEXT("The current saved canonical table is loaded"),Host.Perks->GetPerkTable())) return false;
    const int32 Token=MCDevPassiveToken(HeavyHit);
    for(int32 Invalid:{int32(INDEX_NONE),int32(MAX_int32)}) {
        F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::GrantPassive,Invalid);
        F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::RemovePassive,Invalid);
    }
    F.Mode->ExecuteDevAction(nullptr,EMCDevAction::GrantPassive,Token);
    const auto Unassigned=F.Player(false);
    TestFalse(TEXT("An unassigned non-local controller fails the real host guard"),F.Mode->CanUseDevPanel(Unassigned.Controller));
    F.Mode->ExecuteDevAction(Unassigned.Controller,EMCDevAction::GrantPassive,Token);
    Host.Controller->SetRole(ROLE_SimulatedProxy);
    TestFalse(TEXT("A controller without authority fails the real host guard"),F.Mode->CanUseDevPanel(Host.Controller));
    F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::GrantPassive,Token);Host.Controller->SetRole(ROLE_Authority);
    {
        FWorld Other;const auto Foreign=Other.Player();
        TestFalse(TEXT("An authoritative local controller from another world cannot issue commands here"),F.Mode->CanUseDevPanel(Foreign.Controller));
        F.Mode->ExecuteDevAction(Foreign.Controller,EMCDevAction::GrantPassive,Token);
        TestTrue(TEXT("The rejected command cannot mutate that other world's loadout"),Foreign.Perks->PassivePerkIDs.IsEmpty());
    }
    TestTrue(TEXT("Rejected requests leave the host and unassigned loadouts empty"),Host.Perks->PassivePerkIDs.IsEmpty() && Unassigned.Perks->PassivePerkIDs.IsEmpty());

    FName OldID;
    for(FName ID:Host.Perks->GetPerkTable()->GetRowNames()) if(const auto* Row=Host.Perks->FindDefinition(ID);Row && !Row->bEnabled) {OldID=ID;break;}
    if(!TestFalse(TEXT("A preserved disabled old definition exists in the saved catalog"),OldID.IsNone())) return false;
    TestFalse(TEXT("An old disabled ID is absent from the F3 list"),MCDevPassiveIDs(Host.Perks).Contains(OldID));
    F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::GrantPassive,MCDevPassiveToken(OldID));
    TestFalse(TEXT("Knowing the old row token cannot grant the disabled perk"),Host.Perks->OwnsPerk(OldID));
    TestTrue(TEXT("An invalid old token cannot grant a neighboring enabled row"),Host.Perks->PassivePerkIDs.IsEmpty());

    // Copy definitions into a transient table: never edit or save the package.
    auto* Table=NewObject<UDataTable>(F.World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
    FMCPerkDefinition Current=*Host.Perks->FindDefinition(HeavyHit),Neighbor=*Host.Perks->FindDefinition(DeepBreath);
    Table->AddRow(HeavyHit,Current);Table->AddRow(DeepBreath,Neighbor);
    const auto LiveEdit=F.Player(true,Table);
    const int32 StaleToken=MCDevPassiveToken(HeavyHit);
    TestTrue(TEXT("The row is initially represented in the live F3 list"),MCDevPassiveIDs(LiveEdit.Perks).Contains(HeavyHit));
    Table->FindRow<FMCPerkDefinition>(HeavyHit,TEXT("F3 disabled-token fixture"),false)->bEnabled=false;
    F.Mode->ExecuteDevAction(LiveEdit.Controller,EMCDevAction::GrantPassive,StaleToken);
    TestTrue(TEXT("Disabling a row after showing it cannot grant that row or its sorted neighbor"),LiveEdit.Perks->PassivePerkIDs.IsEmpty());
    Table->RemoveRow(HeavyHit);
    F.Mode->ExecuteDevAction(LiveEdit.Controller,EMCDevAction::GrantPassive,StaleToken);
    TestTrue(TEXT("Deleting a shown row cannot retarget its old command to another ID"),LiveEdit.Perks->PassivePerkIDs.IsEmpty());
    Current.bEnabled=true;Current.Kind=EMCPerkKind::Active;
    const FName NonPassive(TEXT("DevActiveIsNotAPassive"));Table->AddRow(NonPassive,Current);
    F.Mode->ExecuteDevAction(LiveEdit.Controller,EMCDevAction::GrantPassive,MCDevPassiveToken(NonPassive));
    TestFalse(TEXT("Enabled active definitions are excluded from the passive list"),MCDevPassiveIDs(LiveEdit.Perks).Contains(NonPassive));
    TestTrue(TEXT("A passive command cannot inject an active definition into a slot"),LiveEdit.Perks->PassivePerkIDs.IsEmpty()
        && !LiveEdit.Perks->ActiveSkillSlots.ContainsByPredicate([](const auto& Slot){return !Slot.PerkID.IsNone();}));
    return true;
}
#endif
