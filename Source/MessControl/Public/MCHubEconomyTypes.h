#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MCPerkTypes.h"
#include "MCHubEconomyTypes.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class EMCHubCatalogKind : uint8 { NextRunBooster, Cosmetic, Consumable };

UENUM(BlueprintType)
enum class EMCHubItemRarity : uint8 { Common, Rare, Epic, Legendary };

UENUM(BlueprintType)
enum class EMCGrillRewardKind : uint8 { Coins, CatalogItem };

/** Row name is the stable purchase/reward ID. Perk and cosmetic execution stays in their existing systems. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCHubCatalogRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bInShop=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EMCHubCatalogKind Kind=EMCHubCatalogKind::NextRunBooster;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MultiLine=true)) FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName DisplayNameKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName DescriptionKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EMCHubItemRarity Rarity=EMCHubItemRarity::Common;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Price=10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName PerkID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CosmeticID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ConsumableID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="32")) int32 Quantity=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="32")) int32 MaximumQueuedStacks=1;
    /** Relative sampling weight inside this item's rarity availability pool. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float StockWeight=1.f;
    /** Legacy accepted item receipts convert to this many coins per receipt unit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 DuplicateCoins=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SortOrder=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ReelSymbol;
};

/** Weights are relative probabilities; disabled or invalid rows never consume fish. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCGrillRewardRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Weight=1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EMCGrillRewardKind Kind=EMCGrillRewardKind::Coins;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EMCHubItemRarity Rarity=EMCHubItemRarity::Common;
    /** Exactly 0..0 is a losing combination; otherwise both bounds must be positive. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 MinimumCoins=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 MaximumCoins=10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CatalogItem;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="32")) int32 Quantity=1;
    /** Exactly three IDs, shared by physical reels and the presentation copy. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> ReelSymbols;
};

/** The Default row tunes test economy limits and the physical roll timing. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCHubEconomyRulesRow : public FTableRowBase
{
    GENERATED_BODY()
    /** Suspended feature: old inventory is retained, but purchases, rolls and use are disabled. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCalciumEnabled=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) int32 MaximumCoins=1000000;
    /** Legacy charge queue guard only; this does not cap the separate passive inventory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="256")) int32 MaximumPendingBoosters=32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="1024")) int32 MaximumOwnedCosmetics=256;
    /** A single assortment is generated per run; buying or reopening never rerolls it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="64")) int32 MaximumShopItems=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="4")) int32 MaximumShopPassiveItems=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="1")) float CommonShopAvailability=1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="1")) float RareShopAvailability=.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="1")) float EpicShopAvailability=.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="1")) float LegendaryShopAvailability=.05f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.05")) float LidCloseSeconds=.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float RollSeconds=2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.05")) float RevealSeconds=1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.05")) float LidOpenSeconds=.4f;
};

/** Server-authored receipt. RollID makes repeated completion callbacks harmless. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCGrillReward
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid RollID;
    UPROPERTY(BlueprintReadOnly) EMCGrillRewardKind Kind=EMCGrillRewardKind::Coins;
    UPROPERTY(BlueprintReadOnly) EMCHubItemRarity Rarity=EMCHubItemRarity::Common;
    UPROPERTY(BlueprintReadOnly) int32 Coins=0;
    UPROPERTY(BlueprintReadOnly) FName CatalogItem;
    UPROPERTY(BlueprintReadOnly) int32 Quantity=0;
    UPROPERTY(BlueprintReadOnly) bool bConvertedDuplicate=false;
    UPROPERTY(BlueprintReadOnly) TArray<FName> ReelSymbols;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCHubEconomyCatalogEntry
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName ItemID;
    UPROPERTY(BlueprintReadOnly) FMCHubCatalogRow Definition;
    UPROPERTY(BlueprintReadOnly) EMCPerkKind PerkKind=EMCPerkKind::Automatic;
};

/** A paid next-run plan. Identity guards replacement confirmations against stale screens. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCHubPendingPurchase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid InstanceID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CatalogID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName PerkID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DestinationSlot=INDEX_NONE;
    /** Explicit remaining charges for consumables; INDEX_NONE uses the canonical definition. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Charges=INDEX_NONE;
};
