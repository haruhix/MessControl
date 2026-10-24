#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCWorldPreloadSubsystem.generated.h"

class APlayerController;
class FRenderCommandFence;
struct FStreamableHandle;
class UMaterialInterface;
class UPrimitiveComponent;
class UStreamableRenderAsset;

UENUM(BlueprintType)
enum class EMCWorldPreloadPhase : uint8
{
    Idle,
    LoadingAssets,
    PreparingRendering,
    WaitingForStreaming,
    Ready,
    Failed
};

/** Local, world-scoped presentation barrier. It never starts or spawns gameplay. */
UCLASS()
class MESSCONTROL_API UMCWorldPreloadSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCWorldPreloadSubsystem, STATGROUP_Tickables); }

    UFUNCTION(BlueprintCallable, Category="Loading")
    void BeginPreparation(APlayerController* LocalController);
    /** A constructed 3D selector may arrive after replication. Its setup shares this preparation deadline. */
    void SetPresentationReady(bool bReady) { bPresentationReady=bReady; }

    UFUNCTION(BlueprintCallable, Category="Loading")
    void Retry();

    UFUNCTION(BlueprintPure, Category="Loading")
    bool IsReady() const { return Phase == EMCWorldPreloadPhase::Ready; }

    UFUNCTION(BlueprintPure, Category="Loading")
    bool HasFailed() const { return Phase == EMCWorldPreloadPhase::Failed; }

    UFUNCTION(BlueprintPure, Category="Loading")
    bool IsPreparing() const;

    UFUNCTION(BlueprintPure, Category="Loading")
    EMCWorldPreloadPhase GetPhase() const { return Phase; }

    UFUNCTION(BlueprintPure, Category="Loading")
    FText GetStatusText() const;

    UFUNCTION(BlueprintPure, Category="Loading")
    FText GetDetailText() const;

    /** Progress of the discovered asset requests, not an invented total GPU percentage. */
    UFUNCTION(BlueprintPure, Category="Loading")
    float GetAssetProgress() const;

    /** Only finite, observable work has a percentage. Shader/PSO preparation has no reliable denominator. */
    UFUNCTION(BlueprintPure, Category="Loading")
    bool HasMeasuredPhaseProgress() const;

    /** Progress within the current resource stage, never an estimate of total time or readiness. */
    UFUNCTION(BlueprintPure, Category="Loading")
    float GetMeasuredPhaseProgress() const;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    struct FWarmupRequest
    {
        TWeakObjectPtr<UObject> Mesh;
        TWeakObjectPtr<UMaterialInterface> Material;
        bool bInstanced = false;
    };

    struct FStreamingTarget
    {
        TWeakObjectPtr<UStreamableRenderAsset> Asset;
        int32 RequiredLODs = 0;
    };

    void ResetPreparation();
    void ReleaseWarmupComponents();
    void ResetRenderReadiness();
    void SetPhase(EMCWorldPreloadPhase NewPhase);
    void Fail(const FText& Reason, const FString& TechnicalReason);
    void StartLoadWave(TArray<FSoftObjectPath> Paths);
    void FinishLoadWave();
    void BuildRenderingWork();
    void ProcessRenderingWork();
    bool SettleWorldAndRefreshManifest();
    bool IsWorldStreamingReady() const;
    void BuildStreamingTargets();
    bool CheckStreamingTargets();
    void CompletePreparation();
    int32 GetPendingCompilationCount() const;
    uint32 GetPendingPipelineCount() const;
    void TickRenderReadiness();
#if WITH_EDITOR
    void DiagnoseLateCompilation(double Now);
    bool IsEditorFirstUseMaterialReady() const;
    TWeakObjectPtr<UMaterialInterface> EditorFirstUseMaterial;
    double RenderQuietSince = 0;
    double LateCompilationWatchUntil = 0;
    double NextLateCompilationCheckAt = 0;
    double NextLateCompilationLogAt = 0;
#endif

    UPROPERTY(Transient)
    TArray<TObjectPtr<UObject>> RetainedAssets;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UPrimitiveComponent>> WarmupComponents;

    TWeakObjectPtr<APlayerController> Controller;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> WorldPrimitives;
    TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
    TArray<FWarmupRequest> WarmupRequests;
    TArray<FStreamingTarget> StreamingTargets;
    TSet<FSoftObjectPath> SeenPaths;
    TSet<TWeakObjectPtr<UObject>> ExpandedObjects;
    TArray<FSoftObjectPath> ActiveWavePaths;
    TArray<TSharedPtr<FStreamableHandle>> LoadHandles;
    TSharedPtr<FStreamableHandle> ActiveHandle;
    TSharedPtr<FRenderCommandFence> ReadinessFence;
    EMCWorldPreloadPhase Phase = EMCWorldPreloadPhase::Idle;
    FText FailureReason;
    double StartedAt = 0;
    double NextDiagnosticAt = 0;
    double TimeoutSeconds = 120;
    int32 CompletedPaths = 0;
    int32 LoadWave = 0;
    int32 NextWarmupRequest = 0;
    int32 ReadyStreamingTargets = 0;
    int32 StableRenderFrames = 0;
    int32 StableWorldFrames = 0;
    int32 ObservedWorldActors = -1;
    int32 ObservedWorldPrimitives = -1;
    int32 ObservedWorldLevels = -1;
    int32 TextureTargetSize = 1024;
    int64 TextureBudgetBytes = 256ll * 1024 * 1024;
    int64 MeshBudgetBytes = 64ll * 1024 * 1024;
    bool bRenderingWorkBuilt = false;
    bool bStreamingTargetsBuilt = false;
    bool bPresentationReady = true;
};
