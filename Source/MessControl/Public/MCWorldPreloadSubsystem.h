#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCVFXNativePresentationSettings.h"
#include "Async/TaskGraphInterfaces.h"
#include "PSOPrecacheFwd.h"
#include "MCWorldPreloadSubsystem.generated.h"

class APlayerController;
class AMCArenaTooth;
class AMCToothCharacter;
class AMCIceEvent;
class AMCIceEventVFX;
class FRenderCommandFence;
struct FStreamableHandle;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UStreamableRenderAsset;
class UMCEventSFXReadiness;
class USoundBase;
class UNiagaraSystem;
class UMeshComponent;
class UMaterialInstanceDynamic;
class UMCNiagaraMaterialContextResources;
class UMCNiagaraProceduralSurfaceResources;

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
    bool IsReady() const;

    UFUNCTION(BlueprintPure, Category="Loading")
    bool HasFailed() const { return Phase == EMCWorldPreloadPhase::Failed; }
    /** Arena/run-scoped only. Unknown scope waits; explicit lab contexts retain their own preparation. */
    bool UsesPreparedIcePresentation() const;
    /** Transfers the single already-prepared instance. No allocation, loading or activation before Ready. */
    AMCIceEventVFX* ClaimPreparedIcePresentation(AMCIceEvent* Source,FGuid& OutToken,FString& Error);

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
    /** Exact immutable native variants retained by this location's preparation. */
    bool GetPreparedNativeStyle(const UMCVFXNativePresentationSettings* Source,FMCVFXPreparedNativePresentation& Out) const;
    /** No load: immutable captured tool alternatives must already belong to this location's closure. */
    bool IsVisualResourceRetained(const UObject* Object) const;
    /** Isolated preview/fixture pre-commit preparation only. Resident assets, no loading; poll until PSOs are ready. */
    bool PrepareResidentVisualResources(const TArray<UObject*>& Resources,FString& Error);
    /** Location-scoped playback assets/Precache/startup bytes; never decoded-first-buffer certification. */
    const UMCEventSFXReadiness* GetEventAudioReadiness() const;
    /** Explicit resident-only preparation, also available to the isolated Composer world before Commit. */
    bool PrepareEventAudioSources(const TArray<USoundBase*>& Sources,FString& Error);
    void PollEventAudioReadiness();
    /** Explicitly reprepare retained audio resources after enabling output. Does not autoplay. */
    bool ReprepareEventAudioReadiness(FString& Error);
    /** Local material-context pools, shared by location loading and the isolated Composer world. */
    UMCNiagaraMaterialContextResources* GetMaterialContextResources() const;
    UFUNCTION(BlueprintPure,Category="Loading|Surface") UMCNiagaraProceduralSurfaceResources* GetProceduralSurfaceResources()const{return ProceduralSurfaceResources;}
    bool PrepareMaterialContextSources(const TArray<UNiagaraSystem*>& Sources,int32 ConcurrentInstancesPerTemplate,
        FGuid ScopeEpoch,FString& Error);
    /** Explicit preparation only; unknown/custom factories fail instead of receiving a guessed proxy. */
    bool RegisterMaterialContextTarget(UMeshComponent* Target,FString& Error);
    bool RegisterMaterialContextScalar(UMeshComponent* Subject,UMaterialInstanceDynamic* ExistingMID,
        FName AllowedParameter,FString& Error);
    /** Post-Ready identity enrollment for an already prepared scalar factory.
     * Never creates resources or grants a captured-body readiness certificate. */
    bool AdoptResidentMaterialContextScalar(UMeshComponent* Subject,UMaterialInstanceDynamic* ExistingMID,
        FName AllowedParameter,FString& Error);
    /** True means poll succeeded; bWaiting distinguishes finite pending work. Never advances an Idle visual phase. */
    bool PollMaterialContextReadiness(bool& bWaiting,FString& Error);
    /** Explicit source/preview-generation boundary only; stop owned players first. Does not change visual Phase. */
    void ResetMaterialContextPreparation();

    /** Progress within the current resource stage, never an estimate of total time or readiness. */
    UFUNCTION(BlueprintPure, Category="Loading")
    float GetMeasuredPhaseProgress() const;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    friend class FMCNativePrewarmRebuildStability;
    friend class FMCPrewarmedIcePresentationOwnership;
    friend class FMCEventAudioWorldService;
    friend class FMCEventAudioLegacySafety;
    friend class FMCScopedPreloadEnrollment;
    struct FWarmupRequest
    {
        TWeakObjectPtr<UObject> Mesh;
        TWeakObjectPtr<UMaterialInterface> Material;
        bool bInstanced = false;
        bool operator==(const FWarmupRequest& Other) const
        { return Mesh==Other.Mesh && Material==Other.Material && bInstanced==Other.bInstanced; }
    };

    struct FStreamingTarget
    {
        TWeakObjectPtr<UStreamableRenderAsset> Asset;
        int32 RequiredLODs = 0;
    };

    bool IsIcePresentationLocation() const;
    bool IsPreparationRunCurrent() const;
    bool ResolveIcePreparationScope(bool& bRequired,bool& bWaiting);
    bool EnsureScopedPresentationDependencies(bool& bRequired);
    bool CreateIcePresentationOwner(FString& Error);
    void ReleaseIcePresentationOwner();
    void ResetPreparation();
    void ReleaseWarmupComponents();
    void ResetRenderReadiness();
    void SetPhase(EMCWorldPreloadPhase NewPhase);
    void Fail(const FText& Reason, const FString& TechnicalReason);
    void StartLoadWave(TArray<FSoftObjectPath> Paths);
    void FinishLoadWave();
    void BuildRenderingWork();
    bool PrepareNativeStylesForLocation(FString& Error);
    bool PrepareLocationMaterialContexts(FString& Error);
    bool PrepareFreezeHeroSources(bool bRequired,FString& Error);
    bool PrepareFreezeHeroResources(bool bRequired,FString& Error);
    void ReleaseFreezeHeroResources();
    bool TickEventAudioReadiness();
    bool IsEventAudioOutputAvailable() const;
    void ProcessRenderingWork();
    void EnqueueRenderingWarmup(UObject* Mesh,UMaterialInterface* Material=nullptr,bool bInstanced=false);
    void CaptureWorldEnrollment(TSet<TWeakObjectPtr<AActor>>& Actors,
        TSet<TWeakObjectPtr<UPrimitiveComponent>>& Primitives) const;
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
    TArray<TWeakObjectPtr<UMaterialInterface>> EditorFirstUseMaterials;
    double RenderQuietSince = 0;
    double LateCompilationWatchUntil = 0;
    double NextLateCompilationCheckAt = 0;
    double NextLateCompilationLogAt = 0;
