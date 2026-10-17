#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCFreezeBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UBorder;

/** Passive auto-height presentation for the ice event; gameplay supplies the normalized freeze amount. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCFreezeBarWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()

public:
    /** Values are cached even when the widget tree has not been built yet. */
    UFUNCTION(BlueprintCallable, Category="Ice Event")
    void SetFreeze(float Amount, bool bSafe);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Freeze", meta=(ClampMin="0",ClampMax="1")) float PreviewFreezeAmount=.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Freeze") bool bPreviewSafe=false;
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
    virtual void ApplyTheme() override;

protected:
    UPROPERTY(Transient, meta=(BindWidgetOptional))
    TObjectPtr<UProgressBar> FreezeProgress;

    UPROPERTY(Transient, meta=(BindWidgetOptional))
    TObjectPtr<UTextBlock> FreezeCaption;

private:
    void ApplyFreeze();
    UPROPERTY(Transient) TObjectPtr<UBorder> FreezeBackground;

    float FreezeAmount = 0.f;
    bool bInSafeZone = false;
    bool bEditorFixture=false;
};
