#include "MCWorldPreloadSubsystem.h"
#include "MCCoffeeRiverPresentation.h"
#include "MCNiagaraProceduralSurfaceResources.h"
#include "MCIceEvent.h"
#include "MCIceEventVFX.h"
#include "MCWorldPresentationComponent.h"
#include "MCNiagaraMaterialContextComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCFreezeDeathVFX.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"

#include "MCPreloadManifest.h"
#include "MCPreloadSettings.h"
#include "MCUIDataLibrary.h"
#include "MCEventSFXReadiness.h"
#include "MCPerkVFXProfile.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
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
#include "Materials/MaterialInstanceDynamic.h"
#include "PSOPrecache.h"
#include "PSOPrecacheMaterial.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RHIGlobals.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "RenderCommandFence.h"
#include "ShaderPipelineCache.h"
#include "Streaming/StreamableRenderResourceState.h"
#include "UObject/Package.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "IAssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMCWorldPreload, Log, All);

namespace
{
constexpr int32 MaxDependencyWaves = 32;
constexpr int32 MaxManifestPaths = 4096;
constexpr int32 MaxWarmupRequestsPerPump = 128;
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

bool UMCWorldPreloadSubsystem::IsIcePresentationLocation() const
{
    const FString Map=GetWorld()?UGameplayStatics::GetCurrentLevelName(GetWorld(),true):FString();
    return Map==TEXT("L_Mouth")||Map==TEXT("L_Tutorial");
}

bool UMCWorldPreloadSubsystem::IsPreparationRunCurrent() const
{
    if(!IsIcePresentationLocation())return true;
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    return State&&State->IcePresentationScope.Scope!=EMCIcePresentationScope::Unknown
        &&State->IcePresentationScope.GameplayLoadId==State->GameplayLoadId
        &&State->GameplayLoadId.IsValid()&&State->GameplayLoadId==PreparedGameplayLoadId;
}

bool UMCWorldPreloadSubsystem::IsReady() const
{return Phase==EMCWorldPreloadPhase::Ready&&IsPreparationRunCurrent();}

bool UMCWorldPreloadSubsystem::UsesPreparedIcePresentation() const
{
    if(!IsIcePresentationLocation())return false;
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    // Known non-Ice/manual modes retain their explicit legacy/lab preparation;
    // unknown replicated scope must never trigger a cold first-use allocation.
    return !State||State->IcePresentationScope.GameplayLoadId!=State->GameplayLoadId
        ||State->IcePresentationScope.Scope!=EMCIcePresentationScope::NotRequired;
}

bool UMCWorldPreloadSubsystem::ResolveIcePreparationScope(bool& bRequired,bool& bWaiting)
{
    bRequired=false;bWaiting=false;IcePreparationDiagnostic.Reset();
    if(!IsIcePresentationLocation())return true;
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!State||State->IcePresentationScope.Scope==EMCIcePresentationScope::Unknown||!State->GameplayLoadId.IsValid()
        ||State->IcePresentationScope.GameplayLoadId!=State->GameplayLoadId)
    {
        bWaiting=true;IcePreparationDiagnostic=TEXT("Ожидание параметров текущего забега от сервера.");return true;
    }
    if(PreparedGameplayLoadId.IsValid()&&PreparedGameplayLoadId!=State->GameplayLoadId)ReleaseIcePresentationOwner();
    PreparedGameplayLoadId=State->GameplayLoadId;
    bRequired=State->IcePresentationScope.Scope==EMCIcePresentationScope::Required;return true;
}