#endif

    /** Separate from disposable warmup proxies; transferred exactly once per GameplayLoadId. */
    UPROPERTY(Transient) TObjectPtr<AMCIceEventVFX> PreparedIcePresentation;
    FGuid PreparedGameplayLoadId;
    TArray<TWeakObjectPtr<AMCToothCharacter>> PreparedFreezeHeroes;
    bool bIcePresentationConsumed=false;
    FString IcePreparationDiagnostic;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UObject>> RetainedAssets;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> PendingResidentVisuals;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> CertifiedResidentVisuals;
    UPROPERTY(Transient) TMap<TObjectPtr<UMCVFXNativePresentationSettings>,FMCVFXPreparedNativePresentation> NativeStyles;
    UPROPERTY(Transient) TObjectPtr<UMCEventSFXReadiness> EventAudioReadiness;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> EventAudioSources;
    UPROPERTY(Transient) TObjectPtr<UMCNiagaraMaterialContextResources> MaterialContexts;
    UPROPERTY(Transient) TObjectPtr<UMCNiagaraProceduralSurfaceResources> ProceduralSurfaceResources;
    bool PrepareMaterialContextSourcesInternal(const TArray<UNiagaraSystem*>& Sources,int32 Capacity,FGuid Epoch,FString& Error);
    bool PrepareArenaToothMaterialContext(AMCArenaTooth* Tooth,FString& Error);
    void ReleaseArenaToothMaterialContextProbes();
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraSystem>> MaterialContextSources;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ArenaToothMaterialContextProbes;
    TMap<TWeakObjectPtr<AMCArenaTooth>,TWeakObjectPtr<UStaticMesh>> PreparedArenaToothContextMeshes;
    TArray<TWeakObjectPtr<UMeshComponent>> MaterialContextTargets,MaterialContextQueuedTargets;
    TArray<TWeakObjectPtr<UObject>> MaterialContextAddedAssets;
    // Borrowed observations: FMaterial owns these IDs and releases them at destruction.
    TArray<FMaterialPSOPrecacheRequestID> MaterialContextPSORequests;
    FGraphEventArray MaterialContextPSOEvents;
    TSharedPtr<FRenderCommandFence> MaterialContextFence;
    FGuid MaterialContextEpoch;
    int32 MaterialContextCapacity=0,MaterialContextStableFrames=0;
    uint64 MaterialContextLastFrame=MAX_uint64;
    double MaterialContextQuietSince=0;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UPrimitiveComponent>> WarmupComponents;
    TSet<TWeakObjectPtr<UMaterialInterface>> NativeWarmupMaterials;
    TSet<TWeakObjectPtr<AActor>> ObservedActors;
    TSet<TWeakObjectPtr<UPrimitiveComponent>> ObservedPrimitives;
    double LastWarmupPumpAt=0;

    TWeakObjectPtr<APlayerController> Controller;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> WorldPrimitives;
    TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
    TArray<FWarmupRequest> WarmupRequests;
    TArray<FStreamingTarget> StreamingTargets;
    TSet<FSoftObjectPath> SeenPaths;
    TSet<TWeakObjectPtr<UObject>> ExpandedObjects;
    TSet<TWeakObjectPtr<USoundBase>> AuditedLegacyAudioSources;
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
    bool bEventAudioOutputPrepared=false;
    bool bEventAudioPlanValid=false;
    uint32 EventAudioDeviceId=MAX_uint32;
    uint32 EventAudioClosureHash=0;
};
