#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCHubEconomyTypes.h"
#include "MCFishingRewardWidget.generated.h"

class UUserWidget;

/** Measured cosmetic layout only. No catch selection, reward or animation clock belongs to a view. */
struct MESSCONTROL_API FMCFishingRewardUnifiedLayout
{
    FVector2D CardSize=FVector2D(320,288);
    FVector2D IconOffset=FVector2D(0,-10);
    FVector2D IconSize=FVector2D(140,140);
    float AbsoluteScale=1.f;
    bool IsValid() const
    {
        return !CardSize.ContainsNaN() && !IconOffset.ContainsNaN() && !IconSize.ContainsNaN()
            && CardSize.X>0 && CardSize.Y>0 && IconSize.X>0 && IconSize.Y>0
            && FMath::IsFinite(AbsoluteScale) && AbsoluteScale>0;
    }
};

/** Positions/sizes use absolute Slate units; render scale/angle contain only cosmetic motion. */
struct MESSCONTROL_API FMCFishingRewardMotionPose
{
    FVector2D CenterAbsolute=FVector2D::ZeroVector;
    FVector2D SizeAbsolute=FVector2D::ZeroVector;
    FVector2D RenderScale=FVector2D(1,1);
    FLinearColor Color=FLinearColor::White;
    float AngleDegrees=0;
    float Opacity=0;
    bool bVisible=false;
};

/** Read-only snapshot of the one existing receipt owner. Fixed arrays avoid a second emitter/queue. */
struct MESSCONTROL_API FMCFishingRewardPresentationSnapshot
{
    bool bVisible=false;
    bool bWalletFull=false;
    bool bMerged=false;
    EMCHubItemRarity Rarity=EMCHubItemRarity::Common;
    int32 Amount=0;
    int32 WalletAfter=0;
    int32 PendingCount=0;
    float Age=0;
    FText AmountText;
    FText RarityText;
    FText DetailText;
    FLinearColor FishColor=FLinearColor::White;
    FLinearColor Ink=FLinearColor::Black;
    FLinearColor Cream=FLinearColor::White;
    FMCFishingRewardMotionPose Card;
    FMCFishingRewardMotionPose Tokens[3];
    FMCFishingRewardMotionPose Trails[15];
    FMCFishingRewardMotionPose Sparks[8];
    FMCFishingRewardMotionPose Ring;
};

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
    /** Only HUD copy is deferred; the server-owned, persisted wallet stays authoritative. */
    int32 GetDisplayedWallet(int32 AuthoritativeWallet) const;
    FSimpleMulticastDelegate OnCounterBalanceChanged;
    UFUNCTION(BlueprintPure,Category="Hub|Fishing") UWidget* GetCounterIconTarget() const {return CounterIcon.Get();}
    UFUNCTION(BlueprintPure,Category="Hub|Fishing") UWidget* GetCounterCardTarget() const {return CounterCard.Get();}
    bool HasPreparedPresentation() const {return bViewportFontsPrepared && WidgetTree && GetRootWidget();}
    UFUNCTION(BlueprintPure,Category="Hub|Fishing") float GetAnimationAge() const {return AnimationAge;}
    /** Binding and layout never clear/replay a receipt or alter the frozen flight start. */
    void SetUnifiedView(UUserWidget* View,const FMCFishingRewardUnifiedLayout& Layout,bool bPrepared);
    void ClearUnifiedView(const UUserWidget* View);
    bool HasPreparedUnifiedView() const;
    FMCFishingRewardPresentationSnapshot GetPresentationSnapshot() const;
    /** Actor-free editor fixture uses the same pose evaluator, never a receipt/RPC. */
    static void EvaluateSnapshotMotion(FMCFishingRewardPresentationSnapshot& Result,const FGeometry& Geometry,
        FVector2D InBadgeCenter,FVector2D InFlightStartNormalized,FVector2D InFlightEnd,bool bEndpoint,
        FVector2D CardSize,FVector2D IconOffset,FVector2D IconSize,float InResponsiveScale=1.f);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;

protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    virtual void OnLocalizationChanged() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;

private:
    friend struct FMCFishingCounterTestAccess;
    struct FCatchReceipt
    {
        EMCHubItemRarity Rarity=EMCHubItemRarity::Common;
        int32 Amount=0;
        int32 WalletAfter=0;
        bool bMerged=false;
        mutable bool bWalletCreditObserved=false;
    };
    void StartNext();
    void RefreshCopy();
    void PrimeTextMetrics(float FontScale=1.f);
    void RestoreCounterScale();
    void UpdateCounterArrival();
    bool ShouldSuppress() const;
    FVector2D HeroAnchor(const FGeometry& Geometry) const;
    bool CounterAnchor(const FGeometry& Geometry,FVector2D& Out) const;
    float ResponsiveScale(const FGeometry& Geometry) const;
    bool UsesUnifiedView() const;
    FVector2D PresentationCardSize(const FGeometry& Geometry) const;
    FVector2D PresentationIconOffset(const FGeometry& Geometry) const;
    FVector2D PresentationIconSize(const FGeometry& Geometry) const;

    TArray<FCatchReceipt> Pending;
    FCatchReceipt Active;
    TWeakObjectPtr<UWidget> CounterIcon;
    TWeakObjectPtr<UWidget> CounterCard;
    TWeakObjectPtr<UWidget> PulsedCard;
    TWeakObjectPtr<UWorld> PresentationWorld;
    TWeakObjectPtr<UUserWidget> UnifiedView;
    FMCFishingRewardUnifiedLayout UnifiedLayout;
    FGeometry PresentationGeometry;
    bool bUnifiedViewPrepared=false;
    bool bHasPresentationGeometry=false;
    FVector2D OriginalCounterScale=FVector2D(1,1);
    FVector2D OriginalCounterPivot=FVector2D::ZeroVector;
    FVector2D BadgeCenter=FVector2D::ZeroVector;
    FVector2D FlightStartNormalized=FVector2D::ZeroVector;
    FVector2D FlightEnd=FVector2D::ZeroVector;
    float AnimationAge=0;
    float MissingCounterSeconds=0;
    float AmountTextScale=1;
    float DetailTextScale=1;
    float PrimedFontScale=0;
    bool bActive=false;
    bool bSuppressed=true;
    bool bFlightStarted=false;
    bool bHasCounterEndpoint=false;
    bool bCounterPulsing=false;
    bool bCounterArrived=false;
    bool bViewportFontsPrepared=false;
    FText AmountText;
    FText RarityText;
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