bool UMCWorldPreloadSubsystem::CreateIcePresentationOwner(FString& Error)
{
    if(IsValid(PreparedIcePresentation))return true;
    if(bIcePresentationConsumed||!PreparedGameplayLoadId.IsValid()||!GetWorld()||GetWorld()->bIsTearingDown)
    {Error=TEXT("Ice preparation has no current unconsumed location/run token.");return false;}
    auto* Owner=GetWorld()->SpawnActorDeferred<AMCIceEventVFX>(AMCIceEventVFX::StaticClass(),FTransform::Identity,
        nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    PreparedIcePresentation=Owner;
    if(IsValid(Owner))
    {Owner->SetFlags(RF_Transient);Owner->SetActorHiddenInGame(true);Owner->SetActorTickEnabled(false);Owner->FinishSpawning(FTransform::Identity);}
    if(!IsValid(Owner)||!Owner->PrepareForLocation(PreparedGameplayLoadId))
    {Error=TEXT("Ice presentation-only instance could not prepare its actual native resources.");return false;}
    TArray<UObject*> Resources;Owner->GetLocationPreparedResources(Resources);
    for(auto* Resource:Resources)if(IsValid(Resource))RetainedAssets.AddUnique(Resource);
    return true;
}

void UMCWorldPreloadSubsystem::ReleaseIcePresentationOwner()
{
    // Claimed instances have already been transferred and cleared from this
    // pointer. Retry/new run must not destroy their source or accepted tails.
    if(IsValid(PreparedIcePresentation))PreparedIcePresentation->Destroy();
    PreparedIcePresentation=nullptr;bIcePresentationConsumed=false;IcePreparationDiagnostic.Reset();
}

AMCIceEventVFX* UMCWorldPreloadSubsystem::ClaimPreparedIcePresentation(AMCIceEvent* Source,FGuid& OutToken,FString& Error)
{
    OutToken.Invalidate();Error.Reset();
    if(!IsValid(Source)||Source->GetWorld()!=GetWorld()||!IsReady()||bIcePresentationConsumed
        ||!IsValid(PreparedIcePresentation)||PreparedIcePresentation->GetWorld()!=GetWorld())
    {Error=TEXT("This location/run has no ready unconsumed Ice presentation instance.");return nullptr;}
    auto* Result=PreparedIcePresentation.Get();
    if(!Result->ClaimPreparedPresentation(Source,PreparedGameplayLoadId))
    {Error=TEXT("Ice presentation owner/world/token did not match its preparation.");return nullptr;}
    OutToken=PreparedGameplayLoadId;PreparedIcePresentation=nullptr;bIcePresentationConsumed=true;
    return Result;
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
    NativeWarmupMaterials.Reset();
    WorldPrimitives.Reset();
    WarmupRequests.Reset();
    Materials.Reset();
    StreamingTargets.Reset();
    ReadinessFence.Reset();
}

void UMCWorldPreloadSubsystem::ResetPreparation()
{
    if(GetWorld())if(auto* River=GetWorld()->GetSubsystem<UMCCoffeeRiverPresentationResources>())River->ResetUnclaimed();
    ReleaseFreezeHeroResources();ReleaseIcePresentationOwner();PreparedGameplayLoadId.Invalidate();
    ResetMaterialContextPreparation();
    PendingResidentVisuals.Reset();CertifiedResidentVisuals.Reset();
    if(EventAudioReadiness)EventAudioReadiness->Reset();
    EventAudioReadiness=nullptr;EventAudioSources.Reset();bEventAudioPlanValid=false;
    AuditedLegacyAudioSources.Reset();
    bEventAudioOutputPrepared=false;EventAudioDeviceId=MAX_uint32;EventAudioClosureHash=0;
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
    NativeStyles.Reset();
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
    ObservedActors.Reset();ObservedPrimitives.Reset();LastWarmupPumpAt=0;
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
    // A same-map RestartShift publishes a new existing GameplayLoadId. Retire
    // only an unclaimed old prewarm instance; transferred runtime owners/tails
    // belong to their source and are never destroyed by this preparation reset.
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(IsIcePresentationLocation()&&PreparedGameplayLoadId.IsValid()&&State&&State->GameplayLoadId.IsValid()
        &&PreparedGameplayLoadId!=State->GameplayLoadId)ResetPreparation();
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
    const FString ScopedReason=IcePreparationDiagnostic.IsEmpty()?TechnicalReason:TechnicalReason+TEXT(" | ")+IcePreparationDiagnostic;
    FailureReason = Reason;
    ReleaseIcePresentationOwner();
    if (ActiveHandle.IsValid() && !ActiveHandle->HasLoadCompleted()) ActiveHandle->CancelHandle();
    ActiveHandle.Reset();
    ReleaseWarmupComponents();
    SetPhase(EMCWorldPreloadPhase::Failed);
    UE_LOG(LogMCWorldPreload, Error, TEXT("MC_PRELOAD_FAILED world=%s reason=%s"),
        *GetNameSafe(GetWorld()), *ScopedReason);
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
        FMCPreloadManifest::CollectSoftReferences(Object, Dependencies,GetWorld());
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

void UMCWorldPreloadSubsystem::ReleaseFreezeHeroResources()
{
    for(const auto& H:PreparedFreezeHeroes)if(auto* Hero=H.Get()) {
        Hero->ReleaseUncommittedFreezeDeathPresentation();Hero->ReleasePreparedFreezeDeathSource();
    }
    PreparedFreezeHeroes.Reset();
}

bool UMCWorldPreloadSubsystem::PrepareFreezeHeroSources(bool Required,FString& Error)
{
    if(!Required){ReleaseFreezeHeroResources();return true;}
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(!IsIcePresentationLocation()||!GS||!GS->GameplayLoadId.IsValid()||GS->GameplayLoadId!=PreparedGameplayLoadId)
    {Error=TEXT("Freeze owner preparation requires this exact Required Ice location/load epoch.");return false;}
    int32 Count=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Hero=*It;if(!IsValid(Hero)||Hero->IsActorBeingDestroyed())continue;
        if(++Count>32){Error=TEXT("Actual Freeze preparation exceeds the finite32-hero location budget.");return false;}
        if(!Hero->PrepareFreezeDeathSource(Error,false))return false;
        PreparedFreezeHeroes.AddUnique(Hero);
    }
    return true;
}

bool UMCWorldPreloadSubsystem::PrepareFreezeHeroResources(bool Required,FString& Error)
{
    if(!Required)return true;
    for(const auto& H:PreparedFreezeHeroes)if(auto* Hero=H.Get()) {
        if(!Hero->PrepareFreezeDeathFromPreparedSource(PreparedGameplayLoadId,nullptr,nullptr,nullptr,Error))return false;
        auto* FX=Hero->GetPreparedFreezeDeathVisual();
        if(!FX||!FX->ValidatePreparation(Hero,PreparedGameplayLoadId,Error))return false;
        TArray<UObject*> Resources;FX->GetPreparedResources(Resources);
        for(auto* R:Resources)if(IsValid(R)) {
            RetainedAssets.AddUnique(R);
            if(auto* M=Cast<UMaterialInterface>(R))Materials.AddUnique(M);
        }
    }
    return true;
}

void UMCWorldPreloadSubsystem::BuildRenderingWork()
{
    const double BuildStarted=FPlatformTime::Seconds();
    double StageStarted=BuildStarted;
    auto LogStage=[&](const TCHAR* Stage) {
        const double Now=FPlatformTime::Seconds();
        UE_LOG(LogMCWorldPreload,Log,TEXT("MC_PRELOAD_STAGE world=%s stage=%s seconds=%.3f"),
            *GetNameSafe(GetWorld()),Stage,Now-StageStarted);
        StageStarted=Now;
    };
    bool bIceRequired=false,bIceWaiting=false;
    if(!ResolveIcePreparationScope(bIceRequired,bIceWaiting))return;
    if(bIceWaiting){bRenderingWorkBuilt=false;ResetRenderReadiness();return;}
    if(auto* River=GetWorld()->GetSubsystem<UMCCoffeeRiverPresentationResources>()) {
        const auto* State=GetWorld()->GetGameState<AMCGameState>();TArray<UObject*> Resources;
        FString RiverError;
        TArray<UObject*> Loaded;for(UObject* O:RetainedAssets)Loaded.Add(O);
        if(!River->PrepareLocation(Loaded,State?State->GameplayLoadId:FGuid(),false,RiverError)) {
            Fail(LoadingText(TEXT("Loading_RiverFailed"),TEXT("Не подготовлена поверхность реки"),TEXT("River surface preparation failed")),RiverError);return;
        }
        bool RiverWaiting=false;
        if(!River->PollRendering(RiverWaiting,RiverError)){Fail(LoadingText(TEXT("Loading_RiverRendererFailed"),TEXT("Не подготовлен рендерер реки"),TEXT("River renderer preparation failed")),RiverError);return;}
        if(RiverWaiting){bRenderingWorkBuilt=false;ResetRenderReadiness();return;}
        River->GetResources(Resources);for(auto* Object:Resources)RetainedAssets.AddUnique(Object);
    }
    bRenderingWorkBuilt = true;
    LogStage(TEXT("River"));
    FString NativeError;
    if(!PrepareNativeStylesForLocation(NativeError)) {
        Fail(LoadingText(TEXT("Loading_NativeArtFailed"),TEXT("Не удалось подготовить оформление эффектов"),TEXT("Unable to prepare effect appearance")),NativeError);return;
    }
    LogStage(TEXT("NativeStyles"));
    if(bIceRequired&&!bIcePresentationConsumed&&!CreateIcePresentationOwner(NativeError)) {
        Fail(LoadingText(TEXT("Loading_IcePresentationFailed"),TEXT("Не удалось подготовить ледяное событие"),TEXT("Unable to prepare Ice presentation")),NativeError);return;
    }
    LogStage(TEXT("IceOwner"));
    // The finite location libraries have completed their reflected load waves.
    // Retry existing producers now, never synchronously from their first hit/use.
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        TInlineComponentArray<UMCWorldPresentationComponent*> Sources(*It);
        for(auto* Source:Sources)if(IsValid(Source)) {
            FString Error;
            if(!Source->PrepareSources(Error,false)) {
                Fail(LoadingText(TEXT("Loading_PresentationSourceFailed"),TEXT("Не удалось подготовить эффекты локации"),
                    TEXT("Unable to prepare location effects")),Error);return;
            }
        }
    }
    LogStage(TEXT("WorldSources"));
    if(!PrepareFreezeHeroSources(bIceRequired,NativeError)) {
        Fail(LoadingText(TEXT("Loading_FreezeSourceFailed"),TEXT("Не подготовлен источник ледяной смерти"),TEXT("Freeze source preparation failed")),NativeError);return;
    }
    LogStage(TEXT("FreezeSources"));
    if(auto* River=GetWorld()->GetSubsystem<UMCCoffeeRiverPresentationResources>()) {
        TArray<TPair<UNiagaraSystem*,UMaterialInterface*>> Work;River->GetWarmups(Work);
        for(const auto& Pair:Work)EnqueueRenderingWarmup(Pair.Key,Pair.Value);
    }
    TSet<UMaterialInterface*> NiagaraRendererMaterials;
    if(!PrepareLocationMaterialContexts(NativeError)) {
        Fail(LoadingText(TEXT("Loading_MaterialContextFailed"),TEXT("Не удалось подготовить контекст материалов"),
            TEXT("Unable to prepare material contexts")),NativeError);return;
    }
    LogStage(TEXT("PrepareContexts"));
    bool ContextWaiting=false;
    if(!PollMaterialContextReadiness(ContextWaiting,NativeError)) {
        Fail(LoadingText(TEXT("Loading_FreezeContextFailed"),TEXT("Не подготовлен контекст ледяной смерти"),TEXT("Freeze material context preparation failed")),NativeError);return;
    }
    LogStage(TEXT("PollContexts"));
    if(ContextWaiting){bRenderingWorkBuilt=false;return;} // Keep submitted factories while the actual context seals.
    if(MaterialContexts&&!MaterialContexts->IsSealed()&&!MaterialContexts->SealResources(NativeError)) {
        Fail(LoadingText(TEXT("Loading_FreezeContextSealFailed"),TEXT("Не завершена подготовка контекста ледяной смерти"),TEXT("Freeze context sealing failed")),NativeError);return;
    }
    if(!PrepareFreezeHeroResources(bIceRequired,NativeError)) {
        Fail(LoadingText(TEXT("Loading_FreezeOwnerFailed"),TEXT("Не подготовлен настоящий зубик для ледяной смерти"),TEXT("Actual Freeze owner preparation failed")),NativeError);return;
    }
    LogStage(TEXT("PresentationContexts"));
    WorldPrimitives.Reset();Materials.Reset();
    // Enroll the actual owners created above, before collecting their resources.
    // Later gameplay changes still invalidate this snapshot. Our disposable
    // proxies are excluded by exact ownership, never by hidden/transient flags.
    CaptureWorldEnrollment(ObservedActors,ObservedPrimitives);
    ObservedWorldActors=ObservedActors.Num();ObservedWorldPrimitives=ObservedPrimitives.Num();
    ObservedWorldLevels=GetWorld()->GetNumLevels();
    TArray<UPrimitiveComponent*> Primitives;
    const FVector ViewLocation = Controller.IsValid() ? Controller->GetFocalLocation() : FVector::ZeroVector;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for (UPrimitiveComponent* Component : Components)
            if (IsValid(Component) && Component->IsRegistered() && !WarmupComponents.Contains(Component)) Primitives.Add(Component);
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
            EnqueueRenderingWarmup(Mesh);
            for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
                if (Slot.MaterialInterface) Materials.AddUnique(Slot.MaterialInterface);
        }
        else if (USkeletalMesh* SkeletalAsset = Cast<USkeletalMesh>(Object))
        {
            if (!RepresentativeSkeletal) RepresentativeSkeletal = SkeletalAsset;
            EnqueueRenderingWarmup(SkeletalAsset);
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
            EnqueueRenderingWarmup(System);
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
            EnqueueRenderingWarmup(RepresentativeStatic,Material);
            if (Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
            {
                EnqueueRenderingWarmup(RepresentativeStatic,Material,true);
            }
        }
        if (RepresentativeSkeletal && Material->GetUsageByFlag(MATUSAGE_SkeletalMesh))
        {
            EnqueueRenderingWarmup(RepresentativeSkeletal,Material);
        }
    }
    UE_LOG(LogMCWorldPreload, Log, TEXT("MC_PRELOAD_RENDER_WORK world=%s primitives=%d previewComponents=%d materials=%d"),
        *GetNameSafe(GetWorld()), WorldPrimitives.Num(), WarmupRequests.Num(), Materials.Num());
    LogStage(TEXT("ResourceInventory"));
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
    LogStage(TEXT("EditorMaterials"));
    UE_LOG(LogMCWorldPreload,Log,TEXT("MC_PRELOAD_PLAN world=%s seconds=%.3f submitted=%d total=%d"),
        *GetNameSafe(GetWorld()),FPlatformTime::Seconds()-BuildStarted,NextWarmupRequest,WarmupRequests.Num());
}

