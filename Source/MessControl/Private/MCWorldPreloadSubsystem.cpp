#include "MCWorldPreloadSubsystem.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"

#include "MCPreloadManifest.h"
#include "MCPreloadSettings.h"
#include "MCUIDataLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/LevelStreaming.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/StreamableRenderAsset.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "RenderCommandFence.h"
#include "ShaderPipelineCache.h"
#include "Streaming/StreamableRenderResourceState.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "IAssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#include "RHIGlobals.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMCWorldPreload, Log, All);

namespace
{
constexpr int32 MaxDependencyWaves = 32;
constexpr int32 MaxManifestPaths = 4096;
constexpr int32 WarmupRequestsPerTick = 4;
constexpr int32 RequiredStableRenderFrames = 3;
#if WITH_EDITOR
constexpr double RequiredEditorQuietSeconds = 1.;
constexpr double LateCompilationWatchSeconds = 60.;
#endif

FText LoadingText(const TCHAR* Key, const TCHAR* Russian, const TCHAR* English)
{
    const bool bRussian = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName() == TEXT("ru");
    return UMCUIDataLibrary::ResolveText(UMCUIDataLibrary::GetDefaultLocalizationTable(), FName(Key),
        FText::FromString(bRussian ? Russian : English));
}

const TCHAR* PhaseName(EMCWorldPreloadPhase Phase)
{
    switch (Phase)
    {
    case EMCWorldPreloadPhase::Idle: return TEXT("Idle");
    case EMCWorldPreloadPhase::LoadingAssets: return TEXT("LoadingAssets");
    case EMCWorldPreloadPhase::PreparingRendering: return TEXT("PreparingRendering");
    case EMCWorldPreloadPhase::WaitingForStreaming: return TEXT("WaitingForStreaming");
    case EMCWorldPreloadPhase::Ready: return TEXT("Ready");
    case EMCWorldPreloadPhase::Failed: return TEXT("Failed");
    }
    return TEXT("Unknown");
}
}

bool UMCWorldPreloadSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UMCWorldPreloadSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && !IsRunningDedicatedServer()
        && World->GetNetMode() != NM_DedicatedServer;
}

bool UMCWorldPreloadSubsystem::IsPreparing() const
{
    return Phase == EMCWorldPreloadPhase::LoadingAssets
        || Phase == EMCWorldPreloadPhase::PreparingRendering
        || Phase == EMCWorldPreloadPhase::WaitingForStreaming;
}

bool UMCWorldPreloadSubsystem::IsTickable() const
{
    if (!IsInitialized() || IsTemplate()) return false;
#if WITH_EDITOR
    if (IsReady() && FPlatformTime::Seconds() < LateCompilationWatchUntil) return true;
#endif
    return IsPreparing();
}

void UMCWorldPreloadSubsystem::ResetRenderReadiness()
{
    StableRenderFrames = 0;
    ReadinessFence.Reset();
#if WITH_EDITOR
    RenderQuietSince = 0;
#endif
}

void UMCWorldPreloadSubsystem::SetPhase(EMCWorldPreloadPhase NewPhase)
{
    if (Phase == NewPhase) return;
    Phase = NewPhase;
    ResetRenderReadiness();
    UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_PHASE world=%s phase=%s elapsed=%.2f assets=%d/%d"),
        *GetNameSafe(GetWorld()), PhaseName(Phase), FPlatformTime::Seconds() - StartedAt,
        CompletedPaths, SeenPaths.Num());
}

void UMCWorldPreloadSubsystem::ReleaseWarmupComponents()
{
    for (UPrimitiveComponent* Component : WarmupComponents)
        if (IsValid(Component)) Component->DestroyComponent();
    WarmupComponents.Reset();
    WorldPrimitives.Reset();
    WarmupRequests.Reset();
    Materials.Reset();
    StreamingTargets.Reset();
    ReadinessFence.Reset();
}

void UMCWorldPreloadSubsystem::ResetPreparation()
{
    // No completion delegates capture the subsystem. Polling plus cancellation also
    // prevents an older request from completing a new Retry or a different world.
    for (const TSharedPtr<FStreamableHandle>& Handle : LoadHandles)
    {
        if (!Handle.IsValid()) continue;
        if (!Handle->HasLoadCompleted()) Handle->CancelHandle();
        else Handle->ReleaseHandle();
    }
    ActiveHandle.Reset();
    LoadHandles.Reset();
    ActiveWavePaths.Reset();
    ReleaseWarmupComponents();
    RetainedAssets.Reset();
    SeenPaths.Reset();
    ExpandedObjects.Reset();
    CompletedPaths = 0;
    LoadWave = 0;
    NextWarmupRequest = 0;
    ReadyStreamingTargets = 0;
    StableRenderFrames = 0;
    StableWorldFrames = 0;
#if WITH_EDITOR
    RenderQuietSince = 0;
    EditorFirstUseMaterials.Reset();
    LateCompilationWatchUntil = 0;
    NextLateCompilationCheckAt = 0;
    NextLateCompilationLogAt = 0;
#endif
    ObservedWorldActors = -1;
    ObservedWorldPrimitives = -1;
    ObservedWorldLevels = -1;
    bRenderingWorkBuilt = false;
    bStreamingTargetsBuilt = false;
    FailureReason = FText::GetEmpty();
    Phase = EMCWorldPreloadPhase::Idle;
}

void UMCWorldPreloadSubsystem::Deinitialize()
{
    ResetPreparation();
    Controller.Reset();
    Super::Deinitialize();
}

