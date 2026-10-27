#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/Button.h"
#include "MCHubShopWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class UScrollBox;
class UUniformGridPanel;
class UHorizontalBox;
class UMCHubEconomyComponent;
class UMCHubShopWidget;
class UMCCosmeticItemWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCHubShopClosed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMCHubPurchaseRequested,FName,ItemID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMCHubPurchaseSlotRequested,FName,ItemID,int32,DestinationSlot,FName,ExpectedReplacedID,FGuid,ExpectedInstanceID);

/** A planned next-run slot, separate from the current runtime skill inventory. */
UCLASS()
class MESSCONTROL_API UMCHubShopSlotButton : public UButton
{
    GENERATED_BODY()
public:
    void Configure(UMCHubShopWidget* Owner,int32 Slot);
private:
    UFUNCTION() void Clicked();
    TWeakObjectPtr<UMCHubShopWidget> OwnerWidget;
    int32 DestinationSlot=INDEX_NONE;
};

/** A data-bound entry; its parent validates the selected ID before requesting a purchase. */
UCLASS()
class MESSCONTROL_API UMCHubShopItemButton : public UButton
{
    GENERATED_BODY()
public:
    void Configure(UMCHubShopWidget* Owner,FName ID);
    FName GetItemID() const { return ItemID; }
private:
    UFUNCTION() void Clicked();
    TWeakObjectPtr<UMCHubShopWidget> OwnerWidget;
    FName ItemID;
};

/** Editable catalog presentation. The controller owns validated purchase RPCs and closing. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCHubShopWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, Category="Shop") FMCHubShopClosed OnClose;
    UPROPERTY(BlueprintAssignable, Category="Shop") FMCHubPurchaseRequested OnPurchaseRequested;
    UPROPERTY(BlueprintAssignable, Category="Shop") FMCHubPurchaseSlotRequested OnPurchaseSlotRequested;
    UFUNCTION(BlueprintCallable, Category="Shop") void Configure(UMCHubEconomyComponent* Economy);
    UFUNCTION(BlueprintCallable, Category="Shop") void SelectCatalogItem(FName ItemID);
    UFUNCTION(BlueprintCallable, Category="Shop") void SelectReplacementSlot(int32 SlotIndex);
    UFUNCTION(BlueprintCallable, Category="Shop") void SetTransactionMessage(const FText& Message);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shop") TSubclassOf<UMCCosmeticItemWidget> ProductCardClass;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBorder> ShopPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ShopClose;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ShopBuy;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ShopBalance;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ShopSelectedName;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ShopSelectedDescription;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ShopTransactionMessage;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UUniformGridPanel> ShopCatalogGrid;
private:
    void ResolveControls();
    void EnsureCatalogPresentation();
    void EnsureReplacementPresentation();
    void RequestPurchase(int32 Slot,FName ExpectedID,const FGuid& ExpectedInstance);
    UFUNCTION() void RefreshEconomy();
    UFUNCTION() void BuyClicked();
    UFUNCTION() void CloseClicked();
    UFUNCTION() void CardSelected(FName InSlot,FName ItemID,FName CatalogID);
    TWeakObjectPtr<UMCHubEconomyComponent> ObservedEconomy;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCHubShopItemButton>> CatalogButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCCosmeticItemWidget>> CatalogCards;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> ReplacementSlots;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCHubShopSlotButton>> ReplacementButtons;
    FName SelectedItemID;
    bool bChoosingReplacement=false;
    bool bEditorFixture=false;
};