void UMCWorldPreloadSubsystem::EnqueueRenderingWarmup(UObject* Mesh,UMaterialInterface* Material,bool bInstanced)
{
    FWarmupRequest Request;Request.Mesh=Mesh;Request.Material=Material;Request.bInstanced=bInstanced;
    WarmupRequests.AddUnique(Request);
}

void UMCWorldPreloadSubsystem::ProcessRenderingWork()
{
    const double Started=FPlatformTime::Seconds();
    // Give a slow/background frame more work without monopolizing a responsive
    // frame. Always make progress; an individual engine call is not preemptible.
    const double Interval=LastWarmupPumpAt>0 ? Started-LastWarmupPumpAt : 1./60.;
    LastWarmupPumpAt=Started;
    const double Budget=FMath::Clamp(Interval*.2,.002,.020);
    const int32 First=NextWarmupRequest;
    const int32 StopAt = FMath::Min(First + MaxWarmupRequestsPerPump, WarmupRequests.Num());
    while (NextWarmupRequest < StopAt && (NextWarmupRequest==First || FPlatformTime::Seconds()-Started<Budget))
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
    const double Seconds=FPlatformTime::Seconds()-Started;
    if(Seconds>.050)UE_LOG(LogMCWorldPreload,Log,TEXT("MC_PRELOAD_SLOW_PUMP world=%s seconds=%.3f requests=%d remaining=%d"),
        *GetNameSafe(GetWorld()),Seconds,NextWarmupRequest-First,WarmupRequests.Num()-NextWarmupRequest);
}

void UMCWorldPreloadSubsystem::BuildStreamingTargets()
{
    bStreamingTargetsBuilt = true;
    StreamingTargets.Reset();ReadyStreamingTargets=0;
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
    if(MaterialContexts && FApp::CanEverRender()) {
        FString Error;
        if(!MaterialContexts->SealResources(Error)) {
            Fail(LoadingText(TEXT("Loading_MaterialContextFailed"),TEXT("Не удалось подготовить контекст материалов"),
                TEXT("Unable to prepare material contexts")),Error);return;
        }
    }
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

void UMCWorldPreloadSubsystem::CaptureWorldEnrollment(TSet<TWeakObjectPtr<AActor>>& Actors,
    TSet<TWeakObjectPtr<UPrimitiveComponent>>& Primitives) const
{
    Actors.Reset();Primitives.Reset();
    TSet<const UPrimitiveComponent*> Owned;
    for(const UPrimitiveComponent* Component:WarmupComponents)Owned.Add(Component);
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        Actors.Add(*It);
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for(const auto* Component:Components)
            if(IsValid(Component)&&Component->IsRegistered()&&!Owned.Contains(Component))Primitives.Add(const_cast<UPrimitiveComponent*>(Component));
    }
}

