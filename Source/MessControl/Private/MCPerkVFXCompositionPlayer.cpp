#include "MCPerkVFXCompositionPlayer.h"
#include "MCCoffeeRiverPresentation.h"
#include "MCNiagaraProceduralSurfaceResources.h"
#include "ProceduralMeshComponent.h"
#include "MCGameState.h"
#include "MCVFXGameplayInputs.h"
#include "MCNiagaraMaterialContextComponent.h"
#include "MCWorldPreloadSubsystem.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraScript.h"
#include "UObject/Package.h"
#include "NiagaraTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPerkComposition,Log,All);

namespace MCPerkComposition
{
constexpr float SafetySeconds=60.f;
double WorldClock(const UWorld* World)
{const auto* State=World?World->GetGameState():nullptr;return State?State->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.;}
// IsReadyToRun calls EnsureFullyLoaded before testing script availability.
// A gallery draft/fault can own default script objects without a source graph;
// reject that data without entering Niagara's editor post-load assertions.
bool ReadySource(UNiagaraSystem* System,FString& Error)
{
    if(!IsValid(System)||System->IsTemplate()
        ||System->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed))
    {Error=TEXT("The Niagara source is unavailable or not initialized.");return false;}
    auto* Spawn=System->GetSystemSpawnScript();auto* Update=System->GetSystemUpdateScript();
    if(!IsValid(Spawn)||!IsValid(Update))
    {Error=TEXT("The Niagara source has no initialized system scripts.");return false;}
#if WITH_EDITORONLY_DATA
    if(!System->GetOutermost()->bIsCookedForEditor
        &&(!Spawn->GetLatestSource()||!Update->GetLatestSource()||Spawn->GetLatestSource()!=Update->GetLatestSource()))
    {Error=TEXT("The editable Niagara source has no consistent initialized system graph.");return false;}
#endif
    return System->IsReadyToRun();
}
bool Has(const UNiagaraSystem* System,FName Name,const FNiagaraTypeDefinition& Type)
{return System && System->GetExposedParameters().IndexOf(FNiagaraVariable(Type,Name))!=INDEX_NONE;}
void Overrides(UNiagaraComponent* C,const FMCVFXCompositionLayer& L)
{
    for(const auto& V:L.Floats) if(V.bOverride&&!MCVFXGameplayInputs::IsReadOnly(V.Name)) C->SetVariableFloat(V.Name,V.Value);
    for(const auto& V:L.Colors) if(V.bOverride&&!MCVFXGameplayInputs::IsReadOnly(V.Name)) C->SetVariableLinearColor(V.Name,V.Value);
    for(const auto& V:L.Vectors) if(V.bOverride&&!MCVFXGameplayInputs::IsReadOnly(V.Name)) C->SetVariableVec3(V.Name,V.Value);
    for(const auto& V:L.Bools) if(V.bOverride&&!MCVFXGameplayInputs::IsReadOnly(V.Name)) C->SetVariableBool(V.Name,V.Value);
}
}

AMCPerkVFXCompositionPlayer::AMCPerkVFXCompositionPlayer()
{
    bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("EventFrame")));
}

bool AMCPerkVFXCompositionPlayer::StartPrepared(const FMCPreparedPerkVFXComposition& Prepared,const FTransform& Frame,
    USceneComponent* Subject,UNiagaraComponent* LoopAnchor,bool bHostLoop,const FMCPerkVFXGameplayValues& Values,FString& Error,
    float InitialElapsedSeconds,AActor* PersistentOwner)
{
    Error.Reset();
    if(bFreezePrewarmed)return StartPrewarmedFreeze(Prepared,Frame,Subject,Values,InitialElapsedSeconds,Error);
    if(Values.FreezeCapture){Error=TEXT("Captured rows need their exact prepared per-layer admission, never an ordinary player restart.");return false;}
    if(bRunning || !Prepared.Snapshot || Prepared.Systems.Num()!=Prepared.Snapshot->Layers.Num() || Frame.ContainsNaN()
        || !FMath::IsFinite(InitialElapsedSeconds) || InitialElapsedSeconds<0 || (!PersistentOwner&&InitialElapsedSeconds>=MCPerkComposition::SafetySeconds)
        || (PersistentOwner && (!IsValid(PersistentOwner) || PersistentOwner->GetWorld()!=GetWorld())))
    {Error=TEXT("Independent VFX player requires a valid immutable prepared snapshot.");return false;}
    if(!Values.NativeInputs.Validate(Error))return false;
    for(int32 I=0;I<Prepared.Snapshot->Layers.Num();++I)
    {
        const auto& Layer=Prepared.Snapshot->Layers[I];if(!Layer.bEnabled) continue;
        if(!Prepared.Systems[I]) {Error=TEXT("A composition layer was not prepared before its first event.");return false;}
        if(!Values.NativeInputs.ValidateForSystem(Prepared.Systems[I],Error,Values.FreezeCapture.Get()))return false;
        if(FMCCoffeeRiverPresentation::Uses(Prepared.Systems[I]) && (!Values.RiverSurface||!Values.RiverSurface->Validate(Prepared.Systems[I],GetWorld(),Error)))return false;
        if(Layer.Binding==EMCVFXCompositionBinding::Subject && (!IsValid(Subject) || Subject->GetWorld()!=GetWorld()
            || (!Layer.Socket.IsNone() && !Subject->DoesSocketExist(Layer.Socket))))
        {Error=FString::Printf(TEXT("Layer %s needs a valid subject/socket in this world."),*Layer.Name.ToString());return false;}
    }
    RejectedLayerAdmissions.Reset();SurfaceAdmissionError.Reset();
    Data=Prepared;Gameplay=Values;SubjectComponent=Subject;NativeLoopAnchor=LoopAnchor;bHostDrivenLoop=bHostLoop;
    NativeFrameComponent=Values.FrameComponent;NativeFrameSocket=Values.FrameSocket;bHasNativeFrame=NativeFrameComponent.IsValid();
    if(auto* Native=NativeFrameComponent.Get()) {
        if(Native->GetWorld()!=GetWorld() || (!NativeFrameSocket.IsNone()&&!Native->DoesSocketExist(NativeFrameSocket)))
        {Error=TEXT("Native frame must resolve the actual scene component/socket in this world.");return false;}
        NativeFrameOffset=Frame.GetRelativeTransform(Native->GetSocketTransform(NativeFrameSocket,RTS_World));
        AddTickPrerequisiteComponent(Native);
    }
    RetainedCapturedMeshes=Values.CapturedMeshes;PersistentStateOwner=PersistentOwner;bPersistentState=PersistentOwner!=nullptr;
    NativeClockSampleAt=MCPerkComposition::WorldClock(GetWorld());
    SetActorTransform(Frame);LastAnchor=Frame;
    if(LoopAnchor) AddTickPrerequisiteComponent(LoopAnchor);
    Components.SetNum(Data.Systems.Num());Started.Init(false,Components.Num());Stopped.Init(false,Components.Num());
    NativeInputStates.SetNum(Components.Num());PlaybackError.Reset();
    ContextLayers.Init(false,Components.Num());bool NeedsContext=false,RequiresContext=false;
    for(int32 I=0;I<Components.Num();++I)if(Data.Snapshot->Layers[I].bEnabled) {
        ContextLayers[I]=UMCNiagaraMaterialContextResources::NeedsMaterialContext(Data.Systems[I]);
        NeedsContext|=ContextLayers[I];
        RequiresContext|=ContextLayers[I]&&!UMCNiagaraMaterialContextResources::IsArenaToothPearlCoat(Data.Systems[I]);
    }
    Age=InitialElapsedSeconds;bSeekToServerAge=InitialElapsedSeconds>1.f/60.f;
    if(NeedsContext) {
        const auto* Loading=GetWorld()?GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr;
        auto* Resources=Loading?Loading->GetMaterialContextResources():nullptr;
        const bool ContextReady=Resources&&Resources->IsReadyForPlayback(GetWorld());
        if(!ContextReady&&RequiresContext) {
            Error=TEXT("Material-context resources must cross this world's readiness barrier before playback.");return false;
        }
        if(ContextReady)MaterialContextResources=Resources;
        // Reserve every participating layer, including delayed layers, before
        // the first Activate. A failed candidate cannot hide native artwork.
        for(int32 I=0;I<Components.Num();++I) {
            const auto& L=Data.Snapshot->Layers[I];
            if(ContextLayers[I]&&!ContextReady) {
                Started[I]=Stopped[I]=true;
                UE_LOG(LogMCPerkComposition,Warning,TEXT("Pearl Coat layer %s skipped: world material context is not ready. Independent layers continue."),*L.Name.ToString());
                continue;
            }
            if(L.bEnabled && (L.EmissionSeconds<=0 || Age<L.StartSeconds+L.EmissionSeconds)
                && !CreateLayerInactive(I)) {
                if(RejectedLayerAdmissions.Contains(I)){Started[I]=Stopped[I]=true;continue;}
                Error=PlaybackError;StopEmission(true);return false;
            }
        }
    }
    StopAge=0;bStopping=false;bRunning=true;SetActorTickEnabled(true);Advance(0);
    if(!PlaybackError.IsEmpty()) {Error=PlaybackError;return false;}
    return true;
}