void UMCWorldPreloadSubsystem::BeginPreparation(APlayerController* LocalController)
{
    if (!IsValid(LocalController) || !LocalController->IsLocalController()
        || LocalController->GetWorld() != GetWorld() || !GetWorld()
        || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
    // PC presentation is polled every frame. An error must remain visible until
    // explicit Retry, instead of silently starting the failed request again.
    if (IsPreparing() || IsReady() || HasFailed()) return;
    ResetPreparation();
    Controller = LocalController;
    StartedAt = FPlatformTime::Seconds();
    NextDiagnosticAt = StartedAt + 10.;
    const UMCPreloadSettings* Settings = GetDefault<UMCPreloadSettings>();
    TimeoutSeconds = FMath::Clamp<double>(Settings->TimeoutSeconds, 10., 600.);
    TextureTargetSize = FMath::Clamp(Settings->TargetTextureSize, 128, 4096);
    TextureBudgetBytes = FMath::Clamp<int64>(Settings->TextureBudgetMB, 16, 2048) * 1024 * 1024;
    MeshBudgetBytes = FMath::Clamp<int64>(Settings->MeshBudgetMB, 8, 1024) * 1024 * 1024;
    TArray<FSoftObjectPath> Paths;
    FMCPreloadManifest::Collect(GetWorld(), Paths);
    SetPhase(EMCWorldPreloadPhase::LoadingAssets);
    StartLoadWave(MoveTemp(Paths));
}

void UMCWorldPreloadSubsystem::Retry()
{
    APlayerController* LocalController = Controller.Get();
    if (!HasFailed() || !IsValid(LocalController)) return;
    ResetPreparation();
    BeginPreparation(LocalController);
}

void UMCWorldPreloadSubsystem::Fail(const FText& Reason, const FString& TechnicalReason)
{
    FailureReason = Reason;
    if (ActiveHandle.IsValid() && !ActiveHandle->HasLoadCompleted()) ActiveHandle->CancelHandle();
    ActiveHandle.Reset();
    ReleaseWarmupComponents();
    SetPhase(EMCWorldPreloadPhase::Failed);
    UE_LOG(LogMCWorldPreload, Error, TEXT("MC_PRELOAD_FAILED world=%s reason=%s"),
        *GetNameSafe(GetWorld()), *TechnicalReason);
}

void UMCWorldPreloadSubsystem::StartLoadWave(TArray<FSoftObjectPath> Paths)
{
    TArray<FSoftObjectPath> NewPaths;
    for (const FSoftObjectPath& Path : Paths)
    {
        if (Path.IsNull() || SeenPaths.Contains(Path)) continue;
        SeenPaths.Add(Path);
        NewPaths.Add(Path);
    }
    if (SeenPaths.Num() > MaxManifestPaths || ++LoadWave > MaxDependencyWaves)
    {
        Fail(LoadingText(TEXT("loading.error.manifest"), TEXT("Не удалось подготовить список ресурсов локации."), TEXT("The level's resource list could not be prepared.")),
            TEXT("Manifest exceeds bounded dependency discovery limits"));
        return;
    }
    if (NewPaths.IsEmpty())
    {
        ActiveHandle.Reset();
        ActiveWavePaths.Reset();
        SetPhase(EMCWorldPreloadPhase::PreparingRendering);
        return;
    }
    ActiveWavePaths = NewPaths;
    ActiveHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        MoveTemp(NewPaths), FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority,
        false, false, TEXT("MessControl World Preload"));
    if (!ActiveHandle.IsValid())
    {
        Fail(LoadingText(TEXT("loading.error.request"), TEXT("Не удалось загрузить ресурсы локации. Повтори попытку."), TEXT("Level resources could not be loaded. Try again.")),
            TEXT("RequestAsyncLoad did not return a handle"));
        return;
    }
    // Completed handles continue to hold their assets for this world's lifetime.
    LoadHandles.Add(ActiveHandle);
}

void UMCWorldPreloadSubsystem::FinishLoadWave()
{
    TArray<UObject*> Loaded;
    ActiveHandle->GetLoadedAssets(Loaded);
    int32 Missing = 0;
    for (UObject* Object : Loaded)
    {
        if (!IsValid(Object)) { ++Missing; continue; }
        RetainedAssets.AddUnique(Object);
    }
    if (Loaded.Num() != ActiveWavePaths.Num() || Missing > 0)
    {
        for (const FSoftObjectPath& Path : ActiveWavePaths)
            if (!Path.ResolveObject()) UE_LOG(LogMCWorldPreload, Error, TEXT("MC_PRELOAD_MISSING asset=%s"), *Path.ToString());
        Fail(LoadingText(TEXT("loading.error.missing"), TEXT("Не найден нужный ресурс локации. Повтори попытку или проверь файлы игры."), TEXT("A required level resource is missing. Try again or check the game files.")),
            FString::Printf(TEXT("Async wave %d completed with %d missing resources (%d requested, %d returned)"),
                LoadWave, Missing, ActiveWavePaths.Num(), Loaded.Num()));
        return;
    }
    CompletedPaths += ActiveWavePaths.Num();
    TArray<FSoftObjectPath> Dependencies;
    for (UObject* Object : Loaded)
    {
        const TWeakObjectPtr<UObject> WeakObject(Object);
        if (ExpandedObjects.Contains(WeakObject)) continue;
        ExpandedObjects.Add(WeakObject);
        FMCPreloadManifest::CollectSoftReferences(Object, Dependencies);
    }
    StartLoadWave(MoveTemp(Dependencies));
}

int32 UMCWorldPreloadSubsystem::GetPendingCompilationCount() const
{
#if WITH_EDITOR
    // The aggregate includes shader-map finalization, not just worker jobs.
    // Editor executables running -game also need this barrier (GIsEditor=false).
    const int32 ShaderWork = GShaderCompilingManager
        ? FMath::Max(GShaderCompilingManager->GetNumRemainingJobs(), GShaderCompilingManager->IsCompiling() ? 1 : 0) : 0;
    return FMath::Max(FAssetCompilingManager::Get().GetNumRemainingAssets(), ShaderWork);
#else
    return 0;
#endif
}

