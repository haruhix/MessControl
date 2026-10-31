#include "MCPerkPreviewRunner.h"
#include "MCPerkPreviewGameMode.h"

#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCPerkComponent.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCRocketSkillAction.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"

#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "PreviewScene.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace MCPerkPreview
{
const FName RocketID(TEXT("Consumable_Rocket"));
const FName OwnedTag(TEXT("MC.VFXActionPreview"));
constexpr float WarmupSeconds=.3f;
constexpr float MaxFrameStep=.05f;
constexpr float MaxCatchupSeconds=.2f;

FString PhaseName(EMCRocketSkillPhase Phase)
{
    switch(Phase) {
    case EMCRocketSkillPhase::Prepared:return TEXT("Подготовка");
    case EMCRocketSkillPhase::Windup:return TEXT("Достаёт / прицеливается");
    case EMCRocketSkillPhase::Flying:return TEXT("Полёт / восстановление позы");
    case EMCRocketSkillPhase::Detonated:return TEXT("Попадание / остаток");
    default:return TEXT("Неизвестная фаза");
    }
}
}

struct FMCPerkPreviewRunner::FImpl
{
    TStrongObjectPtr<UGameInstance> Instance;
    TStrongObjectPtr<UWorld> PreviewWorld;
    TStrongObjectPtr<UDirectionalLightComponent> KeyLight;
    TStrongObjectPtr<UDirectionalLightComponent> FillLight;
    TStrongObjectPtr<USkyLightComponent> SkyLight;
    TStrongObjectPtr<UMCActiveSkillPerkEffect> Effect;
    TStrongObjectPtr<UMCPerkVFXProfile> Profile;
    TWeakObjectPtr<AMCToothCharacter> Hero;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<AMCPlayerState> Player;
    TWeakObjectPtr<AMCActiveSkillAction> Action;
    TWeakObjectPtr<AStaticMeshActor> Target;
    TWeakObjectPtr<ACameraActor> AimCamera;
    TSet<TWeakObjectPtr<AActor>> StageActors;
    TArray<FMCPerkPreviewEvent> Events;
    FMCPerkDefinition Definition;
    FName PerkID;
    FMCPerkPreviewAdapterEntry AdapterEntry;
    TUniquePtr<IMCPerkPreviewAdapter> Adapter;
    FMCPerkPreviewContext Context;
    bool bHasDefinition=false;
    bool bFactoryPrepared=false;
    bool bInitialized=false;
    FString Status=TEXT("Предпросмотр ещё не подготовлен");
    FString LastPhase=TEXT("Ожидание");
    EMCPerkPreviewScenario Scenario=EMCPerkPreviewScenario::Target;
    double RunStartedAt=0;
    float Elapsed=0;
    float FinishedAt=-1;
    float Warmup=0;
    bool bPendingCommit=false;
    bool bPaused=false;
    bool bRunning=false;
    bool bSawCommittedAction=false;
    FMCPerkPreviewClockStats Clock;

    UWorld* World() const {return PreviewWorld.Get();}

    void RefreshContext()
    {
        Context.World=World();Context.Hero=Hero.Get();Context.Controller=Controller.Get();Context.PlayerState=Player.Get();
        Context.AimCamera=AimCamera.Get();Context.Action=Action.Get();Context.Definition=bHasDefinition?&Definition:nullptr;
        Context.PerkID=PerkID;Context.Scenario=Scenario;Context.Elapsed=Elapsed;
        Context.AddBox=[this](FName Label,FVector Location,FVector Scale) {
            return AddBox(Label,Location,Scale);
        };
        Context.RecordEvent=[this](FName Stage){HandleStage(Stage);};
        Context.ReportStatus=[this](const FString& Text){Status=Text;};
    }

    void AddEvent(FName Stage)
    {
        if(Events.Num()<128) Events.Add({Elapsed,Stage});
    }