bool UMCWorldPreloadSubsystem::SettleWorldAndRefreshManifest()
{
    if (!IsWorldStreamingReady())
    {
        StableWorldFrames = 0;
        ResetRenderReadiness();
        return false;
    }
    TSet<TWeakObjectPtr<AActor>> Actors;
    TSet<TWeakObjectPtr<UPrimitiveComponent>> Primitives;
    CaptureWorldEnrollment(Actors,Primitives);
    const int32 Levels = GetWorld()->GetNumLevels();
    if (!Actors.Includes(ObservedActors) || !ObservedActors.Includes(Actors)
        || !Primitives.Includes(ObservedPrimitives) || !ObservedPrimitives.Includes(Primitives) || Levels != ObservedWorldLevels)
    {
        ObservedWorldActors = Actors.Num();ObservedActors=MoveTemp(Actors);
        ObservedWorldPrimitives = Primitives.Num();ObservedPrimitives=MoveTemp(Primitives);
        ObservedWorldLevels = Levels;
        StableWorldFrames = 0;
        ResetRenderReadiness();
        if (bRenderingWorkBuilt)
        {
            // Keep the existing factory submissions. The refreshed inventory
            // appends missing combinations and all readiness gates run again.
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
    bool ContextWaiting=false;FString ContextError;
    if(!PollMaterialContextReadiness(ContextWaiting,ContextError)) {
        Fail(LoadingText(TEXT("Loading_MaterialContextFailed"),TEXT("Не удалось подготовить контекст материалов"),
            TEXT("Unable to prepare material contexts")),ContextError);return;
    }
    if(ContextWaiting){ResetRenderReadiness();return;}
    if(!TickEventAudioReadiness())
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
    if (HasFailed()) return;
    if(!bRenderingWorkBuilt){ResetRenderReadiness();return;}
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
    if(!IcePreparationDiagnostic.IsEmpty())return FText::FromString(IcePreparationDiagnostic);
    if (IsPreparing() && !bPresentationReady)
        return LoadingText(TEXT("loading.detail.presentation"),TEXT("Ожидаем готовности сцены локации."),TEXT("Waiting for the location presentation."));
    if (Phase == EMCWorldPreloadPhase::LoadingAssets)
        return FText::Format(LoadingText(TEXT("loading.detail.assets"), TEXT("Ресурсы: {0} / {1}"), TEXT("Resources: {0} / {1}")), FText::AsNumber(CompletedPaths), FText::AsNumber(SeenPaths.Num()));
    if(IsPreparing() && EventAudioReadiness && EventAudioReadiness->GetState()==EMCEventSFXReadiness::Preparing)
        return FText::Format(LoadingText(TEXT("loading.detail.audio"),TEXT("Подготавливаем звуковые ресурсы: осталось {0}"),TEXT("Preparing audio resources: {0} remaining")),
            FText::AsNumber(EventAudioReadiness->GetPendingWaveCount()));
    if (Phase == EMCWorldPreloadPhase::PreparingRendering)
        return FText::Format(LoadingText(TEXT("loading.detail.graphics"), TEXT("Подготовка графики: осталось задач — {0}"), TEXT("Preparing graphics: {0} tasks remaining")),
            FText::AsNumber(int64(GetPendingCompilationCount()) + GetPendingPipelineCount() + WarmupRequests.Num() - NextWarmupRequest));
    if (Phase == EMCWorldPreloadPhase::WaitingForStreaming)
        return FText::Format(LoadingText(TEXT("loading.detail.streaming"), TEXT("Ресурсы готовы: {0} / {1}"), TEXT("Resources ready: {0} / {1}")),
            FText::AsNumber(ReadyStreamingTargets), FText::AsNumber(StreamingTargets.Num()));
    return FText::GetEmpty();
}

bool UMCWorldPreloadSubsystem::GetPreparedNativeStyle(const UMCVFXNativePresentationSettings* Source,
    FMCVFXPreparedNativePresentation& Out) const
{
    const auto* Found=NativeStyles.Find(const_cast<UMCVFXNativePresentationSettings*>(Source));
    if(!Source || !Found || Found->Reference.SettingsHash!=Source->ComputeSettingsHash())return false;
    Out=*Found;return true;
}

bool UMCWorldPreloadSubsystem::IsEventAudioOutputAvailable() const
{
    const UWorld* World=GetWorld();
    return World && World->GetNetMode()!=NM_DedicatedServer && World->AllowAudioPlayback()
        && GEngine && GEngine->UseSound() && bool(World->GetAudioDevice());
}

const UMCEventSFXReadiness* UMCWorldPreloadSubsystem::GetEventAudioReadiness() const
{
    const UWorld* World=GetWorld();
    const uint32 DeviceId=World && World->GetAudioDevice()?World->GetAudioDevice().GetDeviceID():MAX_uint32;
    // Enabling/recovering a different output requires explicit preparation;
    // a former silent snapshot must not acquire a certificate from this getter.
    return bEventAudioPlanValid && IsEventAudioOutputAvailable()==bEventAudioOutputPrepared
        && DeviceId==EventAudioDeviceId?EventAudioReadiness.Get():nullptr;
}

bool UMCWorldPreloadSubsystem::PrepareEventAudioSources(const TArray<USoundBase*>& Sources,FString& Error)
{
    Error.Reset();
    if(!IsInGameThread() || !GetWorld() || GetWorld()->bIsTearingDown)
    {Error=TEXT("Audio preparation requires a live owned world on the game thread.");return false;}
    TArray<USoundBase*> Unique;
    for(USoundBase* AudioSource:Sources) {
        if(!IsValid(AudioSource)){bEventAudioPlanValid=false;Error=TEXT("A required scoped audio source is not resident.");return false;}
        Unique.AddUnique(AudioSource);
    }
    if(Unique.Num()>128)
    {bEventAudioPlanValid=false;Error=TEXT("Scoped location audio exceeds 128 distinct source assets.");return false;}
    Unique.Sort([](const USoundBase& A,const USoundBase& B){return A.GetUniqueID()<B.GetUniqueID();});
    const bool bOutputAvailable=IsEventAudioOutputAvailable();
    const uint32 DeviceId=GetWorld()->GetAudioDevice()?GetWorld()->GetAudioDevice().GetDeviceID():MAX_uint32;
    TArray<FMCEventSFXSamplePlan> Plans;uint32 ClosureHash=0;
    for(USoundBase* AudioSource:Unique) {
        FMCEventSFXSamplePlan Plan;
        if(bOutputAvailable) {
            bool Waiting=false;
            if(!FMCPreloadManifest::PrepareEventAudioPlan(AudioSource,Plan,Error,Waiting)) {
                bEventAudioPlanValid=false;
                if(!Waiting)return false;
                EventAudioSources.Reset();for(USoundBase* PendingSource:Unique)EventAudioSources.Add(PendingSource);
                return true; // Real cache preparation is pending; getter stays null and callers poll before Commit.
            }
        } else Plan.Source=AudioSource;
        ClosureHash=HashCombine(ClosureHash,GetTypeHash(AudioSource->GetUniqueID()));
        for(USoundWave* AudioWave:Plan.SampleWaves)ClosureHash=HashCombine(ClosureHash,GetTypeHash(AudioWave->GetUniqueID()));
        Plans.Add(MoveTemp(Plan));
    }
    bool bSameSources=Unique.Num()==EventAudioSources.Num();
    if(bSameSources)for(int32 I=0;I<Unique.Num();++I)if(Unique[I]!=EventAudioSources[I]){bSameSources=false;break;}
    if(bSameSources && bEventAudioPlanValid && EventAudioReadiness && ClosureHash==EventAudioClosureHash
        && bOutputAvailable==bEventAudioOutputPrepared && DeviceId==EventAudioDeviceId
        && EventAudioReadiness->GetState()!=EMCEventSFXReadiness::Failed
        && (EventAudioReadiness->GetState()!=EMCEventSFXReadiness::Ready || EventAudioReadiness->IsReady()))return true;
    if(!EventAudioReadiness)EventAudioReadiness=NewObject<UMCEventSFXReadiness>(this,NAME_None,RF_Transient);
    bEventAudioPlanValid=false;
    if(!EventAudioReadiness->Begin(GetWorld(),Plans,Error))return false;
    EventAudioSources.Reset();for(USoundBase* AudioSource:Unique)EventAudioSources.Add(AudioSource);
    EventAudioClosureHash=ClosureHash;EventAudioDeviceId=DeviceId;
    bEventAudioOutputPrepared=bOutputAvailable;bEventAudioPlanValid=true;
    UE_LOG(LogMCWorldPreload,Log,TEXT("MC_PRELOAD_AUDIO world=%s sources=%d output=%d device=%u state=%d detail=%s"),
        *GetNameSafe(GetWorld()),EventAudioSources.Num(),bOutputAvailable?1:0,DeviceId,
        int32(EventAudioReadiness->GetState()),*EventAudioReadiness->GetDiagnostic());
    return true;
}

void UMCWorldPreloadSubsystem::PollEventAudioReadiness()
{if(bEventAudioPlanValid && EventAudioReadiness)EventAudioReadiness->Poll();}

bool UMCWorldPreloadSubsystem::ReprepareEventAudioReadiness(FString& Error)
{
    TArray<USoundBase*> Sources;for(USoundBase* AudioSource:EventAudioSources)Sources.Add(AudioSource);
    bEventAudioPlanValid=false;
    return PrepareEventAudioSources(Sources,Error);
}

bool UMCWorldPreloadSubsystem::TickEventAudioReadiness()
{
    TArray<USoundBase*> Sources;
    // Only newly authored Composer slots are required audio plans. Shared
    // native/music palettes retain their original runtime behavior and must not
    // make an unchanged location fail because a dynamic graph is not migrated.
    for(UObject* Object:RetainedAssets)if(const auto* Profile=Cast<UMCPerkVFXProfile>(Object))
        for(const auto& Event:Profile->SFXSlots)for(const auto& Layer:Event.Value.Layers) {
            if(Layer.Sound.IsNull())continue;
            auto* AudioSource=Layer.Sound.Get();
            if(!IsValid(AudioSource)) {
                Fail(LoadingText(TEXT("loading.error.audio"),TEXT("Не найден звуковой ресурс Composer для локации."),
                    TEXT("A location Composer sound resource is not resident.")),Layer.Sound.ToString());
                return false;
            }
            Sources.AddUnique(AudioSource);
        }
    for(UObject* Object:RetainedAssets)if(auto* LegacySource=Cast<USoundBase>(Object);LegacySource && !Sources.Contains(LegacySource)
        && !AuditedLegacyAudioSources.Contains(TWeakObjectPtr<USoundBase>(LegacySource))) {
        AuditedLegacyAudioSources.Add(TWeakObjectPtr<USoundBase>(LegacySource));FMCEventSFXSamplePlan Audit;FString AuditError;
        if(!FMCPreloadManifest::BuildEventAudioPlan(LegacySource,Audit,AuditError))
            UE_LOG(LogMCWorldPreload,Log,TEXT("MC_PRELOAD_LEGACY_AUDIO_UNMAPPED world=%s source=%s required=0 originalBehaviorPreserved=1 reason=%s"),
                *GetNameSafe(GetWorld()),*LegacySource->GetPathName(),*AuditError);
    }
    FString Error;
    if(!PrepareEventAudioSources(Sources,Error)) {
        Fail(LoadingText(TEXT("loading.error.audio"),TEXT("Не удалось подготовить звуковые ресурсы локации. Проверь источник звука в Composer."),
            TEXT("Could not prepare location audio resources. Check the sound source in Composer.")),Error);
        return false;
    }
    PollEventAudioReadiness();
    if(!bEventAudioPlanValid || !EventAudioReadiness)return false;
    if(EventAudioReadiness->GetState()==EMCEventSFXReadiness::Failed) {
        Fail(LoadingText(TEXT("loading.error.audio"),TEXT("Не удалось подготовить звуковые ресурсы локации. Проверь источник звука в Composer."),
            TEXT("Could not prepare location audio resources. Check the sound source in Composer.")),EventAudioReadiness->GetDiagnostic());
        return false;
    }
    return EventAudioReadiness->IsReady()
        || EventAudioReadiness->GetState()==EMCEventSFXReadiness::OutputUnavailable;
}

bool UMCWorldPreloadSubsystem::IsVisualResourceRetained(const UObject* Object) const
{return IsValid(Object) && (IsReady()||CertifiedResidentVisuals.ContainsByPredicate([Object](const auto& Asset){return Asset.Get()==Object;}))
    && RetainedAssets.ContainsByPredicate([Object](const auto& Asset){return Asset.Get()==Object;});}

bool UMCWorldPreloadSubsystem::PrepareResidentVisualResources(const TArray<UObject*>& Resources,FString& Error)
{
    Error.Reset();
    if(Phase!=EMCWorldPreloadPhase::Idle || Resources.Num()>64)
    {Error=TEXT("Resident fixture preparation is allowed only before a location/preview commits (maximum 64 roots).");return false;}
    TArray<UObject*> Candidate;TArray<UStaticMesh*> Meshes;
    for(UObject* Object:Resources) {
        if(!IsValid(Object)||(!Object->IsA<UStaticMesh>()&&!Object->IsA<UMaterialInterface>()))
        {Error=TEXT("Resident fixture visuals must be actual loaded static meshes/materials.");return false;}
        Candidate.AddUnique(Object);
        if(auto* Mesh=Cast<UStaticMesh>(Object)) {
#if WITH_EDITOR
            if(Mesh->IsCompiling()){Error=TEXT("Waiting for captured tool geometry compilation.");return false;}
#endif
            Meshes.AddUnique(Mesh);
            for(const auto& Slot:Mesh->GetStaticMaterials())if(Slot.MaterialInterface)Candidate.AddUnique(Slot.MaterialInterface);
        }
    }
#if WITH_EDITOR
    for(UObject* Object:Candidate)if(auto* Material=Cast<UMaterialInterface>(Object);Material&&Material->IsCompiling())
    {Error=TEXT("Waiting for captured visual material compilation.");return false;}
#endif
    for(UObject* Object:Candidate) {
        if(CertifiedResidentVisuals.Contains(Object)||PendingResidentVisuals.Contains(Object))continue;
        RetainedAssets.AddUnique(Object);PendingResidentVisuals.Add(Object);
        if(auto* Mesh=Cast<UStaticMesh>(Object)) {FWarmupRequest Request;Request.Mesh=Mesh;WarmupRequests.Add(Request);}
        else if(auto* Material=Cast<UMaterialInterface>(Object))for(auto* WarmMesh:Meshes) {
            FWarmupRequest Request;Request.Mesh=WarmMesh;Request.Material=Material;WarmupRequests.Add(Request);
        }
    }
    if(FApp::CanEverRender()) {
        ProcessRenderingWork();
        if(NextWarmupRequest<WarmupRequests.Num()) {Error=TEXT("Preparing captured visual render factories.");return false;}
        for(const auto& Component:WarmupComponents)if(IsValid(Component)&&Component->IsPSOPrecaching())
        {Error=TEXT("Waiting for captured visual PSO preparation.");return false;}
    }
    for(UObject* Object:PendingResidentVisuals)CertifiedResidentVisuals.AddUnique(Object);
    PendingResidentVisuals.Reset();return true;
}

bool UMCWorldPreloadSubsystem::PrepareNativeStylesForLocation(FString& Error)
{
    Error.Reset();const auto Loaded=RetainedAssets; // No mutation of iterated array.
    for(UObject* Object:Loaded)if(auto* Source=Cast<UMCVFXNativePresentationSettings>(Object)) {
        // Rendering work can be rebuilt while the retained location closure
        // stays alive. Prepared copies are owned by NativeStyles, never new
        // authoring sources to recursively prepare on the next pass.
        if(Source->HasAnyFlags(RF_Transient) || Source->GetOutermost()==GetTransientPackage())continue;
        TArray<FName> Events;Source->Events.GetKeys(Events);
        FMCVFXPreparedNativePresentation Candidate;
        if(!GetPreparedNativeStyle(Source,Candidate)) {
            if(!FMCVFXPreparedNativePresentation::Prepare(Source->SourceID,Events,Source,this,false,Candidate,Error))return false;
            NativeStyles.Add(Source,Candidate);
        }
        for(UObject* Resource:Candidate.Resources)RetainedAssets.AddUnique(Resource);
        // Material variants enter the same actual procedural factory as the
        // native effect. Shader/PSO/streaming readiness remains the existing
        // BuildRenderingWork/ProcessRenderingWork/fence quiet protocol.
        for(const auto& Pair:Candidate.MaterialTemplates) {
            if(NativeWarmupMaterials.Contains(TWeakObjectPtr<UMaterialInterface>(Pair.Value.Get())))continue;
            auto* C=NewObject<UProceduralMeshComponent>(GetWorld()->GetWorldSettings(),NAME_None,RF_Transient);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);
            C->SetCastShadow(false);C->SetMaterial(0,Pair.Value);
            const TArray<FVector> V={FVector(-1,-1,0),FVector(1,-1,0),FVector(1,1,0),FVector(-1,1,0)};
            const TArray<int32> I={0,1,2,0,2,3};
            const TArray<FVector> N={FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector};
            const TArray<FVector2D> UV={FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)};
            const TArray<FLinearColor> Colors={FLinearColor::White,FLinearColor::White,FLinearColor::White,FLinearColor::White};
            const TArray<FProcMeshTangent> T={FProcMeshTangent(1,0,0),FProcMeshTangent(1,0,0),FProcMeshTangent(1,0,0),FProcMeshTangent(1,0,0)};
            C->CreateMeshSection_LinearColor(0,V,I,N,UV,Colors,T,false);C->SetVisibility(false);
            C->RegisterComponentWithWorld(GetWorld());C->PrecachePSOs();
            WarmupComponents.Add(C);
            NativeWarmupMaterials.Add(TWeakObjectPtr<UMaterialInterface>(Pair.Value.Get()));
        }
    }
    return true;
}

