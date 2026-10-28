#include "MCHubEconomyComponent.h"

#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCPerkComponent.h"
#include "MCPerkEffect.h"
#include "MCPlayerState.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/SecureHash.h"
#include "Templates/UnrealTemplate.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "MCHubEconomy"

namespace
{
bool UsesActiveSlot(EMCPerkKind Kind)
{
    return Kind==EMCPerkKind::Active || Kind==EMCPerkKind::Consumable;
}
EMCHubItemRarity HubRarity(EMCPerkRarity Rarity)
{
    switch(Rarity) {
    case EMCPerkRarity::Rare: return EMCHubItemRarity::Rare;
    case EMCPerkRarity::Legendary: return EMCHubItemRarity::Legendary;
    default: return EMCHubItemRarity::Common;
    }
}
bool ValidCoinReward(const FMCGrillRewardRow& Row)
{
    return Row.bEnabled && Row.Kind==EMCGrillRewardKind::Coins
        && FMath::IsFinite(Row.Weight) && Row.Weight>0 && Row.Weight<=1000000
        && Row.ReelSymbols.Num()==3 && !Row.ReelSymbols.Contains(NAME_None)
        && Row.MinimumCoins>=0 && Row.MaximumCoins>=Row.MinimumCoins && Row.MaximumCoins<=1000000
        && (Row.MinimumCoins!=0 || Row.MaximumCoins==0);
}
FGuid LegacyPurchaseIdentity(FName PerkID)
{
    // Unlike a FName index/hash, the textual ID has a stable identity after save/reconnect.
    FGuid Result;
    const FString Digest=FMD5::HashAnsiString(*FString::Printf(TEXT("MCHubLegacy:%s"),*PerkID.ToString()));
    FGuid::ParseExact(Digest,EGuidFormats::Digits,Result);
    return Result;
}
}

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
        && LoadedRules && LoadedRules->GetRowStruct()==FMCHubEconomyRulesRow::StaticStruct();
    const auto* Rules=TablesValid?LoadedRules->FindRow<FMCHubEconomyRulesRow>(TEXT("Default"),TEXT("Hub economy rules"),false):nullptr;
    if (!Rules) return false;
    const float Chances[]={Rules->CommonShopAvailability,Rules->RareShopAvailability,Rules->EpicShopAvailability,Rules->LegendaryShopAvailability};
    for(float Chance:Chances) if(!FMath::IsFinite(Chance) || Chance<0 || Chance>1) return false;
    return Rules->MaximumCoins>0 && Rules->MaximumPendingBoosters>0 && Rules->MaximumPendingBoosters<=256
        && Rules->MaximumOwnedCosmetics>0 && Rules->MaximumOwnedCosmetics<=1024
        && Rules->MaximumShopItems>0 && Rules->MaximumShopItems<=64
        && Rules->MaximumShopPassiveItems>=0 && Rules->MaximumShopPassiveItems<=4
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
    for (FName Name:LoadedRewards->GetRowNames()) {
        const auto* Row=LoadedRewards->FindRow<FMCGrillRewardRow>(Name,TEXT("Grill symbols"),false);
        if(Row && ValidCoinReward(*Row)) for(FName Symbol:Row->ReelSymbols) Result.AddUnique(Symbol);
    }
    return Result;
}
const FMCHubCatalogRow* UMCHubEconomyComponent::FindCatalogItem(FName ItemID) const
{
    return LoadTables() && !ItemID.IsNone()?LoadedCatalog->FindRow<FMCHubCatalogRow>(ItemID,TEXT("Hub catalog"),false):nullptr;
}
bool UMCHubEconomyComponent::ResolvePerk(FName PerkID,FMCPerkDefinition& Definition) const
{
    const auto* Player=Cast<AMCPlayerState>(GetOwner());
    const auto* Perks=Player?Player->Perks.Get():nullptr;
    const UDataTable* Table=Perks?Perks->GetPerkTable():nullptr;
    if(!Table && Perks) Table=Perks->PerkTable.LoadSynchronous();
    const auto* Row=Table && Table->GetRowStruct()==FMCPerkDefinition::StaticStruct() && !PerkID.IsNone()
        ?Table->FindRow<FMCPerkDefinition>(PerkID,TEXT("Hub canonical perk"),false):nullptr;
    if(!Row || !Row->bEnabled) return false;
    Definition=*Row;return true;
}
bool UMCHubEconomyComponent::IsCalciumEnabled() const { return LoadTables() && GetRules().bCalciumEnabled; }
bool UMCHubEconomyComponent::IsCatalogUsable(const FMCHubCatalogRow& Item,bool bIncludeSuspended) const
{
    if (!Item.bEnabled || Item.Price<0 || Item.Quantity<1 || Item.Quantity>32
        || !FMath::IsFinite(Item.StockWeight) || Item.StockWeight<=0 || Item.StockWeight>1000000) return false;
    if (Item.Kind==EMCHubCatalogKind::Cosmetic) {
        const auto* Cosmetic=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),Item.CosmeticID);
        return Cosmetic && Cosmetic->bEnabled && Cosmetic->Mesh && Item.Quantity==1;
    }
    // Retain the suspended calcium feature's old saved inventory; new consumables use PerkID.
    if (Item.Kind==EMCHubCatalogKind::Consumable && Item.PerkID.IsNone())
        return Item.ConsumableID==TEXT("CalciumSelfGrenade") && (bIncludeSuspended || IsCalciumEnabled());
    FMCPerkDefinition Perk;
    if(!ResolvePerk(Item.PerkID,Perk)) return false;
    const EMCPerkKind Kind=UMCPerkComponent::ResolvePerkKind(Perk,Item.PerkID);
    const bool ValidKind=Kind==EMCPerkKind::Passive || Kind==EMCPerkKind::Active || Kind==EMCPerkKind::Consumable || Kind==EMCPerkKind::ToolUpgrade;
    return ValidKind && Perk.bEnabled && Perk.MaxStacks>0 && Perk.Polarity==EMCPerkPolarity::Positive
        && FMath::IsFinite(Perk.CooldownSeconds) && Perk.CooldownSeconds>=0
        && Perk.ToolUpgrade!=EMCToolUpgrade::Buffer && uint8(Perk.ToolUpgrade)<=uint8(EMCToolUpgrade::Watergun)
        && ((Kind==EMCPerkKind::ToolUpgrade)==(Perk.ToolUpgrade!=EMCToolUpgrade::None))
        && (!Perk.EffectClass || !Perk.EffectClass->HasAnyClassFlags(CLASS_Abstract))
        && (Kind==EMCPerkKind::Consumable || Item.Quantity==1)
        && (Kind!=EMCPerkKind::Consumable || (Perk.InitialCharges>0 && int64(Perk.InitialCharges)*Item.Quantity<=10000));
}
bool UMCHubEconomyComponent::FindCatalogEntry(FName ItemID,FMCHubEconomyCatalogEntry& Entry) const
{
    const auto* Row=FindCatalogItem(ItemID);
    if(!Row || !IsCatalogUsable(*Row)) return false;
    Entry=FMCHubEconomyCatalogEntry();Entry.ItemID=ItemID;Entry.Definition=*Row;
    FMCPerkDefinition Perk;
    if(Row->Kind!=EMCHubCatalogKind::Cosmetic && ResolvePerk(Row->PerkID,Perk)) {
        Entry.PerkKind=UMCPerkComponent::ResolvePerkKind(Perk,Row->PerkID);
        Entry.Definition.DisplayName=Perk.DisplayName;Entry.Definition.Description=Perk.Description;
        Entry.Definition.DisplayNameKey=Perk.DisplayNameKey;Entry.Definition.DescriptionKey=Perk.DescriptionKey;
        Entry.Definition.Icon=Perk.Icon;Entry.Definition.Rarity=HubRarity(Perk.Rarity);
    }
    return true;
}
TArray<FMCHubEconomyCatalogEntry> UMCHubEconomyComponent::GetCatalog() const
{
    TArray<FMCHubEconomyCatalogEntry> Result;
    if (!LoadTables()) return Result;
    for(FName ID:LoadedCatalog->GetRowNames()) {
        FMCHubEconomyCatalogEntry Entry;
        if(FindCatalogEntry(ID,Entry) && Entry.Definition.bInShop) Result.Add(MoveTemp(Entry));
    }
    Result.Sort([](const FMCHubEconomyCatalogEntry& A,const FMCHubEconomyCatalogEntry& B) {
        return A.Definition.SortOrder!=B.Definition.SortOrder?A.Definition.SortOrder<B.Definition.SortOrder:A.ItemID.LexicalLess(B.ItemID);
    });
    return Result;
}
bool UMCHubEconomyComponent::IsInShopStock(FName ItemID) const { return bShopStockGenerated && ShopStockIDs.Contains(ItemID); }
TArray<FMCHubEconomyCatalogEntry> UMCHubEconomyComponent::GetShopCatalog() const
{
    TArray<FMCHubEconomyCatalogEntry> Result;
    for(FName ID:ShopStockIDs) {
        FMCHubEconomyCatalogEntry Entry;
        if(FindCatalogEntry(ID,Entry) && Entry.Definition.bInShop && Entry.Definition.Kind!=EMCHubCatalogKind::Cosmetic) Result.Add(MoveTemp(Entry));
    }
    return Result;
}
bool UMCHubEconomyComponent::EnsureShopStock(const FGuid& RotationId,int32 Seed)
{
    if(!IsAuthority() || bMutating || !RotationId.IsValid() || !LoadTables()) return false;
    if(bShopStockGenerated && ShopRotationId==RotationId) {
        // Repair an obsolete catalog once; purchases/reopening never reroll valid stock.
        const bool Obsolete=ShopStockIDs.ContainsByPredicate([this](FName ID) {
            FMCHubEconomyCatalogEntry Entry;
            return !FindCatalogEntry(ID,Entry) || !Entry.Definition.bInShop || Entry.Definition.Kind==EMCHubCatalogKind::Cosmetic;
        });
        if(!Obsolete) return true;
    }
    auto Candidates=GetCatalog();
    Candidates.Sort([](const FMCHubEconomyCatalogEntry& A,const FMCHubEconomyCatalogEntry& B){return A.ItemID.LexicalLess(B.ItemID);});
    const auto Rules=GetRules();
    const float Availability[]={Rules.CommonShopAvailability,Rules.RareShopAvailability,Rules.EpicShopAvailability,Rules.LegendaryShopAvailability};
    FRandomStream Random(Seed);TSet<FName> CanonicalIDs;TArray<FMCHubEconomyCatalogEntry> Eligible;
    for(const auto& Entry:Candidates) {
        if(Entry.Definition.Kind==EMCHubCatalogKind::Cosmetic) continue;
        const FName Canonical=Entry.Definition.PerkID.IsNone()?Entry.ItemID:Entry.Definition.PerkID;
        if(CanonicalIDs.Contains(Canonical)) continue;
        CanonicalIDs.Add(Canonical);
        const int32 Rarity=static_cast<int32>(Entry.Definition.Rarity);
        if(Rarity>=0 && Rarity<UE_ARRAY_COUNT(Availability) && Random.FRand()<Availability[Rarity]) Eligible.Add(Entry);
    }
    TArray<FName> Selected;int32 PassiveCount=0;
    while(!Eligible.IsEmpty() && Selected.Num()<Rules.MaximumShopItems) {
        double TotalWeight=0;for(const auto& Entry:Eligible) TotalWeight+=Entry.Definition.StockWeight;
        double Pick=Random.FRand()*TotalWeight;int32 Index=Eligible.Num()-1;
        for(int32 I=0;I<Eligible.Num();++I) {Pick-=Eligible[I].Definition.StockWeight;if(Pick<0){Index=I;break;}}
        const auto Entry=Eligible[Index];Eligible.RemoveAt(Index);
        if(Entry.PerkKind==EMCPerkKind::Passive && PassiveCount>=Rules.MaximumShopPassiveItems) continue;
        Selected.Add(Entry.ItemID);if(Entry.PerkKind==EMCPerkKind::Passive) ++PassiveCount;
    }
    TGuardValue<bool> Guard(bMutating,true);
    ShopStockIDs=MoveTemp(Selected);ShopRotationId=RotationId;ShopRotationSeed=Seed;bShopStockGenerated=true;
    NotifyChanged();return true;
}
bool UMCHubEconomyComponent::OwnsCosmetic(FName CosmeticID) const
{
    if (CosmeticID.IsNone()) return true;
    const auto* Row=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),CosmeticID);
    return OwnedCosmetics.Contains(CosmeticID) || (Row && Row->bEnabled && Row->bDefaultUnlocked);
}
TArray<FMCHubPendingPurchase> UMCHubEconomyComponent::BuildPendingPlan(TArray<TArray<int32>>* LegacyIndices) const
{
    TArray<FMCHubPendingPurchase> Plan;TSet<FName> IDs;TSet<int32> Occupied;
    if(LegacyIndices) LegacyIndices->Reset();
    const auto* Player=Cast<AMCPlayerState>(GetOwner());const auto* Perks=Player?Player->Perks.Get():nullptr;
    if(!Perks) return Plan;
    TSet<FGuid> AmbiguousInstances,SeenInstances;
    for(const auto& Purchase:PendingPurchases) {if(SeenInstances.Contains(Purchase.InstanceID)) AmbiguousInstances.Add(Purchase.InstanceID);SeenInstances.Add(Purchase.InstanceID);}
    for(const auto& Purchase:PendingPurchases) {
        FMCPerkDefinition Row;
        if(!Purchase.InstanceID.IsValid() || AmbiguousInstances.Contains(Purchase.InstanceID) || IDs.Contains(Purchase.PerkID) || !ResolvePerk(Purchase.PerkID,Row)) continue;
        const auto Kind=UMCPerkComponent::ResolvePerkKind(Row,Purchase.PerkID);
        if(UsesActiveSlot(Kind) && (Purchase.DestinationSlot<0 || Purchase.DestinationSlot>=4 || Occupied.Contains(Purchase.DestinationSlot))) continue;
        if(Kind==EMCPerkKind::Automatic || (Kind==EMCPerkKind::Consumable && (Purchase.Charges<=0 || Purchase.Charges>10000))) continue;
        if(UsesActiveSlot(Kind)) Occupied.Add(Purchase.DestinationSlot);
        IDs.Add(Purchase.PerkID);Plan.Add(Purchase);if(LegacyIndices) LegacyIndices->Add(TArray<int32>());
    }
    TMap<FName,int32> LegacyPlan;
    for(int32 Index=0;Index<PendingBoosters.Num();++Index) {
        const auto* Item=FindCatalogItem(PendingBoosters[Index]);
        if(!Item || Item->PerkID.IsNone() || !IsCatalogUsable(*Item)) continue;
        FMCPerkDefinition Definition;if(!ResolvePerk(Item->PerkID,Definition)) continue;
        const auto Kind=UMCPerkComponent::ResolvePerkKind(Definition,Item->PerkID);
        if(const auto* Existing=LegacyPlan.Find(Item->PerkID)) {
            if(Kind==EMCPerkKind::Consumable && Plan[*Existing].Charges<10000) {
                ++Plan[*Existing].Charges;if(LegacyIndices) (*LegacyIndices)[*Existing].Add(Index);
            }
            continue;
        }
        if(IDs.Contains(Item->PerkID)) continue;
        int32 Slot=INDEX_NONE;
        if(UsesActiveSlot(Kind)) {for(int32 I=0;I<4;++I) if(!Occupied.Contains(I)){Slot=I;break;}if(Slot==INDEX_NONE) continue;}
        FMCHubPendingPurchase Purchase;Purchase.InstanceID=LegacyPurchaseIdentity(Item->PerkID);
        Purchase.CatalogID=PendingBoosters[Index];Purchase.PerkID=Item->PerkID;Purchase.DestinationSlot=Slot;
        Purchase.Charges=Kind==EMCPerkKind::Consumable?1:INDEX_NONE;
        LegacyPlan.Add(Item->PerkID,Plan.Num());IDs.Add(Item->PerkID);if(Slot!=INDEX_NONE) Occupied.Add(Slot);
        Plan.Add(Purchase);if(LegacyIndices) (*LegacyIndices).Add(TArray<int32>{Index});
    }
    return Plan;
}
TArray<FMCActiveSkillSlot> UMCHubEconomyComponent::GetPendingActiveSlots() const
{
    TArray<FMCActiveSkillSlot> Slots;Slots.SetNum(4);
    for(const auto& Purchase:BuildPendingPlan()) if(Slots.IsValidIndex(Purchase.DestinationSlot)) {
        auto& Slot=Slots[Purchase.DestinationSlot];Slot.PerkID=Purchase.PerkID;Slot.InstanceID=Purchase.InstanceID;Slot.Charges=Purchase.Charges;
    }
    return Slots;
}
int32 UMCHubEconomyComponent::QueuedStacks(FName ItemID) const
{
    const auto* Item=FindCatalogItem(ItemID);
    int32 Result=0;
    for(FName LegacyID:PendingBoosters) {
        const auto* Legacy=FindCatalogItem(LegacyID);
        if(LegacyID==ItemID || (Item && Legacy && !Item->PerkID.IsNone() && Legacy->PerkID==Item->PerkID)) ++Result;
    }
    for(const auto& Purchase:PendingPurchases)
        if(Purchase.CatalogID==ItemID || (Item && !Item->PerkID.IsNone() && Purchase.PerkID==Item->PerkID)) ++Result;
    return Result;
}
bool UMCHubEconomyComponent::CanQueueItem(FName ItemID,const FMCHubCatalogRow& Item,int32 Stacks) const
{
    const auto Rules=GetRules();
    if(Stacks<1 || Stacks>32) return false;
    if(Item.Kind==EMCHubCatalogKind::Cosmetic)
        return !OwnsCosmetic(Item.CosmeticID) && OwnedCosmetics.Num()<FMath::Clamp(Rules.MaximumOwnedCosmetics,1,1024);
    if(Item.PerkID.IsNone()) {
        const int64 Units=int64(Stacks)*Item.Quantity;
        return Units<=32 && int64(QueuedStacks(ItemID))+Units<=Item.MaximumQueuedStacks
            && int64(PendingBoosters.Num())+Units<=FMath::Clamp(Rules.MaximumPendingBoosters,1,256);
    }
    // This is a serialization guard, not a cap on the number of passive skills.
    if(PendingPurchases.Num()>=4096 || Stacks!=1) return false;
    for(const auto& Purchase:PendingPurchases) if(Purchase.PerkID==Item.PerkID) return false;
    for(FName LegacyID:PendingBoosters) if(const auto* Legacy=FindCatalogItem(LegacyID);Legacy && Legacy->PerkID==Item.PerkID) return false;
    FMCPerkDefinition NewDefinition;if(!ResolvePerk(Item.PerkID,NewDefinition)) return false;
    if(NewDefinition.ToolUpgrade!=EMCToolUpgrade::None) for(const auto& Purchase:BuildPendingPlan()) {
        FMCPerkDefinition Existing;
        if(ResolvePerk(Purchase.PerkID,Existing) && Existing.ToolUpgrade==NewDefinition.ToolUpgrade) return false;
    }
    return true;
}
bool UMCHubEconomyComponent::CanPurchase(FName ItemID,FText& Error) const
{
    Error=FText::GetEmpty();
    if(!IsGatheringHub()) {Error=LOCTEXT("HubOnly","Покупки доступны в хабе.");return false;}
    const auto* Item=FindCatalogItem(ItemID);
    if(!Item || !Item->bInShop || !IsCatalogUsable(*Item) || (Item->Kind!=EMCHubCatalogKind::Cosmetic && !IsInShopStock(ItemID))) {
        Error=LOCTEXT("Unavailable","Предмет отсутствует в текущем ассортименте.");return false;
    }
    if(!CanQueueItem(ItemID,*Item)) {Error=LOCTEXT("OwnedOrFull","Предмет уже подготовлен или запас заполнен.");return false;}
    if(Coins<Item->Price) {Error=LOCTEXT("InsufficientCoins","Не хватает монет.");return false;}
    return true;
}
bool UMCHubEconomyComponent::CanPurchaseForSlot(FName ItemID,int32 DestinationSlot,FName ExpectedReplacedID,const FGuid& ExpectedInstanceID,FText& Error) const
{
    if(!CanPurchase(ItemID,Error)) return false;
    const auto* Item=FindCatalogItem(ItemID);FMCPerkDefinition Definition;
    const auto Kind=ResolvePerk(Item->PerkID,Definition)?UMCPerkComponent::ResolvePerkKind(Definition,Item->PerkID):EMCPerkKind::Automatic;
    if(!UsesActiveSlot(Kind)) {
        if(DestinationSlot!=INDEX_NONE || !ExpectedReplacedID.IsNone() || ExpectedInstanceID.IsValid()) {
            Error=LOCTEXT("InvalidPassiveSlot","Этот предмет не занимает активный слот.");return false;
        }
        return true;
    }
    const auto Slots=GetPendingActiveSlots();
    if(DestinationSlot==INDEX_NONE) {
        if(!ExpectedReplacedID.IsNone() || ExpectedInstanceID.IsValid()) {Error=LOCTEXT("StaleSlot","Слоты изменились. Выбери слот ещё раз.");return false;}
        for(const auto& Slot:Slots) if(Slot.PerkID.IsNone()) return true;
        Error=LOCTEXT("ChooseReplacement","Все четыре слота заняты. Выбери, какой заменить.");return false;
    }
    if(!Slots.IsValidIndex(DestinationSlot)) {Error=LOCTEXT("BadSlot","Недопустимый слот.");return false;}
    const auto& Slot=Slots[DestinationSlot];
    if(Slot.PerkID!=ExpectedReplacedID || Slot.InstanceID!=ExpectedInstanceID || (!Slot.PerkID.IsNone() && !ExpectedInstanceID.IsValid())) {
        Error=LOCTEXT("StaleSlot","Слоты изменились. Выбери слот ещё раз.");return false;
    }
    return true;
}
bool UMCHubEconomyComponent::TryPurchase(FName ItemID,FText& Error)
{
    return TryPurchaseForSlot(ItemID,INDEX_NONE,NAME_None,FGuid(),Error);
}
bool UMCHubEconomyComponent::TryPurchaseForSlot(FName ItemID,int32 DestinationSlot,FName ExpectedReplacedID,const FGuid& ExpectedInstanceID,FText& Error)
{
    if(!IsAuthority() || bMutating || !CanPurchaseForSlot(ItemID,DestinationSlot,ExpectedReplacedID,ExpectedInstanceID,Error)) return false;
    const auto* Item=FindCatalogItem(ItemID);FMCPerkDefinition Definition;
    const auto Kind=ResolvePerk(Item->PerkID,Definition)?UMCPerkComponent::ResolvePerkKind(Definition,Item->PerkID):EMCPerkKind::Automatic;
    if(UsesActiveSlot(Kind) && DestinationSlot==INDEX_NONE) {
        const auto Slots=GetPendingActiveSlots();for(int32 I=0;I<Slots.Num();++I) if(Slots[I].PerkID.IsNone()){DestinationSlot=I;break;}
    }
    TGuardValue<bool> Guard(bMutating,true);
    if(Item->Kind==EMCHubCatalogKind::Cosmetic) OwnedCosmetics.AddUnique(Item->CosmeticID);
    else if(Item->PerkID.IsNone()) for(int32 I=0;I<Item->Quantity;++I) PendingBoosters.Add(ItemID);
    else {
        if(UsesActiveSlot(Kind) && !ExpectedReplacedID.IsNone()) {
            PendingPurchases.RemoveAll([&](const FMCHubPendingPurchase& P){return P.DestinationSlot==DestinationSlot && P.PerkID==ExpectedReplacedID && P.InstanceID==ExpectedInstanceID;});
            if(ExpectedInstanceID==LegacyPurchaseIdentity(ExpectedReplacedID)) PendingBoosters.RemoveAll([&](FName ID){
                const auto* Existing=FindCatalogItem(ID);return Existing && Existing->PerkID==ExpectedReplacedID;
            });
        }
        FMCHubPendingPurchase Purchase;Purchase.InstanceID=FGuid::NewGuid();Purchase.CatalogID=ItemID;Purchase.PerkID=Item->PerkID;
        Purchase.DestinationSlot=UsesActiveSlot(Kind)?DestinationSlot:INDEX_NONE;
        Purchase.Charges=Kind==EMCPerkKind::Consumable?Definition.InitialCharges*Item->Quantity:INDEX_NONE;
        PendingPurchases.Add(Purchase);
    }
    Coins-=Item->Price;RefreshMigrationNotice();NotifyChanged();return true;
}
bool UMCHubEconomyComponent::TryEquipLoadout(const FMCCharacterCosmeticLoadout& Loadout,FText& Error)
{
    if (!IsAuthority() || bMutating || !IsGatheringHub()) return false;
    if (!UMCCosmeticCatalog::ValidateLoadout(UMCCosmeticCatalog::GetDefaultCatalog(),Loadout,Error)) return false;
    for (const auto& Slot:Loadout.Slots) if (!OwnsCosmetic(Slot.ItemId)) {Error=LOCTEXT("NotOwned","Этот аксессуар ещё не получен.");return false;}
    TGuardValue<bool> Guard(bMutating,true);EquippedLoadout=Loadout;NotifyChanged();return true;
}
bool UMCHubEconomyComponent::SelectGrillReward(FMCGrillReward& Reward,FText& Error) const
{
    if(!LoadTables()) {Error=LOCTEXT("MissingData","Таблицы экономики ещё не подготовлены.");return false;}
    TArray<const FMCGrillRewardRow*> Choices;double TotalWeight=0;
    auto Names=LoadedRewards->GetRowNames();Names.Sort(FNameLexicalLess());
    for(FName Name:Names) if(const auto* Row=LoadedRewards->FindRow<FMCGrillRewardRow>(Name,TEXT("Grill reward"),false);Row && ValidCoinReward(*Row)) {
        Choices.Add(Row);TotalWeight+=Row->Weight;
    }
    if(Choices.IsEmpty() || !FMath::IsFinite(TotalWeight) || TotalWeight<=0) {Error=LOCTEXT("EmptyRollPool","В таблице нет допустимых денежных наград.");return false;}
    Reward=FMCGrillReward();Reward.RollID=FGuid::NewGuid();FRandomStream Random(int32(GetTypeHash(Reward.RollID)));
    double Pick=Random.FRand()*TotalWeight;const auto* Selected=Choices.Last();
    for(const auto* Row:Choices) {Pick-=Row->Weight;if(Pick<0){Selected=Row;break;}}
    Reward.Kind=EMCGrillRewardKind::Coins;Reward.Rarity=Selected->Rarity;Reward.ReelSymbols=Selected->ReelSymbols;
    Reward.Coins=Random.RandRange(Selected->MinimumCoins,Selected->MaximumCoins);return true;
}
bool UMCHubEconomyComponent::BeginGrillRoll(FGuid& Receipt,FMCGrillReward& Preview,FText& Error)
{
    Receipt.Invalidate();Preview=FMCGrillReward();Error=FText::GetEmpty();auto* Player=Cast<AMCPlayerState>(GetOwner());
    if(!IsAuthority() || !Player || bMutating || !IsGatheringHub()) return false;
    if(HasPendingGrillRoll()) {Error=MigrationNotice.IsEmpty()?LOCTEXT("AlreadyRolling","Сначала дождись своего предыдущего результата."):MigrationNotice;return false;}
    if(Player->FishCaught<1) {Error=LOCTEXT("NeedFish","Для попытки нужна одна рыбка.");return false;}
    FMCGrillReward Selected;if(!SelectGrillReward(Selected,Error)) return false;
    TGuardValue<bool> Guard(bMutating,true);PendingRoll=Selected;--Player->FishCaught;
    Receipt=Selected.RollID;Preview=Selected;NotifyChanged();return true;
}
void UMCHubEconomyComponent::AddCoinsClamped(int32 Amount)
{
    Coins=int32(FMath::Clamp<int64>(int64(Coins)+FMath::Max(0,Amount),0,FMath::Clamp(GetRules().MaximumCoins,1,MAX_int32)));
}
int32 UMCHubEconomyComponent::CreditObjectiveCoins(int32 Amount)
{
    if(!IsAuthority() || bMutating || Amount<=0 || Amount>1000 || !LoadTables() || Coins>=GetRules().MaximumCoins) return 0;
    TGuardValue<bool> Guard(bMutating,true);const int32 Before=Coins;AddCoinsClamped(Amount);const int32 Credited=Coins-Before;
    if(Credited>0) NotifyChanged();return Credited;
}
bool UMCHubEconomyComponent::CompleteGrillRoll(const FGuid& Receipt)
{
    if(!IsAuthority() || bMutating || !Receipt.IsValid() || PendingRoll.RollID!=Receipt) return false;
    TGuardValue<bool> Guard(bMutating,true);FMCGrillReward Reward=PendingRoll;
    if(Reward.Kind==EMCGrillRewardKind::CatalogItem) {
        const auto* Item=FindCatalogItem(Reward.CatalogItem);
        // Old accepted receipts remain valuable. Unknown IDs are quarantined without inventing a price.
        if(!Item || Item->DuplicateCoins<0 || Reward.Quantity<1 || Reward.Quantity>32) {RefreshMigrationNotice();NotifyChanged();return false;}
        Reward.bConvertedDuplicate=true;Reward.Kind=EMCGrillRewardKind::Coins;
        Reward.Coins=int32(FMath::Clamp<int64>(int64(Item->DuplicateCoins)*Reward.Quantity,0,MAX_int32));
    }
    if(Reward.Kind!=EMCGrillRewardKind::Coins || Reward.Coins<0) return false;
    AddCoinsClamped(Reward.Coins);PendingRoll=FMCGrillReward();LastGrillReward=MoveTemp(Reward);
    RefreshMigrationNotice();NotifyChanged();return true;
}
bool UMCHubEconomyComponent::CancelGrillRoll(const FGuid& Receipt)
{
    auto* Player=Cast<AMCPlayerState>(GetOwner());
    if(!IsAuthority() || !Player || bMutating || !Receipt.IsValid() || PendingRoll.RollID!=Receipt) return false;
    TGuardValue<bool> Guard(bMutating,true);PendingRoll=FMCGrillReward();Player->FishCaught=FMath::Min(Player->FishCaught,MAX_int32-1)+1;
    RefreshMigrationNotice();NotifyChanged();return true;
}
void UMCHubEconomyComponent::RefreshMigrationNotice()
{
    const auto Plan=BuildPendingPlan();int32 UnappliedLegacy=PendingBoosters.Num();
    for(const auto& Purchase:Plan) if(Purchase.InstanceID==LegacyPurchaseIdentity(Purchase.PerkID))
        UnappliedLegacy-=Purchase.Charges>0?Purchase.Charges:1;
    int32 InvalidNew=0;for(const auto& Purchase:PendingPurchases)
        if(!Plan.ContainsByPredicate([&](const auto& P){return P.InstanceID==Purchase.InstanceID;})) ++InvalidNew;
    const bool UnknownReceipt=PendingRoll.RollID.IsValid() && PendingRoll.Kind==EMCGrillRewardKind::CatalogItem && !FindCatalogItem(PendingRoll.CatalogItem);
    if(UnknownReceipt) MigrationNotice=LOCTEXT("LegacyReceiptQuarantined","Старая награда сохранена, но её ID отсутствует в каталоге. Нужна проверка таблицы; монеты и покупки сохранены.");
    else if(UnappliedLegacy>0 || InvalidNew>0) MigrationNotice=LOCTEXT("LegacyPurchasesRetained","Часть прежних покупок пока не применена. Они сохранены до проверки совместимости или свободного слота.");
    else MigrationNotice=FText::GetEmpty();
}
FMCHubEconomySaveData UMCHubEconomyComponent::ExportSave()
{
    if(IsAuthority() && !bMutating && HasPendingGrillRoll()) CompleteGrillRoll(PendingRoll.RollID);
    FMCHubEconomySaveData Data;Data.Coins=Coins;Data.OwnedCosmetics=OwnedCosmetics;Data.PendingBoosters=PendingBoosters;
    Data.PendingPurchases=PendingPurchases;Data.ShopRotationId=ShopRotationId;Data.ShopRotationSeed=ShopRotationSeed;
    Data.bShopStockGenerated=bShopStockGenerated;Data.ShopStockIDs=ShopStockIDs;Data.EquippedLoadout=EquippedLoadout;
    Data.LastAppliedRunId=LastAppliedRunId;Data.RunCalciumCharges=RunCalciumCharges;Data.PendingGrillReward=PendingRoll;return Data;
}
bool UMCHubEconomyComponent::ImportSave(const FMCHubEconomySaveData& Data,FText& Error)
{
    Error=FText::GetEmpty();
    if(!IsAuthority() || bMutating || (Data.Version!=1 && Data.Version!=2) || !LoadTables()) return false;
    const auto Rules=GetRules();
    if(Data.OwnedCosmetics.Num()>FMath::Clamp(Rules.MaximumOwnedCosmetics,1,1024)
        || Data.PendingBoosters.Num()>4096 || Data.PendingPurchases.Num()>4096 || Data.ShopStockIDs.Num()>256) return false;
    if(!UMCCosmeticCatalog::ValidateLoadout(UMCCosmeticCatalog::GetDefaultCatalog(),Data.EquippedLoadout,Error)) return false;
    TArray<FName> ValidOwned;
    for(FName ID:Data.OwnedCosmetics) if(const auto* Item=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),ID);Item && Item->bEnabled) ValidOwned.AddUnique(ID);
    for(const auto& Slot:Data.EquippedLoadout.Slots) if(!Slot.ItemId.IsNone() && !ValidOwned.Contains(Slot.ItemId)) {
        const auto* Row=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),Slot.ItemId);
        if(!Row || !Row->bDefaultUnlocked) {Error=LOCTEXT("InvalidOwnedLoadout","В сохранении нет выбранного аксессуара.");return false;}
    }
    // Unknown legacy IDs do not invalidate an otherwise readable wallet/profile.
    if(Data.PendingGrillReward.RollID.IsValid()) {
        const auto& Pending=Data.PendingGrillReward;
        if(Pending.ReelSymbols.Num()!=3 || Pending.ReelSymbols.Contains(NAME_None)) return false;
        if(Pending.Kind==EMCGrillRewardKind::CatalogItem) {if(Pending.Quantity<1 || Pending.Quantity>32) return false;}
        else if(Pending.Kind!=EMCGrillRewardKind::Coins || Pending.Coins<0 || Pending.Coins>1000000) return false;
    }
    {
        TGuardValue<bool> Guard(bMutating,true);
        Coins=FMath::Clamp(Data.Coins,0,FMath::Clamp(Rules.MaximumCoins,1,MAX_int32));OwnedCosmetics=MoveTemp(ValidOwned);
        PendingBoosters=Data.PendingBoosters;PendingPurchases=Data.PendingPurchases;EquippedLoadout=Data.EquippedLoadout;
        ShopRotationId=Data.ShopRotationId;ShopRotationSeed=Data.ShopRotationSeed;bShopStockGenerated=Data.bShopStockGenerated && ShopRotationId.IsValid();
        ShopStockIDs=Data.ShopStockIDs;LastAppliedRunId=Data.LastAppliedRunId;RunCalciumCharges=FMath::Clamp(Data.RunCalciumCharges,0,3);
        PendingRoll=Data.PendingGrillReward;RefreshMigrationNotice();
    }
    if(HasPendingGrillRoll()) CompleteGrillRoll(PendingRoll.RollID);
    RefreshMigrationNotice();Error=MigrationNotice;NotifyChanged();return true;
}
void UMCHubEconomyComponent::CopyTo(UMCHubEconomyComponent* Target)
{
    if(!IsAuthority() || !Target || !Target->IsAuthority()) return;
    const auto Data=ExportSave();FText Error;Target->ImportSave(Data,Error);
}
bool UMCHubEconomyComponent::ApplyPendingBoostersForRun(const FGuid& RunId,FText& Error)
{
    Error=FText::GetEmpty();auto* Player=Cast<AMCPlayerState>(GetOwner());
    if(!IsAuthority() || !Player || !Player->Perks || bMutating || !RunId.IsValid() || !IsRealRun() || !LoadTables()) return false;
    if(LastAppliedRunId==RunId) return true;
    TArray<TArray<int32>> Sources;const auto Plan=BuildPendingPlan(&Sources);
    TArray<FMCPerkGrant> Grants;TArray<FGuid> AppliedPurchases;TSet<int32> AppliedLegacy;
    bool bDeferred=false;TArray<int32> Order;
    for(int32 I=0;I<Plan.Num();++I) Order.Add(I);
    // Validate purchased tool prerequisites together before dependent passive rows.
    Order.StableSort([&](int32 A,int32 B){
        return Player->Perks->GetPerkKind(Plan[A].PerkID)==EMCPerkKind::ToolUpgrade
            && Player->Perks->GetPerkKind(Plan[B].PerkID)!=EMCPerkKind::ToolUpgrade;
    });
    for(int32 I:Order) {
        const auto& Purchase=Plan[I];FMCPerkGrant Grant;Grant.PerkID=Purchase.PerkID;
        Grant.DestinationSlot=Purchase.DestinationSlot;Grant.Charges=Purchase.Charges;
        auto Proposed=Grants;Proposed.Add(Grant);
        if(!Player->Perks->CanAcquirePerkBatch(Proposed)) {bDeferred=true;continue;}
        Grants.Add(Grant);
        if(Sources[I].IsEmpty()) AppliedPurchases.Add(Purchase.InstanceID);
        else for(int32 Index:Sources[I]) AppliedLegacy.Add(Index);
    }
    int32 Calcium=0;
    if(IsCalciumEnabled()) for(int32 I=0;I<PendingBoosters.Num();++I) {
        const auto* Item=FindCatalogItem(PendingBoosters[I]);
        if(Item && Item->PerkID.IsNone() && Item->Kind==EMCHubCatalogKind::Consumable && Item->ConsumableID==TEXT("CalciumSelfGrenade") && Calcium<3) {
            ++Calcium;AppliedLegacy.Add(I);
        }
    }
    TGuardValue<bool> Guard(bMutating,true);
    // A personal prepared batch deliberately does not use the team-fanout reward entry point.
    if(!Grants.IsEmpty() && !Player->Perks->TryAcquirePerkBatch(Grants)) {Error=LOCTEXT("GrantFailed","Бустеры не применены; покупки сохранены для следующего запуска.");return false;}
    PendingPurchases.RemoveAll([&](const auto& P){return AppliedPurchases.Contains(P.InstanceID);});
    for(int32 I=PendingBoosters.Num()-1;I>=0;--I) if(AppliedLegacy.Contains(I)) PendingBoosters.RemoveAt(I);
    LastAppliedRunId=RunId;if(IsCalciumEnabled()) RunCalciumCharges=Calcium;
    RefreshMigrationNotice();
    if(bDeferred && MigrationNotice.IsEmpty()) MigrationNotice=LOCTEXT("DeferredPurchases","Часть покупок не подошла текущему забегу и сохранена для следующего.");
    Error=MigrationNotice;NotifyChanged();return true;
}
bool UMCHubEconomyComponent::TryConsumeCalciumCharge()
{
    if(!IsAuthority() || bMutating || !IsRealRun() || !IsCalciumEnabled() || RunCalciumCharges<=0) return false;
    TGuardValue<bool> Guard(bMutating,true);--RunCalciumCharges;NotifyChanged();return true;
}
void UMCHubEconomyComponent::RefundCalciumCharge()
{
    if(!IsAuthority() || bMutating || !IsRealRun()) return;
    TGuardValue<bool> Guard(bMutating,true);RunCalciumCharges=FMath::Clamp(RunCalciumCharges+1,0,3);NotifyChanged();
}
void UMCHubEconomyComponent::NotifyChanged() {if(GetOwner()) GetOwner()->ForceNetUpdate();OnEconomyChanged.Broadcast();}
void UMCHubEconomyComponent::OnRep_Economy() {OnEconomyChanged.Broadcast();}
void UMCHubEconomyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,Coins,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,OwnedCosmetics,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,PendingBoosters,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,PendingPurchases,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,ShopRotationId,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,ShopRotationSeed,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,bShopStockGenerated,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,ShopStockIDs,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,MigrationNotice,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,EquippedLoadout,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,RunCalciumCharges,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,LastGrillReward,COND_OwnerOnly);
}

#undef LOCTEXT_NAMESPACE