uint32 UMCWorldPreloadSubsystem::GetPendingPipelineCount() const
{
    return FApp::CanEverRender() ? FShaderPipelineCache::NumPrecompilesRemaining() : 0;
}

void UMCWorldPreloadSubsystem::BuildRenderingWork()
{
    bRenderingWorkBuilt = true;
    TSet<UMaterialInterface*> NiagaraRendererMaterials;
    TArray<UPrimitiveComponent*> Primitives;
    const FVector ViewLocation = Controller.IsValid() ? Controller->GetFocalLocation() : FVector::ZeroVector;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for (UPrimitiveComponent* Component : Components)
            if (IsValid(Component) && Component->IsRegistered()) Primitives.Add(Component);
    }
    Primitives.Sort([ViewLocation](const UPrimitiveComponent& A, const UPrimitiveComponent& B)
    {
        return FVector::DistSquared(A.GetComponentLocation(), ViewLocation)
            < FVector::DistSquared(B.GetComponentLocation(), ViewLocation);
    });
    auto AddMaterials = [this](const TArray<UMaterialInterface*>& Used)
    {
        for (UMaterialInterface* Material : Used)
            if (IsValid(Material)) Materials.AddUnique(Material);
    };
    for (UPrimitiveComponent* Component : Primitives)
    {
        WorldPrimitives.Add(Component);
        Component->PrecachePSOs();
        TArray<UMaterialInterface*> Used;
        Component->GetUsedMaterials(Used);
        AddMaterials(Used);
    }
    UStaticMesh* RepresentativeStatic = nullptr;
    USkeletalMesh* RepresentativeSkeletal = nullptr;
    for (UObject* Object : RetainedAssets)
    {
        if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
        {
            if (!RepresentativeStatic || Mesh->GetPathName() == TEXT("/Engine/BasicShapes/Plane.Plane")) RepresentativeStatic = Mesh;
            FWarmupRequest& Request = WarmupRequests.AddDefaulted_GetRef();
            Request.Mesh = Mesh;
            for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
                if (Slot.MaterialInterface) Materials.AddUnique(Slot.MaterialInterface);
        }
        else if (USkeletalMesh* SkeletalAsset = Cast<USkeletalMesh>(Object))
        {
            if (!RepresentativeSkeletal) RepresentativeSkeletal = SkeletalAsset;
            FWarmupRequest& Request = WarmupRequests.AddDefaulted_GetRef();
            Request.Mesh = SkeletalAsset;
            for (const FSkeletalMaterial& Slot : SkeletalAsset->GetMaterials())
                if (Slot.MaterialInterface) Materials.AddUnique(Slot.MaterialInterface);
        }
        else if (UMaterialInterface* Material = Cast<UMaterialInterface>(Object)) Materials.AddUnique(Material);
        else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
        {
            // Renderer overrides can live outside the system's own folder.
            // Collect actual referenced interfaces, not an asset-folder scan.
            for (const auto& Handle : System->GetEmitterHandles())
                if (const auto* Data = Handle.GetEmitterData())
                    for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
                        if (IsValid(Renderer))
                        {
                            TArray<UMaterialInterface*> Used;
                            Renderer->GetUsedMaterials(nullptr, Used);
                            AddMaterials(Used);
                            for (UMaterialInterface* RendererMaterial : Used)
                                if (IsValid(RendererMaterial)) NiagaraRendererMaterials.Add(RendererMaterial);
                        }
#if WITH_EDITOR
            // UE can leave post-load VM work pending-on-demand while the asset
            // compiling manager reports zero jobs. An inactive warmup component
            // does not demand that work, so first gameplay Activate would do it.
            // This nonblocking poll flushes deferred requests asynchronously;
            // the existing compilation/readiness barrier waits for their result.
            const bool bPendingBefore = System->HasOutstandingCompilationRequests(true);
            const bool bReadyBefore = System->IsReadyToRun();
            const bool bPollComplete = System->PollForCompilationComplete(true);
            UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_NIAGARA_FIRST_USE world=%s asset=%s pendingBefore=%d readyBefore=%d pollComplete=%d pendingAfter=%d readyAfter=%d"),
                *GetNameSafe(GetWorld()), *System->GetPathName(), bPendingBefore, bReadyBefore, bPollComplete,
                System->HasOutstandingCompilationRequests(true), System->IsReadyToRun());
#endif
            // Prepare the Niagara renderer's own vertex factories/PSOs while
            // loading. A static-mesh material proxy alone cannot represent them.
            FWarmupRequest& Request = WarmupRequests.AddDefaulted_GetRef();
            Request.Mesh = System;
        }
    }
    // An explicit material reference may be assigned by a later event rather than
    // by a loaded mesh's default slots. These proxies supply the relevant factories.
    for (const TWeakObjectPtr<UMaterialInterface>& WeakMaterial : Materials)
    {
        UMaterialInterface* Material = WeakMaterial.Get();
        if (Material) RetainedAssets.AddUnique(Material);
        if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface) continue;
        if (RepresentativeStatic)
        {
            FWarmupRequest& Request = WarmupRequests.AddDefaulted_GetRef();
            Request.Mesh = RepresentativeStatic;
            Request.Material = Material;
            if (Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
            {
                FWarmupRequest InstancedRequest = Request;
                InstancedRequest.bInstanced = true;
                WarmupRequests.Add(InstancedRequest);
            }
        }
        if (RepresentativeSkeletal && Material->GetUsageByFlag(MATUSAGE_SkeletalMesh))
        {
            FWarmupRequest& Request = WarmupRequests.AddDefaulted_GetRef();
            Request.Mesh = RepresentativeSkeletal;
            Request.Material = Material;
        }
    }
    UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_RENDER_WORK world=%s primitives=%d previewComponents=%d materials=%d"),
        *GetNameSafe(GetWorld()), WorldPrimitives.Num(), WarmupRequests.Num(), Materials.Num());
