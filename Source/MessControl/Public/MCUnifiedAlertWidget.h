#pragma once
#include "MCGameplayUIWidget.h"
#include "MCGameplayAlertWidget.h"
#include "MCUnifiedAlertWidget.generated.h"

class UMCUniversalUIWidget;

/** Mirrors source warning copy/reveal; never queues or advances runtime alerts itself. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedAlertWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedAlertWidget();
    UFUNCTION(BlueprintCallable,Category="UI|Alerts") void SetBackend(UMCGameplayAlertWidget* Source);
    UFUNCTION(BlueprintCallable,Category="UI|Alerts") void RefreshFromBackend();
    /** Negative Count restores the source offset; otherwise supply the New stack's complete height. */
    UFUNCTION(BlueprintCallable,Category="UI|Alerts") void SetBossStackPresentation(int32 Count,float Height);
    UFUNCTION(BlueprintCallable,Category="UI|Preload") bool PreparePresentation();
    UFUNCTION(BlueprintPure,Category="UI|Preload") bool IsPresentationPrepared() const {return bPresentationPrepared;}
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
private:
    bool bPresentationPrepared=false;
    UPROPERTY(Transient) TWeakObjectPtr<UMCGameplayAlertWidget> Backend;
    UPROPERTY(Transient) TObjectPtr<UMCGameplayAlertWidget> PreviewSource;
    FMCGameplayAlertPresentationSnapshot Latest;
    int32 StackCount=-1;
    float StackHeight=0.f,CachedViewportWidth=-1.f;
    FString CachedText;
    void Present(const FMCGameplayAlertPresentationSnapshot& Snapshot);
};
