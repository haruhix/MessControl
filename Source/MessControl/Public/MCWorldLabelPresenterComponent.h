#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "MCWorldLabelPresenterComponent.generated.h"

class UMCGameplayUITheme;
class UMCWorldLabelWidget;
class UTextRenderComponent;
class UDataTable;

/** Explicit source-local mirror. It never owns the text, hidden flags or gameplay visibility. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCWorldLabelPresenterComponent : public UWidgetComponent
{
    GENERATED_BODY()
public:
    UMCWorldLabelPresenterComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel") TObjectPtr<UTextRenderComponent> SourceText;
    /** Reflected readiness root; creation waits for the configured theme and role class to be resident. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel|Resources") TSoftObjectPtr<UMCGameplayUITheme> PresentationTheme;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel") bool bDestroyWhenSourceGone=false;
    /** Cosmetic visibility only: bounded 5-10 Hz camera-to-source query, never a gameplay target test. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel|Visibility",meta=(ClampMin=".1",ClampMax=".2")) float OcclusionRefreshSeconds=.15f;
    /** Below one projected pixel the original glyph is no longer legible. No minimum render-scale is imposed. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel|Visibility",meta=(ClampMin="0",ClampMax="2")) float MinimumProjectedLinePixels=1.f;
    UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="WorldLabel|Diagnostics") float LastPerspectiveScale=0.f;
    UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="WorldLabel|Diagnostics") float LastProjectedLinePixels=0.f;
    UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="WorldLabel|Diagnostics") bool bLastProjectionInView=false;
    UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="WorldLabel|Diagnostics") bool bOcclusionCacheValid=false;
    UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="WorldLabel|Diagnostics") bool bLastOccluded=false;
    virtual void InitWidget() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    bool HasPreparedPresentation() const;
    void RefreshSourceLabel();
    /** Weak owner callback must only refresh this source's caption, never mechanics or VFX. */
    void SetSourceCopyRefresh(const FSimpleDelegate& Callback);
    void RecordNativeSourceCopy(const FText& ActualCopy);
private:
    bool CanMirrorSource() const;
    bool ShouldShowSource() const;
    bool UpdateProjectedPresentation(UMCWorldLabelWidget* NewView);
    bool RefreshOcclusion();
    bool AcquireSourceRendering();
    void RestoreSourceRendering();
    void StartObservingPresentation();
    void StopObservingPresentation();
    void RefreshSourceCopy();
    void StopObservingSourceCopy();
    void HandleSourceCopyTableChanged();
    FSimpleDelegate SourceCopyRefresh;
    TWeakObjectPtr<UDataTable> ObservedCopyTable;
    FDelegateHandle CopyTableChangedHandle;
    FString ObservedCopyCulture;
    bool bSourceCopyRefreshPending=true,bNativeSourceCopyOwned=false;
    FText LastNativeSourceCopy;
    TWeakObjectPtr<UTextRenderComponent> LeasedSource;
    TWeakObjectPtr<UTextRenderComponent> OcclusionSource;
    TWeakObjectPtr<AActor> OcclusionViewTarget;
    double NextOcclusionTestAt=-1.;
    FDelegateHandle PresentationChangedHandle;
    TSharedPtr<SWidget> PreparedViewSlate;
    bool bSavedMainPass=false,bSavedDepthPass=false,bSavedCustomDepth=false,bSavedShadow=false;
    bool bSourceSeen=false,bLeaseConflict=false,bResourcesReady=false;
};
