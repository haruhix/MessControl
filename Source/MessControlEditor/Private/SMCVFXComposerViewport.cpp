#include "SMCVFXComposerViewport.h"

#include "MCDataAssets.h"
#include "MCVFXComposition.h"
#include "MCVFXCompositionActor.h"

#include "AdvancedPreviewScene.h"
#include "AssetViewerSettings.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneView.h"

namespace MCVFXComposerPreview
{
    constexpr float SeekStep=1.f/60.f;
    constexpr float MaxFrameStep=.1f;
    constexpr float MaxSeekSeconds=40.f;
}

/** The default advanced scene ticks/writes its shared rotating-light profile. This studio is private. */
class FMCVFXComposerPreviewScene final : public FAdvancedPreviewScene
{
public:
    FMCVFXComposerPreviewScene()
        : FAdvancedPreviewScene(FPreviewScene::ConstructionValues()
            .AllowAudioPlayback(false).SetTransactional(false).ShouldSimulatePhysics(false))
    {
        // Niagara spawned at world locations belongs to WorldSettings. Keep this
        // private preview owner visible to editor views so bursts can render.
        GetWorld()->GetWorldSettings()->SetIsTemporarilyHiddenInEditor(false);
        DefaultSettings->OnAssetViewerSettingsChanged().Remove(RefreshDelegate);
        RefreshDelegate.Reset();
        SkyComponent->SetVisibility(false);
        PostProcessComponent->bEnabled=false;
        // The authored tooth faces actor +X. Light its face from the same front
        // hemisphere as the default camera instead of illuminating its backpack.
        SetLightDirection(FRotator(-40,145,0));
        SetLightBrightness(3.f);
        SetLightColor(FColor::White);
        if(DirectionalLight) DirectionalLight->SetForwardShadingPriority(1);
        SetSkyBrightness(.5f);
        if(auto* NeutralCubemap=LoadObject<UTextureCube>(nullptr,TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap")))
            SetSkyCubemap(NeutralCubemap);
        SkyLight->SetVisibility(true);
        SkyLight->SetLightColor(FLinearColor::White);
        auto* Fill=NewObject<UDirectionalLightComponent>(GetTransientPackage(),NAME_None,RF_Transient);
        Fill->SetIntensity(.8f);
        Fill->SetLightColor(FLinearColor(.9f,.95f,1.f));
        Fill->SetCastShadows(false);
        Fill->SetForwardShadingPriority(0);
        Fill->SetAtmosphereSunLight(false);
        AddComponent(Fill,FTransform(FRotator(-25,-135,0)));
        if(auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"))) SetFloorMesh(Plane);
        if(auto* NeutralFloor=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
        {
            // Private editor-only instance: a white engine floor overwhelms the
            // effect canvas. Never change the shared engine material or viewer profile.
            auto* FloorInstance=UMaterialInstanceDynamic::Create(NeutralFloor,GetTransientPackage());
            FloorInstance->SetFlags(RF_Transient);
            FloorInstance->SetVectorParameterValue(TEXT("Color"),FLinearColor(.1f,.11f,.12f));
            FloorInstance->SetScalarParameterValue(TEXT("Roughness"),.85f);
            SetFloorMaterial(FloorInstance);
        }
        SetFloorMeshRotation(FRotator::ZeroRotator);
        SetFloorMeshScale(FVector(20));
        SetStudioFloorVisible(true);
    }

    virtual void Tick(float) override
    {
        // Do not run FAdvancedPreviewScene::Tick: it can rotate and write the global profile.
        UpdateCaptureContents();
    }

    void SetStudioFloorVisible(bool bVisible) {FloorMeshComponent->SetVisibility(bVisible);}
};

class FMCVFXComposerViewportClient final : public FEditorViewportClient
{
public:
    FMCVFXComposerViewportClient(const TSharedRef<FAdvancedPreviewScene>& InScene,
        const TSharedRef<SMCVFXComposerViewport>& InWidget)
        : FEditorViewportClient(nullptr,&InScene.Get(),StaticCastSharedRef<SEditorViewport>(InWidget))
        , SceneLifetime(InScene)
        , Widget(InWidget)
    {
        SetViewMode(VMI_Lit);
        SetRealtime(true);
        bIsSimulateInEditorViewport=true;
        ShowWidget(false);
        DrawHelper.bDrawPivot=false;
        DrawHelper.bDrawWorldBox=false;
        DrawHelper.bDrawKillZ=false;
        DrawHelper.bDrawGrid=false;
        OverrideNearClipPlane(1.f);
        const FVector CameraLocation(360,-260,190);
        SetViewLocation(CameraLocation);
        SetViewRotation((FVector(0,0,65)-CameraLocation).Rotation());
        EngineShowFlags.SetSelectionOutline(false);
        ExposureSettings.bFixed=true;
        ExposureSettings.FixedEV100=1.f;
    }

    virtual FLinearColor GetBackgroundColor() const override {return FLinearColor(.035f,.04f,.05f);}

    virtual void OverridePostProcessSettings(FSceneView& View) override
    {
        FPostProcessSettings Studio;
        Studio.bOverride_AutoExposureMethod=true;
        Studio.AutoExposureMethod=AEM_Manual;
        Studio.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
        Studio.AutoExposureApplyPhysicalCameraExposure=false;
        Studio.bOverride_AutoExposureBias=true;
        Studio.AutoExposureBias=0.f;
        Studio.bOverride_BloomIntensity=true;
        Studio.BloomIntensity=.12f;
        View.OverridePostProcessSettings(Studio,1.f);
    }

    virtual void Tick(float DeltaSeconds) override
    {
        FEditorViewportClient::Tick(DeltaSeconds);
        if(const auto Pinned=Widget.Pin())
        {
            Pinned->AdvancePreview(DeltaSeconds);
        }
    }

    virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View,PDI);
        if(!bDrawCompositionBounds) return;
        if(const auto Pinned=Widget.Pin())
        {
            if(const auto* Actor=Pinned->GetPreviewActor())
            {
                TInlineComponentArray<UNiagaraComponent*> Components(Actor);
                for(const auto* Component:Components)
                {
                    if(IsValid(Component) && Component->IsRegistered()
                        && Component->Bounds.GetBox().IsValid)
                    {
                        DrawWireBox(PDI,Component->Bounds.GetBox(),FLinearColor(.1f,1.f,.65f),SDPG_World);
                    }
                }
            }
        }
    }

    bool bDrawCompositionBounds=false;

private:
    // FSceneViewport retains this client beyond the derived widget's members.
    TSharedRef<FAdvancedPreviewScene> SceneLifetime;
    TWeakPtr<SMCVFXComposerViewport> Widget;
};

void SMCVFXComposerViewport::Construct(const FArguments&)
{
    PreviewScene=MakeShared<FMCVFXComposerPreviewScene>();

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.ObjectFlags=RF_Transient;
    SpawnParameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    UWorld* World=PreviewScene->GetWorld();
    auto* Subject=World->SpawnActor<ASkeletalMeshActor>(FVector::ZeroVector,FRotator::ZeroRotator,SpawnParameters);
    SubjectActor=Subject;
    if(Subject)
    {
        Subject->SetActorEnableCollision(false);
        Subject->SetActorTickEnabled(false);
        auto* Mesh=Subject->GetSkeletalMeshComponent();
        const auto* Appearance=LoadObject<UMCPlayerAppearance>(nullptr,TEXT("/Game/Data/DA_PlayerAppearance.DA_PlayerAppearance"));
        auto* Tooth=Appearance && Appearance->SkeletalMesh?Appearance->SkeletalMesh.Get()
            :LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Art/Rig/SK_ToothHero.SK_ToothHero"));
        Mesh->SetSkeletalMeshAsset(Tooth);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetSimulatePhysics(false);
        Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Mesh->SetEnableAnimation(false);
        Mesh->SetRelativeRotation(FRotator(0,-90,0));
        // Match the authored surface without constructing a character or mutating the appearance asset.
        if(Appearance && Appearance->SkeletalMesh==Tooth)
        {
            Mesh->SetRelativeRotation(Appearance->MeshTransform.GetRotation());
            Mesh->SetRelativeScale3D(Appearance->MeshTransform.GetScale3D());
            if(Appearance->Material) Mesh->SetMaterial(0,Appearance->Material);
        }
        Mesh->RefreshBoneTransforms();
        Mesh->UpdateBounds();
        // ASkeletalMeshActor uses this mesh as its root. Its authored relative
        // rotation therefore IS the actor rotation; Reset must retain that base.
        SubjectBaseRotation=Subject->GetActorQuat();
        if(Tooth)
        {
            SubjectBaseLocation=FVector(0,0,2.f-Mesh->Bounds.GetBox().Min.Z);
            Subject->SetActorLocation(SubjectBaseLocation);
        }
        else SubjectWarning=TEXT("Не найден /Game/Art/Rig/SK_ToothHero: эффект можно смотреть без зубика. ");
    }
    auto* Actor=World->SpawnActor<AMCVFXCompositionActor>(FVector::ZeroVector,FRotator::ZeroRotator,SpawnParameters);
    PreviewActor=Actor;
    if(Actor) Actor->SetExternallyTicked(true);
    else PreparationError=TEXT("Не удалось создать отдельный preview actor.");

    SEditorViewport::Construct(SEditorViewport::FArguments());
    SetViewPreset(2);
}

SMCVFXComposerViewport::~SMCVFXComposerViewport()
{
    if(auto* Actor=PreviewActor.Get())
    {
        Actor->StopComposition(true);
        Actor->Destroy();
    }
    if(auto* Subject=SubjectActor.Get()) Subject->Destroy();
    // FSceneViewport holds a raw client pointer; detach it before destroying the isolated world.
    if(ComposerViewportClient) ComposerViewportClient->Viewport=nullptr;
}

TSharedRef<FEditorViewportClient> SMCVFXComposerViewport::MakeEditorViewportClient()
{
    ComposerViewportClient=MakeShared<FMCVFXComposerViewportClient>(PreviewScene.ToSharedRef(),SharedThis(this));
    return ComposerViewportClient.ToSharedRef();
}

void SMCVFXComposerViewport::SetComposition(UMCVFXComposition* InComposition)
{
    Composition=InComposition;
    bPrepared=false;
    bHasStarted=false;
    bPaused=false;
    bPendingPlay=false;
    ReadinessSystems.Reset();
    ReadinessMaterials.Reset();
    ReadinessMessage.Reset();
    PreparationError.Reset();
    PlaybackNote.Reset();
    ResetSubject();
    if(auto* Actor=PreviewActor.Get())
    {
        Actor->StopComposition(true);
        if(InComposition) bPrepared=Actor->PrepareComposition(InComposition,PreparationError);
    }
    else PreparationError=TEXT("Preview actor недоступен.");
    if(bPrepared)
    {
        for(const auto& Layer:InComposition->Layers)
        {
            if(!Layer.bEnabled) continue;
            if(auto* System=Layer.System.Get())
            {
                ReadinessSystems.AddUnique(System);
                for(const auto& Handle:System->GetEmitterHandles())
                    if(const auto* Data=Handle.GetEmitterData())
                        for(auto* Renderer:Data->GetRenderers()) if(Renderer)
                        {
                            TArray<UMaterialInterface*> Materials;
                            Renderer->GetUsedMaterials(nullptr,Materials);
                            for(auto* Material:Materials) if(Material) ReadinessMaterials.AddUnique(Material);
                        }
            }
        }
        if(const auto* Subject=SubjectActor.Get())
            for(auto* Material:Subject->GetSkeletalMeshComponent()->GetMaterials())
                if(Material) ReadinessMaterials.AddUnique(Material);
    }
    Invalidate();
}

void SMCVFXComposerViewport::Play()
{
    auto* Actor=PreviewActor.Get();
    if(!Actor || !bPrepared) return;
    if(!CheckPreviewReadiness()) {bPendingPlay=true;return;}
    bPendingPlay=false;
    PlaybackNote.Reset();
    if(bHasStarted && bPaused && Actor->IsRunning())
    {
        Actor->PauseComposition(false);
        bPaused=false;
    }
    else if(!Actor->IsPlaying())
    {
        auto* Subject=SubjectActor.Get();
        bHasStarted=Actor->PlayPrepared(Subject?Subject->GetSkeletalMeshComponent():nullptr,true);
        bPaused=false;
        if(!bHasStarted) PreparationError=Actor->GetLastError();
    }
    Invalidate();
}

void SMCVFXComposerViewport::Pause()
{
    bPendingPlay=false;
    if(auto* Actor=PreviewActor.Get();Actor && bHasStarted)
    {
        Actor->PauseComposition(true);
        bPaused=true;
    }
    Invalidate();
}

void SMCVFXComposerViewport::Reset()
{
    if(auto* Actor=PreviewActor.Get()) Actor->StopComposition(true);
    bHasStarted=false;
    bPaused=false;
    bPendingPlay=false;
    PlaybackNote.Reset();
    ResetSubject();
    Play();
}

void SMCVFXComposerViewport::Seek(float Seconds)
{
    const auto* Data=Composition.Get();
    if(!bPrepared || !Data || !FMath::IsFinite(Seconds)) return;
    if(!CheckPreviewReadiness())
    {
        bPendingPlay=false;
        PlaybackNote=TEXT("Перемотка не запускалась: дождитесь готовности и повторите действие.");
        return;
    }
    const float Maximum=FMath::Clamp(Data->DurationSeconds+Data->TailSeconds,0.f,MCVFXComposerPreview::MaxSeekSeconds);
    const float Target=FMath::Clamp(Seconds,0.f,Maximum);
    Reset();
    if(!bHasStarted) return;
    float Simulated=0;
    while(Simulated<Target && IsPlaying())
    {
        const float Step=FMath::Min(MCVFXComposerPreview::SeekStep,Target-Simulated);
        AdvancePreview(Step);
        Simulated+=Step;
    }
    Pause();
    PlaybackNote=TEXT("Перемотка: повторный прогон с шагом 1/60 с; GPU и эффекты с историей воспроизводятся приблизительно.");
    Invalidate();
}

void SMCVFXComposerViewport::SetMotionEnabled(bool bEnabled)
{
    bMotionEnabled=bEnabled;
    Reset();
}

void SMCVFXComposerViewport::SetGroundVisible(bool bVisible)
{
    if(PreviewScene) static_cast<FMCVFXComposerPreviewScene*>(PreviewScene.Get())->SetStudioFloorVisible(bVisible);
    Invalidate();
}

void SMCVFXComposerViewport::SetBoundsVisible(bool bVisible)
{
    if(ComposerViewportClient) ComposerViewportClient->bDrawCompositionBounds=bVisible;
    Invalidate();
}

void SMCVFXComposerViewport::Focus()
{
    if(!ComposerViewportClient) return;
    FBox Box(ForceInit);
    if(const auto* Subject=SubjectActor.Get()) Box+=Subject->GetComponentsBoundingBox(true);
    if(const auto* Actor=PreviewActor.Get())
    {
        TInlineComponentArray<UNiagaraComponent*> Components(Actor);
        for(const auto* Component:Components)
        {
            if(IsValid(Component) && Component->IsRegistered()) Box+=Component->Bounds.GetBox();
        }
    }
    if(!Box.IsValid) Box=FBox(FVector(-100,-100,0),FVector(100,100,150));
    // Explicit framing avoids stale orbit/flight transitions when switching between a moving subject and a preset.
    const FIntPoint Pixels=ComposerViewportClient->Viewport?ComposerViewportClient->Viewport->GetSizeXY():FIntPoint(1000,800);
    const double Aspect=FMath::Max(.2,static_cast<double>(Pixels.X)/FMath::Max(1,Pixels.Y));
    const double HalfHorizontal=FMath::DegreesToRadians(FMath::Clamp(ComposerViewportClient->ViewFOV,10.f,120.f)*.5);
    const double HalfVertical=FMath::Atan(FMath::Tan(HalfHorizontal)/Aspect);
    const double Distance=FMath::Max(150.,Box.ExpandBy(20).GetExtent().Size()/FMath::Sin(FMath::Min(HalfHorizontal,HalfVertical))*1.1);
    const FVector Camera=Box.GetCenter()-ComposerViewportClient->GetViewRotation().Vector()*Distance;
    ComposerViewportClient->ToggleOrbitCamera(false);
    ComposerViewportClient->GetViewTransform().TransitionToLocation(Camera,TWeakPtr<SWidget>(),true);
    Invalidate();
}

void SMCVFXComposerViewport::SetViewPreset(int32 Preset)
{
    if(!ComposerViewportClient) return;
    // Match the native action preview: +X is front, +Y is side. Preset 2 is
    // the welcoming front three-quarter default; authored mesh transforms stay intact.
    const FVector Offset=Preset==1?FVector(0,420,110):Preset==2?FVector(360,-260,190):FVector(420,0,95);
    FVector Center(0,0,65);
    if(const auto* Subject=SubjectActor.Get()) Center=Subject->GetComponentsBoundingBox(true).GetCenter();
    ComposerViewportClient->ToggleOrbitCamera(false);
    ComposerViewportClient->GetViewTransform().TransitionToLocation(Center+Offset,TWeakPtr<SWidget>(),true);
    ComposerViewportClient->SetViewRotation((-Offset).Rotation());
    Focus();
}

float SMCVFXComposerViewport::GetPlaybackTime() const
{
    const auto* Actor=PreviewActor.Get();
    return bHasStarted && Actor?Actor->GetPlaybackTime():0.f;
}

bool SMCVFXComposerViewport::IsPlaying() const
{
    const auto* Actor=PreviewActor.Get();
    return Actor && Actor->IsPlaying();
}

FString SMCVFXComposerViewport::GetDiagnostics() const
{
    if(!PreparationError.IsEmpty()) return PreparationError;
    if(!ReadinessMessage.IsEmpty()) return ReadinessMessage
        +(bPendingPlay?TEXT(" Play запустится автоматически после готовности. "):TEXT(" "))+PlaybackNote;
    FString Result;
    if(const auto* Actor=PreviewActor.Get())
    {
        if(!Actor->GetLastError().IsEmpty()) return Actor->GetLastError();
        Result=FString::Printf(TEXT("EditorPreview · %.2f с · живых слоёв: %d. "),GetPlaybackTime(),Actor->GetLiveLayerCount());
    }
    if(!bPrepared) Result+=TEXT("Выберите композицию и добавьте слой Niagara. ");
    Result+=SubjectWarning;
    Result+=TEXT("Зубик — неподвижная поза исходного меша; движение влияет на слои Subject, остальные привязаны к своему якорю. Это проверка следа, не анимации персонажа. Bounds — текущие границы компонентов, без записи в NS. ");
    if(!PlaybackNote.IsEmpty()) Result+=PlaybackNote;
    else Result+=TEXT("Перемотка GPU/эффектов с историей приблизительна. Игровая интеграция и прогрев проверяются отдельно.");
    return Result;
}

AMCVFXCompositionActor* SMCVFXComposerViewport::GetPreviewActor() const {return PreviewActor.Get();}
UWorld* SMCVFXComposerViewport::GetPreviewWorld() const {return PreviewScene?PreviewScene->GetWorld():nullptr;}

void SMCVFXComposerViewport::AdvancePreview(float DeltaSeconds)
{
    if(bPendingPlay && CheckPreviewReadiness()) Play();
    if(!PreviewScene || bPaused || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0) return;
    const float Step=FMath::Min(DeltaSeconds,MCVFXComposerPreview::MaxFrameStep);
    if(auto* Actor=PreviewActor.Get();Actor && Actor->IsPlaying())
    {
        UpdateSubject(Step);
        // Only this caller advances the composition clock in the preview world.
        Actor->AdvanceComposition(Step);
    }
    PreviewScene->GetWorld()->Tick(LEVELTICK_All,Step);
}

bool SMCVFXComposerViewport::CheckPreviewReadiness()
{
    ReadinessMessage.Reset();
    for(const auto& Reference:ReadinessSystems)
    {
        auto* System=Reference.Get();
        if(!System)
        {
            ReadinessMessage=TEXT("Система предпросмотра больше не загружена. Подготовьте композицию заново.");
            return false;
        }
        if(System->HasOutstandingCompilationRequests(true)) System->PollForCompilationComplete(true);
        if(System->HasOutstandingCompilationRequests(true) || !System->IsReadyToRun())
        {
            ReadinessMessage=FString::Printf(TEXT("Ожидание Niagara: %s. Таймер не запущен. Если ошибка компиляции — откройте исходную NS."),*System->GetName());
            return false;
        }
    }
    for(const auto& Reference:ReadinessMaterials)
        if(const auto* Material=Reference.Get();Material && Material->IsCompiling())
        {
            ReadinessMessage=FString::Printf(TEXT("Ожидание материала предпросмотра: %s. Таймер не запущен."),*Material->GetName());
            return false;
        }
    return true;
}

void SMCVFXComposerViewport::ResetSubject()
{
    MotionTime=0;
    if(auto* Subject=SubjectActor.Get())
    {
        Subject->SetActorLocationAndRotation(SubjectBaseLocation,SubjectBaseRotation);
    }
}

void SMCVFXComposerViewport::UpdateSubject(float DeltaSeconds)
{
    if(!bMotionEnabled) return;
    MotionTime+=DeltaSeconds;
    if(auto* Subject=SubjectActor.Get())
    {
        const float Phase=MotionTime*.7f;
        const FVector Offset(160.f*FMath::Sin(Phase),90.f*(1.f-FMath::Cos(Phase)),0);
        const FVector Direction(160.f*FMath::Cos(Phase),90.f*FMath::Sin(Phase),0);
        Subject->SetActorLocationAndRotation(SubjectBaseLocation+Offset,Direction.Rotation().Quaternion()*SubjectBaseRotation);
    }
}