UMCNiagaraMaterialContextResources* UMCWorldPreloadSubsystem::GetMaterialContextResources() const
{ return MaterialContexts; }

void UMCWorldPreloadSubsystem::ResetMaterialContextPreparation()
{
    if(ProceduralSurfaceResources)ProceduralSurfaceResources->Reset();ProceduralSurfaceResources=nullptr;
    if(MaterialContexts)MaterialContexts->Reset();
    if(!MaterialContextPSORequests.IsEmpty())ReleasePSOPrecacheData(MaterialContextPSORequests);
    MaterialContextPSORequests.Reset();MaterialContextPSOEvents.Reset();MaterialContextFence.Reset();
    for(const auto& Object:MaterialContextAddedAssets)if(Object.IsValid()) {
        RetainedAssets.Remove(Object.Get());
        if(auto* M=Cast<UMaterialInterface>(Object.Get())) {
            Materials.Remove(M);
#if WITH_EDITOR
            EditorFirstUseMaterials.Remove(M);
#endif
        }
    }
    MaterialContextAddedAssets.Reset();MaterialContextTargets.Reset();MaterialContextQueuedTargets.Reset();
    MaterialContextSources.Reset();MaterialContexts=nullptr;MaterialContextEpoch.Invalidate();
    MaterialContextCapacity=0;MaterialContextStableFrames=0;MaterialContextLastFrame=MAX_uint64;
    MaterialContextQuietSince=0;
}