    void HandleStage(FName Stage)
    {
        if(auto* W=World()) Elapsed=float(FMath::Max(0.,W->GetTimeSeconds()-RunStartedAt));
        AddEvent(Stage);
    }

    AStaticMeshActor* AddBox(FName Label,FVector Location,FVector Scale)
    {
        auto* W=World();if(!W) return nullptr;
        FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Box=W->SpawnActor<AStaticMeshActor>(Location,FRotator::ZeroRotator,Spawn);
        if(!Box) return nullptr;
        Box->Tags.Add(MCPerkPreview::OwnedTag);Box->SetActorLabel(Label.ToString(),false);
        auto* Mesh=Box->GetStaticMeshComponent();
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
        {
            if(Label==TEXT("VFXPreview_Floor"))
            {
                auto* Floor=UMaterialInstanceDynamic::Create(Material,Mesh);
                Floor->SetFlags(RF_Transient);
                Floor->SetVectorParameterValue(TEXT("Color"),FLinearColor(.09f,.11f,.13f));
                Floor->SetScalarParameterValue(TEXT("Roughness"),.9f);
                Mesh->SetMaterial(0,Floor);
            }
            else Mesh->SetMaterial(0,Material);
        }
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
        Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);
        Box->SetActorScale3D(Scale);
        return Box;
    }

    void ClearGameplay()
    {
        bPendingCommit=false;bRunning=false;bSawCommittedAction=false;bPaused=false;Warmup=0;
        if(Adapter) {RefreshContext();Adapter->Cancel(Context);Adapter->Reset(Context);}
        Adapter.Reset();Context=FMCPerkPreviewContext();bFactoryPrepared=false;
        if(auto* A=Action.Get()) {
            A->OnVFXStage.RemoveAll(this);
            A->Cancel(EMCActiveSkillEndReason::WorldEnded);
            if(IsValid(A)) A->Finish(EMCActiveSkillEndReason::WorldEnded);
        }
        Action.Reset();Effect.Reset();Profile.Reset();
        if(auto* W=World()) {
            // Everything outside the immutable stage belongs to this sandbox. This
            // also catches committed hazards and auto-spawned camera/cue actors.
            TArray<AActor*> Remove;
            for(TActorIterator<AActor> It(W);It;++It)
                if(IsValid(*It) && !StageActors.Contains(TWeakObjectPtr<AActor>(*It))) Remove.Add(*It);
            for(auto* A:Remove) {
                W->GetTimerManager().ClearAllTimersForObject(A);
                A->Destroy(false,false);
            }
            // SpawnSystemAtLocation may create an ownerless component, not an
            // actor. Never sweep Niagara in any other editor/game world.
            TArray<UNiagaraComponent*> Niagara;
            for(TObjectIterator<UNiagaraComponent> It;It;++It)
                if(IsValid(*It) && It->GetWorld()==W && !It->IsTemplate()) Niagara.Add(*It);
            for(auto* C:Niagara) {C->DeactivateImmediate();C->DestroyComponent();}
        }
        Hero.Reset();Controller.Reset();Player.Reset();
    }

    bool SpawnHero()
    {
        auto* W=World();if(!W) return false;
        FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* PC=W->SpawnActor<APlayerController>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
        if(!PC) {Status=TEXT("Не удалось создать временный Controller");return false;}
        Controller=PC;
        // Base PlayerController has no MessControl UI/save/music hooks and no LocalPlayer.
        // With no NetDriver/ULocalPlayer, CMC otherwise treats this pawn as a
        // remote controller and skips actual controlled movement in the sandbox.
        // This native designation adds no LocalPlayer, input viewport or session.
        PC->SetAsLocalPlayerController();
        PC->bShowMouseCursor=false;PC->SetControlRotation(FRotator::ZeroRotator);
        // Base APlayerController already creates the GameMode's PlayerState in
        // PostInitializeComponents. Reuse it instead of orphaning a second
        // state in GameState::PlayerArray on every preview reset.
        auto* PS=PC->GetPlayerState<AMCPlayerState>();
        if(!PS)PS=W->SpawnActor<AMCPlayerState>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
        if(!PS) {Status=TEXT("Не удалось создать временный PlayerState");return false;}
        Player=PS;PC->SetPlayerState(PS);PS->SetOwner(PC);
        if(auto* GS=W->GetGameState<AMCGameState>())GS->AddPlayerState(PS);
        PS->SetPlayerName(TEXT("VFX Preview"));
        auto* H=W->SpawnActor<AMCToothCharacter>(FVector(0,0,90),FRotator::ZeroRotator,Spawn);
        if(!H) {Status=TEXT("Не удалось создать зубика предпросмотра");return false;}
        Hero=H;H->Tags.Add(MCPerkPreview::OwnedTag);PC->Possess(H);
        H->SetActorLocation(FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f),false,nullptr,ETeleportType::TeleportPhysics);
        H->Status->Initialize(100.f);
        H->GetCharacterMovement()->DisableMovement();
        H->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        H->GetMesh()->bEnableUpdateRateOptimizations=false;
        H->GetMesh()->RefreshBoneTransforms();
        if(auto* Camera=AimCamera.Get()) PC->SetViewTarget(Camera);
        if(!H->GetMesh()->GetSkeletalMeshAsset() || !H->GetMesh()->GetAnimInstance() || !H->CanWork()) {
            Status=TEXT("Зубик создан, но штатный AnimInstance или состояние физики пока не готовы");return false;
        }
        return true;
    }

    bool PrepareAction()
    {
        auto* PS=Player.Get();auto* H=Hero.Get();
        if(!PS || !H || !PS->Perks) {Status=TEXT("Нет полноценного контекста зубика");return false;}
        if(AdapterEntry.Kind!=EMCPerkPreviewAdapterKind::ActiveSkill)return true;
        auto* Class=Definition.EffectClass.Get();
        if(!Class || !Class->IsChildOf(UMCActiveSkillPerkEffect::StaticClass())) {
            Status=TEXT("Строка не содержит совместимую фабрику настоящего действия");return false;
        }
        Effect.Reset(NewObject<UMCActiveSkillPerkEffect>(PS->Perks,Class,NAME_None,RF_Transient));
        Effect->OwnerComponent=PS->Perks;
        FMCActiveSkillInputHandle Input;Input.SlotIndex=0;Input.SlotInstanceID=FGuid::NewGuid();Input.PressSequence=1;
        auto* Prepared=Effect->PrepareAction(PerkID,Definition,Input);
        if(!Prepared) {Status=TEXT("Штатная подготовка действия отклонена: проверьте хват, цель и состояние зубика");return false;}
        Action=Prepared;
        FString AdapterError;
        if(!Adapter || !Adapter->ValidateAction(*Prepared,AdapterError)) {
            Status=AdapterError.IsEmpty()?TEXT("Фабрика вернула несовместимое действие"):AdapterError;
            Prepared->Finish(EMCActiveSkillEndReason::InputCanceled);Action.Reset();return false;
        }
        if(Profile.IsValid()) {
            FString Error;
            if(!Prepared->PrepareVFXProfile(Profile.Get(),Error)) {
                Status=TEXT("Невалидный VFX-профиль: ")+Error;
                Prepared->Finish(EMCActiveSkillEndReason::InputCanceled);Action.Reset();return false;
            }
        }
        Prepared->OnVFXStage.AddRaw(this,&FImpl::HandleStage);
        RefreshContext();LastPhase=Adapter->GetPhaseName(Context);
        return true;
    }

    bool ResourcesReady()
    {
        const auto* A=Action.Get();if(AdapterEntry.Kind==EMCPerkPreviewAdapterKind::ActiveSkill && !A)return false;
        TArray<UNiagaraSystem*> Systems;
        if(A)for(FName Stage:AdapterEntry.Stages) {
            TArray<UNiagaraSystem*> StageSystems;A->GetPreparedVFXSystems(Stage,StageSystems);Systems.Append(StageSystems);
        }
        TSet<UNiagaraSystem*> Checked;
        for(auto* System:Systems) {
            if(!System || Checked.Contains(System))continue;
            Checked.Add(System);
            if(System->HasOutstandingCompilationRequests(true)) System->PollForCompilationComplete(true);
            if(System->HasOutstandingCompilationRequests(true) || !System->IsReadyToRun()) {
                Status=FString::Printf(TEXT("Ожидание Niagara %s; действие ещё не началось"),*System->GetName());return false;
            }
            for(const auto& Handle:System->GetEmitterHandles())if(const auto* Emitter=Handle.GetInstance().GetEmitterData())
                for(auto* Renderer:Emitter->GetRenderers())if(Renderer) {
                    TArray<UMaterialInterface*> Materials;Renderer->GetUsedMaterials(nullptr,Materials);
                    for(auto* Material:Materials)if(Material && Material->IsCompiling()) {
                        Status=FString::Printf(TEXT("Ожидание материала Niagara %s; действие ещё не началось"),*Material->GetName());return false;
                    }
                }
        }
        for(TActorIterator<AActor> It(World());It;++It) {
            TInlineComponentArray<UMeshComponent*> Meshes(*It);
            for(auto* Mesh:Meshes) for(int32 Index=0;Index<Mesh->GetNumMaterials();++Index)
                if(auto* Material=Mesh->GetMaterial(Index);Material && Material->IsCompiling()) {
                    Status=FString::Printf(TEXT("Ожидание материала %s; действие ещё не началось"),*Material->GetName());return false;
                }
        }
        return true;
    }
};

