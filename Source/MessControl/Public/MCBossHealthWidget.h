#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCBossHealthWidget.generated.h"

class UCanvasPanel;
class UProgressBar;
class UTextBlock;
class UBorder;
class UVerticalBox;

/** Local, non-interactive encounter UI. Gameplay health remains in the boss's replicated snapshot. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCBossHealthWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void ShowHealth(float Health, float MaxHealth, float DeltaSeconds);
    void HideHealth();
    void SetLetterbox(bool bShow);
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> HealthPanel;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> CurrentBar;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> LossBar;
    UPROPERTY(Transient) TObjectPtr<UBorder> TopBar;
    UPROPERTY(Transient) TObjectPtr<UBorder> BottomBar;
    float LastFraction=1.f;
    float DelayedFraction=1.f;
    float LossHold=0.f;
    bool bShowingHealth=false;
    bool bLetterbox=false;
};
