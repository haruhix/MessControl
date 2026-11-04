#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCFishingRewardWidget.h"
#include "MCUnifiedCatchWidget.generated.h"

class UCanvasPanel;
class UImage;
class USizeBox;
class UTextBlock;
class UMCUniversalUIWidget;
class UTexture2D;
class UMaterialInterface;

/** Icon-only compatibility leaf. The containing card/frame/text remain editable UMG. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedCatchGlyph : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedCatchGlyph();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Icon") bool bAllowAnalyticFallback=true;
    void SetPresentationIcon(UTexture2D* InTexture,UMaterialInterface* InMaterial,FLinearColor Hue,FLinearColor Ink,FLinearColor Cream);
    virtual void BuildPresentationTree() override;
protected:
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override;
private:
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Texture;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> Material;
    FLinearColor FallbackHue=FLinearColor::White,FallbackInk=FLinearColor::Black,FallbackCream=FLinearColor::White;
};

/** Source-owned receipt motion with one editable PSD-family card and fixed retained motion nodes. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedCatchWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedCatchWidget();
    /** Rarity illustrations only; they do not add species, items or catch mechanics. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Art")
    TMap<EMCHubItemRarity,TSoftObjectPtr<UTexture2D>> CatchIcons;
    /** Prepared static UI materials override texture brushes; no runtime MID is created. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Art")
    TMap<EMCHubItemRarity,TSoftObjectPtr<UMaterialInterface>> CatchIconMaterials;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Art") bool bAllowAnalyticIconFallback=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Layout") bool bAdaptTextLayout=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Layout",meta=(ClampMin="180")) float MinimumCardWidth=348;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Layout",meta=(ClampMin="180")) float MaximumCardWidth=640;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Layout",meta=(ClampMin="48")) float IllustrationHeight=156;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Layout",meta=(ClampMin="24")) float IconSize=140;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Color") FLinearColor CommonFill=FLinearColor(.07f,.18f,.42f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Color") FLinearColor RareFill=FLinearColor(.34f,.085f,.88f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Color") FLinearColor EpicFill=FLinearColor(.47f,.073f,.20f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Catch|Color") FLinearColor LegendaryFill=FLinearColor(1.f,.73f,0.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Catch") EMCHubItemRarity PreviewRarity=EMCHubItemRarity::Common;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Catch",meta=(ClampMin="0")) int32 PreviewAmount=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Catch",meta=(ClampMin="0")) int32 PreviewWalletAfter=125;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Catch") bool bPreviewWalletFull=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Catch") bool bPreviewMerged=false;

    void SetBackend(UMCFishingRewardWidget* Source);
    void RefreshFromBackend();
    /** Safe before a catch/visible paint; fixed-font metrics and the saved tree are sufficient. */
    void PreparePresentation();
    bool IsPresentationPrepared() const {return bPrepared;}
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void OnLocalizationChanged() override;
private:
    void ResolvePresentationTree();
    void Present(const FMCFishingRewardPresentationSnapshot& Snapshot);
    void PresentPreview(float Time);
    void UpdateCopy(const FMCFishingRewardPresentationSnapshot& Snapshot);
    void MeasureLayout();
    void PublishLayout();
    void HideMotion();
    FText RarityLabel(EMCHubItemRarity Rarity,const FText& Fallback=FText::GetEmpty()) const;
    FLinearColor RarityColor(EMCHubItemRarity Rarity) const;
    UTexture2D* ResolveIcon(EMCHubItemRarity Rarity) const;
    UMaterialInterface* ResolveIconMaterial(EMCHubItemRarity Rarity) const;
    void ApplyPose(UWidget* Node,const FMCFishingRewardMotionPose& Pose,FVector2D BaseSize);
    UMCUniversalUIWidget* MakeShape(FName Name,FLinearColor Fill);
    UTextBlock* MakeText(FName Name,int32 FontSize,FLinearColor Color);

    UPROPERTY(Transient) TMap<EMCHubItemRarity,TObjectPtr<UTexture2D>> PreparedIcons;
    UPROPERTY(Transient) TMap<EMCHubItemRarity,TObjectPtr<UMaterialInterface>> PreparedIconMaterials;
    TWeakObjectPtr<UMCFishingRewardWidget> Backend;
    TWeakObjectPtr<UCanvasPanel> Canvas;
    TWeakObjectPtr<USizeBox> CardBounds;
    TWeakObjectPtr<UMCUniversalUIWidget> Card;
    TWeakObjectPtr<UTextBlock> Title;
    TWeakObjectPtr<UTextBlock> Amount;
    TWeakObjectPtr<UTextBlock> Rarity;
    TWeakObjectPtr<USizeBox> Illustration;
    FMCFishingRewardUnifiedLayout Layout;
    bool bPrepared=false,bPreparing=false,bMeasuring=false;
    bool bHasCopy=false;
    EMCHubItemRarity LastRarity=EMCHubItemRarity::Common;
    FText LastAmount,LastRarityText,LastTitle;
    float LastAbsoluteScale=0;
    FVector2D LastViewportSize=FVector2D::ZeroVector;
};