#if WITH_EDITOR
    // Hidden warmup components do not take the normal first-draw path that
    // submits deferred Editor shader maps. Prepare the finite known arena
    // resources, reachable active-skill interfaces and actual Niagara renderer
    // materials. These interfaces include external/shared smoke parents. MICs
    // select the shader variant even when its parent lives outside the system
    // folder; GetMaterialResource does so without scanning or
    // loading arbitrary material parents or unrelated content.
    EditorFirstUseMaterials.Reset();
    const FString Map = UGameplayStatics::GetCurrentLevelName(GetWorld(), true);
    const bool bPrepareLegacyArenaMaterials = Map == TEXT("L_Mouth") || Map == TEXT("L_Tutorial");
    {
        const FString FirstUsePaths[] = {
            TEXT("/Game/Art/Materials/MM_Standart_SSS.MM_Standart_SSS"),
            TEXT("/Game/Gameplay/VFX/M_Reaction.M_Reaction"),
            TEXT("/Game/Gameplay/VFX/M_ReactionSoft.M_ReactionSoft"),
            TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"),
            TEXT("/Game/Gameplay/VFX/MI_PassiveGhost.MI_PassiveGhost"),
            TEXT("/Game/Gameplay/VFX/MI_PassiveLightning.MI_PassiveLightning"),
            TEXT("/Game/Gameplay/VFX/MI_PassiveFeedbackSoft.MI_PassiveFeedbackSoft"),
            TEXT("/Game/Gameplay/VFX/MI_PassiveFeedbackGlow.MI_PassiveFeedbackGlow"),
            TEXT("/Game/Gameplay/VFX/M_CartoonReceiptSolid.M_CartoonReceiptSolid")
        };
        const EShaderPlatform Platform = GetFeatureLevelShaderPlatform(GetWorld()->GetFeatureLevel());
        TSet<FMaterialResource*> SubmittedResources;
        for (const TWeakObjectPtr<UMaterialInterface>& WeakMaterial : Materials)
        {
            UMaterialInterface* Material = WeakMaterial.Get();
            if (!Material) continue;
            const FString Path = Material->GetPathName();
            // Materials contains only this location's discovered/retained
            // dependencies. Prefix matching here is not an AssetRegistry scan.
            bool bTargeted = Path.StartsWith(TEXT("/Game/Gameplay/ActiveSkills/")) || NiagaraRendererMaterials.Contains(Material);
            if (bPrepareLegacyArenaMaterials)
                for (const FString& FirstUsePath : FirstUsePaths) bTargeted |= Path == FirstUsePath;
            if (!bTargeted) continue;
            EditorFirstUseMaterials.AddUnique(Material);
            FMaterialResource* Resource = Material->GetMaterialResource(Platform);
            const bool bNeedsSubmission = Resource && !Resource->IsGameThreadShaderMapComplete();
            const int32 PendingBefore = GetPendingCompilationCount();
            // Non-static MICs share their parent's resource. Submit that map
            // once, while retaining/checking each known interface below.
            const bool bRequested = bNeedsSubmission && !SubmittedResources.Contains(Resource);
            if (bRequested)
            {
                SubmittedResources.Add(Resource);
                Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::Normal);
            }
            // A request is not proof that jobs were queued: the public engine API
            // returns void. Log both compiler state and actual map completeness;
            // the barrier below also checks this specific resource, so a no-op
            // cannot be mistaken for completed preparation.
            UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_EDITOR_FIRST_USE world=%s asset=%s resource=%d hasShaderMap=%d complete=%d requested=%d pendingBefore=%d pendingAfter=%d platform=%d"),
                *GetNameSafe(GetWorld()), *Material->GetPathName(), Resource ? 1 : 0,
                Resource && Resource->GetGameThreadShaderMap() ? 1 : 0,
                Resource && Resource->IsGameThreadShaderMapComplete() ? 1 : 0,
                bRequested ? 1 : 0, PendingBefore, GetPendingCompilationCount(), int32(Platform));
        }
    }
#endif
}

void UMCWorldPreloadSubsystem::ProcessRenderingWork()
{
    const int32 StopAt = FMath::Min(NextWarmupRequest + WarmupRequestsPerTick, WarmupRequests.Num());
    while (NextWarmupRequest < StopAt)
    {
        const FWarmupRequest& Request = WarmupRequests[NextWarmupRequest++];
        UPrimitiveComponent* Component = nullptr;
        if (UStaticMesh* Mesh = Cast<UStaticMesh>(Request.Mesh.Get()))
        {
            UStaticMeshComponent* Static = Request.bInstanced
                ? NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient)
                : NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
            Static->SetStaticMesh(Mesh);
            if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Static))
                Instanced->AddInstance(FTransform::Identity);
            Component = Static;
        }
        else if (USkeletalMesh* SkeletalAsset = Cast<USkeletalMesh>(Request.Mesh.Get()))
        {
            USkeletalMeshComponent* Skeletal = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
            Skeletal->SetSkeletalMeshAsset(SkeletalAsset);
            Skeletal->bPauseAnims = true;
            Component = Skeletal;
        }
        else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Request.Mesh.Get()))
        {
            UNiagaraComponent* Niagara = NewObject<UNiagaraComponent>(this, NAME_None, RF_Transient);
            Niagara->SetAutoActivate(false);
            Niagara->SetAutoDestroy(false);
            Niagara->SetAsset(System);
            Component = Niagara;
        }
        if (!Component) continue;
        Component->PrimaryComponentTick.bCanEverTick = false;
        Component->bAutoActivate = false;
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->SetHiddenInGame(true);
        Component->SetVisibility(false);
        Component->SetWorldLocation(FVector(0, 0, -100000));
        if (UMaterialInterface* Material = Request.Material.Get())
            for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index) Component->SetMaterial(Index, Material);
        Component->RegisterComponentWithWorld(GetWorld());
        Component->SetComponentTickEnabled(false);
        Component->PrecachePSOs();
        WarmupComponents.Add(Component);
    }
}