void AMCPerkVFXCompositionPlayer::StartLayer(int32 Index)
{
    const auto& Layer=Data.Snapshot->Layers[Index];Started[Index]=true;
    if(!Layer.bEnabled) {Stopped[Index]=true;return;}
    auto* Subject=SubjectComponent.Get();
    if(Layer.Binding==EMCVFXCompositionBinding::Subject && (!Subject || (!Layer.Socket.IsNone() && !Subject->DoesSocketExist(Layer.Socket))))
    {Stopped[Index]=true;ReleaseLayer(Index);UE_LOG(LogMCPerkComposition,Warning,TEXT("Canceled VFX layer %s: subject/socket disappeared before delayed start."),*Layer.Name.ToString());return;}
    if(!CreateLayerInactive(Index)) {
        if(RejectedLayerAdmissions.Contains(Index)){Stopped[Index]=true;return;}
        StopEmission(true);return;
    }
    auto* C=Components[Index].Get();
    if(bFreezePrewarmed&&FreezeCapturedLayers.IsValidIndex(Index)&&FreezeCapturedLayers[Index]) {
        if(!ApplyValues(Index)){StopEmission(true);return;}
        C->SetPaused(false);return; // Existing primed particles; never Activate/reset.
    }
    if(!ApplyValues(Index)) {StopEmission(true);return;}
    if(auto* Surface=ProceduralSurfaceResources.Get();Surface&&Surface->Owns(C)){C->SetPaused(false);return;}
    if(!C->IsRegistered())C->RegisterComponentWithWorld(GetWorld());C->Activate(true);
    if(bSeekToServerAge) C->SeekToDesiredAge(FMath::Max(0.f,Age-Layer.StartSeconds));
}

bool AMCPerkVFXCompositionPlayer::CreateLayerInactive(int32 Index)
{
    if(IsValid(Components[Index]))return true;
    const auto& Layer=Data.Snapshot->Layers[Index];auto* Subject=SubjectComponent.Get();
    UNiagaraComponent* C=nullptr;
    const bool SurfaceLayer=UMCNiagaraProceduralSurfaceResources::Uses(Data.Systems[Index]);
    if(SurfaceLayer) {
        auto* Loading=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();auto* Resources=Loading?Loading->GetProceduralSurfaceResources():nullptr;
        auto* Native=Cast<UProceduralMeshComponent>(Gameplay.VisualSubject.IsValid()?Gameplay.VisualSubject.Get():Subject);
        FString SurfaceError;
        C=Resources?Resources->Acquire(Data.Systems[Index],Native,UMCNiagaraProceduralSurfaceResources::SourceFor(Data.Systems[Index]),Gameplay.EpisodeID,SurfaceError):nullptr;
        if(!C) {
            if(SurfaceError.IsEmpty())SurfaceError=TEXT("This original surface was not prepared before the world barrier.");
            SurfaceAdmissionError=SurfaceError;RejectedLayerAdmissions.Add(Index);
            UE_LOG(LogMCPerkComposition,Warning,TEXT("Original surface layer %s rejected: %s. Independent ordinary rows remain; native draw is the compatibility fallback."),
                *Layer.Name.ToString(),*SurfaceError);
            return false;
        }
        ProceduralSurfaceResources=Resources;
    } else C=FMCCoffeeRiverPresentation::Uses(Data.Systems[Index])?NewObject<UMCCoffeeRiverNiagaraComponent>(this,NAME_None,RF_Transient):NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient);
    if(!SurfaceLayer)C->SetAutoActivate(false);C->SetAutoDestroy(false);C->SetAsset(Data.Systems[Index]);
    C->SetTickBehavior(ENiagaraTickBehavior::UsePrereqs);
    C->AddTickPrerequisiteActor(this);
    if(bSeekToServerAge&&!SurfaceLayer) {
        // Late replicated receipts catch up invisibly; never replay a fresh burst.
        // Ordinary active playback keeps its original TickDeltaTime path.
        C->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
        C->SetCanRenderWhileSeeking(false);C->SetMaxSimTime(.002f);
    }
    if(SurfaceLayer){C->SetAbsolute(true,true,true);C->SetWorldTransform(FTransform::Identity);}
    else if(Layer.Binding==EMCVFXCompositionBinding::Subject) {C->SetupAttachment(Subject,Layer.Socket);C->SetRelativeTransform(Layer.Transform);}
    else if(Layer.Binding==EMCVFXCompositionBinding::Composition) {C->SetupAttachment(GetRootComponent());C->SetRelativeTransform(Layer.Transform);}
    else C->SetWorldTransform(Layer.Transform*LastAnchor);
    if(bFreezePreparing&&FreezeCapturedLayers.IsValidIndex(Index)&&FreezeCapturedLayers[Index]){C->SetAbsolute(true,true,true);C->SetWorldTransform(FTransform(FQuat::Identity,Gameplay.FreezeCapture->GetPreparationOrigin()));}
    Components[Index]=C;
    if(!SurfaceLayer)AddInstanceComponent(C);
    if(!ApplyValues(Index)) {ReleaseLayer(Index);return false;}
    if(ContextLayers[Index]) {
        auto* Resources=MaterialContextResources.Get();
        FString ContextError;
        if(!Resources || !Resources->ReserveActivation(C,ContextError)) {
            ReleaseLayer(Index);
            if(UMCNiagaraMaterialContextResources::IsArenaToothPearlCoat(Data.Systems[Index])) {
                RejectedLayerAdmissions.Add(Index);
                UE_LOG(LogMCPerkComposition,Warning,TEXT("Pearl Coat layer %s skipped: %s. Independent layers continue."),*Layer.Name.ToString(),*ContextError);
            } else PlaybackError=MoveTemp(ContextError);
            return false;
        }
    }
    return true;
}

