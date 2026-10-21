#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/WidgetComponent.h"
#include "MCVoiceIndicatorComponent.generated.h"

class UBorder;
class UImage;

/** Editable mic brush and three envelope bars. No replicated talking flag. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCVoiceIndicatorWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    UFUNCTION(BlueprintCallable,Category="Voice") void SetVoiceState(bool bSpeaking,float Level,float Time);
protected:
    virtual void NativeOnInitialized() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override;
private:
    UPROPERTY(Transient) TObjectPtr<UImage> Microphone;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> Bars;
};

UCLASS(ClassGroup=(UI),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCVoiceIndicatorComponent : public UWidgetComponent
{
    GENERATED_BODY()
public:
    UMCVoiceIndicatorComponent();
    virtual void TickComponent(float DeltaSeconds,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Voice",meta=(ClampMin="0",Units="cm")) float HeightAboveHead=166.f;
protected:
    virtual void BeginPlay() override;
};