void UMCWorldPreloadSubsystem::BuildStreamingTargets()
{
    bStreamingTargetsBuilt = true;
    TSet<UStreamableRenderAsset*> Seen;
    int64 RemainingTextures = TextureBudgetBytes;
    int64 RemainingMeshes = MeshBudgetBytes;
    int32 Capped = 0;
    int32 Virtual = 0;
    const IConsoleVariable* MipBiasCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.MipBias"));
    const int32 QualityMipBias = MipBiasCVar ? FMath::Clamp(FMath::FloorToInt(MipBiasCVar->GetFloat()), 0, 16) : 0;
    auto AddTarget = [this, &Seen, &RemainingTextures, &RemainingMeshes, &Capped, &Virtual, QualityMipBias](UStreamableRenderAsset* Asset)
    {
        if (!IsValid(Asset) || Seen.Contains(Asset)) return;
        Seen.Add(Asset);
        RetainedAssets.AddUnique(Asset);
        const FStreamableRenderResourceState& State = Asset->GetStreamableResourceState();
        FStreamingTarget& Target = StreamingTargets.AddDefaulted_GetRef();
        Target.Asset = Asset;
        Target.RequiredLODs = 0;
        if (!State.bSupportsStreaming || State.MaxNumLODs == 0) return;
        // VT pages use a different residency system. Do not request every virtual
        // tile while treating it as an ordinary bounded texture mip operation.
        if (State.bSupportsVirtualStreaming) { ++Virtual; return; }
        UTexture2D* Texture = Cast<UTexture2D>(Asset);
        int32 Desired = State.MaxNumLODs;
        if (Texture)
        {
            int32 TopSize = FMath::Max(Texture->GetSizeX(), Texture->GetSizeY()) >> State.AssetLODBias;
            while (TopSize > TextureTargetSize && Desired > State.NumNonStreamingLODs)
            {
                TopSize >>= 1;
                --Desired;
            }
            Desired = FMath::Min(Desired, Texture->GetNumMipsAllowed(false));
            // Preparation respects low texture-quality settings rather than
            // repeatedly fighting the normal streamer's requested mip bias.
            Desired = FMath::Min(Desired, int32(State.MaxNumLODs) - QualityMipBias);
        }
        else if (!Cast<UStaticMesh>(Asset) && !Cast<USkeletalMesh>(Asset)) return;
        // Optional high-resolution chunks may deliberately be absent from this
        // installation. Never turn their absence into an endless loading screen.
        Desired = FMath::Clamp(Desired, int32(State.NumNonStreamingLODs), int32(State.NumNonOptionalLODs));
        int64& Budget = Texture ? RemainingTextures : RemainingMeshes;
        const int64 CurrentBytes = FMath::Max<int64>(0, Asset->CalcCumulativeLODSize(State.NumResidentLODs));
        int64 Bytes = FMath::Max<int64>(0, Asset->CalcCumulativeLODSize(Desired));
        while (Desired > State.NumResidentLODs && FMath::Max<int64>(0, Bytes - CurrentBytes) > Budget)
        {
            --Desired;
            Bytes = FMath::Max<int64>(0, Asset->CalcCumulativeLODSize(Desired));
        }
        if (Desired < State.MaxNumLODs) ++Capped;
        // Gate the intended quality target, not incidental higher mips which the
        // normal streamer is entitled to evict during this loading screen.
        Target.RequiredLODs = Desired;
        // Already resident resources are not additional pressure from this preheat.
        Budget = FMath::Max<int64>(0, Budget - FMath::Max<int64>(0, Bytes - CurrentBytes));
    };
    // Active level and character materials get first use of the bounded budget.
    for (const TWeakObjectPtr<UMaterialInterface>& WeakMaterial : Materials)
    {
        if (UMaterialInterface* Material = WeakMaterial.Get())
        {
            TArray<UTexture*> Textures;
            Material->GetUsedTextures(Textures);
            for (UTexture* Texture : Textures) AddTarget(Cast<UTexture2D>(Texture));
        }
    }
    TArray<UObject*> Assets;
    Assets.Reserve(RetainedAssets.Num());
    for (UObject* Asset : RetainedAssets) Assets.Add(Asset);
    for (UObject* Asset : Assets) AddTarget(Cast<UStreamableRenderAsset>(Asset));
    for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComponent : WorldPrimitives)
    {
        if (const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(WeakComponent.Get())) AddTarget(Static->GetStaticMesh());
        else if (const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(WeakComponent.Get())) AddTarget(Skeletal->GetSkeletalMeshAsset());
    }
    UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_STREAM_TARGETS world=%s assets=%d targetTextureSize=%d textureExtraMB=%.1f meshExtraMB=%.1f capped=%d virtualTextures=%d"),
        *GetNameSafe(GetWorld()), StreamingTargets.Num(), TextureTargetSize,
        double(TextureBudgetBytes - RemainingTextures) / (1024. * 1024.),
        double(MeshBudgetBytes - RemainingMeshes) / (1024. * 1024.), Capped, Virtual);
}