FMCPerkPreviewRunner::FMCPerkPreviewRunner():Impl(MakeUnique<FImpl>()) {}

FMCPerkPreviewRunner::~FMCPerkPreviewRunner()
{
    Impl->ClearGameplay();
    for(auto* C:{Impl->KeyLight.Get(),Impl->FillLight.Get()})if(C)C->DestroyComponent();
    if(auto* C=Impl->SkyLight.Get())C->DestroyComponent();
    if(auto* W=Impl->World()) {
        W->EndPlay(EEndPlayReason::Quit);W->CleanupWorld();
        if(GEngine)GEngine->DestroyWorldContext(W);
        W->ReleasePhysicsScene();
    }
    Impl->KeyLight.Reset();Impl->FillLight.Reset();Impl->SkyLight.Reset();
    Impl->PreviewWorld.Reset();Impl->Instance.Reset();
}

bool FMCPerkPreviewRunner::Initialize()
{
    if(Impl->PreviewWorld.IsValid()) return Impl->bInitialized;
    if(!GEngine || !IsInGameThread()) {Impl->Status=TEXT("Engine ещё не готов к предпросмотру");return false;}
    Impl->Instance.Reset(NewObject<UGameInstance>(GetTransientPackage(),NAME_None,RF_Transient));
    // FPreviewScene fixes WorldType to GamePreview before subsystem creation;
    // ResultPassives intentionally supports only Game/PIE. Create this private
    // transient Game world directly instead of changing any global world policy
    // or relabelling an already initialized editor/preview world.
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true)
        .SetTransactional(false).SetDefaultGameMode(AMCPerkPreviewGameMode::StaticClass());
    const FName WorldName=MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("MCVFXPreviewWorld"));
    Impl->PreviewWorld.Reset(UWorld::CreateWorld(EWorldType::Game,false,WorldName,GetTransientPackage(),false,
        GMaxRHIFeatureLevel,&Values,true));
    auto* W=GetWorld();
    if(!W || W->GetOutermost()!=GetTransientPackage()) {Impl->Status=TEXT("Не удалось создать изолированный мир");return false;}
    W->SetFlags(RF_Transient);W->SetGameInstance(Impl->Instance.Get());
    auto& WorldContext=GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(W);WorldContext.OwningGameInstance=Impl->Instance.Get();
    W->InitWorld(Values);W->UpdateWorldComponents(true,false);
    if(!W->SetGameMode(FURL()) || !W->GetAuthGameMode<AMCPerkPreviewGameMode>()) {
        Impl->Status=TEXT("Не удалось создать безопасный GameMode песочницы");return false;
    }
    W->InitializeActorsForPlay(FURL());
    Impl->KeyLight.Reset(NewObject<UDirectionalLightComponent>(GetTransientPackage(),NAME_None,RF_Transient));
    Impl->KeyLight->SetIntensity(3.f);Impl->KeyLight->SetLightColor(FLinearColor::White);
    Impl->KeyLight->SetForwardShadingPriority(1);Impl->KeyLight->SetAtmosphereSunLight(false);
    Impl->KeyLight->SetWorldRotation(FRotator(-35,145,0));Impl->KeyLight->RegisterComponentWithWorld(W);
    Impl->SkyLight.Reset(NewObject<USkyLightComponent>(GetTransientPackage(),NAME_None,RF_Transient));
    Impl->SkyLight->SetIntensity(1.f);Impl->SkyLight->SetLightColor(FLinearColor::White);
    Impl->SkyLight->SourceType=SLS_SpecifiedCubemap;Impl->SkyLight->SetMobility(EComponentMobility::Movable);
    if(auto* Cube=LoadObject<UTextureCube>(nullptr,TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap")))Impl->SkyLight->SetCubemap(Cube);
    Impl->SkyLight->RegisterComponentWithWorld(W);
    // A low-energy, shadowless cross fill keeps the far hand readable in side
    // inspection without adding another hard silhouette or washing out enamel.
    Impl->FillLight.Reset(NewObject<UDirectionalLightComponent>(GetTransientPackage(),NAME_None,RF_Transient));
    auto* Fill=Impl->FillLight.Get();
    Fill->SetIntensity(1.25f);Fill->SetLightColor(FLinearColor(.88f,.94f,1.f));Fill->SetCastShadows(false);
    Fill->SetForwardShadingPriority(0);Fill->SetAtmosphereSunLight(false);
    Fill->SetWorldRotation(FRotator(-25,15,0));Fill->RegisterComponentWithWorld(W);
    // World-space Niagara bursts are owned by this world's WorldSettings. Preview
    // worlds hide that actor in editor views, which also hides its particle proxies.
    // Only this private sandbox is changed; the user's level is never touched.
    W->GetWorldSettings()->SetIsTemporarilyHiddenInEditor(false);
    W->BeginPlay();
    if(auto* GS=W->GetGameState<AMCGameState>()) {GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;GS->bDevManualEvents=true;}
    Impl->AddBox(TEXT("VFXPreview_Floor"),FVector(6000,0,-20),FVector(160,70,.4f));
    Impl->Target=Impl->AddBox(TEXT("VFXPreview_Target"),FVector(900,0,140),FVector(.4f,2.8f,2.8f));
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
    Impl->AimCamera=W->SpawnActor<ACameraActor>(FVector(0,0,125),FRotator::ZeroRotator,Spawn);
    for(TActorIterator<AActor> It(W);It;++It) Impl->StageActors.Add(*It);
    USkyLightComponent::UpdateSkyCaptureContents(W);
    Reset();
    Impl->bInitialized=Impl->Hero.IsValid();return Impl->bInitialized;
}

void FMCPerkPreviewRunner::Reset()
{
    if(!Impl->World()) return;
    Impl->ClearGameplay();Impl->Events.Reset();Impl->Elapsed=0;Impl->FinishedAt=-1;Impl->LastPhase=TEXT("Ожидание");
    if(auto* Target=Impl->Target.Get()) {
        Target->SetActorLocation(FVector(900,0,140));Target->SetActorHiddenInGame(false);Target->SetActorEnableCollision(true);
    }
    if(Impl->SpawnHero()) Impl->Status=TEXT("Готово: настоящий зубик, штатная анимация и физические руки");
    else if(Impl->Status.IsEmpty()) Impl->Status=TEXT("Не удалось подготовить анимацию зубика");
}

bool FMCPerkPreviewRunner::StartRocket(EMCPerkPreviewScenario Scenario,UMCPerkVFXProfile* ProfileOverride)
{return StartAction(MCPerkPreview::RocketID,Scenario,ProfileOverride);}

bool FMCPerkPreviewRunner::StartAction(FName PerkID,EMCPerkPreviewScenario Scenario,UMCPerkVFXProfile* ProfileOverride)
{
    if(!Initialize()) return false;
    Reset();if(!Impl->Hero.IsValid()) return false;
    const auto* Entry=MCPerkPreviewAdapters::Find(PerkID);
    if(!Entry || !Entry->Create) {Impl->Status=TEXT("У этого действия пока нет зарегистрированного реального сценария.");return false;}
    if(!Entry->Scenarios.Contains(Scenario)) {Impl->Status=TEXT("Этот сценарий не поддерживается выбранным действием.");return false;}
    Impl->PerkID=PerkID;Impl->AdapterEntry=*Entry;Impl->Adapter=Entry->Create();Impl->Scenario=Scenario;
    if(!Impl->Adapter){Impl->Status=TEXT("Не удалось создать адаптер сценария.");return false;}
    const FName RowID=Entry->SourceRowID.IsNone()?PerkID:Entry->SourceRowID;
    const bool NeedsRow=Entry->Kind!=EMCPerkPreviewAdapterKind::Context || !Entry->SourceRowID.IsNone();
    auto* Catalog=NeedsRow?LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks")):nullptr;
    const auto* Row=Catalog && Catalog->GetRowStruct()==FMCPerkDefinition::StaticStruct()
        ?Catalog->FindRow<FMCPerkDefinition>(RowID,TEXT("VFXActionPreview"),false):nullptr;
    if(!Row && NeedsRow) {Impl->Status=TEXT("Каноническая строка сценария отсутствует в DT_Perks: ")+RowID.ToString();return false;}
    Impl->bHasDefinition=Row!=nullptr;Impl->Definition=Row?*Row:FMCPerkDefinition();Impl->Profile.Reset(ProfileOverride);
    if(ProfileOverride && Entry->bSupportsProfileOverride)Impl->Definition.VFXProfile=ProfileOverride;
    if(ProfileOverride && !Entry->bSupportsProfileOverride && !ProfileOverride->Slots.IsEmpty()) {
        Impl->Status=TEXT("Этот сценарий использует реальные игровые эффекты, но ещё не поддерживает замену VFX-профиля.");return false;
    }
    Impl->RefreshContext();
    if(auto* Target=Impl->Target.Get()) {
        const bool Rocket=PerkID==MCPerkPreview::RocketID;
        Target->SetActorHiddenInGame(!Rocket);Target->SetActorEnableCollision(Rocket);
        if(Rocket)Impl->Context.Targets.Add(Target);
    }
    FString Error;
    Impl->Status=TEXT("Подготовка позы и ресурсов перед действием…");
    if(!Impl->Adapter->Prepare(Impl->Context,Error)) {Impl->Status=Error.IsEmpty()?TEXT("Не удалось подготовить настоящий сценарий."):Error;return false;}
    Impl->Warmup=MCPerkPreview::WarmupSeconds;Impl->bPendingCommit=true;
    Impl->LastPhase=TEXT("Подготовка песочницы");return true;
}

void FMCPerkPreviewRunner::Tick(float DeltaSeconds)
{
    auto* W=GetWorld();if(!W || Impl->bPaused || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0.f) return;
    ++Impl->Clock.TickCalls;
    if(LastTickFrame==GFrameCounter){++Impl->Clock.SkippedSameFrameCalls;return;}
    LastTickFrame=GFrameCounter;
    Impl->Clock.LastInputDelta=DeltaSeconds;Impl->Clock.InputSeconds+=DeltaSeconds;
    float Remaining=FMath::Min(DeltaSeconds,MCPerkPreview::MaxCatchupSeconds);
    Impl->Clock.LastAdvancedDelta=Remaining;Impl->Clock.AdvancedSeconds+=Remaining;
    Impl->Clock.DroppedCatchupSeconds+=FMath::Max(0.f,DeltaSeconds-Remaining);
    // Consume ordinary slow Editor frames instead of turning every .2 s into
    // .033 s of permanent slow motion. Catch-up after a long stall stays bounded.
    // This is only the private sandbox; never increment GFrameCounter or tick
    // the user's editor/PIE world to force animation evaluation.
    while(Remaining>UE_SMALL_NUMBER) {
        const float Step=FMath::Min(Remaining,MCPerkPreview::MaxFrameStep);Remaining-=Step;
        TickStep(Step);++Impl->Clock.WorldSteps;
    }
}

void FMCPerkPreviewRunner::TickStep(float Step)
{
    auto* W=GetWorld();if(!W)return;
    W->Tick(LEVELTICK_All,Step);
    if(Impl->bPendingCommit) {
        Impl->Warmup=FMath::Max(0.f,Impl->Warmup-Step);
        if(Impl->Warmup>0.f) return;
        if(!Impl->bFactoryPrepared) {
            if(!Impl->PrepareAction()) {Impl->bPendingCommit=false;return;}
            Impl->bFactoryPrepared=true;
        }
        if(!Impl->ResourcesReady()) return;
        auto* A=Impl->Action.Get();
        const bool Active=Impl->AdapterEntry.Kind==EMCPerkPreviewAdapterKind::ActiveSkill;
        if(Active && (!A || !A->CanStart())) {Impl->Status=TEXT("Действие больше не может начаться; нажмите повтор после проверки состояния");Impl->bPendingCommit=false;return;}
        Impl->RunStartedAt=W->GetTimeSeconds();Impl->Elapsed=0;Impl->Events.Reset();Impl->AddEvent(TEXT("Prepared"));
        if(Active)A->StartCommitted();
        Impl->bPendingCommit=false;Impl->bRunning=true;Impl->bSawCommittedAction=Active;
        Impl->Status=TEXT("Выполняется настоящий сценарий: ")+Impl->PerkID.ToString();
        Impl->RefreshContext();if(Impl->Adapter)Impl->Adapter->OnCommitted(Impl->Context);
        Impl->LastPhase=Impl->Adapter?Impl->Adapter->GetPhaseName(Impl->Context):TEXT("Действие");
    }
    if(Impl->bRunning) {
        Impl->Elapsed=float(FMath::Max(0.,W->GetTimeSeconds()-Impl->RunStartedAt));
        Impl->RefreshContext();
        if(Impl->Adapter && Impl->FinishedAt<0) {
            Impl->Adapter->Tick(Impl->Context,Step);Impl->RefreshContext();
            Impl->LastPhase=Impl->Adapter->GetPhaseName(Impl->Context);
        }
        const bool PassiveComplete=Impl->AdapterEntry.Kind!=EMCPerkPreviewAdapterKind::ActiveSkill && Impl->Adapter && Impl->Adapter->IsComplete(Impl->Context);
        if(Impl->FinishedAt<0 && ((!Impl->Action.IsValid() && Impl->bSawCommittedAction) || PassiveComplete)) {
            Impl->bSawCommittedAction=false;Impl->FinishedAt=Impl->Elapsed;Impl->AddEvent(TEXT("ActionFinished"));Impl->LastPhase=TEXT("Действие завершено / хвост эффекта");
            if(Impl->AdapterEntry.Kind==EMCPerkPreviewAdapterKind::ActiveSkill)
                Impl->Status=TEXT("Действие завершено. Остаточные частицы доживают; можно повторить или сбросить");
        }
        // The native action owns its lifetime. A delayed composition layer or
        // still-live Niagara residue must finish before a repeat clears it.
        if(Impl->FinishedAt>=0) {
            bool bResidue=false;
            for(TActorIterator<AMCPerkVFXCompositionPlayer> It(W);It;++It)
                if(IsValid(*It) && It->IsRunning()){bResidue=true;break;}
            if(!bResidue)for(TObjectIterator<UNiagaraComponent> It;It;++It)
                if(IsValid(*It) && !It->IsTemplate() && It->GetWorld()==W && It->GetAsset() && !It->IsComplete())
                {bResidue=true;break;}
            const float Tail=Impl->Elapsed-Impl->FinishedAt;
            if(!bResidue && Tail>=(Impl->Adapter?Impl->Adapter->GetTailSeconds():3.f))Impl->bRunning=false;
            else if(Tail>=60.f) {
                Impl->bRunning=false;Impl->AddEvent(TEXT("PreviewTailSafetyLimit"));
                Impl->Status=TEXT("Предпросмотр остановлен: остаточный эффект не завершился за 60 секунд. Проверьте Niagara lifetime/loop; это защитный предел, а не успешное завершение.");
                for(TActorIterator<AMCPerkVFXCompositionPlayer> It(W);It;++It)if(IsValid(*It))It->StopEmission(true);
                for(TObjectIterator<UNiagaraComponent> It;It;++It)
                    if(IsValid(*It) && !It->IsTemplate() && It->GetWorld()==W)It->DeactivateImmediate();
            }
        }
    }
    USkyLightComponent::UpdateSkyCaptureContents(W);
}

void FMCPerkPreviewRunner::Release()
{Impl->RefreshContext();if(Impl->Adapter)Impl->Adapter->Release(Impl->Context);}
void FMCPerkPreviewRunner::Cancel()
{Impl->RefreshContext();if(Impl->Adapter)Impl->Adapter->Cancel(Impl->Context);Impl->bPendingCommit=false;Impl->bRunning=false;}

void FMCPerkPreviewRunner::SetPaused(bool bPaused) {Impl->bPaused=bPaused;}
bool FMCPerkPreviewRunner::IsPaused() const {return Impl->bPaused;}
bool FMCPerkPreviewRunner::IsRunning() const {return Impl->bPendingCommit || Impl->bRunning;}
UWorld* FMCPerkPreviewRunner::GetWorld() const {return Impl->World();}
FPreviewScene* FMCPerkPreviewRunner::GetPreviewScene() const {return nullptr;}
AMCToothCharacter* FMCPerkPreviewRunner::GetHero() const {return Impl->Hero.Get();}
AMCActiveSkillAction* FMCPerkPreviewRunner::GetAction() const {return Impl->Action.Get();}
FString FMCPerkPreviewRunner::GetStatus() const {return Impl->Status;}
FString FMCPerkPreviewRunner::GetPhaseName() const {return Impl->LastPhase;}
float FMCPerkPreviewRunner::GetElapsed() const {return Impl->Elapsed;}
EMCPerkPreviewScenario FMCPerkPreviewRunner::GetScenario() const {return Impl->Scenario;}
FName FMCPerkPreviewRunner::GetPerkID() const {return Impl->PerkID;}
const TArray<FMCPerkPreviewEvent>& FMCPerkPreviewRunner::GetEvents() const {return Impl->Events;}
FMCPerkPreviewClockStats FMCPerkPreviewRunner::GetClockStats() const {return Impl->Clock;}
FString FMCPerkPreviewRunner::GetEventLog() const
{
    FString Result;
    for(const auto& Event:Impl->Events) Result+=FString::Printf(TEXT("%.2f с  ·  %s\n"),Event.Seconds,*Event.Stage.ToString());
    return Result.IsEmpty()?TEXT("Событий ещё нет"):Result;
}
