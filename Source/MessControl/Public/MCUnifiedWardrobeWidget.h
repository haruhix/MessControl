#pragma once

#include "MCGameplayUIWidget.h"
#include "MCUnifiedWardrobeWidget.generated.h"

class UMCHubWardrobeWidget;
class UMCCosmeticItemWidget;
class UMCUniversalUIWidget;
class UImage;
class UTextBlock;
class USizeBox;
class UMCUnifiedWardrobeWidget;

UENUM()
enum class EMCUnifiedWardrobeAction : uint8 { Select, Primary, Apply, Back };

USTRUCT()
struct FMCUnifiedWardrobeProduct
{
    GENERATED_BODY()
    UPROPERTY(Transient) FName Slot;
    UPROPERTY(Transient) FName ItemID;
    UPROPERTY(Transient) FName CatalogID;
    UPROPERTY(Transient) FText Name;
    UPROPERTY(Transient) FText Description;
    UPROPERTY(Transient) FText Price;
    UPROPERTY(Transient) FText State;
    UPROPERTY(Transient) FSlateBrush Icon;
    UPROPERTY(Transient) bool bOwned=false;
    UPROPERTY(Transient) bool bAffordable=false;
    UPROPERTY(Transient) bool bDraftSelected=false;
    UPROPERTY(Transient) TWeakObjectPtr<UMCCosmeticItemWidget> Source;
};

USTRUCT()
struct FMCUnifiedWardrobeTile
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> Card;
    UPROPERTY(Transient) TObjectPtr<UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Fallback;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Price;
    UPROPERTY(Transient) TObjectPtr<USizeBox> IconBounds;
    UPROPERTY(Transient) TObjectPtr<USizeBox> Bounds;
};

/** Retained runtime-only action adapter; stable identity survives source catalogue refresh. */
UCLASS()
class MESSCONTROL_API UMCUnifiedWardrobeBinding : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<UMCUnifiedWardrobeWidget> Owner;
    UPROPERTY(Transient) EMCUnifiedWardrobeAction Action=EMCUnifiedWardrobeAction::Select;
    UPROPERTY(Transient) FName Slot;
    UPROPERTY(Transient) FName ItemID;
    UPROPERTY(Transient) FName CatalogID;
    UFUNCTION() void Pressed();
    UFUNCTION() void Clicked();
private:
    FName PressedSlot,PressedItem,PressedCatalog;
    bool bCaptured=false;
    bool bPressedOwned=false;
};

/** PSD wardrobe view. The native wardrobe still owns try-on, colour, purchasing and Apply/Cancel. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedWardrobeWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedWardrobeWidget();
    /** Only matching existing cosmetic IDs may override art; no catalogue/economy values are changed. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Art|Cosmetics") TMap<FName,TSoftObjectPtr<UTexture2D>> CosmeticIconArt;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview") FName PreviewCosmeticID=TEXT("Hat_Head_CowboyHat");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") bool bAdaptHeaderContour=true;
    /** Display face for the PSD title only; ordinary copy retains the shared composite font. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance|Typography") FSlateFontInfo WardrobeTitleFont;
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ApplyTheme() override;
    void SetBackend(UMCHubWardrobeWidget* Source);
    void RefreshFromBackend();
    bool HasBoundBackend() const {return Backend.IsValid();}
    UWidget* GetDefaultFocusTarget() const;
    void RequestBack();
    /** Bounded viewport layout; also called by actor-free preview's second layout pass. */
    void RefreshAdaptiveLayout();
    /** Read actual opaque panel geometry in the root viewport paint coordinate system. */
    bool GetPreviewViewportRect(const FGeometry& ViewportPaint,FBox2D& OutRect) const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    friend class UMCUnifiedWardrobeBinding;
    void BindActions();
    void RebuildGrid();
    void Present();
    void SelectProduct(FName InSlot,FName ItemID,FName CatalogID);
    void ActivateProduct(FName InSlot,FName ItemID,FName CatalogID,TOptional<bool> ExpectedOwned);
    void ForwardButton(FName Name);
    void RefreshHeaderGeometry();
    void OverrideArt(FMCUnifiedWardrobeProduct& Product) const;
    const FMCUnifiedWardrobeProduct* SelectedProduct() const;
    UFUNCTION() void PaletteChanged(FVector HSV);
    TWeakObjectPtr<UMCHubWardrobeWidget> Backend;
    UPROPERTY(Transient) TArray<FMCUnifiedWardrobeProduct> Products;
    UPROPERTY(Transient) TArray<FMCUnifiedWardrobeTile> Tiles;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUnifiedWardrobeBinding>> Bindings;
    FName SelectedSlot,SelectedItem;
    bool bSynchronizing=false;
    bool bRefreshingAdaptiveLayout=false;
    bool bAdaptiveTreeReady=false;
    FVector2D LastAdaptiveSize=FVector2D::ZeroVector;
    FVector2D LastAdaptiveGridSize=FVector2D::ZeroVector;
    FVector2D TickLayoutSize=FVector2D::ZeroVector;
    FString LastAdaptiveCopy;
    int32 LastAdaptiveTileCount=INDEX_NONE;
    bool bLastPaletteExpanded=false;
    bool EnsureAdaptiveTree();
};
