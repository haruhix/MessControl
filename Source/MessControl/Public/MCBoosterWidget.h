#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCUIDataTypes.h"
#include "MCBoosterWidget.generated.h"

class UBorder;
class UDataTable;
class UImage;
class UTextBlock;
class UTexture2D;
class UScrollBox;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class EMCBoosterGlyph : uint8 { Bottle, Recovery, Armed };

/** Already-authorized gameplay state. The widget neither discovers actors nor activates effects. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCUIBoosterViewData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster") FName GameplayPerkID;
    /** Count of the entire queue, matching the existing gameplay HUD. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster") int32 QueueCount = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster") bool bFuryActive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster") bool bGrenadeArmed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster") bool bActivationBlocked = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster") FText ActivationInput = FText::AsCultureInvariant(TEXT("G"));
};

/** Neutral vector symbol shown while an authored row image is absent, loading or unavailable. */
UCLASS()
class MESSCONTROL_API UMCBoosterFallbackIcon : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Icon") FLinearColor Tint = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Icon") FLinearColor FillTint = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Icon") EMCBoosterGlyph Glyph = EMCBoosterGlyph::Bottle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Icon", meta=(ClampMin=".5",ClampMax="6")) float LineWidth = 2.f;

protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
};

/** Compact, data-authored booster HUD with a full description available as a tooltip/Details preview. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCBoosterWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Presentation")
    void ApplyPresentation(const FMCUIBoosterViewData& Data);

    /** Optional override; otherwise the current theme/default booster table is cached during setup. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data") TObjectPtr<UDataTable> BoosterTable;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Booster", meta=(RowType="/Script/MessControl.MCUIBoosterRow"))
    FDataTableRowHandle PreviewBoosterRow;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Booster", meta=(ClampMin="0")) int32 PreviewQueueCount = 3;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Preview|Booster") bool bPreviewRowFound = false;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Preview|Booster") bool bPreviewUsedFallback = false;

    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
    virtual void ApplyTheme() override;

protected:
    virtual void NativeDestruct() override;

private:
    void CacheDataTable();
    void BindPresentationTree();
    void RefreshPresentation(const FMCUIResolvedContent& Content, bool bShowDetails);
    void UpdateImage(const TSoftObjectPtr<UTexture2D>& Image);
    void ShowLoadedImage(UTexture2D* Texture);
    FString DisplayCulture() const;
    FText Copy(FName Key, const FText& Fallback) const;

    UPROPERTY(Transient) TObjectPtr<UDataTable> CachedBoosterTable;
    UPROPERTY(Transient) TObjectPtr<UDataTable> CachedLocalizationTable;
    UPROPERTY(Transient) TObjectPtr<UBorder> MainPanel;
    UPROPERTY(Transient) TObjectPtr<UBorder> Keycap;
    UPROPERTY(Transient) TObjectPtr<UImage> Illustration;
    UPROPERTY(Transient) TObjectPtr<UMCBoosterFallbackIcon> FallbackIcon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KeyLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CountLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> FuryPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FuryLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> ArmedPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ArmedLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> DetailsPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DescriptionLabel;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> DetailsScroll;
    UPROPERTY(Transient) FMCUIBoosterViewData Presentation;

    TSoftObjectPtr<UTexture2D> RequestedImage;
    TSharedPtr<FStreamableHandle> ImageLoadHandle;
    TWeakObjectPtr<UWidget> BoundRoot;
    bool bImageRequestFinished = false;
    bool bApplyingPreview = false;
};
