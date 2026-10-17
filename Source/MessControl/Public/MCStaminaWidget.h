#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCStaminaWidget.generated.h"

class UProgressBar;
class UTextBlock;

/** Replaceable, auto-height stamina presentation. Gameplay owns and updates the resource. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCStaminaWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Stamina") bool bShowSwimEnergy=false;
    UPROPERTY(BlueprintReadOnly,Category="Stamina") float Current=0;
    UPROPERTY(BlueprintReadOnly,Category="Stamina") float Maximum=100;
    UPROPERTY(BlueprintReadOnly,Category="Stamina") float Normalized=1;
    UPROPERTY(BlueprintReadOnly,Category="Stamina") bool bExhausted=false;
    UFUNCTION(BlueprintCallable,Category="Stamina") void Refresh();
    UFUNCTION(BlueprintImplementableEvent,Category="Stamina")
    void OnStaminaChanged(float NewCurrent,float NewMaximum,float NewNormalized,bool Exhausted);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float Dt) override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> StaminaProgress;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StaminaValue;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StaminaCaption;
private:
    float RefreshElapsed=.1f;
    bool bHasSample=false;
    int32 LastLabelMode=INDEX_NONE,LastDrowningTenths=INDEX_NONE;
    bool bLastDeepWater=false;
    FLinearColor LastColor=FLinearColor::Transparent;
    void PresentSample();
};
