#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCCosmeticCatalog.generated.h"

class UStaticMesh;
class UMaterialInterface;

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
};