void AMCPerkVFXCompositionPlayer::ReleaseLayer(int32 Index)
{
    if(auto* C=Components.IsValidIndex(Index)?Components[Index].Get():nullptr;IsValid(C)) {
        if(auto* Resources=MaterialContextResources.Get())Resources->ReleaseActivation(C);
        if(auto* Surface=ProceduralSurfaceResources.Get();Surface&&Surface->Owns(C)) {
            C->RemoveTickPrerequisiteActor(this);Surface->Release(C);
        } else {C->DeactivateImmediate();C->DestroyComponent();}
        Components[Index]=nullptr;
    }
    if(NativeInputStates.IsValidIndex(Index))NativeInputStates[Index].Reset();
    if(FreezeLayers.IsValidIndex(Index))FreezeLayers[Index].Reset();
}

bool AMCPerkVFXCompositionPlayer::ApplyValues(int32 Index)
{
    auto* C=Components.IsValidIndex(Index)?Components[Index].Get():nullptr;
    if(!IsValid(C)) return true;
    const auto* NS=Data.Systems[Index].Get();
    using namespace MCPerkComposition;
    if(Gameplay.Radius>0 && Has(NS,TEXT("User.Radius"),FNiagaraTypeDefinition::GetFloatDef())) C->SetVariableFloat(TEXT("User.Radius"),Gameplay.Radius);
    if(!Gameplay.Direction.IsNearlyZero() && Has(NS,TEXT("User.Direction"),FNiagaraTypeDefinition::GetVec3Def())) C->SetVariableVec3(TEXT("User.Direction"),Gameplay.Direction.GetSafeNormal());
    if(Gameplay.Fill>=0 && Has(NS,TEXT("User.Fill"),FNiagaraTypeDefinition::GetFloatDef())) C->SetVariableFloat(TEXT("User.Fill"),Gameplay.Fill);
    if(Gameplay.Intensity>=0 && Has(NS,TEXT("User.Intensity"),FNiagaraTypeDefinition::GetFloatDef())) C->SetVariableFloat(TEXT("User.Intensity"),Gameplay.Intensity);
    if(Gameplay.Duration>0 && Has(NS,TEXT("User.Duration"),FNiagaraTypeDefinition::GetFloatDef())) C->SetVariableFloat(TEXT("User.Duration"),Gameplay.Duration);
    if(Gameplay.bHasColor && Has(NS,TEXT("User.Color"),FNiagaraTypeDefinition::GetColorDef())) C->SetVariableLinearColor(TEXT("User.Color"),Gameplay.Color);
    if(Gameplay.bHasEndpoints) {
        if(Has(NS,TEXT("User.SourcePosition"),FNiagaraTypeDefinition::GetPositionDef()))C->SetVariablePosition(TEXT("User.SourcePosition"),Gameplay.SourcePosition);
        if(Has(NS,TEXT("User.TargetPosition"),FNiagaraTypeDefinition::GetPositionDef()))C->SetVariablePosition(TEXT("User.TargetPosition"),Gameplay.TargetPosition);
        if(Has(NS,TEXT("User.TargetOffset"),FNiagaraTypeDefinition::GetVec3Def()))
            C->SetVariableVec3(TEXT("User.TargetOffset"),Gameplay.TargetPosition-C->GetComponentLocation());
    }
    if(Gameplay.bHasSeed && Has(NS,TEXT("User.Seed"),FNiagaraTypeDefinition::GetIntDef()))C->SetVariableInt(TEXT("User.Seed"),int32(Gameplay.Seed));
    // Native actions already supply endpoints on the legacy loop anchor. Copy
    // exact value types only; in particular preserve LWC Position originals.
    if(auto* Anchor=NativeLoopAnchor.Get())
    {
        const auto& Store=Anchor->GetOverrideParameters();TArray<FNiagaraVariable> Variables;Store.GetParameters(Variables);
        for(const auto& V:Variables)
        {
            if(!V.GetName().ToString().StartsWith(TEXT("User.")) || NS->GetExposedParameters().IndexOf(V)==INDEX_NONE) continue;
            const auto& Type=V.GetType();
            if(Type==FNiagaraTypeDefinition::GetPositionDef())
            {if(const FVector* Position=Store.GetPositionParameterValue(V.GetName())) C->SetVariablePosition(V.GetName(),*Position);}
            else if(Type==FNiagaraTypeDefinition::GetFloatDef() || Type==FNiagaraTypeDefinition::GetVec3Def()
                || Type==FNiagaraTypeDefinition::GetColorDef() || Type==FNiagaraTypeDefinition::GetBoolDef())
                if(const uint8* Value=Store.GetParameterData(V)) C->GetOverrideParameters().SetParameterData(Value,V,false);
        }
    }
    Overrides(C,UMCPerkVFXProfile::AsLayer(Data.Outer));
    Overrides(C,Data.Snapshot->Layers[Index]);
    const auto& NativeLayer=Data.Snapshot->Layers[Index];
    const auto* VisibleSubject=Gameplay.VisualSubject.IsValid()?Gameplay.VisualSubject.Get():SubjectComponent.Get();
    const auto* BindingSubject=SubjectComponent.Get();
    const FTransform ParentFrame=NativeLayer.Binding==EMCVFXCompositionBinding::Subject && BindingSubject
        ?BindingSubject->GetSocketTransform(NativeLayer.Socket,RTS_World)
        :NativeLayer.Binding==EMCVFXCompositionBinding::Composition?GetActorTransform():LastAnchor;
    const FTransform EvaluatedFrame=C->IsRegistered()?C->GetComponentTransform():NativeLayer.Transform*ParentFrame;
    MCVFXGameplayInputs::ApplySubjectFrame(C,NS,VisibleSubject,EvaluatedFrame,Gameplay.Direction);
    if(FMCCoffeeRiverPresentation::Uses(NS)&&(!Gameplay.RiverSurface||!Gameplay.RiverSurface->Bind(C,PlaybackError)))return false;
    if(ContextLayers.IsValidIndex(Index)&&ContextLayers[Index]) {
        auto* Resources=MaterialContextResources.Get();
        if(!Resources || !Resources->IsReadyForPlayback(GetWorld())) {
            PlaybackError=TEXT("This material-context generation is no longer prepared for the actual world.");return false;
        }
        if(!Resources->BindPreparedScalarTarget(C,PlaybackError))return false;
    }
    const int32 PartCount=FMath::Min(RetainedCapturedMeshes.Num(),3);
    if(Has(NS,TEXT("User.ToolPartCount"),FNiagaraTypeDefinition::GetIntDef()))C->SetVariableInt(TEXT("User.ToolPartCount"),PartCount);
    if(Has(NS,TEXT("User.EchoOverhead"),FNiagaraTypeDefinition::GetBoolDef()))C->SetVariableBool(TEXT("User.EchoOverhead"),Gameplay.bCapturedMeshOverhead);
    const double Now=WorldClock(GetWorld());
    if(Has(NS,TEXT("User.ContactDelay"),FNiagaraTypeDefinition::GetFloatDef()))C->SetVariableFloat(TEXT("User.ContactDelay"),
        float(Gameplay.CapturedContactAt-Now)+Age-NativeLayer.StartSeconds);
    if(Gameplay.LocalStateAgeSeconds>=0.f && Has(NS,TEXT("User.StateAge"),FNiagaraTypeDefinition::GetFloatDef()))
        C->SetVariableFloat(TEXT("User.StateAge"),Age);
    for(int32 I=0;I<3;++I) {
        const auto* Part=RetainedCapturedMeshes.IsValidIndex(I)?&RetainedCapturedMeshes[I]:nullptr;
        const FTransform Pose=Part?Part->ContactPose:FTransform::Identity;
        const FName MeshName(*FString::Printf(TEXT("User.ToolMesh%d"),I));
        const FName PositionName(*FString::Printf(TEXT("User.ToolPosition%d"),I));
        const FName OrientationName(*FString::Printf(TEXT("User.ToolOrientation%d"),I));
        const FName ScaleName(*FString::Printf(TEXT("User.ToolScale%d"),I));
        if(Has(NS,MeshName,FNiagaraTypeDefinition(UStaticMesh::StaticClass())))C->SetVariableStaticMesh(MeshName,Part?Part->Mesh.Get():nullptr);
        if(Has(NS,PositionName,FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(PositionName,Pose.GetLocation());
        if(Has(NS,OrientationName,FNiagaraTypeDefinition::GetQuatDef()))C->SetVariableQuat(OrientationName,Pose.GetRotation());
        if(Has(NS,ScaleName,FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(ScaleName,Pose.GetScale3D());
    }
    if(!MCVFXNativeInputs::Apply(C,Gameplay.NativeInputs,PlaybackError,
        NativeInputStates.IsValidIndex(Index)?&NativeInputStates[Index]:nullptr,nullptr,
        FreezeCapturedLayers.IsValidIndex(Index)&&FreezeCapturedLayers[Index]?Gameplay.FreezeCapture.Get():nullptr)) {
        UE_LOG(LogMCPerkComposition,Warning,TEXT("Canceled VFX layer %s: %s"),
            *Data.Snapshot->Layers[Index].Name.ToString(),*PlaybackError);return false;
    }
    if(bFreezePrewarmed&&!bFreezePreparing&&FreezeLayers.IsValidIndex(Index)&&FreezeCapturedLayers[Index]
        &&!FreezeLayers[Index].UpdateAge(FMath::Max(0.f,float(Now-Gameplay.FreezeCapture->GetAcceptedAt())-NativeLayer.StartSeconds),PlaybackError))return false;
    if(bSeekToServerAge&&!UMCNiagaraProceduralSurfaceResources::Uses(NS)&&(!FreezeCapturedLayers.IsValidIndex(Index)||!FreezeCapturedLayers[Index])) C->SetDesiredAge(FMath::Max(0.f,Age-Data.Snapshot->Layers[Index].StartSeconds));
    return true;
}

bool AMCPerkVFXCompositionPlayer::ValidateGameplayValues(const FMCPerkVFXGameplayValues& Values,FString& Error) const
{
    if(!Values.NativeInputs.Validate(Error))return false;
    if(!Data.Snapshot) {Error=TEXT("A VFX update requires its immutable prepared snapshot.");return false;}
    if(Gameplay.RiverSurface.Get()!=Values.RiverSurface.Get()) {Error=TEXT("River retained context cannot change after admission.");return false;}
    if(Gameplay.FreezeCapture.Get()!=Values.FreezeCapture.Get()
        ||(bFreezePrewarmed&&Gameplay.FreezeEventAcceptedAt!=Values.FreezeEventAcceptedAt))
    {Error=TEXT("A fatal-freeze layer cannot switch its captured resource after admission.");return false;}
    if(MaterialContextResources.IsValid()) {
        const auto* Before=Gameplay.VisualSubject.IsValid()?Gameplay.VisualSubject.Get():SubjectComponent.Get();
        const auto* After=Values.VisualSubject.IsValid()?Values.VisualSubject.Get():SubjectComponent.Get();
        if(Before!=After) {Error=TEXT("A material-context target change requires a separately prepared activation.");return false;}
    }
    for(int32 I=0;I<Data.Systems.Num();++I)if(Data.Snapshot->Layers[I].bEnabled
        && !Values.NativeInputs.ValidateForSystem(Data.Systems[I],Error,Values.FreezeCapture.Get()))return false;
    return true;
}

bool AMCPerkVFXCompositionPlayer::UpdateGameplayValues(const FMCPerkVFXGameplayValues& Values,FString* OutError)
{
FString Error;
if(!ValidateGameplayValues(Values,Error)) {if(OutError)*OutError=Error;return false;}
Gameplay=Values;RetainedCapturedMeshes=Values.CapturedMeshes;NativeClockSampleAt=MCPerkComposition::WorldClock(GetWorld());
if(bPersistentState&&!Gameplay.FreezeCapture&&Values.LocalStateAgeSeconds>=0.f)Age=FMath::Max(Age,Values.LocalStateAgeSeconds);
for(int32 I=0;I<Components.Num();++I)if(!ApplyValues(I)) {
    if(OutError)*OutError=PlaybackError;StopEmission(true);return false;
}
return true;
}
void AMCPerkVFXCompositionPlayer::UpdateEventFrame(const FTransform& Frame)
{if(!Frame.ContainsNaN() && Frame.GetRotation().IsNormalized()){LastAnchor=Frame;SetActorTransform(Frame);}}

bool AMCPerkVFXCompositionPlayer::BindToPresentationEpoch(FGuid Epoch,FString& Error)
{
    auto* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!Epoch.IsValid() || !IsValid(State) || State->GameplayLoadId!=Epoch)
    {Error=TEXT("Presentation must bind to its matching current run identity.");return false;}
    PresentationGameState=State;PresentationEpoch=Epoch;bPresentationEpochBound=true;return true;
}

void AMCPerkVFXCompositionPlayer::Advance(float DeltaSeconds)
{
    if(!bRunning || !Data.Snapshot || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0) return;
    if(bPersistentState && (!PersistentStateOwner.IsValid() || PersistentStateOwner->IsActorBeingDestroyed())) {StopEmission(true);return;}
    if(bPresentationEpochBound) {
        const auto* State=PresentationGameState.Get();
        if(!State || State->GameplayLoadId!=PresentationEpoch) {StopEmission(true);return;}
    }
    if(bFreezePrewarmed)Age=FMath::Max(Age,float(MCPerkComposition::WorldClock(GetWorld())-Gameplay.FreezeEventAcceptedAt));
    else if(bPersistentState&&Gameplay.LocalStateAgeSeconds>=0.f)
        Age=FMath::Max(Age,Gameplay.LocalStateAgeSeconds+float(FMath::Max(0.,MCPerkComposition::WorldClock(GetWorld())-NativeClockSampleAt)));
    else Age+=DeltaSeconds;
    if(!bStopping)
    {
        if(auto* Native=NativeFrameComponent.Get()) {
            if(!NativeFrameSocket.IsNone()&&!Native->DoesSocketExist(NativeFrameSocket)) {StopEmission(false);return;}
            LastAnchor=NativeFrameOffset*Native->GetSocketTransform(NativeFrameSocket,RTS_World);SetActorTransform(LastAnchor);
        }
        else if(bHasNativeFrame) {StopEmission(false);return;}
        else if(auto* Anchor=NativeLoopAnchor.Get())
        {
            LastAnchor=Anchor->GetComponentTransform();SetActorTransform(LastAnchor);
        }
        else if(bHostDrivenLoop) {StopEmission(false);return;}
        for(int32 I=0;I<Started.Num();++I)
        {
            const auto& Layer=Data.Snapshot->Layers[I];
            if(!Layer.bEnabled) {Started[I]=true;Stopped[I]=true;continue;}
            const float End=Layer.EmissionSeconds>0?Layer.StartSeconds+Layer.EmissionSeconds:MAX_flt;
            if(!Started[I] && Age>=Layer.StartSeconds)
            {
                if(Age<End) StartLayer(I);
                else {Started[I]=true;Stopped[I]=true;}
                if(!bRunning)return;
            }
            if(!Stopped[I] && Started[I] && Age>=End)
            {if(IsValid(Components[I])) {
                if(auto* Surface=ProceduralSurfaceResources.Get();Surface&&Surface->Owns(Components[I]))ReleaseLayer(I);
                else {if(bSeekToServerAge)Components[I]->SetAgeUpdateMode(ENiagaraAgeUpdateMode::TickDeltaTime);Components[I]->Deactivate();}
            }Stopped[I]=true;}
            if(Stopped[I] && Layer.EmissionSeconds>0 && Age>=End+Data.Snapshot->TailSeconds && IsValid(Components[I]))
            {ReleaseLayer(I);}
            if(Started[I] && IsValid(Components[I]) && Components[I]->IsComplete())ReleaseLayer(I);
            if(IsValid(Components[I]) && !ApplyValues(I)) {StopEmission(true);return;}
        }
        if(!bPersistentState && Age>=MCPerkComposition::SafetySeconds)
        {
            UE_LOG(LogMCPerkComposition,Warning,TEXT("Independent VFX composition exceeded the explicit %.0fs safety lifetime; stopping emission (preview DurationSeconds is not a runtime TTL)."),MCPerkComposition::SafetySeconds);
            StopEmission(false);
        }
    }
    if(bStopping)
    {
        for(int32 I=0;I<Components.Num();++I)if(IsValid(Components[I])&&Components[I]->IsComplete())ReleaseLayer(I);
        if(GetLiveLayerCount()==0 || Age>=StopAge+Data.Snapshot->TailSeconds) {StopEmission(true);return;}
    }
    else if(!bHostDrivenLoop && GetPendingLayerCount()==0 && GetLiveLayerCount()==0) StopEmission(true);
}

void AMCPerkVFXCompositionPlayer::StopEmission(bool bImmediate)
{
    if(bImmediate)
    {
        bRunning=false;SetActorTickEnabled(false);
        for(int32 I=0;I<Components.Num();++I)ReleaseLayer(I);
        Components.Reset();Started.Reset();Stopped.Reset();NativeInputStates.Reset();
        ContextLayers.Reset();MaterialContextResources.Reset();FreezeLayers.Reset();FreezeMaterialVariants.Reset();FreezeMaterialSourceSlots.Reset();FreezeCapturedLayers.Reset();FreezeCapturePrepared.Reset();FreezeCapturePreparationErrors.Reset();Gameplay.FreezeCapture.Reset();bFreezePrewarmed=false;bFreezePreparing=false;
        if(!IsActorBeingDestroyed()) Destroy();return;
    }
    if(!bRunning || bStopping) return;
    bStopping=true;StopAge=Age;
    bPersistentState=false;PersistentStateOwner.Reset(); // Accepted phase tail is world-owned and explicitly bounded.
    if(auto* Anchor=NativeLoopAnchor.Get())
    {
        RemoveTickPrerequisiteComponent(Anchor);
        if(auto* SourceAction=Anchor->GetOwner()) RemoveTickPrerequisiteActor(SourceAction);
    }
    NativeLoopAnchor.Reset();SubjectComponent.Reset();
    if(auto* Native=NativeFrameComponent.Get())RemoveTickPrerequisiteComponent(Native);
    NativeFrameComponent.Reset();bHasNativeFrame=false;
    for(int32 I=0;I<Started.Num();++I)
    {
        const bool WasStarted=Started[I];
        Started[I]=true;Stopped[I]=true;
        if(!WasStarted) {ReleaseLayer(I);continue;}
        if(auto* C=Components[I].Get();IsValid(C))
        {
            if(auto* Surface=ProceduralSurfaceResources.Get();Surface&&Surface->Owns(C)){ReleaseLayer(I);continue;}
            // A source action/hand may disappear immediately after cancellation.
            // Keep existing particles at their last real frame for their tail.
            C->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
            if(bSeekToServerAge)C->SetAgeUpdateMode(ENiagaraAgeUpdateMode::TickDeltaTime);
            C->Deactivate();
        }
    }
}

int32 AMCPerkVFXCompositionPlayer::GetLiveLayerCount() const
{int32 Count=0;for(int32 I=0;I<Components.Num();++I)if(Started.IsValidIndex(I)&&Started[I]&&IsValid(Components[I])&&!Components[I]->IsComplete())++Count;return Count;}
int32 AMCPerkVFXCompositionPlayer::GetPendingLayerCount() const
{int32 Count=0;for(bool Value:Started) if(!Value) ++Count;return Count;}
void AMCPerkVFXCompositionPlayer::Tick(float DeltaSeconds) {Super::Tick(DeltaSeconds);Advance(DeltaSeconds);}
void AMCPerkVFXCompositionPlayer::EndPlay(const EEndPlayReason::Type Reason)
{
    bRunning=false;
    for(int32 I=0;I<Components.Num();++I)ReleaseLayer(I);
    Components.Reset();NativeInputStates.Reset();ContextLayers.Reset();MaterialContextResources.Reset();
    Data=FMCPreparedPerkVFXComposition();FreezeLayers.Reset();FreezeMaterialVariants.Reset();FreezeMaterialSourceSlots.Reset();FreezeCapturedLayers.Reset();FreezeCapturePrepared.Reset();FreezeCapturePreparationErrors.Reset();Gameplay.FreezeCapture.Reset();bFreezePrewarmed=false;bFreezePreparing=false;Super::EndPlay(Reason);
}

bool AMCPerkVFXCompositionPlayer::PrepareFreezeMaterials(int32 Layer,int32 Slots,
    TConstArrayView<UMaterialInterface*> ExplicitMaterials,UMaterialInterface* ExplicitGlass,
    TArray<UMaterialInterface*>& Out,UMaterialInterface*& OutGlass,FString& Error)
{
    if(!Data.Systems.IsValidIndex(Layer)||Slots<1||Slots>FMCFreezeCapturedGeometry::MaxSlots)
    {Error=TEXT("Captured material preparation requires the exact finite source partitions.");return false;}
    const auto& Store=Data.Systems[Layer]->GetExposedParameters();
    auto Saved=[&](FName Name)->UMaterialInterface* {
        const FNiagaraVariable Key(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()),Name);
        if(Store.IndexOf(Key)==INDEX_NONE)return nullptr;
        auto* M=Cast<UMaterialInterface>(Store.GetUObject(Key).Get());
        return M&&M->IsAsset()&&!M->HasAnyFlags(RF_Transient)&&M->GetOutermost()!=GetTransientPackage()?M:nullptr;
    };
    TArray<UMaterialInterface*> Parents;Parents.Reserve(Slots);
    for(int32 Slot=0;Slot<Slots;++Slot) {
        auto* Parent=Saved(*FString::Printf(TEXT("User.Native.FreezeCapture.Material%d"),Slot));
        // Existing explicit native API remains supported for reviewed fixtures;
        // normal location/Composer enrollment always passes no substitute.
        if(!Parent&&ExplicitMaterials.IsValidIndex(Slot))Parent=ExplicitMaterials[Slot];
        if(!Parent){Error=TEXT("Captured NS lacks its exact saved typed material default for source slot ")+FString::FromInt(Slot);return false;}
        Parents.Add(Parent);
    }
    auto* Glass=Saved(TEXT("User.Native.FreezeCapture.Glass"));if(!Glass)Glass=ExplicitGlass;
    if(!Glass){Error=TEXT("Captured NS lacks its saved typed Glass material default.");return false;}
    // No partially-created row is advertised as ready. Bound: four captured
    // rows across the fatal episode, at most eight body partitions + glass.
    for(int32 Slot=0;Slot<Slots;++Slot) {
        auto* M=UMaterialInstanceDynamic::Create(Parents[Slot],this);
        if(!M){Error=TEXT("Captured body material variant allocation failed before readiness.");return false;}
        FreezeMaterialVariants.Add(M);FreezeMaterialSourceSlots.Add(Slot);Out.Add(M);
    }
    OutGlass=UMaterialInstanceDynamic::Create(Glass,this);
    if(!OutGlass){Error=TEXT("Captured glass variant allocation failed before readiness.");return false;}
    FreezeMaterialVariants.Add(CastChecked<UMaterialInstanceDynamic>(OutGlass));FreezeMaterialSourceSlots.Add(INDEX_NONE);
    RefreshPreparedFreezeMaterialUniforms();return true;
}

void AMCPerkVFXCompositionPlayer::RefreshPreparedFreezeMaterialUniforms()
{
    const auto* Mesh=Cast<USkeletalMeshComponent>(SubjectComponent.Get());if(!Mesh)return;
    for(int32 I=0;I<FreezeMaterialVariants.Num();++I) {
        auto* M=FreezeMaterialVariants[I].Get();const int32 Slot=FreezeMaterialSourceSlots[I];
        if(!M||Slot==INDEX_NONE)continue;
        if(auto* Original=Mesh->GetMaterial(Slot))M->CopyMaterialUniformParameters(Original);
        M->SetScalarParameterValue(TEXT("BodyStretch"),0);M->SetScalarParameterValue(TEXT("Damage"),0);M->SetScalarParameterValue(TEXT("Coffee"),0);
    }
}

void AMCPerkVFXCompositionPlayer::GetPreparedFreezeResources(TArray<UObject*>& Out) const
{for(UMaterialInstanceDynamic* M:FreezeMaterialVariants)if(IsValid(M))Out.AddUnique(M);}

bool AMCPerkVFXCompositionPlayer::PrepareFreezeCaptureLayers(const FMCPreparedPerkVFXComposition& Prepared,USceneComponent* Subject,
    const TSharedPtr<FMCFreezeCapturedGeometry>& Geometry,FGuid Epoch,TConstArrayView<UMaterialInterface*> Materials,UMaterialInterface* Glass,FString& Error)
{
    Error.Reset();
    if(bRunning||bFreezePrewarmed||!Components.IsEmpty()||!Prepared.Snapshot||!Subject||!Subject->GetOwner()||Subject->GetWorld()!=GetWorld()
        ||!Epoch.IsValid()||Prepared.Systems.Num()!=Prepared.Snapshot->Layers.Num()||Prepared.Systems.Num()>64)
    {Error=TEXT("Freeze event preparation needs one immutable local event, subject and epoch.");return false;}
    int32 Captured=0;FreezeCapturedLayers.Init(false,Prepared.Systems.Num());FreezeCapturePrepared.Init(false,Prepared.Systems.Num());
    FreezeCapturePreparationErrors.SetNum(Prepared.Systems.Num());bFreezeCaptureRejected=false;FreezeCaptureAdmissionError.Reset();
    auto RejectCapture=[&](int32 I,const FString& Why) {
        FreezeCapturePrepared[I]=false;bFreezeCaptureRejected=true;
        FreezeCapturePreparationErrors[I]=Why.IsEmpty()?TEXT("Captured body row could not prepare its exact resource."):Why;
        if(FreezeCaptureAdmissionError.IsEmpty())FreezeCaptureAdmissionError=FreezeCapturePreparationErrors[I];
    };
    for(int32 I=0;I<Prepared.Systems.Num();++I) {
        const auto& L=Prepared.Snapshot->Layers[I];if(!L.bEnabled)continue;auto* NS=Prepared.Systems[I].Get();
        if(!NS){Error=TEXT("Unknown/unloaded enabled source cannot be guessed as a captured or ordinary row.");return false;}
        FreezeCapturedLayers[I]=FMCPreparedFreezeGeometryLayer::UsesCapture(NS);
        if(FreezeCapturedLayers[I]) {
            FString CaptureError;
            if(++Captured>4||!MCPerkComposition::ReadySource(NS,CaptureError)||!UMCVFXComposition::ValidateParameters(L,NS,CaptureError)
                ||!UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Prepared.Outer),NS,CaptureError)
                ||!Geometry||Geometry->GetWorld()!=GetWorld()||Geometry->GetEpoch()!=Epoch||Geometry->IsSealed()
                ||!Prepared.Outer.Transform.Equals(FTransform::Identity)||!L.Transform.Equals(FTransform::Identity)
                ||L.Binding==EMCVFXCompositionBinding::Subject||!FMCPreparedFreezeGeometryLayer::ValidateSystem(NS,CaptureError)) {
                RejectCapture(I,CaptureError.IsEmpty()?TEXT("Captured row has unavailable exact pose/NI resources or an unsupported captured frame."):CaptureError);
                continue; // Crucially still validate/prepare every independent ordinary row.
            }
            FreezeCapturePrepared[I]=true; // Eligible, becomes actual Prime/paused proof below.
        } else if(!MCPerkComposition::ReadySource(NS,Error)||!UMCVFXComposition::ValidateParameters(L,NS,Error)
            ||!UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Prepared.Outer),NS,Error)
            ||!FMCVFXNativeInputs().ValidateForSystem(NS,Error)) {
            if(Error.IsEmpty())Error=TEXT("Ordinary gallery source is not ready or rejected its exact declared inputs: ")+GetPathNameSafe(NS);
            return false;
        }
    }
    Data=Prepared;Gameplay=FMCPerkVFXGameplayValues();Gameplay.FreezeCapture=Geometry;SubjectComponent=Subject;FreezePreparedEpoch=Epoch;
    Tags.AddUnique(FName(TEXT("MC.VFXPresentationComponent.Player")));
    if(const auto* State=GetWorld()->GetGameState<AMCGameState>();State&&State->GameplayLoadId.IsValid())
        if(!BindToPresentationEpoch(Epoch,Error))return false;
    LastAnchor=Prepared.Outer.Transform*Subject->GetOwner()->GetActorTransform();SetActorTransform(LastAnchor);
    Components.SetNum(Data.Systems.Num());Started.Init(false,Components.Num());Stopped.Init(false,Components.Num());
    NativeInputStates.SetNum(Components.Num());ContextLayers.Init(false,Components.Num());FreezeLayers.SetNum(Components.Num());
    bool NeedsContext=false;
    for(int32 I=0;I<Components.Num();++I)if(Data.Snapshot->Layers[I].bEnabled&&!FreezeCapturedLayers[I]) {
        ContextLayers[I]=UMCNiagaraMaterialContextResources::NeedsMaterialContext(Data.Systems[I]);NeedsContext|=ContextLayers[I];
    }
    if(NeedsContext) {
        const auto* Loading=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();auto* Resources=Loading?Loading->GetMaterialContextResources():nullptr;
        if(!Resources||!Resources->IsReadyForPlayback(GetWorld())){Error=TEXT("Ordinary material-context rows must use the existing ready context resources.");return false;}
        MaterialContextResources=Resources;
    }
    bFreezePreparing=true;
    for(int32 I=0;I<Components.Num();++I) {
        if(!Data.Snapshot->Layers[I].bEnabled){Started[I]=Stopped[I]=true;continue;}
        if(FreezeCapturedLayers[I]) {
            if(!FreezeCapturePrepared[I])continue;
            FString CaptureError;
            TArray<UMaterialInterface*> LayerMaterials;UMaterialInterface* LayerGlass=nullptr;
            if(!PrepareFreezeMaterials(I,Geometry->GetSlotCount(),Materials,Glass,LayerMaterials,LayerGlass,CaptureError)
                ||!CreateLayerInactive(I)||!FreezeLayers[I].Prime(Components[I],Geometry.ToSharedRef(),LayerMaterials,LayerGlass,CaptureError)) {
                RejectCapture(I,CaptureError.IsEmpty()?PlaybackError:CaptureError);PlaybackError.Reset();ReleaseLayer(I);continue;
            }
            auto* C=Components[I].Get();C->SetForceSolo(true);C->SetAllowScalability(false);C->RegisterComponentWithWorld(GetWorld());C->Activate(true);
            C->AdvanceSimulation(2,1.f/60.f);C->SetPaused(true);
            if(!FreezeLayers[I].VerifySimulatedAndPaused(CaptureError)) {
                RejectCapture(I,CaptureError);ReleaseLayer(I);continue; // Never destroys the ordinary plan or another phase.
            }
        } else {
            if(!CreateLayerInactive(I)){Error=PlaybackError;bFreezePreparing=false;StopEmission(true);return false;}
            Components[I]->RegisterComponentWithWorld(GetWorld());
        }
    }
    // True means an independent event plan is prepared. Captured-row failures
    // remain explicit through HasRejected... and are not a readiness certificate.
    bFreezePreparing=false;bFreezePrewarmed=true;SetActorTickEnabled(false);return true;
}

