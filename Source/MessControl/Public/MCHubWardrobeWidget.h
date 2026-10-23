#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "MCGameplayUIWidget.h"
#include "MCCosmeticCatalog.h"
#include "MCHubWardrobeWidget.generated.h"

class UBorder;
class UButton;
class UUniformGridPanel;
class UTextBlock;
class UWidgetTree;
class UMCHubEconomyComponent;
class UMCCosmeticItemWidget;

UENUM(BlueprintType)
enum class EMCWardrobePaletteMode : uint8 { SaturationValue, Hue };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMCWardrobeHSVSelection,FVector,HSV);

/** Shader-free Designer control. HSV is normalized 0..1; no gameplay or saving. */
UCLASS(meta=(DisplayName="Wardrobe Color Palette"))
class MESSCONTROL_API UMCWardrobeColorPalette : public UWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette")
    EMCWardrobePaletteMode Mode=EMCWardrobePaletteMode::SaturationValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FVector2D DesiredSize=FVector2D(216,216);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette", meta=(ClampMin="0.002",ClampMax="0.1")) float KeyboardStep=.015f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette", meta=(ClampMin="3",ClampMax="16")) float IndicatorRadius=6.f;
    UPROPERTY(BlueprintAssignable, Category="Palette") FMCWardrobeHSVSelection OnHSVChanged;
    UFUNCTION(BlueprintPure, Category="Palette") FVector GetHSV() const {return SelectedHSV;}
    /** Presentation sync only; never emits user input or rounds the saved RGB. */
    UFUNCTION(BlueprintCallable, Category="Palette") void SetHSV(FVector HSV);
    /** Same frontend selection path used by mouse/touch; coordinates are local 0..1. */
    UFUNCTION(BlueprintCallable, Category="Palette") void SelectNormalizedPoint(FVector2D Point);
    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    FVector SelectedHSV=FVector(0,0,1);
    TSharedPtr<SWidget> PaletteSlate;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMCWardrobeColorDraft,FLinearColor,Color);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCWardrobeCancel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMCWardrobeCosmeticDraft,FMCCharacterCosmeticLoadout,Loadout);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMCWardrobePurchase,FName,CatalogId);

/** Presentation only: the owning controller handles preview, confirmation and closing. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCHubWardrobeWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Wardrobe") void Configure(FLinearColor Color);
    UFUNCTION(BlueprintPure, Category="Wardrobe") FLinearColor GetDraftColor() const {return DraftColor;}
    UFUNCTION(BlueprintCallable,Category="Wardrobe") void ConfigureCosmetics(UMCHubEconomyComponent* Economy);
    UFUNCTION(BlueprintCallable,Category="Wardrobe") void SetTransactionMessage(const FText& Message);
    UFUNCTION(BlueprintPure,Category="Wardrobe") FMCCharacterCosmeticLoadout GetDraftLoadout() const {return DraftLoadout;}
    /** The local draft may contain unowned try-on items; only this sanitized loadout may be equipped. */
    UFUNCTION(BlueprintPure,Category="Wardrobe") FMCCharacterCosmeticLoadout GetApplicableLoadout() const;
    UPROPERTY(BlueprintAssignable,Category="Wardrobe") FMCWardrobeCosmeticDraft OnCosmeticDraftChanged;
    UPROPERTY(BlueprintAssignable,Category="Wardrobe") FMCWardrobePurchase OnPurchaseRequested;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Wardrobe") TSubclassOf<UMCCosmeticItemWidget> CosmeticTileClass;
    /** Narrow tree migration shared by the runtime fallback and explicit Editor authoring. */
    static bool AddCosmeticsPresentation(UWidgetTree* Tree);
    UPROPERTY(BlueprintAssignable, Category="Wardrobe") FMCWardrobeColorDraft OnDraftChanged;
    UPROPERTY(BlueprintAssignable, Category="Wardrobe") FMCWardrobeColorDraft OnApply;
    UPROPERTY(BlueprintAssignable, Category="Wardrobe") FMCWardrobeCancel OnCancel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Wardrobe")
    FLinearColor PreviewBagColor=FLinearColor(.24f,.65f,1.f);

    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;

    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBorder> WardrobePanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBorder> WardrobeColorSwatch;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UMCWardrobeColorPalette> WardrobeColorPalette;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UMCWardrobeColorPalette> WardrobeHueStrip;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> WardrobeApply;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> WardrobeCancel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UUniformGridPanel> WardrobeFaceItems;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UUniformGridPanel> WardrobeHatItems;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> WardrobeCoins;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> WardrobeTransactionMessage;
private:
    void ResolveControls();
    void BindControls();
    void UnbindControls();
    void RefreshValues();
    bool IsPreviewing() const {return bEditorFixture || IsDesignTime();}
    UFUNCTION() void PaletteChanged(FVector HSV);
    UFUNCTION() void ApplyClicked();
    UFUNCTION() void CancelClicked();
    UFUNCTION() void RefreshCosmetics();
    UFUNCTION() void CosmeticPressed(FName InSlot,FName ItemId,FName CatalogId);
    UFUNCTION() void PurchasePressed(FName CatalogId);
    UPROPERTY(Transient) TObjectPtr<UMCHubEconomyComponent> CosmeticEconomy;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCCosmeticItemWidget>> CosmeticTiles;
    FMCCharacterCosmeticLoadout DraftLoadout;
    FLinearColor DraftColor=FLinearColor(.24f,.65f,1.f);
    FVector DraftHSV=FVector(0,0,1);
    bool bSynchronizing=false;
    bool bEditorFixture=false;
};
