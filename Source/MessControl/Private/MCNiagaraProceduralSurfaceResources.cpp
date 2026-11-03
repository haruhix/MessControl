#include "MCNiagaraProceduralSurfaceResources.h"
#include "MCNiagaraProceduralSurfaceComponent.h"
#include "MCNiagaraProceduralSurfaceRenderer.h"
#include "MCGameState.h"
#include "NiagaraComponent.h"
#if WITH_EDITOR
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#endif
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraParameterStore.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "RenderCommandFence.h"
#include "ShaderPipelineCache.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace MCNativeSurfacePreparation
{
const UMCNiagaraProceduralSurfaceComponent* Template(const UNiagaraSystem* System,FString* Error=nullptr)
{
    const UMCNiagaraProceduralSurfaceComponent* Found=nullptr;
    if(System)for(const auto& Handle:System->GetEmitterHandles()) {
        const auto* Data=Handle.GetInstance().GetEmitterData();if(!Data)continue;
        for(auto* R:Data->GetRenderers()) {
            const auto* Owned=Cast<UMCNiagaraProceduralSurfaceRendererProperties>(R);
            const auto* Legacy=Cast<UNiagaraComponentRendererProperties>(R);
            const auto* T=Owned?Owned->TemplateComponent.Get():Legacy?Cast<UMCNiagaraProceduralSurfaceComponent>(Legacy->TemplateComponent):nullptr;
            if(T) {
                if(Found||Data->SimTarget!=ENiagaraSimTarget::CPUSim||Data->bLocalSpace) {
                    if(Error)*Error=TEXT("One original surface needs exactly one world-space CPU ComponentRenderer.");return nullptr;
                }
                Found=T;
            }
        }
    }
    return Found;
}
void Subject(UNiagaraComponent* C,UProceduralMeshComponent* Value)
{
    auto& Store=C->GetOverrideParameters();const FNiagaraVariable K(FNiagaraTypeDefinition(USceneComponent::StaticClass()),TEXT("User.VisualSubject"));
    if(Store.IndexOf(K)==INDEX_NONE)Store.AddParameter(K);Store.SetUObject(Value,K);
}
}

UMCNiagaraProceduralSurfaceResources::UMCNiagaraProceduralSurfaceResources()=default;
UMCNiagaraProceduralSurfaceResources::~UMCNiagaraProceduralSurfaceResources()=default;
UWorld* UMCNiagaraProceduralSurfaceResources::GetWorld()const{return PreparedWorld.Get();}
bool UMCNiagaraProceduralSurfaceResources::Uses(const UNiagaraSystem* NS)
{return MCNativeSurfacePreparation::Template(NS)!=nullptr;}
FName UMCNiagaraProceduralSurfaceResources::SourceFor(const UNiagaraSystem* NS)
{const auto* T=MCNativeSurfacePreparation::Template(NS);return T?T->SourceID:NAME_None;}
bool UMCNiagaraProceduralSurfaceResources::IsCurrent()const
{
    const auto* World=PreparedWorld.Get();if(!World||!PreparedEpoch.IsValid()||World->bIsTearingDown)return false;
    const auto* State=World->GetGameState<AMCGameState>();
    return State&&State->GameplayLoadId.IsValid()?State->GameplayLoadId==PreparedEpoch:World->GetOutermost()==GetTransientPackage();
}

