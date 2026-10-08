#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCFreezeBarWidget.generated.h"

class UProgressBar;
class UTextBlock;

/** Passive presentation for the ice event; gameplay supplies the normalized freeze amount. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCFreezeBarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Values are cached even when the widget tree has not been built yet. */
    UFUNCTION(BlueprintCallable, Category="Ice Event")
    void SetFreeze(float Amount, bool bSafe);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

    UPROPERTY(Transient, meta=(BindWidgetOptional))
    TObjectPtr<UProgressBar> FreezeProgress;

    UPROPERTY(Transient, meta=(BindWidgetOptional))
    TObjectPtr<UTextBlock> FreezeCaption;

private:
    void BuildDefaultWidgetTree();
    void ApplyFreeze();

    float FreezeAmount = 0.f;
    bool bInSafeZone = false;
};
