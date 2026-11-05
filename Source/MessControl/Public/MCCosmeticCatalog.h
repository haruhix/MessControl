#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCHubEconomyTypes.h"
#include "MCCosmeticCatalog.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UTexture2D;

/** Slot identifiers are data, so later accessory categories do not require a new enum. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCCosmeticSlotSelection
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Cosmetics") FName Slot;
    /** None means the character's default, with no added accessory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Cosmetics") FName ItemId;
    bool operator==(const FMCCosmeticSlotSelection& Other) const { return Slot==Other.Slot && ItemId==Other.ItemId; }
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCCharacterCosmeticLoadout
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Cosmetics") TArray<FMCCosmeticSlotSelection> Slots;
    FName ItemForSlot(FName Slot) const;
    void SetItemForSlot(FName Slot,FName ItemId);
    bool operator==(const FMCCharacterCosmeticLoadout& Other) const { return Slots==Other.Slots; }
};

/** One accessory, attached to the existing rig. This never changes the skin or its face morphs. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCCosmeticCatalogRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") FName Slot;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") FName AttachBone=TEXT("head_x");
    /** Exported examples retain the character's reference mesh coordinates. Convert once using the actual UE reference bone. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") bool bMeshSpaceFit=true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") FTransform LocalTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") TArray<TObjectPtr<UMaterialInterface>> Materials;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") bool bEnabled=true;
    /** Native tests/samples may be free; ownership is still validated by the economy. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics") bool bDefaultUnlocked=false;
    /** Explicit migration switch. False preserves the existing DT_HubCatalog offer unchanged. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce") bool bUseCanonicalCommerce=false;
    /** Stable purchase ID. Migration retains the existing Hub catalog ID; None uses this cosmetic row name. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce",meta=(EditCondition="bUseCanonicalCommerce")) FName ShopItemId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Localization",meta=(EditCondition="bUseCanonicalCommerce")) FName DisplayNameKey;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Localization",meta=(EditCondition="bUseCanonicalCommerce")) FName DescriptionKey;
    /** Required runtime product icon. Fitting thumbnails are independent derived editor data. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce",meta=(EditCondition="bUseCanonicalCommerce")) TSoftObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce",meta=(EditCondition="bUseCanonicalCommerce",ClampMin="0")) int32 BasePrice=15;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce",meta=(EditCondition="bUseCanonicalCommerce",ClampMin="0",ClampMax="100")) int32 DiscountPercent=0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce",meta=(EditCondition="bUseCanonicalCommerce")) EMCHubItemRarity Rarity=EMCHubItemRarity::Common;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce",meta=(EditCondition="bUseCanonicalCommerce")) bool bInShop=true;
    /** Independent probability for this item each shop rotation; no capped/weighted clothing pool. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce",meta=(EditCondition="bUseCanonicalCommerce",ClampMin="0",ClampMax="1")) float ShopAppearanceChance=1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clothing|Commerce",meta=(EditCondition="bUseCanonicalCommerce")) int32 SortOrder=0;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

};

UCLASS()
class MESSCONTROL_API UMCCosmeticCatalog : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Known presentation slots in this version. Other authored slots are accepted through their table rows. */
    static const FName FaceSlot;
    static const FName HatSlot;
    UFUNCTION(BlueprintPure, Category="Cosmetics") static UDataTable* GetDefaultCatalog();
    UFUNCTION(BlueprintPure, Category="Cosmetics") static TArray<FName> GetItemsForSlot(UDataTable* Catalog,FName Slot);
    UFUNCTION(BlueprintPure, Category="Cosmetics") static FName GetEquippedItem(const FMCCharacterCosmeticLoadout& Loadout,FName Slot) { return Loadout.ItemForSlot(Slot); }
    UFUNCTION(BlueprintPure, Category="Cosmetics") static FMCCharacterCosmeticLoadout WithEquippedItem(FMCCharacterCosmeticLoadout Loadout,FName Slot,FName ItemId);
    /** Validates structure and item/slot compatibility, never grants ownership. Rejects duplicate/unknown slots atomically. */
    static bool ValidateLoadout(const UDataTable* Catalog,const FMCCharacterCosmeticLoadout& Loadout,FText& OutError);
    static const FMCCosmeticCatalogRow* Find(const UDataTable* Catalog,FName ItemId);
    /** Shared authority/display calculation; floor rounding, int64 intermediate. Invalid inputs return INDEX_NONE. */
    UFUNCTION(BlueprintPure, Category="Clothing") static int32 EffectivePrice(const FMCCosmeticCatalogRow& Row);
    static bool ValidateCommerce(const FMCCosmeticCatalogRow& Row,FText& OutError);
    static FName ShopItemIdForRow(FName CosmeticId,const FMCCosmeticCatalogRow& Row);
};