bool UMCNiagaraProceduralSurfaceResources::Prepare(UWorld* World,FGuid Epoch,const TArray<UObject*>& Loaded,int32 TongueTriangles,FString& Error)
{
    Error.Reset();
    if(!World||!Epoch.IsValid()||TongueTriangles<0||TongueTriangles>26214)
    {Error=TEXT("Surface preparation requires a bounded actual location tongue topology and epoch.");return false;}
    TArray<UNiagaraSystem*> Systems;
    for(auto* O:Loaded)if(auto* NS=Cast<UNiagaraSystem>(O);Uses(NS))Systems.AddUnique(NS);
    if(PreparedWorld.IsValid()) {
        bool Same=bPreparationSucceeded&&PreparedWorld.Get()==World&&PreparedEpoch==Epoch&&IsCurrent()&&Systems.Num()==PreparedSystems.Num();
        for(auto* NS:Systems)Same&=PreparedSystems.Contains(NS);
        if(!Same)Error=TEXT("A failed or changed surface preparation requires explicit scope Reset; never certify its prefix.");
        return Same;
    }
    PreparedWorld=World;PreparedEpoch=Epoch;PreparationStarted=FPlatformTime::Seconds();
    if(!TongueTriangles)UE_LOG(LogTemp,Display,TEXT("MC_SURFACE_PREPARE_NO_NATIVE_TONGUE: flat800-triangle reserve only; clipped-tongue bank is unavailable."));
    for(auto* NS:Systems)PreparedSystems.Add(NS);
    for(auto* NS:Systems) {
        const auto* T=MCNativeSurfacePreparation::Template(NS,&Error);if(!T)return false;
        bool OwnedRenderer=false;
        for(const auto& Handle:NS->GetEmitterHandles())if(const auto* Data=Handle.GetEmitterData())
            for(auto* Renderer:Data->GetRenderers())if(const auto* Owned=Cast<UMCNiagaraProceduralSurfaceRendererProperties>(Renderer))
                OwnedRenderer|=Owned->TemplateComponent==T;
        if(!OwnedRenderer){Error=TEXT("Upgrade the original surface renderer before location preparation; legacy stock pools cannot retain a draw across GC.");return false;}
        if(T->SmallPreparedInstances<1||T->SmallPreparedInstances>64||T->LargePreparedInstances<0||T->LargePreparedInstances>32)
        {Error=TEXT("Surface template declares an unsupported finite concurrent reserve.");return false;}
        const int32 Small=T->SourceID==TEXT("World.Ice.Surface")?TongueTriangles:800;
        const int32 Large=5*TongueTriangles;
        if(Small<=0||Small>T->MaximumTriangles||(T->LargePreparedInstances&&Large>T->MaximumTriangles))
        {Error=TEXT("Surface template capacity cannot hold this location's original native topology.");return false;}
        for(int32 Kind=0;Kind<2;++Kind)for(int32 I=0;I<(Kind?(TongueTriangles>0?T->LargePreparedInstances:0):T->SmallPreparedInstances);++I) {
            const int32 Capacity=Kind?Large:Small;
            FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Host=World->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,Spawn);
            if(!Host){Error=TEXT("Surface preparation presentation owner allocation failed.");return false;}
            PresentationActors.Add(Host);Host->SetActorEnableCollision(false);Host->SetActorTickEnabled(false);Host->SetReplicates(false);
            auto* Root=NewObject<USceneComponent>(Host,NAME_None,RF_Transient);Host->SetRootComponent(Root);Host->AddInstanceComponent(Root);Root->RegisterComponent();
            auto* C=NewObject<UNiagaraComponent>(Host,NAME_None,RF_Transient);
            if(!C){Error=TEXT("Surface controller preparation allocation failed.");return false;}
            Retained.Add(C);C->SetAutoActivate(false);C->SetAutoDestroy(false);C->SetAsset(NS);
            C->SetupAttachment(Host->GetRootComponent());Host->AddInstanceComponent(C);
            C->SetForceSolo(true);C->SetAllowScalability(false);C->SetTickBehavior(ENiagaraTickBehavior::UsePrereqs);
            C->SetAbsolute(true,true,true);C->SetWorldTransform(FTransform::Identity);
            C->SetVariableBool(TEXT("User.SurfacePreparing"),true);C->SetVariableInt(TEXT("User.SurfaceTriangleCapacity"),Capacity);
            MCNativeSurfacePreparation::Subject(C,nullptr);
            C->RegisterComponentWithWorld(World);
            // Location owns this exact draw before any particle is activated.
            // Renderer/proxy rebuilds never allocate or destroy it.
            auto* Draw=DuplicateObject<UMCNiagaraProceduralSurfaceComponent>(T,Host);
            Draw->ClearFlags(RF_ArchetypeObject);Draw->SetFlags(RF_Transient);
            Draw->SetupAttachment(C);Host->AddInstanceComponent(Draw);Draw->RegisterComponentWithWorld(World);Draw->SetComponentTickEnabled(true);
            PreparedDraws.Add(Draw);
            FLease L;L.System=NS;L.Component=C;L.Host=Host;L.Draw=Draw;L.SourceID=T->SourceID;L.Capacity=Capacity;Leases.Add(L);
            C->Activate(true);C->AdvanceSimulation(2,1.f/60.f);
        }
    }
    SubmittedFrame=GFrameCounter;bPreparationSucceeded=true;return true;
}