bool AMCPerkVFXCompositionPlayer::StartPrewarmedFreeze(const FMCPreparedPerkVFXComposition& Prepared,const FTransform& Frame,USceneComponent* Subject,
    const FMCPerkVFXGameplayValues& Values,float InitialAge,FString& Error)
{
    if(!bFreezePrewarmed||bRunning||Prepared.Snapshot!=Data.Snapshot||Prepared.Systems!=Data.Systems
        ||SubjectComponent.Get()!=Subject||!Subject||Subject->GetWorld()!=GetWorld()||Frame.ContainsNaN()||!Frame.GetRotation().IsNormalized()
        ||Frame.GetScale3D().GetAbsMin()<.0001||!FMath::IsFinite(InitialAge)||InitialAge<0||InitialAge>=5.f
        ||!FMath::IsFinite(Values.FreezeEventAcceptedAt)||Values.FreezeEventAcceptedAt<0||!Values.NativeInputs.Validate(Error))
    {if(Error.IsEmpty())Error=TEXT("Freeze event admission differs from its exact prepared event/subject/boundary.");return false;}
    const double Now=MCPerkComposition::WorldClock(GetWorld());const float EventAge=float(Now-Values.FreezeEventAcceptedAt);
    if(!FMath::IsFinite(EventAge)||EventAge<0||FMath::Abs(EventAge-InitialAge)>.1f)
    {Error=TEXT("Freeze event scheduler cannot substitute the accepted event boundary.");return false;}
    if(const auto* State=GetWorld()->GetGameState<AMCGameState>();State&&State->GameplayLoadId.IsValid()&&State->GameplayLoadId!=FreezePreparedEpoch)
    {Error=TEXT("Freeze event belongs to a retired world epoch.");return false;}
    bool Rejected[64]={};bFreezeCaptureRejected=false;FreezeCaptureAdmissionError.Reset();
    const bool ExactPacket=Values.FreezeCapture&&Values.FreezeCapture->IsSealed()
        &&Gameplay.FreezeCapture.Get()==Values.FreezeCapture.Get()&&Frame.Equals(Values.FreezeCapture->GetFrame())
        &&Values.FreezeEventAcceptedAt>=Values.FreezeCapture->GetAcceptedAt();
    for(int32 I=0;I<Components.Num();++I)if(Data.Snapshot->Layers[I].bEnabled) {
        const auto& Layer=Data.Snapshot->Layers[I];
        if(Layer.EmissionSeconds>0&&EventAge>=Layer.StartSeconds+Layer.EmissionSeconds)continue;
        if(FreezeCapturedLayers[I]) {
            FString CaptureError;
            if(!FreezeCapturePrepared[I]||!ExactPacket||!Values.NativeInputs.ValidateForSystem(Data.Systems[I],CaptureError,Values.FreezeCapture.Get())
                ||!FreezeLayers[I].ValidateAccepted(Values.FreezeCapture.Get(),CaptureError)) {
                Rejected[I]=true;bFreezeCaptureRejected=true;
                if(FreezeCaptureAdmissionError.IsEmpty())FreezeCaptureAdmissionError=!FreezeCapturePreparationErrors[I].IsEmpty()
                    ?FreezeCapturePreparationErrors[I]:CaptureError.IsEmpty()?TEXT("Actual captured packet is unavailable for this body row."):CaptureError;
            }
        } else if(!Values.NativeInputs.ValidateForSystem(Data.Systems[I],Error))return false;
    }
    Gameplay=Values;Age=EventAge;NativeClockSampleAt=Now;LastAnchor=Prepared.Outer.Transform*Frame;SetActorTransform(LastAnchor);
    bHostDrivenLoop=false;bPersistentState=false;bSeekToServerAge=InitialAge>1.f/60.f;bStopping=false;StopAge=0;bRunning=true;
    const float FatalAge=ExactPacket?FMath::Max(0.f,float(Now-Values.FreezeCapture->GetAcceptedAt())):0;
    for(int32 I=0;I<Components.Num();++I) {
        const auto& L=Data.Snapshot->Layers[I];const bool Expired=L.EmissionSeconds>0&&Age>=L.StartSeconds+L.EmissionSeconds;
        if(!L.bEnabled){Started[I]=Stopped[I]=true;continue;}
        if(Expired||Rejected[I]){Started[I]=Stopped[I]=true;ReleaseLayer(I);continue;}
        const bool Due=Age>=L.StartSeconds;
        if(FreezeCapturedLayers[I]) {
            if(!FreezeLayers[I].Commit(Values.FreezeCapture.ToSharedRef(),FMath::Max(0.f,FatalAge-L.StartSeconds),Error,Due)) {
                // Failed exact row never restarts/replaces another row. Ordinary
                // gallery rows retain their own independent event schedule.
                bFreezeCaptureRejected=true;FreezeCaptureAdmissionError=Error;Error.Reset();
                Started[I]=Stopped[I]=true;ReleaseLayer(I);continue;
            }
            Started[I]=Due;Stopped[I]=false;
        } else {
            if(L.Binding==EMCVFXCompositionBinding::World)Components[I]->SetWorldTransform(L.Transform*LastAnchor);
            if(bSeekToServerAge){Components[I]->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);Components[I]->SetCanRenderWhileSeeking(false);Components[I]->SetMaxSimTime(.002f);}
            if(Due)StartLayer(I);
        }
    }
    SetActorTickEnabled(true);return bRunning;
}

