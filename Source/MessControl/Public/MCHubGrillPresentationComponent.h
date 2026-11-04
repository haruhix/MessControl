#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCGameplayUIWidget.h"
#include "MCHubGrillPresentationComponent.generated.h"

class AMCHubGrillStation;
class UImage;
class UTextBlock;
class UBorder;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UWidgetComponent;
class UStaticMeshComponent;
class USizeBox;
class UMCUniversalUIWidget;

/** Three views of the physical reel atlas. Their offsets come from the station's actual reel angles. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCHubGrillHologramWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCHubGrillHologramWidget(const FObjectInitializer& Initializer);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    UFUNCTION(BlueprintCallable,Category="Grill") void SetStation(AMCHubGrillStation* Station);
    void TickPresentation(float DeltaSeconds);
    /** New only: source text grows its container; original 390x190 presentation is retained. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Presentation|World|Grill",meta=(ClampMin="390",ClampMax="900")) float MaximumNewCaptionWidth=680.f;
    /** Designer specimens never replace a runtime station/reward. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Grill",meta=(MultiLine=true)) FText PreviewTitleOverride;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Grill",meta=(MultiLine=true)) FText PreviewRewardOverride;
    UFUNCTION(BlueprintPure,Category="Grill|Presentation") bool HasPreparedNewPresentation() const;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Design") TSoftObjectPtr<UMaterialInterface> ReelMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Design",meta=(ClampMin="0",ClampMax="1")) float ScanlineStrength=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Design",meta=(ClampMin="0",ClampMax="1")) float ColorSeparation=.15f;
private:
    void ResolveControls();
    void PrepareMaterials();
    void BuildWorldPresentationTree();
    void ResolveWorldControls(UWidget* SearchRoot=nullptr);
    void BindWorldReelBrushes();
    void RefreshWorldPresentation();
    void RefreshWorldCaptionLayout();
    void PrimeWorldPresentationFonts();
    UPROPERTY(Transient) TObjectPtr<USizeBox> NewHologramRoot;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NewHologramTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NewHologramReward;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> NewHologramRewardCard;
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> NewHologramReels;
    bool bWorldFontsPrepared=false,bCatalogNamesPrepared=false;
    TWeakObjectPtr<UDataTable> PreparedGrillCatalog;
    bool bHasNewCaptionMetrics=false;
    FText MeasuredNewTitle,MeasuredNewReward;
    FSlateFontInfo MeasuredNewTitleFont,MeasuredNewRewardFont;
    float MeasuredNewCaptionMaximum=-1.f,MeasuredNewCaptionScale=-1.f;
    FMargin MeasuredNewOuterPadding,MeasuredNewHeadingPadding,MeasuredNewRewardPadding;
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> Reels;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> ReelMaterials;
    UPROPERTY(Transient) TObjectPtr<UBorder> HologramPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HologramTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HologramReward;
    TWeakObjectPtr<AMCHubGrillStation> Source;
    bool bPreview=false;
};

/** Local presentation only. No camera capture and no second roll or reward selection. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCHubGrillPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCHubGrillPresentationComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Hologram") FVector HologramLocalOffset=FVector(0,0,290);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Hologram") float VisibleDistance=2200.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Hologram") TSubclassOf<UMCHubGrillHologramWidget> WidgetClass;
private:
    UPROPERTY(Transient) TObjectPtr<UWidgetComponent> Display;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> IntakeFish;
    TWeakObjectPtr<AMCHubGrillStation> Station;
    void RefreshDisplaySizing();
    void RestoreDisplaySizing();
    TWeakObjectPtr<UWidgetComponent> DesiredSizeDisplay;
    TOptional<bool> CurrentDisplayDrawAtDesiredSize;
    bool bChangedDisplayDrawAtDesiredSize=false;
    bool bWasVisible=false;
    float VisibilityAlpha=0;
    FGuid IntakeRoll;
    FVector IntakeStart=FVector::ZeroVector;
};