bool UMCNiagaraProceduralSurfaceResources::Poll(bool& Waiting,FString& Error)
{
    Waiting=true;Error.Reset();if(!IsCurrent()||!bPreparationSucceeded){Error=TEXT("Surface preparation failed or belongs to a retired world/load; explicit Reset is required.");return false;}
    if(bReady){Waiting=false;return true;}
    if(Leases.IsEmpty()){bReady=true;Waiting=false;return true;}
    if(GFrameCounter<=SubmittedFrame+1)return true;
    for(auto& L:Leases) {
        auto* C=L.Component.Get();if(!IsValid(C)){Error=TEXT("Actual surface controller was lost before readiness.");return false;}
        TArray<USceneComponent*> Children;C->GetChildrenComponents(false,Children);
        int32 Count=0;UMCNiagaraProceduralSurfaceComponent* Draw=nullptr;
        for(auto* Child:Children)if(auto* Surface=Cast<UMCNiagaraProceduralSurfaceComponent>(Child)){Draw=Surface;++Count;}
        if(!Count) {
            if(FPlatformTime::Seconds()-PreparationStarted>10.) {
                Error=TEXT("Actual ComponentRenderer did not create its prepared surface draw within10seconds: ")+GetPathNameSafe(L.System.Get());return false;
            }
            return true; // Actual ComponentRenderer must execute its own game-thread creation.
        }
        if(Count!=1||!Draw->IsDrawPrepared()||Draw->GetPreparedTriangleCapacity()!=L.Capacity) {
            Error=Draw?Draw->GetSurfaceError():TEXT("Surface renderer did not create its exact prepared draw.");return false;
        }
        L.Draw=Draw;
        if(Draw->IsPSOPrecaching())return true;
        C->SetPaused(true);C->SetVariableBool(TEXT("User.SurfacePreparing"),false);Draw->ReleasePreparedSubject();
    }
    // Variant precaching replaces the component's current request list. Await
    // all pending runtime PSOs as the normal location barrier does, including
    // authored variants other than the material restored on the prepared draw.
    if(FShaderPipelineCache::NumPrecompilesRemaining()>0){Fence.Reset();return true;}
    if(!Fence){Fence=MakeUnique<FRenderCommandFence>();Fence->BeginFence();return true;}
    if(!Fence->IsFenceComplete())return true;
    bReady=true;Waiting=false;return true;
}

