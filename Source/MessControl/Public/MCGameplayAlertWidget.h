#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCGameplayAlertWidget.generated.h"

/** One readable central warning at a time. Entirely local, with no gameplay side effects. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCGameplayAlertWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="HUD|Alerts")
    void QueueAlert(const FText& Message,float HoldSeconds=3.f,FName EventId=NAME_None);
    UFUNCTION(BlueprintCallable,Category="HUD|Alerts") void ClearAlerts();
    UFUNCTION(BlueprintPure,Category="HUD|Alerts") bool IsPresentingAlert() const { return bPresenting; }
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
private:
    struct FAlert
    {
        FText Message;
        FName EventId;
        float HoldSeconds=3.f;
        double QueuedAt=0;
    };
    TArray<FAlert> Pending;
    FAlert Current;
    bool bPresenting=false;
    float Elapsed=0;
    void PresentNext();
};