bool UMCWorldPreloadSubsystem::PrepareMaterialContextSources(const TArray<UNiagaraSystem*>& Sources,
    int32 Capacity,FGuid Epoch,FString& Error)
{
    if(!PrepareMaterialContextSourcesInternal(Sources,Capacity,Epoch,Error))return false;
    const FString Map=GetWorld()->GetOutermost()->GetName();
    const bool Preview=GetWorld()->GetOutermost()==GetTransientPackage()&&GetWorld()->WorldType==EWorldType::Game;
    if(!Preview&&!Map.Contains(TEXT("L_Mouth"))&&!Map.Contains(TEXT("L_Tutorial")))return true;
    TArray<UObject*> SurfaceSources;for(auto* NS:Sources)if(UMCNiagaraProceduralSurfaceResources::Uses(NS))SurfaceSources.AddUnique(NS);
    if(SurfaceSources.IsEmpty())return true;
    int32 Triangles=0;for(TActorIterator<AMCTongue> It(GetWorld());It;++It)Triangles=FMath::Max(Triangles,It->TriangleIndices().Num()/3);
    if(!ProceduralSurfaceResources)ProceduralSurfaceResources=NewObject<UMCNiagaraProceduralSurfaceResources>(this,NAME_None,RF_Transient);
    return ProceduralSurfaceResources->Prepare(GetWorld(),Epoch,SurfaceSources,Triangles,Error);
}

bool UMCWorldPreloadSubsystem::PrepareMaterialContextSourcesInternal(const TArray<UNiagaraSystem*>& Sources,
    int32 Capacity,FGuid Epoch,FString& Error)
{
    Error.Reset();
    if(!IsInGameThread()||!GetWorld()||Capacity<1||Capacity>128||Sources.Num()>1024)
    {Error=TEXT("Material-context preparation needs a finite resident scope, epoch and capacity1..128.");return false;}
    TArray<UNiagaraSystem*> Candidate;
    for(auto* NS:Sources) {
        if(!IsValid(NS)){Error=TEXT("Material-context source must already be resident.");return false;}
        if(UMCNiagaraMaterialContextResources::NeedsMaterialContext(NS))Candidate.AddUnique(NS);
    }
    if(Candidate.Num()>128){Error=TEXT("Material-context source budget of128 exceeded.");return false;}
    if(!Candidate.IsEmpty()&&!Epoch.IsValid()){Error=TEXT("Material contexts require a real preparation epoch.");return false;}
    const bool Same=MaterialContexts && MaterialContextEpoch==Epoch && MaterialContextCapacity==Capacity
        &&Candidate.Num()==MaterialContextSources.Num();
    bool Reuse=Same;
    if(Reuse)for(auto* NS:Candidate)Reuse&=MaterialContextSources.Contains(NS);
    if(Reuse) {
        for(auto* NS:Candidate)if(!MaterialContexts->PrepareSystem(NS,Capacity,Error)){Reuse=false;break;}
        if(Reuse)return true;
    }
    if(MaterialContexts && MaterialContexts->HasActiveActivations())
    {Error=TEXT("Stop the current material-context presentation before explicit reprepare.");return false;}
    if(Phase==EMCWorldPreloadPhase::Ready)
    {Error=TEXT("Changed material-context resources require a new location/precommit boundary.");return false;}
    if(Candidate.IsEmpty()){if(MaterialContexts||!MaterialContextSources.IsEmpty())ResetMaterialContextPreparation();return true;}
    auto* Prepared=NewObject<UMCNiagaraMaterialContextResources>(this,NAME_None,RF_Transient);
    if(!Prepared->Initialize(GetWorld(),Epoch,Error))return false;
    for(auto* NS:Candidate)if(!Prepared->PrepareSystem(NS,Capacity,Error)){Prepared->Reset();return false;}
    ResetMaterialContextPreparation();MaterialContexts=Prepared;MaterialContextEpoch=Epoch;MaterialContextCapacity=Capacity;
    for(auto* NS:Candidate)MaterialContextSources.Add(NS);
    TArray<UObject*> Resources;Prepared->GetPreparedResources(Resources);
    for(auto* R:Resources) {
        if(!RetainedAssets.Contains(R)){RetainedAssets.Add(R);MaterialContextAddedAssets.Add(R);}
        if(auto* M=Cast<UMaterialInterface>(R))Materials.AddUnique(M);
    }
    Error.Reset();return true;
}

