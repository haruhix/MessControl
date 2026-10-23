#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCHubEconomyTypes.h"
#include "MCCosmeticItemWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UTextBlock;
class UTexture2D;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMCCosmeticItemPressed,FName,Slot,FName,ItemId,FName,CatalogId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMCCosmeticItemPurchase,FName,CatalogId);

/** Designer-editable product card. Selection and purchase are deliberately separate actions. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCCosmeticItemWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Cosmetics") void Configure(FName InSlot,FName InItem,FName InCatalog,
        FText InName,bool bInOwned,bool bInSelected,int32 InPrice,bool bInAffordable);
    UFUNCTION(BlueprintCallable,Category="Products") void SetProductPresentation(UTexture2D* InIcon,FText InDescription,
        EMCHubItemRarity InRarity,bool bInShowPurchase,FText InStatusOverride);
    UPROPERTY(BlueprintAssignable,Category="Cosmetics") FMCCosmeticItemPressed OnPressed;
    UPROPERTY(BlueprintAssignable,Category="Cosmetics") FMCCosmeticItemPurchase OnPurchaseRequested;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview") FText PreviewName;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview") int32 PreviewPrice=15;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview") TObjectPtr<UTexture2D> PreviewIcon;
    /** Optional transparent-margin crops: texture object name -> normalized min U,V,max U,V. ScaleToFit preserves the item. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Presentation") TMap<FName,FVector4> ThumbnailUVRegions;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    UFUNCTION(BlueprintPure,Category="Cosmetics") FName GetItemId() const {return ItemId;}
    UFUNCTION(BlueprintPure,Category="Cosmetics") FName GetItemSlot() const {return ItemSlot;}
    UFUNCTION(BlueprintPure,Category="Products") FName GetCatalogId() const {return CatalogId;}
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBorder> CosmeticCardSurface;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBorder> CosmeticThumbnailFrame;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CosmeticButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CosmeticBuy;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> CosmeticThumbnail;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CosmeticName;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CosmeticDescription;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CosmeticRarity;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CosmeticPrice;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CosmeticState;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CosmeticBuyLabel;
private:
    void Resolve();void Refresh();void BindControls();void ApplySurface();
    UFUNCTION() void Pressed();
    UFUNCTION() void BuyPressed();
    UFUNCTION() void Hovered();
    UFUNCTION() void Unhovered();
    FName ItemSlot,ItemId,CatalogId;
    FText Label,Description,StateOverride;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> ProductIcon;
    EMCHubItemRarity ProductRarity=EMCHubItemRarity::Common;
    bool bOwned=true,bSelected=false,bAffordable=true,bFixture=false,bPurchaseVisible=false,bHovered=false;
    int32 Price=0;
    float HoverAmount=0.f;
    float SelectionAmount=0.f;
};
