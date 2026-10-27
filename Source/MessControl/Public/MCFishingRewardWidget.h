#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCHubEconomyTypes.h"
#include "MCFishingRewardWidget.generated.h"

/** Owner-only catch feedback. Receipts are visual snapshots, never wallet mutations. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCFishingRewardWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCFishingRewardWidget();
    void SetCounterTarget(UWidget* Icon,UWidget* Card);
    UFUNCTION(BlueprintCallable,Category="Hub|Fishing")
    void PresentCatch(EMCHubItemRarity Rarity,int32 FishCoins,int32 WalletAfter);
    UFUNCTION(BlueprintCallable,Category="Hub|Fishing") void ClearPresentation();
    UFUNCTION(BlueprintPure,Category="Hub|Fishing") bool IsAnimating() const;
    bool HasPreparedPresentation() const {return bViewportFontsPrepared && WidgetTree && GetRootWidget();}
    UFUNCTION(BlueprintPure,Category="Hub|Fishing") float GetAnimationAge() const {return AnimationAge;}
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;

protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    virtual void OnLocalizationChanged() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;

private:
    struct FCatchReceipt
    {
        EMCHubItemRarity Rarity=EMCHubItemRarity::Common;
        int32 Amount=0;
        int32 WalletAfter=0;
        bool bMerged=false;
    };
    void StartNext();
    void RefreshCopy();
    void PrimeTextMetrics(float FontScale=1.f);
    void RestoreCounterScale();
    bool ShouldSuppress() const;
    FVector2D HeroAnchor(const FGeometry& Geometry) const;
    bool CounterAnchor(const FGeometry& Geometry,FVector2D& Out) const;
    float ResponsiveScale(const FGeometry& Geometry) const;

    TArray<FCatchReceipt> Pending;
    FCatchReceipt Active;
    TWeakObjectPtr<UWidget> CounterIcon;
    TWeakObjectPtr<UWidget> CounterCard;
    TWeakObjectPtr<UWidget> PulsedCard;
    TWeakObjectPtr<UWorld> PresentationWorld;
    FVector2D OriginalCounterScale=FVector2D(1,1);
    FVector2D OriginalCounterPivot=FVector2D::ZeroVector;
    FVector2D BadgeCenter=FVector2D::ZeroVector;
    FVector2D FlightStartNormalized=FVector2D::ZeroVector;
    FVector2D FlightEnd=FVector2D::ZeroVector;
    float AnimationAge=0;
    float AmountTextScale=1;
    float DetailTextScale=1;
    float PrimedFontScale=0;
    bool bActive=false;
    bool bSuppressed=true;
    bool bFlightStarted=false;
    bool bHasCounterEndpoint=false;
    bool bCounterPulsing=false;
    bool bViewportFontsPrepared=false;
    FText AmountText;
    FText DetailText;
    FLinearColor FishColor=FLinearColor(.2f,.65f,.6f);
    FMCUIThemePalette Colors;
    FSlateBrush BadgeBrush;
    FSlateBrush ShadowBrush;
    FSlateBrush FishDiscBrush;
    FSlateBrush DotBrush;
    FSlateBrush DepthBrush;
    FSlateBrush SheenBrush;
    FSlateBrush InsetBrush;
    FSlateBrush RingBrush;
    UPROPERTY(Transient) FSlateFontInfo AmountFont;
    UPROPERTY(Transient) FSlateFontInfo FullFont;
    UPROPERTY(Transient) FSlateFontInfo DetailFont;
};