bool UMCNiagaraProceduralSurfaceResources::IsReadyForPlayback(const UWorld* World)const
{return bReady&&PreparedWorld.Get()==World&&IsCurrent();}
UNiagaraComponent* UMCNiagaraProceduralSurfaceResources::Acquire(UNiagaraSystem* NS,UProceduralMeshComponent* Subject,FName ID,FGuid Episode,FString& Error)
{
    if(!IsReadyForPlayback(Subject?Subject->GetWorld():nullptr)||!Subject||Subject->GetNumSections()!=1)
    {Error=TEXT("Actual surface needs this world's completed scoped draw preparation.");return nullptr;}
    const auto* Section=Subject->GetProcMeshSection(0);const int32 Needed=Section?Section->ProcIndexBuffer.Num()/3:0;
    FLease* Best=nullptr;
    for(auto& L:Leases)if(!L.bClaimed&&L.System.Get()==NS&&L.SourceID==ID&&L.Capacity>=Needed)
        if(!Best||L.Capacity<Best->Capacity)Best=&L;
    if(!Best||Needed<=0){Error=TEXT("No exact prewarmed surface lease is available; retain native artwork.");return nullptr;}
    auto* C=Best->Component.Get();auto* Draw=Best->Draw.Get();
    if(!IsValid(C)||!IsValid(Draw)||!C->IsPaused())return nullptr;
    MCNativeSurfacePreparation::Subject(C,Subject);
    if(!Draw->BindPreparedSubject(Subject,Error)){MCNativeSurfacePreparation::Subject(C,nullptr);Draw->ReleasePreparedSubject();return nullptr;}
    Best->Subject=Subject;Best->Episode=Episode;Best->bClaimed=true;return C; // Player applies context, then resumes this same particle/controller.
}
bool UMCNiagaraProceduralSurfaceResources::Owns(const UNiagaraComponent* C)const
{return C&&Leases.ContainsByPredicate([&](const auto& L){return L.Component.Get()==C;});}
bool UMCNiagaraProceduralSurfaceResources::Release(UNiagaraComponent* C)
{
    auto* L=Leases.FindByPredicate([&](const auto& Entry){return Entry.Component.Get()==C;});if(!L)return false;
    C->SetPaused(true);if(auto* Draw=L->Draw.Get())Draw->ReleasePreparedSubject();
    MCNativeSurfacePreparation::Subject(C,nullptr);L->Subject.Reset();L->Episode.Invalidate();L->bClaimed=false;return true;
}
bool UMCNiagaraProceduralSurfaceResources::HasAcceptedDraw(UProceduralMeshComponent* Subject,FName ID,FGuid Episode)const
{
    if(!IsReadyForPlayback(Subject?Subject->GetWorld():nullptr))return false;
    for(const auto& L:Leases)if(L.bClaimed&&L.Subject.Get()==Subject&&L.SourceID==ID&&(!Episode.IsValid()||L.Episode==Episode)) {
        auto* C=L.Component.Get();auto* Draw=L.Draw.Get();FString Error;
        if(IsValid(C)&&!C->IsPaused()&&IsValid(Draw)&&Draw->IsRendererEligible()&&Draw->IsVisible()&&Draw->CanDrawSubject(Subject,Error))return true;
    }
    return false;
}
void UMCNiagaraProceduralSurfaceResources::GetResources(TArray<UObject*>& Out)const
{
    for(const auto& L:Leases) {
        if(auto* NS=L.System.Get())Out.AddUnique(NS);
        if(auto* Draw=L.Draw.Get()){Out.AddUnique(Draw);TArray<UMaterialInterface*> Materials;Draw->GetPreparedMaterials(Materials);for(auto* M:Materials)Out.AddUnique(M);}
    }
}
FString UMCNiagaraProceduralSurfaceResources::GetPreparationStateJson()const
{
    auto Root=MakeShared<FJsonObject>();Root->SetBoolField(TEXT("ready"),bReady&&IsCurrent());
    Root->SetStringField(TEXT("world"),GetPathNameSafe(PreparedWorld.Get()));Root->SetStringField(TEXT("epoch"),PreparedEpoch.ToString());
    TArray<TSharedPtr<FJsonValue>> Rows;int64 Total=0;
    for(const auto& L:Leases) {
        auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("system"),GetPathNameSafe(L.System.Get()));
        R->SetStringField(TEXT("component"),GetPathNameSafe(L.Component.Get()));R->SetStringField(TEXT("draw"),GetPathNameSafe(L.Draw.Get()));
        R->SetStringField(TEXT("source_id"),L.SourceID.ToString());R->SetStringField(TEXT("subject"),GetPathNameSafe(L.Subject.Get()));
        R->SetNumberField(TEXT("triangle_capacity"),L.Capacity);R->SetBoolField(TEXT("claimed"),L.bClaimed);Total+=L.Capacity;
#if WITH_EDITOR
        // Same synchronized public-instance inspection as Composer diagnostics.
        // No activation, reset, compilation, resource allocation or graph reads.
        if(auto* C=L.Component.Get();IsValid(C)) {
            R->SetBoolField(TEXT("paused"),C->IsPaused());
            R->SetBoolField(TEXT("component_tick_enabled"),C->IsComponentTickEnabled());
            R->SetBoolField(TEXT("component_active"),C->IsActive());
            R->SetBoolField(TEXT("asset_ready_to_run"),C->GetAsset()&&C->GetAsset()->IsReadyToRun());
            auto Controller=C->GetSystemInstanceController();
            R->SetBoolField(TEXT("controller_available"),Controller.IsValid());
            if(Controller) {
                Controller->WaitForConcurrentTickAndFinalize();
                auto* Instance=Controller->GetSystemInstance_Unsafe();
                R->SetBoolField(TEXT("instance_available"),Instance!=nullptr);
                if(Instance) {
                    R->SetNumberField(TEXT("instance_age"),Instance->GetAge());
                    R->SetNumberField(TEXT("instance_tick_count"),Instance->GetTickCount());
                    R->SetNumberField(TEXT("actual_execution_state"),int32(Instance->GetActualExecutionState()));
                    R->SetNumberField(TEXT("requested_execution_state"),int32(Instance->GetRequestedExecutionState()));
                    R->SetBoolField(TEXT("instance_ready_to_run"),Instance->IsReadyToRun());
                    R->SetBoolField(TEXT("pending_spawn"),Instance->IsPendingSpawn());
                    TArray<TSharedPtr<FJsonValue>> Emitters;int32 Index=0;
                    for(const auto& Emitter:Instance->GetEmitters()) {
                        auto E=MakeShared<FJsonObject>();
                        E->SetNumberField(TEXT("index"),Index);
                        E->SetStringField(TEXT("name"),Emitter->GetEmitterHandle().GetName().ToString());
                        E->SetNumberField(TEXT("execution_state"),int32(Emitter->GetExecutionState()));
                        E->SetNumberField(TEXT("particles"),Emitter->GetNumParticles());
                        E->SetNumberField(TEXT("total_spawned_particles"),Emitter->GetTotalSpawnedParticles());
                        E->SetBoolField(TEXT("should_tick"),Emitter->ShouldTick());
                        E->SetBoolField(TEXT("waiting_for_pso"),Emitter->IsWaitingForPSOCaching());
                        const auto* Data=Emitter->GetVersionedEmitter().GetEmitterData();
                        E->SetBoolField(TEXT("stateful_data_available"),Data!=nullptr);
                        if(Data)E->SetBoolField(TEXT("stateful_data_ready_to_run"),Data->IsReadyToRun());
                        // InitEmitters ends by allocating both parameter frames.
                        // Avoid claiming a current engine age before any actual tick.
                        const bool HasEngineFrame=Instance->GetTickCount()>0;
                        E->SetBoolField(TEXT("engine_parameters_age_available"),HasEngineFrame);
                        if(HasEngineFrame) {
                            const auto& Parameters=Instance->GetEmitterParameters(Index);
                            E->SetNumberField(TEXT("engine_parameters_age"),Parameters.EmitterAge);
                            E->SetNumberField(TEXT("engine_spawn_count_scale"),Parameters.EmitterSpawnCountScale);
                        }
                        Emitters.Add(MakeShared<FJsonValueObject>(E));++Index;
                    }
                    R->SetArrayField(TEXT("emitters"),Emitters);
                }
            }
        }
#endif
        Rows.Add(MakeShared<FJsonValueObject>(R));
    }
    Root->SetNumberField(TEXT("total_triangle_capacity"),double(Total));Root->SetArrayField(TEXT("leases"),Rows);
    FString Out;const auto Writer=TJsonWriterFactory<>::Create(&Out);FJsonSerializer::Serialize(Root,Writer);return Out;
}
void UMCNiagaraProceduralSurfaceResources::Reset()
{
    for(UNiagaraComponent* C:Retained)if(IsValid(C)){C->DeactivateImmediate();C->DestroyComponent();}
    Retained.Reset();PreparedDraws.Reset();Leases.Reset();Fence.Reset();PreparedWorld.Reset();PreparedEpoch.Invalidate();bReady=false;
    for(AActor* Host:PresentationActors)if(IsValid(Host))Host->Destroy();PresentationActors.Reset();PreparedSystems.Reset();bPreparationSucceeded=false;
}

void UMCNiagaraProceduralSurfaceResources::UpdateRendererEligibility(UNiagaraComponent* Component,bool Enabled)
{
    if(!IsCurrent())return;
    if(auto* Lease=Leases.FindByPredicate([&](const auto& L){return L.Component.Get()==Component;}))
        if(auto* Draw=Lease->Draw.Get())Draw->SetRendererEligibility(Enabled);
}
