#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/Widget.h"
#include "MCWorldStatusWidget.generated.h"

class UBorder;
class UProgressBar;
class USizeBox;
class UHorizontalBoxSlot;

/** Small analytic pie filling 0 -> 1; expiry becomes a permanent spoiled-food pictogram. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCStatusTimerWidget : public UWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status") float Fraction=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status") bool bSpoiled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status",meta=(ClampMin="0",ClampMax="1")) float SpoiledAlpha=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FVector2D DesiredSize=FVector2D(30,30);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor Surface=FLinearColor(.025f,.10f,.13f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor Rim=FLinearColor(.14f,.85f,.94f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor Fill=FLinearColor(1.f,.68f,.19f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor SpoiledTint=FLinearColor(.65f,.82f,.35f);
    virtual TSharedRef<SWidget> RebuildWidget() override;
};

/** Slate-drawn pill skin; named UProgressBars remain the editable health data controls. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCStatusHealthWidget : public UWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status") float HealthFraction=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status") float TrailFraction=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FMargin FramePadding=FMargin(4.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor Surface=FLinearColor(.025f,.10f,.13f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor Rim=FLinearColor(.14f,.85f,.94f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor Fill=FLinearColor(.32f,.9f,.58f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor Loss=FLinearColor(.9f,.58f,.19f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style") FLinearColor Highlight=FLinearColor(.92f,.98f,.94f);
    virtual TSharedRef<SWidget> RebuildWidget() override;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCWorldStatusSnapshot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float HealthFraction=1.f;
    /** Consumed spoil/fuse budget: fresh/unarmed=0, expiry=1. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float TimerFraction=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bHasTimer=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bSpoiled=false;
    /** Presentation only: food fragments retain gameplay health but hide the entire HP host. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bShowHealth=true;
};

/** Editable Designer tree: HP + adjacent pie, or a centred standalone fragment timer. No gameplay writes. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCWorldStatusWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCWorldStatusWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    UFUNCTION(BlueprintCallable,Category="Status") void SetSnapshot(const FMCWorldStatusSnapshot& Snapshot);
    UFUNCTION(BlueprintCallable,Category="Status") void SetDisplayActive(bool bActive);
    UFUNCTION(BlueprintPure,Category="Status") bool IsDisplayActive() const {return bTargetVisible;}
    /** Cosmetic deterministic time for Designer captures, never modifies an actor or world clock. */
    UFUNCTION(BlueprintCallable,Category="Status|Preview") void PresentAtTime(float VisualSeconds);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style",meta=(ClampMin=".1",ClampMax=".6")) float LossSeconds=.28f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style",meta=(ClampMin=".05",ClampMax=".5")) float AppearSeconds=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style",meta=(ClampMin=".05",ClampMax=".5")) float FadeSeconds=.20f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style",meta=(ClampMin=".05",ClampMax=".3")) float TimerFollowSeconds=.14f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style",meta=(ClampMin=".1",ClampMax=".4")) float SpoilTransitionSeconds=.22f;
    /** Derive status mint from the shared theme's mint/cyan without changing other UI. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Style",meta=(ClampMin="0",ClampMax="1")) float StatusMintAccentMix=.42f;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> StatusHealth;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> StatusLossTrail;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UMCStatusTimerWidget> StatusTimer;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UMCStatusHealthWidget> StatusHealthPresentation;
private:
    UPROPERTY(Transient) FMCWorldStatusSnapshot Current;
    float TrailFrom=1.f,TrailValue=1.f,LossAge=1.f,VisibilityAge=0.f,VisibilityFrom=0.f,VisibilityValue=0.f;
    float TimerFrom=0.f,TimerValue=0.f,TimerAge=1.f,SpoilFrom=0.f,SpoilValue=0.f,SpoilAge=1.f;
    bool bHasSnapshot=false,bTargetVisible=false;
    TWeakObjectPtr<UWidget> LayoutHealthHost;
    TWeakObjectPtr<UHorizontalBoxSlot> LayoutTimerSlot;
    FMargin TimerPaddingWithHealth=FMargin(0);
    ESlateVisibility HealthHostVisibility=ESlateVisibility::Visible;
    bool bTimerOnlyLayout=false;
    void RefreshStatusLayout();
    void RefreshVisuals();
};
