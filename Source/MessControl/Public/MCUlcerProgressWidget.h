#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCUlcerProgressWidget.generated.h"
class AMCMouthSurface;
class AMCToothpick;
class UBorder;
class UCanvasPanel;
class UImage;
class UTextBlock;
class UMCHUDIcon;

/** Screen-space progress attached to the lesion, independently of the main HUD. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUlcerProgressWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<AMCMouthSurface> Source;
    TWeakObjectPtr<AMCToothpick> ToothpickSource;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") FLinearColor ProgressColor=FLinearColor(.25f,.94f,.74f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") bool bOverrideProgressColor=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance",meta=(ClampMin="2",ClampMax="12")) float RingThickness=7;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
private:
    void RefreshSource();
    void RefreshPresentation();
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> ProgressPanel;
    UPROPERTY(Transient) TObjectPtr<UBorder> ProgressBackground;
    UPROPERTY(Transient) TObjectPtr<UMCHUDIcon> ToolIcon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PullIcon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ProgressText;
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> RingSegments;
    float DisplayProgress=0.f;
    float PulseTime=0.f;
    bool bVisible=false;
    bool bPull=false;
    bool bToothpick=false;
    FText PercentPattern;
    FText PullKey;
    FText PickaxeKey;
    FText SprayKey;
    int32 LastPercent=MIN_int32;
    int32 LastPrompt=-1;
};
