#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCGameplayAlertWidget.generated.h"

class UBorder;
class UTextBlock;
class UCanvasPanelSlot;

/** Read-only painted source state. Hiding its legacy panel never changes this snapshot. */
struct MESSCONTROL_API FMCGameplayAlertPresentationSnapshot
{
    FText Text;
    float Reveal=0.f;
    FVector2D RenderScale=FVector2D(1,1),RenderTranslation=FVector2D::ZeroVector;
    float TopOffset=18.f;
    int32 BossStackCount=0;
    float BossStackHeight=-1.f;
    bool bVisible=false;
};

/** One readable central warning at a time. Entirely local, with no gameplay side effects. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCGameplayAlertWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="HUD|Alerts")
    void QueueAlert(const FText& Message,float HoldSeconds=3.f,FName EventId=NAME_None);
    UFUNCTION(BlueprintCallable,Category="HUD|Alerts") void ClearAlerts();
    UFUNCTION(BlueprintPure,Category="HUD|Alerts") bool IsPresentingAlert() const { return bPresenting; }
    UFUNCTION(BlueprintCallable,Category="HUD|Alerts") void SetBossStackCount(int32 Count,float StackHeight=-1.f);
    UFUNCTION(BlueprintCallable,Category="HUD|Alerts") void QueueOwnedAlert(const FText& Message,const FText& OwnerName,FName OwnerID,float HoldSeconds=3.f,FName EventId=NAME_None);
    FMCGameplayAlertPresentationSnapshot GetPresentationSnapshot() const;
    /** Hide only source artwork; queue, timing and viewport lifetime remain owned here. */
    UFUNCTION(BlueprintCallable,Category="HUD|Alerts") void SetUnifiedViewActive(bool bActive);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
private:
    struct FAlert
    {
        FText Message;
        FText OwnerName;
        FName OwnerID;
        FName EventId;
        float HoldSeconds=3.f;
        double QueuedAt=0;
    };
    TArray<FAlert> Pending;
    FAlert Current;
    bool bPresenting=false;
    bool bUnifiedViewActive=false;
    float CurrentReveal=0.f;
    float Elapsed=0;
    UPROPERTY(Transient) TObjectPtr<UBorder> AlertPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> AlertText;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> AlertSlot;
    int32 BossStackCount=0;
    float BossStackHeight=-1.f;
    void PresentNext();
    void PresentCurrent();
    void ApplyReveal();
};
