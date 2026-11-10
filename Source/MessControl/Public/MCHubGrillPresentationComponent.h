#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCGameplayUIWidget.h"
#include "MCHubEconomyTypes.h"
#include "MCHubGrillPresentationComponent.generated.h"

class AMCHubGrillStation;
class UMCHubEconomyComponent;
class UTextBlock;
class UBorder;
class UMaterialInterface;
class UWidgetComponent;
class UStaticMeshComponent;
class USizeBox;
class UMCUniversalUIWidget;

/** A single cosmetic strip that stops on the station's accepted reward. */
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
    bool HasPreparedReelCatalog() const {return PreparedEconomy.IsValid();}
    /** New only: source text may grow the container beyond the base rail width. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Presentation|World|Grill",meta=(ClampMin="390",ClampMax="900")) float MaximumNewCaptionWidth=680.f;
    /** Designer specimens never replace a runtime station/reward. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Grill",meta=(MultiLine=true)) FText PreviewTitleOverride;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Grill",meta=(MultiLine=true)) FText PreviewRewardOverride;
    UFUNCTION(BlueprintPure,Category="Grill|Presentation") bool HasPreparedNewPresentation() const;
    /** Legacy Blueprint settings retained for saved instances. The current rail uses Slate cards. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Legacy") TSoftObjectPtr<UMaterialInterface> ReelMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Legacy",meta=(ClampMin="0",ClampMax="1")) float ScanlineStrength=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grill|Legacy",meta=(ClampMin="0",ClampMax="1")) float ColorSeparation=.15f;
protected:
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
    virtual void OnLocalizationChanged() override;
private:
    enum class ECardIcon : uint8 { Clothing, Perk, Booster, Coin, Trash };
    struct FReelCard
    {
        FText Caption;
        TSoftObjectPtr<UTexture2D> Icon;
        ECardIcon Shape=ECardIcon::Perk;
        EMCHubItemRarity Rarity=EMCHubItemRarity::Common;
        FText RailCaption;
        float RailCaptionWidth=0.f;
    };
    void ResolveControls();
    void BuildWorldPresentationTree();
    void ResolveWorldControls(UWidget* SearchRoot=nullptr);
    void RefreshWorldPresentation();
    void RefreshWorldCaptionLayout();
    void PrimeWorldPresentationFonts();
    FText GrillText(FName Key,const TCHAR* Russian,const TCHAR* English) const;
    FReelCard MakeCard(const FMCGrillReward& Reward,const UMCHubEconomyComponent* Economy) const;
    void PrepareCardText(FReelCard& Card) const;
    void RefreshReel(const AMCHubGrillStation& Grill,const UMCHubEconomyComponent* Economy);
    void DrawCardIcon(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& IconGeometry,
        const FReelCard& Card,const FLinearColor& Tint) const;
    UPROPERTY(Transient) TObjectPtr<USizeBox> HologramReelSlot;
    UPROPERTY(Transient) TObjectPtr<USizeBox> NewHologramRoot;
    UPROPERTY(Transient) TObjectPtr<USizeBox> NewHologramReelSlot;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NewHologramTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NewHologramReward;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> NewHologramRewardCard;
    bool bWorldFontsPrepared=false;
    bool bHasNewCaptionMetrics=false;
    FText MeasuredNewTitle,MeasuredNewReward;
    FSlateFontInfo MeasuredNewTitleFont,MeasuredNewRewardFont;
    float MeasuredNewCaptionMaximum=-1.f,MeasuredNewCaptionScale=-1.f;
    FMargin MeasuredNewOuterPadding,MeasuredNewHeadingPadding,MeasuredNewRewardPadding;
    UPROPERTY(Transient) TObjectPtr<UBorder> HologramPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HologramTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HologramReward;
    TWeakObjectPtr<AMCHubGrillStation> Source;
    TWeakObjectPtr<const UMCHubEconomyComponent> PreparedEconomy;
    TArray<FReelCard> FillerCards;
    TArray<FReelCard> ReelCards;
    FGuid DisplayedRoll;
    FSlateFontInfo CardFont;
    float ReelCursor=3.f;
    bool bResultCentered=false;
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