bool UMCWorldPreloadSubsystem::CheckStreamingTargets()
{
    ReadyStreamingTargets = 0;
    for (const FStreamingTarget& Target : StreamingTargets)
    {
        UStreamableRenderAsset* Asset = Target.Asset.Get();
        if (!IsValid(Asset)) return false;
        if (Asset->HasPendingInitOrStreaming()) continue;
        const FStreamableRenderResourceState& State = Asset->GetStreamableResourceState();
        if (State.NumResidentLODs < Target.RequiredLODs)
        {
            Asset->StreamIn(Target.RequiredLODs, true);
            continue;
        }
        if (const UTexture2D* Texture = Cast<UTexture2D>(Asset))
            if (!Texture->GetResource()) continue;
        ++ReadyStreamingTargets;
    }
    return ReadyStreamingTargets == StreamingTargets.Num();
}

void UMCWorldPreloadSubsystem::CompletePreparation()
{
#if WITH_EDITOR
    for (UObject* Object : RetainedAssets)
        if (const UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
            UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_NIAGARA_FIRST_USE_READY world=%s asset=%s pending=%d ready=%d"),
                *GetNameSafe(GetWorld()), *System->GetPathName(), System->HasOutstandingCompilationRequests(true), System->IsReadyToRun());
    for (const TWeakObjectPtr<UMaterialInterface>& Material : EditorFirstUseMaterials)
        if (Material.IsValid())
            UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_EDITOR_FIRST_USE_READY world=%s asset=%s complete=%d"),
                *GetNameSafe(GetWorld()), *Material->GetPathName(), IsEditorFirstUseMaterialReady() ? 1 : 0);
#endif
    // These transient components only supplied vertex-factory/material data for
    // precaching. Compiled PSOs are engine/driver cached; no hidden actors or
    // extra scene primitives need to survive after preparation completes.
    for (UPrimitiveComponent* Component : WarmupComponents)
        if (IsValid(Component)) Component->DestroyComponent();
    WarmupComponents.Reset();
    WarmupRequests.Reset();
    UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_READY world=%s resources=%d streaming=%d/%d elapsed=%.2f"),
        *GetNameSafe(GetWorld()), RetainedAssets.Num(), ReadyStreamingTargets,
        StreamingTargets.Num(), FPlatformTime::Seconds() - StartedAt);
    SetPhase(EMCWorldPreloadPhase::Ready);
#if WITH_EDITOR
    LateCompilationWatchUntil = FPlatformTime::Seconds() + LateCompilationWatchSeconds;
#endif
}

#if WITH_EDITOR
bool UMCWorldPreloadSubsystem::IsEditorFirstUseMaterialReady() const
{
    const EShaderPlatform Platform = GetFeatureLevelShaderPlatform(GetWorld()->GetFeatureLevel());
    for (const TWeakObjectPtr<UMaterialInterface>& Material : EditorFirstUseMaterials)
    {
        if (!Material.IsValid()) return false;
        const FMaterialResource* Resource = Material->GetMaterialResource(Platform);
        if (!Resource || !Resource->IsGameThreadShaderMapComplete()) return false;
    }
    return true;
}

void UMCWorldPreloadSubsystem::DiagnoseLateCompilation(double Now)
{
    if (Now < NextLateCompilationCheckAt || Now >= LateCompilationWatchUntil) return;
    NextLateCompilationCheckAt = Now + .5;
    const int32 Compilation = GetPendingCompilationCount();
    const uint32 Pipelines = GetPendingPipelineCount();
    if ((Compilation == 0 && Pipelines == 0) || Now < NextLateCompilationLogAt) return;
    NextLateCompilationLogAt = Now + 5.;
    // Compilation managers are editor-wide: another editor tab can cause work.
    // This observes only; never cover active gameplay with another loading screen.
    UE_LOG(LogMCWorldPreload, Warning, TEXT("MC_PRELOAD_LATE_WORK world=%s afterReady=%.2f compilation=%d pipelines=%u editorGlobal=1"),
        *GetNameSafe(GetWorld()), Now - (LateCompilationWatchUntil - LateCompilationWatchSeconds), Compilation, Pipelines);
    for (IAssetCompilingManager* Manager : FAssetCompilingManager::Get().GetRegisteredManagers())
        if (Manager && Manager->GetNumRemainingAssets() > 0)
            UE_LOG(LogMCWorldPreload, Warning, TEXT("MC_PRELOAD_LATE_CATEGORY type=%s remaining=%d"),
                *Manager->GetAssetTypeName().ToString(), Manager->GetNumRemainingAssets());
    int32 Reported = 0;
    for (const TWeakObjectPtr<UMaterialInterface>& Material : Materials)
        if (Material.IsValid() && Material->IsCompiling())
        {
            UE_LOG(LogMCWorldPreload, Warning, TEXT("MC_PRELOAD_LATE_MATERIAL asset=%s"), *Material->GetPathName());
            if (++Reported >= 8) break;
        }
}
#endif

bool UMCWorldPreloadSubsystem::IsWorldStreamingReady() const
{
    const UWorld* World = GetWorld();
    if (!World || World->IsVisibilityRequestPending()) return false;
    for (const ULevelStreaming* Level : World->GetStreamingLevels())
    {
        if (!Level) continue;
        if (Level->ShouldBeLoaded() && !Level->IsLevelLoaded()) return false;
        if (Level->ShouldBeVisible() && !Level->IsLevelVisible()) return false;
    }
    return true;
}