bool UMCWorldPreloadSubsystem::RegisterMaterialContextTarget(UMeshComponent* Target,FString& Error)
{
    Error.Reset();
    if(!MaterialContexts||!IsValid(Target)||Target->GetWorld()!=GetWorld()||!Target->IsRegistered())
    {Error=TEXT("Register an actual same-world mesh after resident material-context sources are prepared.");return false;}
    if(!UMCNiagaraMaterialContextResources::ValidateTargetFactory(Target,Error))return false;
    if(MaterialContexts->IsSealed()) {
        if(MaterialContexts->IsTargetPrepared(Target))return true;
        Error=TEXT("Actual mesh factory changed after sealing; explicitly reprepare before playback.");return false;
    }
    const bool KnownFactory=MaterialContexts->IsTargetPrepared(Target);
    if(!KnownFactory&&MaterialContextTargets.Num()>=128)
    {Error=TEXT("Material-context target budget of128 exceeded.");return false;}
    if(!KnownFactory) {
        MaterialContextTargets.AddUnique(Target);MaterialContextQueuedTargets.Remove(Target);
        MaterialContextFence.Reset();MaterialContextStableFrames=0;MaterialContextQuietSince=0;
    }
    // Factory certification happens only after its actual PSO entries are submitted.
    // Retain/prepare real target resources now, without installing an overlay.
    TArray<UObject*> Resources;
    if(auto* S=Cast<UStaticMeshComponent>(Target))Resources.Add(S->GetStaticMesh());
    else if(auto* SkeletalTarget=Cast<USkeletalMeshComponent>(Target))Resources.Add(SkeletalTarget->GetSkeletalMeshAsset());
    TArray<UMaterialInterface*> Used;Target->GetUsedMaterials(Used);
    for(auto* M:Used)if(IsValid(M)){Resources.AddUnique(M);Materials.AddUnique(M);}
    for(auto* R:Resources)if(!RetainedAssets.Contains(R)){RetainedAssets.Add(R);MaterialContextAddedAssets.Add(R);}
    return true;
}

bool UMCWorldPreloadSubsystem::RegisterMaterialContextScalar(UMeshComponent* Subject,UMaterialInstanceDynamic* MID,
    FName Parameter,FString& Error)
{
    if(!RegisterMaterialContextTarget(Subject,Error))return false;
    if(!MaterialContexts->RegisterExistingScalarTarget(Subject,MID,Parameter,Error))return false;
    if(!RetainedAssets.Contains(MID)){RetainedAssets.Add(MID);MaterialContextAddedAssets.Add(MID);}
    return true;
}

bool UMCWorldPreloadSubsystem::AdoptResidentMaterialContextScalar(UMeshComponent* Subject,
    UMaterialInstanceDynamic* MID,FName Parameter,FString& Error)
{
    if(!IsReady()||!MaterialContexts||!IsValid(Subject)||Subject->GetWorld()!=GetWorld())
    {Error=TEXT("Late native scalar enrollment requires this location's existing Ready scope.");return false;}
    return MaterialContexts->AdoptPreparedScalarTarget(Subject,MID,Parameter,Error);
}

bool UMCWorldPreloadSubsystem::PrepareLocationMaterialContexts(FString& Error)
{
    TArray<UNiagaraSystem*> Sources;
    for(UObject* Object:RetainedAssets)if(auto* NS=Cast<UNiagaraSystem>(Object))Sources.AddUnique(NS);
    TArray<AActor*> Owners;
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        TInlineComponentArray<UMCVFXPresentationComponent*> Components(*It);
        bool HasContext=false;
        for(auto* C:Components)if(IsValid(C)) {
            TArray<UNiagaraSystem*> Local;C->GetPreparedResources(Local);
            for(auto* NS:Local)if(IsValid(NS)) {
                Sources.AddUnique(NS);HasContext|=UMCNiagaraMaterialContextResources::NeedsMaterialContext(NS);
            }
        }
        if(HasContext)Owners.Add(*It);
    }
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    bool Changed=false;
    if(MaterialContexts&&MaterialContexts->IsSealed()) {
        TArray<UNiagaraSystem*> Needed;
        for(auto* NS:Sources)if(UMCNiagaraMaterialContextResources::NeedsMaterialContext(NS))Needed.AddUnique(NS);
        Changed=Needed.Num()!=MaterialContextSources.Num();
        for(auto* NS:Needed)Changed|=!MaterialContextSources.Contains(NS);
        // While sealed PrepareSystem is a public reuse/schema check: all
        // mutation branches reject before adding a new template/MID pool.
        for(auto* NS:Needed){FString Probe;Changed|=!MaterialContexts->PrepareSystem(NS,64,Probe);}
        for(auto* Owner:Owners) {
            TInlineComponentArray<UMeshComponent*> Meshes(Owner);
            for(auto* Mesh:Meshes)if(IsValid(Mesh)&&Mesh->IsRegistered()) {
                FString Unsupported;if(!UMCNiagaraMaterialContextResources::ValidateTargetFactory(Mesh,Unsupported))continue;
                Changed|=!MaterialContextTargets.Contains(Mesh)||!MaterialContexts->IsTargetPrepared(Mesh);
            }
            if(auto* Tooth=Cast<AMCToothCharacter>(Owner)) {
                FString Probe;Changed|=!Tooth->PreparePresentationMaterialContext(this,Probe); // Sealed: exact existing binding only.
            }
        }
    }
    if(Changed) {
        if(!IsPreparing()){Error=TEXT("Changed actual context target requires an explicit preparation boundary.");return false;}
        // Do not reset the roster/source snapshots here. Only these exact
        // uncommitted consumers release their own inactive reservations.
        for(const auto& H:PreparedFreezeHeroes)if(auto* Hero=H.Get())Hero->ReleaseUncommittedFreezeDeathPresentation();
        if(MaterialContexts->HasActiveActivations()) {Error=TEXT("A foreign active context consumer prevents safe preparation rebuild.");return false;}
        ResetMaterialContextPreparation();
    }
    if(!PrepareMaterialContextSources(Sources,64,GS?GS->GameplayLoadId:FGuid(),Error))return false;
    if(!MaterialContexts || MaterialContexts->IsSealed())return true;
    // Only actual owners of a prepared context source, not every decorative mesh
    // or the global Composer catalog. Providers may register additional exact targets.
    for(auto* Owner:Owners) {
        TInlineComponentArray<UMeshComponent*> Meshes(Owner);
        for(auto* Mesh:Meshes)if(IsValid(Mesh)&&Mesh->IsRegistered()) {
            FString Unsupported;
            if(!UMCNiagaraMaterialContextResources::ValidateTargetFactory(Mesh,Unsupported))continue;
            if(!RegisterMaterialContextTarget(Mesh,Error))return false;
        }
        if(auto* Tooth=Cast<AMCToothCharacter>(Owner))
            if(!Tooth->PreparePresentationMaterialContext(this,Error))return false;
    }
    return true;
}

