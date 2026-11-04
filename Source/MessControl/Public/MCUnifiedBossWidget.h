#pragma once
#include "MCGameplayUIWidget.h"
#include "MCBossHealthWidget.h"
#include "MCUnifiedBossWidget.generated.h"

class UMCUniversalUIWidget;
class UTextBlock;

/** Shape-only encounter view. Existing boss UI owns source selection and health timing. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedBossWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedBossWidget();
    UFUNCTION(BlueprintCallable,Category="UI|Boss") void SetBackend(UMCBossHealthWidget* Source);
    UFUNCTION(BlueprintCallable,Category="UI|Boss") void RefreshFromBackend();
    UFUNCTION(BlueprintPure,Category="UI|Boss") int32 GetDisplayedBossCount() const {return DisplayedCount;}
    UFUNCTION(BlueprintPure,Category="UI|Boss") float GetDisplayedStackHeight() const {return DisplayedHeight;}
    UFUNCTION(BlueprintCallable,Category="UI|Preload") bool PreparePresentation();
    UFUNCTION(BlueprintPure,Category="UI|Preload") bool IsPresentationPrepared() const {return bPresentationPrepared;}
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
private:
    bool bPresentationPrepared=false;
    UPROPERTY(Transient) TWeakObjectPtr<UMCBossHealthWidget> Backend;
    UPROPERTY(Transient) TObjectPtr<UMCBossHealthWidget> PreviewSource;
    FMCBossHealthPresentationSnapshot Latest;
    int32 DisplayedCount=0;
    float DisplayedHeight=0.f,CachedWidth=-1.f;
    FString CachedCopy;
    UMCUniversalUIWidget* MakeShape(FName Name);
    UTextBlock* MakeText(FName Name,int32 Size);
    void Present(const FMCBossHealthPresentationSnapshot& Snapshot);
    void UpdateLayout();
};
