#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCStaminaWidget.generated.h"

class UProgressBar;
class UTextBlock;

/** Replaceable stamina presentation. Gameplay owns and updates the resource. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCStaminaWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly,Category="Stamina") float Current=0;
    UPROPERTY(BlueprintReadOnly,Category="Stamina") float Maximum=100;
    UPROPERTY(BlueprintReadOnly,Category="Stamina") float Normalized=1;
    UPROPERTY(BlueprintReadOnly,Category="Stamina") bool bExhausted=false;
    UFUNCTION(BlueprintCallable,Category="Stamina") void Refresh();
    UFUNCTION(BlueprintImplementableEvent,Category="Stamina")
    void OnStaminaChanged(float NewCurrent,float NewMaximum,float NewNormalized,bool Exhausted);
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float Dt) override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> StaminaProgress;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StaminaValue;
private:
    float RefreshElapsed=.1f;
    bool bHasSample=false;
    FLinearColor LastColor=FLinearColor::Transparent;
};