bool UMCWorldPreloadSubsystem::PollMaterialContextReadiness(bool& bWaiting,FString& Error)
{
    double StepStarted=FPlatformTime::Seconds();
    auto LogStep=[&](const TCHAR* Step,const UObject* Resource=nullptr) {
        const double Now=FPlatformTime::Seconds();
        if(Now-StepStarted>.010)UE_LOG(LogMCWorldPreload,Log,TEXT("MC_PRELOAD_CONTEXT_STEP world=%s step=%s resource=%s seconds=%.3f"),
            *GetNameSafe(GetWorld()),Step,*GetPathNameSafe(Resource),Now-StepStarted);
        StepStarted=Now;
    };
    bWaiting=false;Error.Reset();
    if(!IsInGameThread()||!GetWorld()){Error=TEXT("Poll material-context readiness on its live world game thread.");return false;}
    if(ProceduralSurfaceResources) {
        bool Waiting=false;if(!ProceduralSurfaceResources->Poll(Waiting,Error))return false;
        if(Waiting){bWaiting=true;return true;}
        TArray<UObject*> Prepared;ProceduralSurfaceResources->GetResources(Prepared);
        for(auto* O:Prepared){RetainedAssets.AddUnique(O);if(auto* M=Cast<UMaterialInterface>(O))Materials.AddUnique(M);}
    }
    LogStep(TEXT("SurfacePoll"));
    if(!MaterialContexts)return true;
    if(MaterialContexts->IsSealed()) {
        if(MaterialContexts->IsPreparedForEpoch(MaterialContextEpoch))return true;
        Error=TEXT("Material-context run epoch changed; explicitly reprepare.");return false;
    }
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS&&GS->GameplayLoadId.IsValid()&&GS->GameplayLoadId!=MaterialContextEpoch)
    {Error=TEXT("Material-context preparation belongs to a retired run epoch.");return false;}
    // Asset-only execution is honest: no render certificate is manufactured.
    if(!FApp::CanEverRender())return true;
    if(MaterialContextTargets.IsEmpty()) {Error=TEXT("Material context has no explicit supported mesh target/factory.");return false;}
    for(const auto& NS:MaterialContextSources) {
        if(!IsValid(NS.Get())){Error=TEXT("Prepared material-context NS was invalidated.");return false;}
#if WITH_EDITOR
        NS->PollForCompilationComplete(true);
        LogStep(TEXT("NiagaraPoll"),NS);
        if(NS->HasOutstandingCompilationRequests(true))bWaiting=true;
#endif
        if(!NS->IsReadyToRun())bWaiting=true;
    }
    TArray<UObject*> Resources;MaterialContexts->GetPreparedResources(Resources);
    for(const auto& Weak:MaterialContextTargets)if(auto* Target=Weak.Get()) {
        TArray<UMaterialInterface*> Used;Target->GetUsedMaterials(Used);
        for(auto* M:Used)if(IsValid(M))Resources.AddUnique(M);
    }
#if WITH_EDITOR
    const auto Platform=GetFeatureLevelShaderPlatform(GetWorld()->GetFeatureLevel());
    TSet<FMaterialResource*> Submitted;
    for(auto* R:Resources)if(auto* M=Cast<UMaterialInterface>(R)) {
        auto* Shader=M->GetMaterialResource(Platform);
        if(!Shader){Error=TEXT("Material-context interface has no resource for this shader platform.");return false;}
        if(!Shader->IsGameThreadShaderMapComplete()) {
            if(!Submitted.Contains(Shader)){Submitted.Add(Shader);Shader->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::Normal);}
            bWaiting=true;
        }
    }
    LogStep(TEXT("ShaderSubmission"));
#endif
    if(bWaiting){MaterialContextFence.Reset();MaterialContextStableFrames=0;MaterialContextQuietSince=0;return true;}
    TArray<UMaterialInterface*> Overlays;MaterialContexts->GetPreparedOverlayMaterials(Overlays);
    for(const auto& Weak:MaterialContextTargets) {
        auto* Target=Weak.Get();
        if(!IsValid(Target)){Error=TEXT("Prepared material-context mesh target disappeared before Commit.");return false;}
        if(MaterialContextQueuedTargets.Contains(Target)&&MaterialContexts->IsTargetPrepared(Target))continue;
        if(auto* S=Cast<UStaticMeshComponent>(Target)) {
#if WITH_EDITOR
            if(S->GetStaticMesh()->IsCompiling()){bWaiting=true;continue;}
#endif
            if(!S->GetStaticMesh()->GetRenderData()){bWaiting=true;continue;}
        } else if(auto* SkeletalTarget=Cast<USkeletalMeshComponent>(Target)) {
#if WITH_EDITOR
            if(SkeletalTarget->GetSkeletalMeshAsset()->IsCompiling()){bWaiting=true;continue;}
#endif
            auto* Render=SkeletalTarget->GetSkeletalMeshAsset()->GetResourceForRendering();
            if(!Render||!Render->IsLODDataReady()){bWaiting=true;continue;}
        }
        FPSOPrecacheParams Base;Target->SetupPrecachePSOParams(Base);
        FMaterialInterfacePSOPrecacheParamsList Original;Target->CollectPSOPrecacheData(Base,Original);
        LogStep(TEXT("TargetFactory"),Target);
        if(Original.IsEmpty()){Error=TEXT("Supported target returned no real PSO factory data.");return false;}
        FMaterialInterfacePSOPrecacheParamsList Work=Original;
        for(auto* Material:Overlays) {
            if(Target->IsA<USkeletalMeshComponent>()&&!Material->GetUsageByFlag(MATUSAGE_SkeletalMesh))
                continue; // Unrelated static-only templates do not invalidate this location.
            for(const auto& Source:Original)if(!Source.VertexFactoryDataList.IsEmpty()) {
                auto Entry=Source;Entry.MaterialInterface=Material;Entry.PSOPrecacheParams.bCastShadow=false;
                const bool WPO=Material->IsUsingWorldPositionOffset_Concurrent(GMaxRHIShaderPlatform);
                Entry.PSOPrecacheParams.bAnyMaterialHasWorldPositionOffset|=WPO;
                Entry.PSOPrecacheParams.bRequireRTDynamicGeometryUpdate|=WPO&&Target->IsA<USkeletalMeshComponent>();
                AddMaterialInterfacePSOPrecacheParamsToList(Entry,Work);
            }
        }
        PrecacheMaterialPSOs(Work,MaterialContextPSORequests,MaterialContextPSOEvents);
        LogStep(TEXT("TargetPSOs"),Target);
        if(!MaterialContexts->RegisterPreparedTarget(Target,Error))return false;
        MaterialContextQueuedTargets.Add(Target);MaterialContextFence.Reset();MaterialContextStableFrames=0;
    }
    for(const auto& E:MaterialContextPSOEvents)if(E.IsValid()&&!E->IsComplete())bWaiting=true;
    if(bWaiting){MaterialContextFence.Reset();MaterialContextStableFrames=0;MaterialContextQuietSince=0;return true;}
    if(!MaterialContextFence.IsValid()) {
        MaterialContextFence=MakeShared<FRenderCommandFence>();MaterialContextFence->BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
        MaterialContextQuietSince=FPlatformTime::Seconds();bWaiting=true;return true;
    }
    if(!MaterialContextFence->IsFenceComplete()){bWaiting=true;return true;}
#if WITH_EDITOR
    if(FPlatformTime::Seconds()-MaterialContextQuietSince<RequiredEditorQuietSeconds){bWaiting=true;return true;}
#endif
    if(MaterialContextLastFrame!=GFrameCounter){MaterialContextLastFrame=GFrameCounter;++MaterialContextStableFrames;}
    if(MaterialContextStableFrames<RequiredStableRenderFrames){bWaiting=true;return true;}
    // Idle is the isolated authoring path. Normal worlds seal at their existing global barrier.
    if(Phase==EMCWorldPreloadPhase::Idle && !MaterialContexts->SealResources(Error))return false;
    return true;
}