bool UMCWorldPreloadSubsystem::SettleWorldAndRefreshManifest()
{
    if (!IsWorldStreamingReady())
    {
        StableWorldFrames = 0;
        ResetRenderReadiness();
        return false;
    }
    int32 Actors = 0;
    int32 Primitives = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        ++Actors;
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for (const UPrimitiveComponent* Component : Components)
            if (IsValid(Component) && Component->IsRegistered()) ++Primitives;
    }
    const int32 Levels = GetWorld()->GetNumLevels();
    if (Actors != ObservedWorldActors || Primitives != ObservedWorldPrimitives || Levels != ObservedWorldLevels)
    {
        ObservedWorldActors = Actors;
        ObservedWorldPrimitives = Primitives;
        ObservedWorldLevels = Levels;
        StableWorldFrames = 0;
        ResetRenderReadiness();
        if (bRenderingWorkBuilt)
        {
            ReleaseWarmupComponents();
            NextWarmupRequest = 0;
            ReadyStreamingTargets = 0;
            bRenderingWorkBuilt = false;
            bStreamingTargetsBuilt = false;
            SetPhase(EMCWorldPreloadPhase::PreparingRendering);
        }
    }
    if (++StableWorldFrames < RequiredStableRenderFrames) return false;
    if (!bRenderingWorkBuilt)
    {
        // A streamed presentation level can instantiate actors after PC BeginPlay.
        // Re-discover that actual world once it is visible, before taking the
        // component/material snapshot used by the graphics barrier.
        TArray<FSoftObjectPath> WorldPaths;
        FMCPreloadManifest::Collect(GetWorld(), WorldPaths);
        if (WorldPaths.ContainsByPredicate([this](const FSoftObjectPath& Path) { return !Path.IsNull() && !SeenPaths.Contains(Path); }))
        {
            SetPhase(EMCWorldPreloadPhase::LoadingAssets);
            StartLoadWave(MoveTemp(WorldPaths));
            return false;
        }
    }
    return true;
}

void UMCWorldPreloadSubsystem::TickRenderReadiness()
{
    if (!bPresentationReady || !SettleWorldAndRefreshManifest())
    {
        ResetRenderReadiness();
        return;
    }
    if (!FApp::CanEverRender())
    {
        // NullRHI/commandlet automation can exercise the asset barrier but cannot
        // certify graphics. This is deliberately visible in its validation log.
        UE_LOG(LogMCWorldPreload, Warning, TEXT("MC_PRELOAD_ASSETS_ONLY world=%s renderingUnavailable=1 visualValidation=0"), *GetNameSafe(GetWorld()));
        CompletePreparation();
        return;
    }
#if WITH_EDITOR
    FAssetCompilingManager::Get().ProcessAsyncTasks(true);
    if (GShaderCompilingManager) GShaderCompilingManager->ProcessAsyncResults(true, false);
#endif
    if (GetPendingCompilationCount() > 0)
    {
        ResetRenderReadiness();
        return;
    }
    if (!bRenderingWorkBuilt) BuildRenderingWork();
    if (NextWarmupRequest < WarmupRequests.Num())
    {
        ResetRenderReadiness();
        ProcessRenderingWork();
        return;
    }
    if (!bStreamingTargetsBuilt)
    {
        BuildStreamingTargets();
        SetPhase(EMCWorldPreloadPhase::WaitingForStreaming);
    }
    bool bComponentsReady = true;
    for (const TWeakObjectPtr<UPrimitiveComponent>& Component : WorldPrimitives)
        if (Component.IsValid() && Component->IsPSOPrecaching()) bComponentsReady = false;
    for (const UPrimitiveComponent* Component : WarmupComponents)
        if (IsValid(Component) && Component->IsPSOPrecaching()) bComponentsReady = false;
#if WITH_EDITOR
    // The Niagara manager's global job count excludes results awaiting
    // retrieval. Apply completed VM results nonblockingly and keep this world
    // behind the barrier while any of its retained systems still has work.
    for (UObject* Object : RetainedAssets)
        if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
            if (System->HasOutstandingCompilationRequests(true))
            {
                System->PollForCompilationComplete(true);
                bComponentsReady &= !System->HasOutstandingCompilationRequests(true);
            }
    bComponentsReady &= IsEditorFirstUseMaterialReady();
#endif
    const bool bStreamingReady = CheckStreamingTargets();
    if (!bComponentsReady || !bStreamingReady || GetPendingPipelineCount() > 0 || GetPendingCompilationCount() > 0)
    {
        ResetRenderReadiness();
        return;
    }
#if WITH_EDITOR
    // Texture/mesh completion callbacks can enqueue another shader wave a few
    // frames later. Require sustained quiet as well as the existing RHI fences.
    const double Now = FPlatformTime::Seconds();
    if (RenderQuietSince == 0) RenderQuietSince = Now;
    if (Now - RenderQuietSince < RequiredEditorQuietSeconds) return;
#endif
    if (!ReadinessFence.IsValid())
    {
        ReadinessFence = MakeShared<FRenderCommandFence>();
        ReadinessFence->BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
        return;
    }
    if (!ReadinessFence->IsFenceComplete()) return;
    if (++StableRenderFrames < RequiredStableRenderFrames)
    {
        ReadinessFence->BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
        return;
    }
    CompletePreparation();
}

