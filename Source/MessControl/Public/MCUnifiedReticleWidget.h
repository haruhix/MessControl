#pragma once

#include "MCGameplayUIWidget.h"
#include "MCUIReticlePresentation.h"
#include "MCUnifiedReticleWidget.generated.h"

class UMCGameplayHUD;
class UMCUIContourShape;
class UBorder;
class UTextBlock;

/** Generic editable UMG reticle renderer. Source owner alone evaluates/provides geometry and state. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedReticleWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedReticleWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void PreparePresentation();
    bool IsPresentationPrepared() const {return bPrepared;}
    /** No provider-specific evaluation and no viewport/desktop conversion: source paint -> destination paint. */
    bool ConsumePresentation(const FMCUIReticlePresentation& Snapshot,const FGeometry& SourcePaintGeometry);
    void ClearPresentationFor(const UUserWidget* Source);
    bool HasPresentedSourceFrame(const UUserWidget* Source,uint64 SourceFrame) const;
    /** HUD convenience only. Future Hub provider may call Consume/query directly with its own cached snapshot. */
    void SetBackend(UMCGameplayHUD* Source);
    void RefreshFromBackend();
    /** Set before loading preparation. Overflow fails to source fallback; no interaction-time node creation. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reticle|Capacity",meta=(ClampMin="32",ClampMax="512")) int32 PreparedContourCount=32;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reticle|Typography",meta=(ClampMin="180")) float MaximumLabelWidth=360;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void OnLocalizationChanged() override;
private:
    void OnSourceSnapshot(const FMCUIReticlePresentation& Snapshot);
    bool CanPresentInViewport() const;
    bool PresentGeometry(const FMCUIReticlePresentation& Snapshot,const FGeometry& SourcePaintGeometry,bool bDesignerFixture);
    void HideReticleCanvas();
    void BindSourceNotification();
    void UnbindSourceNotification();
    TWeakObjectPtr<UMCGameplayHUD> Backend;
    FDelegateHandle SourceSnapshotHandle;
    TWeakObjectPtr<UUserWidget> PresentedSource;
    uint64 PresentedFrame=MAX_uint64;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUIContourShape>> ContourViews;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> BoxViews;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> LabelViews;
    TWeakObjectPtr<UDataTable> PreparedCopyTable;
    FText PreparedFoodCopy;
    FString PreparedCulture;
    float PreparedFontScale=0;
    bool bPrepared=false,bPreparing=false;
};
