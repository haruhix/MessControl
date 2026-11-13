#include "MCHubEconomyComponent.h"

#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCHubProfileSubsystem.h"
#include "MCPerkComponent.h"
#include "MCPerkEffect.h"
#include "MCPlayerState.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
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
bool ValidGrillReceipt(const FMCGrillReward& Reward)
{
    if(!Reward.RollID.IsValid() || Reward.Coins<0) return false;
    if(Reward.bCatalogGrant) {
        if(Reward.Kind!=EMCGrillRewardKind::CatalogItem || Reward.CatalogItem.IsNone() || Reward.Quantity<1 || Reward.Quantity>32) return false;
        const auto& Purchase=Reward.GrantedPurchase;
        if(Reward.bConvertedDuplicate) return Reward.GrantedCosmeticID.IsNone() && Purchase.PerkID.IsNone() && Reward.GrantedRaidCopies==0;
        if(Reward.Coins!=0) return false;
        if(!Reward.GrantedCosmeticID.IsNone()) return Reward.Quantity==1 && Purchase.PerkID.IsNone() && Reward.GrantedRaidCopies==0;
        if(Reward.GrantedRaidCopies<0 || Reward.GrantedRaidCopies>1024) return false;
        if(Reward.GrantedRaidCopies>0 && (Purchase.DestinationSlot!=INDEX_NONE || Purchase.Charges!=INDEX_NONE)) return false;
        const bool bActiveSlot=Purchase.DestinationSlot>=0 && Purchase.DestinationSlot<4;
        return !Purchase.PerkID.IsNone() && Purchase.CatalogID==Reward.CatalogItem && Purchase.InstanceID==Reward.RollID
            && (Purchase.DestinationSlot==INDEX_NONE || bActiveSlot)
            && (Purchase.Charges==INDEX_NONE || (bActiveSlot && Purchase.Charges>0 && Purchase.Charges<=10000));
    }
    if(Reward.Kind==EMCGrillRewardKind::Trash) return Reward.Coins==0;
    if(Reward.ReelSymbols.Num()!=3 || Reward.ReelSymbols.Contains(NAME_None)) return false;
    if(Reward.Kind==EMCGrillRewardKind::CatalogItem) return Reward.Quantity>=1 && Reward.Quantity<=32;
    return Reward.Kind==EMCGrillRewardKind::Coins && Reward.Coins<=1000000;
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
    if (!LoadedCosmetics) LoadedCosmetics=UMCCosmeticCatalog::GetDefaultCatalog();
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
    if(!LoadTables() || ItemID.IsNone()) return nullptr;
    const auto* Legacy=LoadedCatalog->FindRow<FMCHubCatalogRow>(ItemID,TEXT("Hub catalog"),false);
    // Existing non-clothing IDs keep their original source/validation, even if a malformed garment claims one.
    if(Legacy && Legacy->Kind!=EMCHubCatalogKind::Cosmetic) return Legacy;
    const FMCCosmeticCatalogRow* Clothing=nullptr;FName CosmeticId=NAME_None;
    if(LoadedCosmetics && LoadedCosmetics->GetRowStruct()==FMCCosmeticCatalogRow::StaticStruct())
        for(const auto& Pair:LoadedCosmetics->GetRowMap()) {
            const auto* Candidate=reinterpret_cast<const FMCCosmeticCatalogRow*>(Pair.Value);
            if(!Candidate || !Candidate->bUseCanonicalCommerce) continue;
            if(UMCCosmeticCatalog::ShopItemIdForRow(Pair.Key,*Candidate)!=ItemID) continue;
            // Ambiguous canonical purchase IDs must never debit/grant the wrong cosmetic.
            if(Clothing) return nullptr;Clothing=Candidate;CosmeticId=Pair.Key;
        }
    if(!Clothing) {
        // Once migrated, an obsolete Hub alias must not reintroduce independently editable old commerce fields.
        const auto* Linked=Legacy && Legacy->Kind==EMCHubCatalogKind::Cosmetic?UMCCosmeticCatalog::Find(LoadedCosmetics,Legacy->CosmeticID):nullptr;
        return Linked && Linked->bUseCanonicalCommerce?nullptr:Legacy;
    }
    if(Legacy && (Legacy->Kind!=EMCHubCatalogKind::Cosmetic || Legacy->CosmeticID!=CosmeticId)) return nullptr;
    auto& Cached=EffectiveClothingOffers.FindOrAdd(ItemID);if(!Cached) Cached=MakeUnique<FMCHubCatalogRow>();
    // Retain opaque legacy receipt metadata, then replace every clothing-commerce field from the one canonical row.
    *Cached=Legacy?*Legacy:FMCHubCatalogRow();
    Cached->bEnabled=Clothing->bEnabled;Cached->bInShop=Clothing->bInShop;Cached->Kind=EMCHubCatalogKind::Cosmetic;
    Cached->DisplayName=Clothing->DisplayName;Cached->Description=Clothing->Description;
    Cached->DisplayNameKey=Clothing->DisplayNameKey;Cached->DescriptionKey=Clothing->DescriptionKey;
    Cached->Icon=Clothing->Icon;Cached->Rarity=Clothing->Rarity;Cached->Price=UMCCosmeticCatalog::EffectivePrice(*Clothing);
    Cached->CosmeticID=CosmeticId;Cached->PerkID=NAME_None;Cached->ConsumableID=NAME_None;
    Cached->Quantity=1;Cached->MaximumQueuedStacks=1;Cached->StockWeight=1.f;Cached->SortOrder=Clothing->SortOrder;
    return Cached.Get();
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
        const auto* Cosmetic=UMCCosmeticCatalog::Find(LoadedCosmetics,Item.CosmeticID);FText CommerceError;
        return Cosmetic && Cosmetic->bEnabled && Cosmetic->Mesh && Item.Quantity==1
            && (!Cosmetic->bUseCanonicalCommerce || UMCCosmeticCatalog::ValidateCommerce(*Cosmetic,CommerceError));
    }
    // Retain the suspended calcium feature's old saved inventory; new consumables use PerkID.
    if (Item.Kind==EMCHubCatalogKind::Consumable && Item.PerkID.IsNone())
        return Item.ConsumableID==TEXT("CalciumSelfGrenade") && (bIncludeSuspended || IsCalciumEnabled());
    FMCPerkDefinition Perk;
    if(!ResolvePerk(Item.PerkID,Perk)) return false;
    const EMCPerkKind Kind=UMCPerkComponent::ResolvePerkKind(Perk,Item.PerkID);
    const bool ValidKind=Kind==EMCPerkKind::Passive || Kind==EMCPerkKind::Active || Kind==EMCPerkKind::Consumable || Kind==EMCPerkKind::ToolUpgrade;
    // A deliberately authored negative passive may be sold with its explicit
    // risk description. Its polarity still excludes ordinary positive chests.
    const bool StorePolarity=Perk.Polarity==EMCPerkPolarity::Positive
        || (Kind==EMCPerkKind::Passive && Perk.bRoguePerk && Perk.Polarity==EMCPerkPolarity::Negative);
    return ValidKind && Perk.bEnabled && Perk.MaxStacks>0 && StorePolarity
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
    if(Row->Kind==EMCHubCatalogKind::Cosmetic) if(const auto* Clothing=UMCCosmeticCatalog::Find(LoadedCosmetics,Row->CosmeticID);Clothing && Clothing->bUseCanonicalCommerce) {
        Entry.bCanonicalClothing=true;Entry.BasePrice=Clothing->BasePrice;Entry.DiscountPercent=Clothing->DiscountPercent;Entry.ShopAppearanceChance=Clothing->ShopAppearanceChance;
    }
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
    TArray<FName> CatalogIds=LoadedCatalog->GetRowNames();
    if(LoadedCosmetics && LoadedCosmetics->GetRowStruct()==FMCCosmeticCatalogRow::StaticStruct())
        for(const auto& Pair:LoadedCosmetics->GetRowMap()) {
            const auto* Clothing=reinterpret_cast<const FMCCosmeticCatalogRow*>(Pair.Value);
            if(Clothing && Clothing->bUseCanonicalCommerce) CatalogIds.AddUnique(UMCCosmeticCatalog::ShopItemIdForRow(Pair.Key,*Clothing));
        }
    for(FName ID:CatalogIds) {
        FMCHubEconomyCatalogEntry Entry;
        if(FindCatalogEntry(ID,Entry) && Entry.Definition.bInShop) Result.Add(MoveTemp(Entry));
    }
    Result.Sort([](const FMCHubEconomyCatalogEntry& A,const FMCHubEconomyCatalogEntry& B) {
        return A.Definition.SortOrder!=B.Definition.SortOrder?A.Definition.SortOrder<B.Definition.SortOrder:A.ItemID.LexicalLess(B.ItemID);
    });
    return Result;
}
bool UMCHubEconomyComponent::IsInShopStock(FName ItemID) const
{
    if(!bShopStockGenerated) return false;
    const auto* Item=FindCatalogItem(ItemID);
    if(Item && UsesCanonicalClothingStock(*Item)) {
        const auto* Clothing=UMCCosmeticCatalog::Find(LoadedCosmetics,Item->CosmeticID);
        return Clothing && Item->bInShop && IsCatalogUsable(*Item) && IsClothingInShopRotation(ItemID,*Clothing);
    }
    return ShopStockIDs.Contains(ItemID);
}
TArray<FMCHubEconomyCatalogEntry> UMCHubEconomyComponent::GetShopCatalog() const
{
    TArray<FMCHubEconomyCatalogEntry> Result;
    for(FName ID:ShopStockIDs) {
        FMCHubEconomyCatalogEntry Entry;
        if(FindCatalogEntry(ID,Entry) && Entry.Definition.bInShop && Entry.Definition.Kind!=EMCHubCatalogKind::Cosmetic) Result.Add(MoveTemp(Entry));
    }
    return Result;
}
bool UMCHubEconomyComponent::UsesCanonicalClothingStock(const FMCHubCatalogRow& Item) const
{
    const auto* Clothing=Item.Kind==EMCHubCatalogKind::Cosmetic?UMCCosmeticCatalog::Find(LoadedCosmetics,Item.CosmeticID):nullptr;
    return Clothing && Clothing->bUseCanonicalCommerce;
}
bool UMCHubEconomyComponent::IsClothingInShopRotation(FName ItemID,const FMCCosmeticCatalogRow& Clothing) const
{
    if(!bShopStockGenerated || !ShopRotationId.IsValid() || ItemID.IsNone() || !Clothing.bEnabled || !Clothing.bInShop) return false;
    const float Chance=Clothing.ShopAppearanceChance;
    if(!FMath::IsFinite(Chance) || Chance<=0.f) return false;
    if(Chance>=1.f) return true;
    // Separate derived membership: clothing IDs never enter the persisted booster stock.
    // Retiring/remapping clothing cannot invalidate or reroll the original assortment.
    // One stable Bernoulli per purchase ID; no shared stream or appearance cap.
    const FString Identity=FString::Printf(TEXT("MCClothingStockV1:%s:%d:%s"),*ShopRotationId.ToString(EGuidFormats::Digits),ShopRotationSeed,*ItemID.ToString().ToLower());
    const FTCHARToUTF8 Bytes(*Identity);FMD5 Hash;uint8 Digest[16];Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());Hash.Final(Digest);
    const uint32 Value=(uint32(Digest[0])<<24)|(uint32(Digest[1])<<16)|(uint32(Digest[2])<<8)|uint32(Digest[3]);
    return double(Value)/4294967296.0<double(Chance);
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
        if(IsRaidPerkKind(Row,Kind)) continue;
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
        if(IsRaidPerkKind(Definition,Kind)) continue;
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
    int32 Result=Item?GetRaidPerkCopies(Item->PerkID):0;
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
    FMCPerkDefinition NewDefinition;
    if(!ResolvePerk(Item.PerkID,NewDefinition)) return false;
    if(IsRaidPerkKind(NewDefinition,UMCPerkComponent::ResolvePerkKind(NewDefinition,Item.PerkID)))
        return CanAddRaidPerkCopies(Item.PerkID,Stacks*Item.Quantity);
    // This is a serialization guard, not a cap on the number of passive skills.
    if(PendingPurchases.Num()>=4096 || Stacks!=1) return false;
    for(const auto& Purchase:PendingPurchases) if(Purchase.PerkID==Item.PerkID) return false;
    for(FName LegacyID:PendingBoosters) if(const auto* Legacy=FindCatalogItem(LegacyID);Legacy && Legacy->PerkID==Item.PerkID) return false;
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
    if(PendingRoll.bCatalogGrant && !PendingRoll.bConvertedDuplicate) {
        Error=LOCTEXT("WaitForSlotReward","Дождись выдачи приза из автомата.");return false;
    }
    const auto* Item=FindCatalogItem(ItemID);
    if(!Item || !Item->bInShop || !IsCatalogUsable(*Item) || ((Item->Kind!=EMCHubCatalogKind::Cosmetic || UsesCanonicalClothingStock(*Item)) && !IsInShopStock(ItemID))) {
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
    if(!UsesActiveSlot(Kind) || IsRaidPerkKind(Definition,Kind)) {
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
    const bool bRaidPerk=IsRaidPerkKind(Definition,Kind);
    if(!bRaidPerk && UsesActiveSlot(Kind) && DestinationSlot==INDEX_NONE) {
        const auto Slots=GetPendingActiveSlots();for(int32 I=0;I<Slots.Num();++I) if(Slots[I].PerkID.IsNone()){DestinationSlot=I;break;}
    }
    TGuardValue<bool> Guard(bMutating,true);
    if(Item->Kind==EMCHubCatalogKind::Cosmetic) OwnedCosmetics.AddUnique(Item->CosmeticID);
    else if(bRaidPerk) AddRaidPerkCopies(Item->PerkID,Item->Quantity);
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
bool UMCHubEconomyComponent::BuildGrillCatalogGrant(const FMCGrillRewardRow& Row,FMCGrillReward& Reward) const
{
    FMCHubEconomyCatalogEntry Entry;
    if(Row.Quantity<1 || Row.Quantity>32 || !FindCatalogEntry(Row.CatalogItem,Entry)) return false;
    const auto& Item=Entry.Definition;
    const bool bCosmetic=Item.Kind==EMCHubCatalogKind::Cosmetic;
    if(!bCosmetic && Item.PerkID.IsNone()) return false; // Retired consumables use the legacy receipt path only.
    FMCPerkDefinition RaidDefinition;
    const bool bRaidPerk=!bCosmetic && GetRaidPerkDefinition(Item.PerkID,RaidDefinition);
    if(Row.Quantity!=1 && Entry.PerkKind!=EMCPerkKind::Consumable) return false;

    Reward.bCatalogGrant=true;
    Reward.CatalogItem=Row.CatalogItem;
    Reward.Rarity=Item.Rarity;
    Reward.Quantity=Row.Quantity;
    Reward.bConvertedDuplicate=bCosmetic?OwnsCosmetic(Item.CosmeticID):!bRaidPerk && QueuedStacks(Row.CatalogItem)>0;
    if(Reward.bConvertedDuplicate) {
        const int32 UnitPrice=Entry.bCanonicalClothing?Entry.BasePrice:Item.Price;
        Reward.Coins=int32(FMath::Min<int64>(int64(UnitPrice)*Row.Quantity/10,MAX_int32));
        return true;
    }
    if(!CanQueueItem(Row.CatalogItem,Item,bRaidPerk?Row.Quantity:1)) return false;
    if(bCosmetic) {
        Reward.GrantedCosmeticID=Item.CosmeticID;
        return true;
    }

    auto& Purchase=Reward.GrantedPurchase;
    Purchase.CatalogID=Row.CatalogItem;
    Purchase.PerkID=Item.PerkID;
    if(bRaidPerk) {
        Reward.GrantedRaidCopies=Row.Quantity*Item.Quantity;
        return true;
    }
    if(UsesActiveSlot(Entry.PerkKind)) {
        // An unattended roll cannot replace an already prepared item.
        Purchase.DestinationSlot=GetPendingActiveSlots().IndexOfByPredicate([](const FMCActiveSkillSlot& Slot){return Slot.PerkID.IsNone();});
        if(Purchase.DestinationSlot==INDEX_NONE) return false;
    }
    if(Entry.PerkKind==EMCPerkKind::Consumable) {
        FMCPerkDefinition Perk;
        if(!ResolvePerk(Item.PerkID,Perk)) return false;
        const int64 Charges=int64(Perk.InitialCharges)*Item.Quantity*Row.Quantity;
        if(Charges<1 || Charges>10000) return false;
        Purchase.Charges=int32(Charges);
    }
    return true;
}
TArray<UMCHubEconomyComponent::FGrillCandidate> UMCHubEconomyComponent::BuildGrillPool() const
{
    TArray<FGrillCandidate> Choices;
    if(!LoadTables()) return Choices;
    auto Names=LoadedRewards->GetRowNames();Names.Sort(FNameLexicalLess());
    for(FName Name:Names) {
        const auto* Row=LoadedRewards->FindRow<FMCGrillRewardRow>(Name,TEXT("Grill reward"),false);
        if(!Row || !Row->bEnabled || !FMath::IsFinite(Row->Weight) || Row->Weight<=0 || Row->Weight>1000000) continue;
        FMCGrillReward Reward;
        Reward.Kind=Row->Kind;
        Reward.Rarity=Row->Rarity;
        Reward.ReelSymbols=Row->ReelSymbols;
        switch(Row->Kind) {
        case EMCGrillRewardKind::Coins:
            if(!ValidCoinReward(*Row)) continue;
            Reward.Coins=Row->MinimumCoins;
            break;
        case EMCGrillRewardKind::CatalogItem:
            if(!BuildGrillCatalogGrant(*Row,Reward)) continue;
            break;
        case EMCGrillRewardKind::Trash:
            break;
        default:
            continue;
        }
        Choices.Add(FGrillCandidate{MoveTemp(Reward),Row});
    }
    return Choices;
}
TArray<FMCGrillReward> UMCHubEconomyComponent::GetGrillPreviewPool() const
{
    TArray<FMCGrillReward> Pool;
    for(auto& Choice:BuildGrillPool()) Pool.Add(MoveTemp(Choice.Reward));
    return Pool;
}
bool UMCHubEconomyComponent::SelectGrillReward(FMCGrillReward& Reward,FText& Error) const
{
    if(!LoadTables()) {Error=LOCTEXT("MissingData","Таблицы экономики ещё не подготовлены.");return false;}
    const auto Choices=BuildGrillPool();
    if(Choices.IsEmpty()) {Error=LOCTEXT("EmptyRollPool","В таблице нет доступных призов.");return false;}
    double TotalWeight=0;
    for(const auto& Choice:Choices) TotalWeight+=Choice.Row->Weight;
    const FGuid Receipt=FGuid::NewGuid();
    FRandomStream Random(int32(GetTypeHash(Receipt)));
    double Pick=Random.FRand()*TotalWeight;
    const FGrillCandidate* Selected=&Choices.Last();
    for(const auto& Choice:Choices) {
        Pick-=Choice.Row->Weight;
        if(Pick<0) {Selected=&Choice;break;}
    }
    Reward=Selected->Reward;
    Reward.RollID=Receipt;
    if(Reward.Kind==EMCGrillRewardKind::Coins) Reward.Coins=Random.RandRange(Selected->Row->MinimumCoins,Selected->Row->MaximumCoins);
    if(!Reward.GrantedPurchase.PerkID.IsNone()) Reward.GrantedPurchase.InstanceID=Receipt;
    return true;
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
    if(!IsAuthority() || bMutating || PendingRoll.RollID!=Receipt || !ValidGrillReceipt(PendingRoll)) return false;
    TGuardValue<bool> Guard(bMutating,true);FMCGrillReward Reward=PendingRoll;
    if(Reward.bCatalogGrant) {
        if(Reward.bConvertedDuplicate) {
            const int32 Before=Coins;
            AddCoinsClamped(Reward.Coins);
            Reward.Coins=FMath::Max(0,Coins-Before);
        } else if(!Reward.GrantedCosmeticID.IsNone()) {
            OwnedCosmetics.AddUnique(Reward.GrantedCosmeticID);
        } else {
            FMCPerkDefinition Definition;
            const FName PerkID=Reward.GrantedPurchase.PerkID;
            const int32 Charges=Reward.GrantedPurchase.Charges;
            if(Reward.GrantedRaidCopies>0) {
                if(!CanAddRaidPerkCopies(PerkID,Reward.GrantedRaidCopies)) return false;
                AddRaidPerkCopies(PerkID,Reward.GrantedRaidCopies);
            } else if(GetRaidPerkDefinition(PerkID,Definition) && CanAddRaidPerkCopies(PerkID,1)
                && (Definition.Kind==EMCPerkKind::Consumable ? Charges>0 && Charges<=10000 : Charges==INDEX_NONE)) {
                AddRaidPerkCopies(PerkID,1,Charges);
            } else PendingPurchases.Add(Reward.GrantedPurchase);
        }
    } else if(Reward.Kind==EMCGrillRewardKind::CatalogItem) {
        const auto* Item=FindCatalogItem(Reward.CatalogItem);
        // Old accepted receipts remain valuable. Unknown IDs are quarantined without inventing a price.
        if(!Item || Item->DuplicateCoins<0 || Reward.Quantity<1 || Reward.Quantity>32) {RefreshMigrationNotice();NotifyChanged();return false;}
        Reward.bConvertedDuplicate=true;Reward.Kind=EMCGrillRewardKind::Coins;
        Reward.Coins=int32(FMath::Clamp<int64>(int64(Item->DuplicateCoins)*Reward.Quantity,0,MAX_int32));
    }
    if(Reward.Kind==EMCGrillRewardKind::Coins) AddCoinsClamped(Reward.Coins);
    PendingRoll=FMCGrillReward();LastGrillReward=MoveTemp(Reward);
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
    const bool UnknownReceipt=PendingRoll.RollID.IsValid() && PendingRoll.Kind==EMCGrillRewardKind::CatalogItem
        && !PendingRoll.bCatalogGrant && !FindCatalogItem(PendingRoll.CatalogItem);
    if(UnknownReceipt) MigrationNotice=LOCTEXT("LegacyReceiptQuarantined","Старая награда сохранена, но её ID отсутствует в каталоге. Нужна проверка таблицы; монеты и покупки сохранены.");
    else if(UnappliedLegacy>0 || InvalidNew>0) MigrationNotice=LOCTEXT("LegacyPurchasesRetained","Часть прежних покупок сохранена до проверки совместимости.");
    else MigrationNotice=FText::GetEmpty();
}
FMCHubEconomySaveData UMCHubEconomyComponent::ExportSave()
{
    if(IsAuthority() && !bMutating && HasPendingGrillRoll()) CompleteGrillRoll(PendingRoll.RollID);
    FMCHubEconomySaveData Data;Data.Version=3;Data.Coins=Coins;Data.OwnedCosmetics=OwnedCosmetics;Data.PendingBoosters=PendingBoosters;
    Data.PendingPurchases=PendingPurchases;Data.ShopRotationId=ShopRotationId;Data.ShopRotationSeed=ShopRotationSeed;
    Data.bShopStockGenerated=bShopStockGenerated;Data.ShopStockIDs=ShopStockIDs;Data.EquippedLoadout=EquippedLoadout;
    Data.LastAppliedRunId=LastAppliedRunId;Data.RunCalciumCharges=RunCalciumCharges;Data.PendingGrillReward=PendingRoll;
    Data.RaidPerkStock=RaidPerkStock;Data.RaidLoadout=RaidLoadout;Data.RaidTravelTicket=RaidTravelTicket;
    return Data;
}
bool UMCHubEconomyComponent::ImportSave(const FMCHubEconomySaveData& Data,FText& Error)
{
    Error=FText::GetEmpty();
    if(!IsAuthority() || bMutating || Data.Version<1 || Data.Version>3 || !LoadTables()) return false;
    const auto Rules=GetRules();
    if(Data.OwnedCosmetics.Num()>FMath::Clamp(Rules.MaximumOwnedCosmetics,1,1024)
        || Data.PendingBoosters.Num()>4096 || Data.PendingPurchases.Num()>4096 || Data.ShopStockIDs.Num()>256
        || Data.RaidPerkStock.Num()>MaximumRaidStockEntries || Data.RaidLoadout.ActivePerks.Num()>FMCHubRaidLoadout::SlotsPerKind
        || Data.RaidLoadout.PassivePerks.Num()>FMCHubRaidLoadout::SlotsPerKind) return false;
    TSet<FName> StockIDs;
    for(const auto& Entry:Data.RaidPerkStock) {
        if(Entry.PerkID.IsNone() || Entry.Copies<=0 || Entry.Copies>MaximumRaidStockCopies || StockIDs.Contains(Entry.PerkID)
            || Entry.LegacyChargePacks.Num()>Entry.Copies
            || Entry.LegacyChargePacks.ContainsByPredicate([](int32 Charges){return Charges<=0 || Charges>10000;})) return false;
        StockIDs.Add(Entry.PerkID);
    }
    if(!UMCCosmeticCatalog::ValidateLoadout(UMCCosmeticCatalog::GetDefaultCatalog(),Data.EquippedLoadout,Error)) return false;
    TArray<FName> ValidOwned;
    for(FName ID:Data.OwnedCosmetics) if(const auto* Item=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),ID);Item && Item->bEnabled) ValidOwned.AddUnique(ID);
    for(const auto& Slot:Data.EquippedLoadout.Slots) if(!Slot.ItemId.IsNone() && !ValidOwned.Contains(Slot.ItemId)) {
        const auto* Row=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),Slot.ItemId);
        if(!Row || !Row->bDefaultUnlocked) {Error=LOCTEXT("InvalidOwnedLoadout","В сохранении нет выбранного аксессуара.");return false;}
    }
    // Unknown legacy IDs do not invalidate an otherwise readable wallet/profile.
    if(Data.PendingGrillReward.RollID.IsValid() && !ValidGrillReceipt(Data.PendingGrillReward)) return false;
    {
        TGuardValue<bool> Guard(bMutating,true);
        Coins=FMath::Clamp(Data.Coins,0,FMath::Clamp(Rules.MaximumCoins,1,MAX_int32));OwnedCosmetics=MoveTemp(ValidOwned);
        PendingBoosters=Data.PendingBoosters;PendingPurchases=Data.PendingPurchases;EquippedLoadout=Data.EquippedLoadout;
        ShopRotationId=Data.ShopRotationId;ShopRotationSeed=Data.ShopRotationSeed;bShopStockGenerated=Data.bShopStockGenerated && ShopRotationId.IsValid();
        ShopStockIDs=Data.ShopStockIDs;LastAppliedRunId=Data.LastAppliedRunId;RunCalciumCharges=FMath::Clamp(Data.RunCalciumCharges,0,3);
        RaidPerkStock=Data.RaidPerkStock;
        RaidLoadout=Data.RaidLoadout;
        RaidTravelTicket=Data.RaidTravelTicket;
        MigrateRaidPerkStock();
        FText RaidError;
        if(!ValidateRaidLoadout(RaidLoadout,RaidError)) {
            RaidLoadout=FMCHubRaidLoadout();
            RaidTravelTicket.Invalidate();
        }
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
    FGuid TravelTicket;
    const bool bRaidTravel=FGuid::ParseExact(GetWorld()->URL.GetOption(TEXT("MCRaidTicket="),TEXT("")),EGuidFormats::Digits,TravelTicket)
        && TravelTicket.IsValid();
    const auto* Arena=GetWorld()->GetGameState<AMCGameState>();
    // The arena lifts this barrier after the team acknowledges loading.
    if(bRaidTravel && (!Arena || Arena->bLobbyWaiting || !Player->GetPawn())) return false;
    const bool bUseRaidLoadout=bRaidTravel && RaidTravelTicket==TravelTicket;
    TArray<TArray<int32>> Sources;const auto Plan=BuildPendingPlan(&Sources);
    TArray<FMCPerkGrant> Grants;
    TArray<FGuid> AppliedPurchases;
    TSet<int32> AppliedLegacy;
    TSet<FName> AppliedRaidPerks;
    bool bDeferred=false;
    const auto TryPlanGrant=[&](const FMCPerkGrant& Grant) {
        Grants.Add(Grant);
        if(Player->Perks->CanAcquirePerkBatch(Grants)) return true;
        Grants.Pop(EAllowShrinking::No);
        bDeferred=true;
        return false;
    };
    const auto AppendLegacyGrant=[&](int32 I) {
        const auto& Purchase=Plan[I];FMCPerkGrant Grant;Grant.PerkID=Purchase.PerkID;
        Grant.DestinationSlot=Purchase.DestinationSlot;Grant.Charges=Purchase.Charges;
        if(!TryPlanGrant(Grant)) return;
        if(Sources[I].IsEmpty()) AppliedPurchases.Add(Purchase.InstanceID);
        else for(int32 Index:Sources[I]) AppliedLegacy.Add(Index);
    };
    // Tool prerequisites go first. Chosen actives have first claim on their slots.
    const auto IsToolGrant=[&](int32 I){return Player->Perks->GetPerkKind(Plan[I].PerkID)==EMCPerkKind::ToolUpgrade;};
    for(int32 I=0;I<Plan.Num();++I) if(IsToolGrant(I)) AppendLegacyGrant(I);
    if(bUseRaidLoadout) {
        const auto AppendRaidGrant=[&](FName ID,EMCPerkKind Kind,int32 Destination) {
            FMCPerkDefinition Definition;
            const auto* Entry=RaidPerkStock.FindByPredicate([ID](const auto& Row){return Row.PerkID==ID;});
            if(!Entry || Entry->Copies<=0 || !GetRaidPerkDefinition(ID,Definition)
                || (Definition.Kind!=Kind && !(Kind==EMCPerkKind::Active && Definition.Kind==EMCPerkKind::Consumable))) {
                bDeferred=true;
                return;
            }
            FMCPerkGrant Grant;
            Grant.PerkID=ID;
            Grant.DestinationSlot=Destination;
            if(Definition.Kind==EMCPerkKind::Consumable && !Entry->LegacyChargePacks.IsEmpty())
                Grant.Charges=Entry->LegacyChargePacks[0];
            if(TryPlanGrant(Grant)) AppliedRaidPerks.Add(ID);
        };
        for(int32 I=0;I<RaidLoadout.ActivePerks.Num();++I) AppendRaidGrant(RaidLoadout.ActivePerks[I],EMCPerkKind::Active,I);
        for(FName ID:RaidLoadout.PassivePerks) AppendRaidGrant(ID,EMCPerkKind::Passive,INDEX_NONE);
    }
    for(int32 I=0;I<Plan.Num();++I) if(!IsToolGrant(I)) AppendLegacyGrant(I);
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
    if(bUseRaidLoadout) {
        for(auto& Entry:RaidPerkStock) if(AppliedRaidPerks.Contains(Entry.PerkID)) {
            --Entry.Copies;
            if(!Entry.LegacyChargePacks.IsEmpty()) Entry.LegacyChargePacks.RemoveAt(0,1,EAllowShrinking::No);
        }
        RaidPerkStock.RemoveAll([](const auto& Entry){return Entry.Copies<=0;});
        RaidLoadout.ActivePerks.RemoveAll([this](FName ID){return GetRaidPerkCopies(ID)<=0;});
        RaidLoadout.PassivePerks.RemoveAll([this](FName ID){return GetRaidPerkCopies(ID)<=0;});
        RaidTravelTicket.Invalidate();
    }
    LastAppliedRunId=RunId;if(IsCalciumEnabled()) RunCalciumCharges=Calcium;
    RefreshMigrationNotice();
    if(bDeferred && MigrationNotice.IsEmpty()) MigrationNotice=LOCTEXT("DeferredPurchases","Часть покупок не подошла текущему забегу и сохранена для следующего.");
    Error=MigrationNotice;
    if(bUseRaidLoadout)
        if(auto* PC=Cast<APlayerController>(Player->GetOwner()); PC && GetWorld()->GetGameInstance())
            if(auto* Profiles=GetWorld()->GetGameInstance()->GetSubsystem<UMCHubProfileSubsystem>()) Profiles->Store(Player,PC);
    NotifyChanged();
    return true;
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
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,RaidPerkStock,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UMCHubEconomyComponent,RaidLoadout,COND_OwnerOnly);
}

#undef LOCTEXT_NAMESPACE