void UMCWorldPreloadSubsystem::Tick(float DeltaSeconds)
{
    if (!GetWorld() || GetWorld()->bIsTearingDown) return;
    const double Now = FPlatformTime::Seconds();
#if WITH_EDITOR
    if (IsReady())
    {
        DiagnoseLateCompilation(Now);
        return;
    }
#endif
    if (!IsPreparing()) return;
    if (Now - StartedAt > TimeoutSeconds)
    {
        Fail(LoadingText(TEXT("loading.error.timeout"), TEXT("Подготовка локации заняла слишком много времени. Повтори попытку."), TEXT("Preparing the level took too long. Try again.")),
            FString::Printf(TEXT("Timeout after %.1fs phase=%s assets=%d/%d compilation=%d pipelines=%u streaming=%d/%d"),
                Now - StartedAt, PhaseName(Phase), CompletedPaths, SeenPaths.Num(), GetPendingCompilationCount(),
                GetPendingPipelineCount(), ReadyStreamingTargets, StreamingTargets.Num()));
        return;
    }
    if (Now >= NextDiagnosticAt)
    {
        NextDiagnosticAt = Now + 10.;
        UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_WAIT phase=%s elapsed=%.1f compilation=%d pipelines=%u streaming=%d/%d"),
            PhaseName(Phase), Now - StartedAt, GetPendingCompilationCount(), GetPendingPipelineCount(),
            ReadyStreamingTargets, StreamingTargets.Num());
    }
    if (Phase == EMCWorldPreloadPhase::LoadingAssets)
    {
        if (!ActiveHandle.IsValid() || ActiveHandle->WasCanceled())
        {
            Fail(LoadingText(TEXT("loading.error.canceled"), TEXT("Загрузка локации прервана. Повтори попытку."), TEXT("Level loading was interrupted. Try again.")), TEXT("Async load handle canceled or absent"));
            return;
        }
        if (ActiveHandle->HasLoadCompleted()) FinishLoadWave();
        return;
    }
    TickRenderReadiness();
}

float UMCWorldPreloadSubsystem::GetAssetProgress() const
{
    if (IsReady() || Phase == EMCWorldPreloadPhase::PreparingRendering || Phase == EMCWorldPreloadPhase::WaitingForStreaming) return 1.f;
    if (SeenPaths.IsEmpty()) return 0.f;
    const float WaveProgress = ActiveHandle.IsValid() ? ActiveHandle->GetProgress() * ActiveWavePaths.Num() : 0.f;
    return FMath::Clamp((CompletedPaths + WaveProgress) / SeenPaths.Num(), 0.f, 1.f);
}

bool UMCWorldPreloadSubsystem::HasMeasuredPhaseProgress() const
{
    if (!bPresentationReady || HasFailed()) return false;
    if (Phase == EMCWorldPreloadPhase::LoadingAssets)
        return !SeenPaths.IsEmpty() && ActiveHandle.IsValid();
    if (Phase == EMCWorldPreloadPhase::WaitingForStreaming)
        return bStreamingTargetsBuilt && !StreamingTargets.IsEmpty();
    return false;
}

float UMCWorldPreloadSubsystem::GetMeasuredPhaseProgress() const
{
    if (!HasMeasuredPhaseProgress()) return 0.f;
    const float Progress = Phase == EMCWorldPreloadPhase::LoadingAssets ? GetAssetProgress()
        : float(ReadyStreamingTargets) / StreamingTargets.Num();
    // Completion of a discovered batch is not completion of dependency discovery,
    // render fences or the multiplayer barrier. These stages remain explicitly named.
    return FMath::IsFinite(Progress) ? FMath::Clamp(Progress,0.f,.99f) : 0.f;
}

FText UMCWorldPreloadSubsystem::GetStatusText() const
{
    if (IsPreparing() && !bPresentationReady)
        return LoadingText(TEXT("loading.phase.presentation"),TEXT("Подготавливаем сцену"),TEXT("Preparing the scene"));
    switch (Phase)
    {
    case EMCWorldPreloadPhase::Idle: return LoadingText(TEXT("loading.phase.connecting"), TEXT("Подключаемся"), TEXT("Connecting"));
    case EMCWorldPreloadPhase::LoadingAssets: return LoadingText(TEXT("loading.phase.assets"), TEXT("Загружаем ресурсы локации"), TEXT("Loading level resources"));
    case EMCWorldPreloadPhase::PreparingRendering: return LoadingText(TEXT("loading.phase.graphics"), TEXT("Подготавливаем графику"), TEXT("Preparing graphics"));
    case EMCWorldPreloadPhase::WaitingForStreaming: return LoadingText(TEXT("loading.phase.streaming"), TEXT("Подготавливаем текстуры и модели"), TEXT("Preparing textures and meshes"));
    case EMCWorldPreloadPhase::Ready: return LoadingText(TEXT("loading.phase.ready"), TEXT("Готово"), TEXT("Ready"));
    case EMCWorldPreloadPhase::Failed: return LoadingText(TEXT("loading.phase.failed"), TEXT("Не удалось подготовить локацию"), TEXT("Could not prepare the level"));
    }
    return FText::GetEmpty();
}

FText UMCWorldPreloadSubsystem::GetDetailText() const
{
    if (HasFailed()) return FailureReason;
    if (IsPreparing() && !bPresentationReady)
        return LoadingText(TEXT("loading.detail.presentation"),TEXT("Ожидаем готовности сцены локации."),TEXT("Waiting for the location presentation."));
    if (Phase == EMCWorldPreloadPhase::LoadingAssets)
        return FText::Format(LoadingText(TEXT("loading.detail.assets"), TEXT("Ресурсы: {0} / {1}"), TEXT("Resources: {0} / {1}")), FText::AsNumber(CompletedPaths), FText::AsNumber(SeenPaths.Num()));
    if (Phase == EMCWorldPreloadPhase::PreparingRendering)
        return FText::Format(LoadingText(TEXT("loading.detail.graphics"), TEXT("Подготовка графики: осталось задач — {0}"), TEXT("Preparing graphics: {0} tasks remaining")),
            FText::AsNumber(int64(GetPendingCompilationCount()) + GetPendingPipelineCount() + WarmupRequests.Num() - NextWarmupRequest));
    if (Phase == EMCWorldPreloadPhase::WaitingForStreaming)
        return FText::Format(LoadingText(TEXT("loading.detail.streaming"), TEXT("Ресурсы готовы: {0} / {1}"), TEXT("Resources ready: {0} / {1}")),
            FText::AsNumber(ReadyStreamingTargets), FText::AsNumber(StreamingTargets.Num()));
    return FText::GetEmpty();
}
