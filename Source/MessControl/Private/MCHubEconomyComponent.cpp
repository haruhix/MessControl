#include "MCHubEconomyComponent.h"

#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "Engine/World.h"
#include "Algo/Count.h"
#include "Kismet/GameplayStatics.h"
#include "Templates/UnrealTemplate.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "MCHubEconomy"

UMCHubEconomyComponent::UMCHubEconomyComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetIsReplicatedByDefault(true);
    CatalogTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/Hub/Economy/DT_HubCatalog.DT_HubCatalog")));
    GrillRewardsTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/Hub/Economy/DT_GrillRewards.DT_GrillRewards")));
    RulesTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/Hub/Economy/DT_HubEconomyRules.DT_HubEconomyRules")));
}

void UMCHubEconomyComponent::BeginPlay() { Super::BeginPlay(); LoadTables(); }
bool UMCHubEconomyComponent::IsAuthority() const { return GetOwner() && GetOwner()->HasAuthority(); }
bool UMCHubEconomyComponent::IsGatheringHub() const
{
    const auto* Hub=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    return Hub && Hub->Phase==EMCHubPhase::Gathering;
}
bool UMCHubEconomyComponent::IsRealRun() const
{
    const UWorld* World=GetWorld();
    const auto* State=World?World->GetGameState<AMCGameState>():nullptr;
    return State && !State->bTutorialActive && !World->GetGameState<AMCHubGameState>()
        && FCString::Atoi(World->URL.GetOption(TEXT("MCHubRun="),TEXT("0")))!=0
        && UGameplayStatics::GetCurrentLevelName(this,true)!=TEXT("L_Tutorial");
}
bool UMCHubEconomyComponent::LoadTables() const
{
    if (!LoadedCatalog) LoadedCatalog=CatalogTable.LoadSynchronous();
    if (!LoadedRewards) LoadedRewards=GrillRewardsTable.LoadSynchronous();
    if (!LoadedRules) LoadedRules=RulesTable.LoadSynchronous();
    const bool TablesValid=LoadedCatalog && LoadedCatalog->GetRowStruct()==FMCHubCatalogRow::StaticStruct()
        && LoadedRewards && LoadedRewards->GetRowStruct()==FMCGrillRewardRow::StaticStruct()
        && LoadedRules && LoadedRules->GetRowStruct()==FMCHubEconomyRulesRow::StaticStruct()
        && LoadedRules->FindRow<FMCHubEconomyRulesRow>(TEXT("Default"),TEXT("Hub economy rules"),false);
    if (!TablesValid) return false;
    const auto* Rules=LoadedRules->FindRow<FMCHubEconomyRulesRow>(TEXT("Default"),TEXT("Hub economy rules"),false);
    return Rules->MaximumCoins>0 && Rules->MaximumPendingBoosters>0 && Rules->MaximumPendingBoosters<=256
        && Rules->MaximumOwnedCosmetics>0 && Rules->MaximumOwnedCosmetics<=1024
        && FMath::IsFinite(Rules->LidCloseSeconds) && Rules->LidCloseSeconds>=.05f
        && FMath::IsFinite(Rules->RollSeconds) && Rules->RollSeconds>=.1f
        && FMath::IsFinite(Rules->RevealSeconds) && Rules->RevealSeconds>=.05f
        && FMath::IsFinite(Rules->LidOpenSeconds) && Rules->LidOpenSeconds>=.05f;
}
FMCHubEconomyRulesRow UMCHubEconomyComponent::GetRules() const
{
    if (LoadTables()) return *LoadedRules->FindRow<FMCHubEconomyRulesRow>(TEXT("Default"),TEXT("Hub economy rules"),false);
    return FMCHubEconomyRulesRow();
}
TArray<FName> UMCHubEconomyComponent::GetGrillSymbols() const
{
    TArray<FName> Result;
    if (!LoadTables()) return Result;
    for (FName Name:LoadedRewards->GetRowNames())
    {
        const auto* Row=LoadedRewards->FindRow<FMCGrillRewardRow>(Name,TEXT("Grill symbols"),false);
        if (!Row || !Row->bEnabled || !FMath::IsFinite(Row->Weight) || Row->Weight<=0
            || Row->ReelSymbols.Num()!=3 || Row->ReelSymbols.Contains(NAME_None)) continue;
        if (Row->Kind==EMCGrillRewardKind::CatalogItem)
        {
            const auto* Item=FindCatalogItem(Row->CatalogItem);
            if (!Item || !IsCatalogUsable(*Item)) continue;
        }
        for (FName Symbol:Row->ReelSymbols) Result.AddUnique(Symbol);
    }
    return Result;
}
const FMCHubCatalogRow* UMCHubEconomyComponent::FindCatalogItem(FName ItemID) const
{
    return LoadTables() && !ItemID.IsNone()?LoadedCatalog->FindRow<FMCHubCatalogRow>(ItemID,TEXT("Hub catalog"),false):nullptr;
}
bool UMCHubEconomyComponent::IsCalciumEnabled() const
{
    return LoadTables() && GetRules().bCalciumEnabled;
}
bool UMCHubEconomyComponent::IsCatalogUsable(const FMCHubCatalogRow& Item,bool bIncludeSuspended) const
{
    if (!Item.bEnabled || Item.Price<0 || Item.Quantity<1 || Item.Quantity>32
        || Item.MaximumQueuedStacks<1 || Item.MaximumQueuedStacks>32) return false;
    if (Item.Kind==EMCHubCatalogKind::Cosmetic)
    {
        const auto* Cosmetic=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),Item.CosmeticID);
        return Cosmetic && Cosmetic->bEnabled && Cosmetic->Mesh && Item.Quantity==1;
    }
    if (Item.Kind==EMCHubCatalogKind::Consumable)
        return Item.ConsumableID==TEXT("CalciumSelfGrenade") && (bIncludeSuspended || IsCalciumEnabled());
    const auto* Player=Cast<AMCPlayerState>(GetOwner());
    const UDataTable* PerkTable=Player && Player->Perks?Player->Perks->GetPerkTable():nullptr;
    if (!PerkTable && Player && Player->Perks) PerkTable=Player->Perks->PerkTable.LoadSynchronous();
    const auto* Perk=PerkTable && PerkTable->GetRowStruct()==FMCPerkDefinition::StaticStruct()
        ? PerkTable->FindRow<FMCPerkDefinition>(Item.PerkID,TEXT("Hub booster validation"),false):nullptr;
    return Item.Kind==EMCHubCatalogKind::NextRunBooster && Perk && Perk->MaxStacks>0
        && Perk->Polarity==EMCPerkPolarity::Positive && Perk->ToolUpgrade!=EMCToolUpgrade::Buffer;
}
TArray<FMCHubEconomyCatalogEntry> UMCHubEconomyComponent::GetCatalog() const
{
    TArray<FMCHubEconomyCatalogEntry> Result;
    if (!LoadTables()) return Result;
    for (FName ID:LoadedCatalog->GetRowNames())
        if (const auto* Row=FindCatalogItem(ID);Row && Row->bInShop && IsCatalogUsable(*Row))
        {
            FMCHubEconomyCatalogEntry Entry; Entry.ItemID=ID; Entry.Definition=*Row; Result.Add(Entry);
        }
    Result.Sort([](const FMCHubEconomyCatalogEntry& A,const FMCHubEconomyCatalogEntry& B) {
        return A.Definition.SortOrder!=B.Definition.SortOrder?A.Definition.SortOrder<B.Definition.SortOrder:A.ItemID.LexicalLess(B.ItemID);
    });
    return Result;
}
bool UMCHubEconomyComponent::OwnsCosmetic(FName CosmeticID) const
{
    if (CosmeticID.IsNone()) return true;
    const auto* Row=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),CosmeticID);
    return OwnedCosmetics.Contains(CosmeticID) || (Row && Row->bEnabled && Row->bDefaultUnlocked);
}
int32 UMCHubEconomyComponent::QueuedStacks(FName ItemID) const
{
    return int32(Algo::Count(PendingBoosters,ItemID));
}
bool UMCHubEconomyComponent::CanQueueItem(FName ItemID,const FMCHubCatalogRow& Item,int32 Stacks) const
{
    const auto Rules=GetRules();
    if (Stacks<1 || Stacks>32) return false;
    if (Item.Kind==EMCHubCatalogKind::Cosmetic)
        return !OwnsCosmetic(Item.CosmeticID) && OwnedCosmetics.Num()<FMath::Clamp(Rules.MaximumOwnedCosmetics,1,1024);
    const int64 Units=int64(Stacks)*Item.Quantity;
    return Units<=32 && int64(QueuedStacks(ItemID))+Units<=Item.MaximumQueuedStacks
        && int64(PendingBoosters.Num())+Units<=FMath::Clamp(Rules.MaximumPendingBoosters,1,256);
}
bool UMCHubEconomyComponent::CanPurchase(FName ItemID,FText& Error) const
{
    Error=FText::GetEmpty();
    if (!IsGatheringHub()) { Error=LOCTEXT("HubOnly","Покупки доступны в хабе."); return false; }
    const auto* Item=FindCatalogItem(ItemID);
    if (!Item || !Item->bInShop || !IsCatalogUsable(*Item)) { Error=LOCTEXT("Unavailable","Предмет пока недоступен."); return false; }
    if (!CanQueueItem(ItemID,*Item)) { Error=LOCTEXT("OwnedOrFull","Предмет уже есть или запас заполнен."); return false; }
    if (Coins<Item->Price) { Error=LOCTEXT("InsufficientCoins","Не хватает монет."); return false; }
    return true;
}
bool UMCHubEconomyComponent::TryPurchase(FName ItemID,FText& Error)
{
    if (!IsAuthority() || bMutating || !CanPurchase(ItemID,Error)) return false;
    TGuardValue<bool> Guard(bMutating,true);
    const auto* Item=FindCatalogItem(ItemID);
    Coins-=Item->Price;
    if (Item->Kind==EMCHubCatalogKind::Cosmetic) OwnedCosmetics.AddUnique(Item->CosmeticID);
    else for (int32 I=0;I<Item->Quantity;++I) PendingBoosters.Add(ItemID);
    NotifyChanged(); return true;
}
bool UMCHubEconomyComponent::TryEquipLoadout(const FMCCharacterCosmeticLoadout& Loadout,FText& Error)
{
    if (!IsAuthority() || bMutating || !IsGatheringHub()) return false;
    if (!UMCCosmeticCatalog::ValidateLoadout(UMCCosmeticCatalog::GetDefaultCatalog(),Loadout,Error)) return false;
    for (const auto& Slot:Loadout.Slots)
        if (!OwnsCosmetic(Slot.ItemId)) { Error=LOCTEXT("NotOwned","Этот аксессуар ещё не получен."); return false; }
    TGuardValue<bool> Guard(bMutating,true);
    EquippedLoadout=Loadout; NotifyChanged(); return true;
}
bool UMCHubEconomyComponent::SelectGrillReward(FMCGrillReward& Reward,FText& Error) const
{
    if (!LoadTables()) { Error=LOCTEXT("MissingData","Тестовые таблицы экономики ещё не подготовлены."); return false; }
    TArray<const FMCGrillRewardRow*> Choices;
    double TotalWeight=0;
    TArray<FName> Names=LoadedRewards->GetRowNames(); Names.Sort(FNameLexicalLess());
    for (FName Name:Names)
    {
        const auto* Row=LoadedRewards->FindRow<FMCGrillRewardRow>(Name,TEXT("Grill reward"),false);
        if (!Row || !Row->bEnabled || !FMath::IsFinite(Row->Weight) || Row->Weight<=0 || Row->Weight>1000000
            || Row->ReelSymbols.Num()!=3 || Row->ReelSymbols.Contains(NAME_None)) continue;
        if (Row->Kind==EMCGrillRewardKind::Coins)
        {
            // Exactly 0..0 is an authored losing combination. A 0..N range
            // would ambiguously make the same reel tuple either win or lose.
            if (Row->MinimumCoins<0 || Row->MaximumCoins<Row->MinimumCoins || Row->MaximumCoins>1000000
                || (Row->MinimumCoins==0 && Row->MaximumCoins!=0)) continue;
        }
        else
        {
            const auto* Item=FindCatalogItem(Row->CatalogItem);
            if (Row->Kind!=EMCGrillRewardKind::CatalogItem || !Item || !IsCatalogUsable(*Item) || Row->Quantity<1 || Row->Quantity>32) continue;
        }
        Choices.Add(Row); TotalWeight+=Row->Weight;
    }
    if (Choices.IsEmpty() || !FMath::IsFinite(TotalWeight) || TotalWeight<=0) { Error=LOCTEXT("EmptyRollPool","В таблице нет допустимых наград."); return false; }
    Reward=FMCGrillReward(); Reward.RollID=FGuid::NewGuid();
    FRandomStream Random(int32(GetTypeHash(Reward.RollID)));
    double Pick=double(Random.FRand())*TotalWeight;
    const FMCGrillRewardRow* Selected=Choices.Last();
    for (const auto* Row:Choices) { Pick-=Row->Weight; if (Pick<0) { Selected=Row; break; } }
    Reward.Kind=Selected->Kind; Reward.Rarity=Selected->Rarity; Reward.ReelSymbols=Selected->ReelSymbols;
    if (Reward.Kind==EMCGrillRewardKind::Coins) Reward.Coins=Random.RandRange(Selected->MinimumCoins,Selected->MaximumCoins);
    else { Reward.CatalogItem=Selected->CatalogItem; Reward.Quantity=Selected->Quantity; }
    return true;
}
bool UMCHubEconomyComponent::BeginGrillRoll(FGuid& Receipt,FMCGrillReward& Preview,FText& Error)
{
    Receipt.Invalidate(); Preview=FMCGrillReward(); Error=FText::GetEmpty();
    auto* Player=Cast<AMCPlayerState>(GetOwner());
    if (!IsAuthority() || !Player || bMutating || !IsGatheringHub()) return false;
    if (HasPendingGrillRoll()) { Error=LOCTEXT("AlreadyRolling","Сначала дождись своего предыдущего результата."); return false; }
    if (Player->FishCaught<1) { Error=LOCTEXT("NeedFish","Для попытки нужна одна рыбка."); return false; }
    FMCGrillReward Selected;
    if (!SelectGrillReward(Selected,Error)) return false;
    TGuardValue<bool> Guard(bMutating,true);
    PendingRoll=Selected; --Player->FishCaught;
    Receipt=Selected.RollID; Preview=Selected;
    NotifyChanged(); return true;
}
void UMCHubEconomyComponent::AddCoinsClamped(int32 Amount)
{
    Coins=int32(FMath::Clamp<int64>(int64(Coins)+FMath::Max(0,Amount),0,FMath::Clamp(GetRules().MaximumCoins,1,MAX_int32)));
}
int32 UMCHubEconomyComponent::CreditObjectiveCoins(int32 Amount)
{
    if(!IsAuthority() || bMutating || Amount<=0 || Amount>1000 || !LoadTables()) return 0;
    if(Coins>=FMath::Clamp(GetRules().MaximumCoins,1,MAX_int32)) return 0;
    TGuardValue<bool> Guard(bMutating,true);
    const int32 Before=Coins;
    AddCoinsClamped(Amount);
    const int32 Credited=Coins-Before;
    if(Credited>0) NotifyChanged();
    return Credited;
}
bool UMCHubEconomyComponent::CompleteGrillRoll(const FGuid& Receipt)
{
    if (!IsAuthority() || bMutating || !Receipt.IsValid() || PendingRoll.RollID!=Receipt) return false;
    TGuardValue<bool> Guard(bMutating,true);
    FMCGrillReward Reward=PendingRoll;
    if (Reward.Kind==EMCGrillRewardKind::CatalogItem)
    {
        const auto* Item=FindCatalogItem(Reward.CatalogItem);
        // An already accepted receipt survives suspension; it is stored, never
        // activated. Fresh rolls still exclude suspended items in SelectGrillReward.
        if (!Item || !IsCatalogUsable(*Item,true)) return false;
        if (!CanQueueItem(Reward.CatalogItem,*Item,Reward.Quantity))
        {
            Reward.bConvertedDuplicate=true; Reward.Kind=EMCGrillRewardKind::Coins;
            Reward.Coins=int32(FMath::Clamp<int64>(int64(FMath::Max(0,Item->DuplicateCoins))*Reward.Quantity,0,MAX_int32));
        }
        else if (Item->Kind==EMCHubCatalogKind::Cosmetic) OwnedCosmetics.AddUnique(Item->CosmeticID);
        else for (int32 I=0;I<Reward.Quantity*Item->Quantity;++I) PendingBoosters.Add(Reward.CatalogItem);
    }
    if (Reward.Kind==EMCGrillRewardKind::Coins) AddCoinsClamped(Reward.Coins);
    PendingRoll=FMCGrillReward(); LastGrillReward=MoveTemp(Reward);
    NotifyChanged(); return true;
}
bool UMCHubEconomyComponent::CancelGrillRoll(const FGuid& Receipt)
{
    auto* Player=Cast<AMCPlayerState>(GetOwner());
    if (!IsAuthority() || !Player || bMutating || !Receipt.IsValid() || PendingRoll.RollID!=Receipt) return false;
    TGuardValue<bool> Guard(bMutating,true);
    PendingRoll=FMCGrillReward(); Player->FishCaught=FMath::Min(Player->FishCaught,MAX_int32-1)+1;
    NotifyChanged(); return true;
}
FMCHubEconomySaveData UMCHubEconomyComponent::ExportSave()
{
    if (IsAuthority() && !bMutating && HasPendingGrillRoll()) CompleteGrillRoll(PendingRoll.RollID);
    FMCHubEconomySaveData Data; Data.Coins=Coins; Data.OwnedCosmetics=OwnedCosmetics; Data.PendingBoosters=PendingBoosters;
    Data.EquippedLoadout=EquippedLoadout; Data.LastAppliedRunId=LastAppliedRunId; Data.RunCalciumCharges=RunCalciumCharges;
    Data.PendingGrillReward=PendingRoll; return Data;
}
bool UMCHubEconomyComponent::ImportSave(const FMCHubEconomySaveData& Data,FText& Error)
{
    if (!IsAuthority() || bMutating || Data.Version!=1 || !LoadTables()) return false;
    const auto Rules=GetRules();
    if (Data.OwnedCosmetics.Num()>FMath::Clamp(Rules.MaximumOwnedCosmetics,1,1024)
        || Data.PendingBoosters.Num()>FMath::Clamp(Rules.MaximumPendingBoosters,1,256)) return false;
    if (!UMCCosmeticCatalog::ValidateLoadout(UMCCosmeticCatalog::GetDefaultCatalog(),Data.EquippedLoadout,Error)) return false;
    TArray<FName> ValidOwned;
    for (FName ID:Data.OwnedCosmetics)
        if (const auto* Item=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),ID);Item && Item->bEnabled) ValidOwned.AddUnique(ID);
    for (const auto& Slot:Data.EquippedLoadout.Slots)
        if (!Slot.ItemId.IsNone() && !ValidOwned.Contains(Slot.ItemId))
        {
            const auto* Row=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),Slot.ItemId);
            if (!Row || !Row->bDefaultUnlocked) { Error=LOCTEXT("InvalidOwnedLoadout","В сохранении нет выбранного аксессуара."); return false; }
        }
    TMap<FName,int32> Queued;
    for (FName ItemID:Data.PendingBoosters)
    {
        const auto* Item=FindCatalogItem(ItemID);
        if (!Item || !IsCatalogUsable(*Item,true) || Item->Kind==EMCHubCatalogKind::Cosmetic) return false;
        if (++Queued.FindOrAdd(ItemID)>Item->MaximumQueuedStacks) return false;
    }
    if (Data.PendingGrillReward.RollID.IsValid())
    {
        const auto& Pending=Data.PendingGrillReward;
        if (Pending.ReelSymbols.Num()!=3 || Pending.ReelSymbols.Contains(NAME_None)) return false;
        if (Pending.Kind==EMCGrillRewardKind::CatalogItem)
        {
            const auto* Item=FindCatalogItem(Pending.CatalogItem);
            if (!Item || !IsCatalogUsable(*Item,true) || Pending.Quantity<1 || Pending.Quantity>32) return false;
        }
        else if (Pending.Kind!=EMCGrillRewardKind::Coins || Pending.Coins<0 || Pending.Coins>1000000) return false;
    }
    {
        TGuardValue<bool> Guard(bMutating,true);
        Coins=FMath::Clamp(Data.Coins,0,FMath::Clamp(Rules.MaximumCoins,1,MAX_int32));
        OwnedCosmetics=MoveTemp(ValidOwned);
        PendingBoosters=Data.PendingBoosters; EquippedLoadout=Data.EquippedLoadout;
        LastAppliedRunId=Data.LastAppliedRunId; RunCalciumCharges=FMath::Clamp(Data.RunCalciumCharges,0,3);
        PendingRoll=Data.PendingGrillReward;
    }
    if (HasPendingGrillRoll()) CompleteGrillRoll(PendingRoll.RollID); else NotifyChanged();
    return true;
}
void UMCHubEconomyComponent::CopyTo(UMCHubEconomyComponent* Target)
{
    if (!IsAuthority() || !Target || !Target->IsAuthority()) return;
    const auto Data=ExportSave(); FText Error; Target->ImportSave(Data,Error);
}
bool UMCHubEconomyComponent::ApplyPendingBoostersForRun(const FGuid& RunId,FText& Error)
{
    auto* Player=Cast<AMCPlayerState>(GetOwner());
    if (!IsAuthority() || !Player || !Player->Perks || bMutating || !RunId.IsValid() || !IsRealRun()) return false;
    if (LastAppliedRunId==RunId) return true;
    if (!LoadTables()) return false;
    TArray<FName> PerkIDs, Suspended; int32 Calcium=0;
    for (FName ItemID:PendingBoosters)
    {
        const auto* Item=FindCatalogItem(ItemID);
        if (Item && Item->Kind==EMCHubCatalogKind::Consumable && Item->ConsumableID==TEXT("CalciumSelfGrenade")
            && !IsCalciumEnabled() && IsCatalogUsable(*Item,true))
        {
            Suspended.Add(ItemID);
            continue;
        }
        if (!Item || !IsCatalogUsable(*Item)) { Error=LOCTEXT("InvalidPending","Не удалось применить купленный предмет: проверь таблицу."); return false; }
        if (Item->Kind==EMCHubCatalogKind::Consumable && Item->ConsumableID==TEXT("CalciumSelfGrenade")) ++Calcium;
        else if (Item->Kind==EMCHubCatalogKind::NextRunBooster) PerkIDs.Add(Item->PerkID);
        else return false;
    }
    TGuardValue<bool> Guard(bMutating,true);
    // This batch is personal: ServerGrantReward would spread legendary tools to teammates.
    if (!PerkIDs.IsEmpty() && !Player->Perks->ServerGrantPerks(PerkIDs)) { Error=LOCTEXT("GrantFailed","Бустеры не применены; покупки сохранены для следующего запуска."); return false; }
    PendingBoosters=MoveTemp(Suspended); LastAppliedRunId=RunId;
    if (IsCalciumEnabled()) RunCalciumCharges=FMath::Clamp(Calcium,0,3);
    NotifyChanged(); return true;
}
bool UMCHubEconomyComponent::TryConsumeCalciumCharge()
{
    if (!IsAuthority() || bMutating || !IsRealRun() || !IsCalciumEnabled() || RunCalciumCharges<=0) return false;
    TGuardValue<bool> Guard(bMutating,true); --RunCalciumCharges; NotifyChanged(); return true;
}
void UMCHubEconomyComponent::RefundCalciumCharge()
{
    if (!IsAuthority() || bMutating || !IsRealRun()) return;
    TGuardValue<bool> Guard(bMutating,true); RunCalciumCharges=FMath::Clamp(RunCalciumCharges+1,0,3); NotifyChanged();
}
void UMCHubEconomyComponent::NotifyChanged()
{
    if (GetOwner()) GetOwner()->ForceNetUpdate();
    OnEconomyChanged.Broadcast();
}
void UMCHubEconomyComponent::OnRep_Economy() { OnEconomyChanged.Broadcast(); }
void UMCHubEconomyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,Coins,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,OwnedCosmetics,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,PendingBoosters,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,EquippedLoadout,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,RunCalciumCharges,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,LastGrillReward,COND_OwnerOnly);
}

#undef LOCTEXT_NAMESPACE
