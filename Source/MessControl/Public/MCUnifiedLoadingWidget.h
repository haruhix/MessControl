#pragma once

#include "MCLoadingWidget.h"
#include "MCUnifiedLoadingWidget.generated.h"

class UMCUniversalUIWidget;
class UTextBlock;
class UTexture2D;

/** Same loading owner/actions/tip clock, with an editable PSD-family presentation tree. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedLoadingWidget : public UMCLoadingWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedLoadingWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void PreparePresentation();
    bool IsPresentationPrepared() const {return bPrepared;}
    /** Optional artwork only. Configure in the saved class so its reference joins location prewarming. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading|Art") TSoftObjectPtr<UTexture2D> BackgroundArtwork;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading|Layout",meta=(ClampMin="280")) float MaximumPanelWidth=720;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void OnLocalizationChanged() override;
private:
    UMCUniversalUIWidget* MakeShape(FName Name,FLinearColor Fill,FMargin ShapePadding);
    UTextBlock* MakeLabel(FName Name,int32 FontSize);
    void UpdateLayout();
    void SyncFrameVisibility();
    bool bPrepared=false,bPreparing=false;
    float PreparedFontScale=0;
    FVector2D LastViewport=FVector2D::ZeroVector;
};