bool AMCPerkVFXCompositionPlayer::HasVisibleCapturedFreezeBody() const
{
    if(!bFreezePrewarmed||!bRunning)return false;
    for(int32 I=0;I<FreezeLayers.Num();++I)
        if(FreezeCapturedLayers[I]&&Started.IsValidIndex(I)&&Started[I]&&FreezeLayers[I].HasVisibleCapturedBody())return true;
    return false;
}

void AMCPerkVFXCompositionPlayer::StopCapturedFreezeLayers()
{
    if(!bFreezePrewarmed)return;
    for(int32 I=0;I<FreezeCapturedLayers.Num();++I)if(FreezeCapturedLayers[I]){Started[I]=Stopped[I]=true;ReleaseLayer(I);}
    Gameplay.FreezeCapture.Reset();
    if(GetLiveLayerCount()==0&&GetPendingLayerCount()==0)StopEmission(true);
}

bool AMCPerkVFXCompositionPlayer::HasAdmittedCapturedFreezeLayers() const
{
    if(!bFreezePrewarmed||!bRunning)return false;
    for(int32 I=0;I<FreezeCapturedLayers.Num();++I)
        if(FreezeCapturedLayers[I]&&FreezeCapturePrepared[I]&&!Stopped[I]&&IsValid(Components[I]))return true;
    return false;
}
