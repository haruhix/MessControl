#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
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
    /** A duplicate/full-inventory grill reward turns into this many personal coins. */
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="256")) int32 MaximumPendingBoosters=32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="1024")) int32 MaximumOwnedCosmetics=256;
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
};
