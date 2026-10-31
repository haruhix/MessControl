#include "MCPerkVFXCompositionPlayer.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPerkComposition,Log,All);

namespace MCPerkComposition
{
constexpr float SafetySeconds=60.f;
bool Has(const UNiagaraSystem* System,FName Name,const FNiagaraTypeDefinition& Type)
{return System && System->GetExposedParameters().IndexOf(FNiagaraVariable(Type,Name))!=INDEX_NONE;}
void Overrides(UNiagaraComponent* C,const FMCVFXCompositionLayer& L)
{
    for(const auto& V:L.Floats) if(V.bOverride) C->SetVariableFloat(V.Name,V.Value);
    for(const auto& V:L.Colors) if(V.bOverride) C->SetVariableLinearColor(V.Name,V.Value);
    for(const auto& V:L.Vectors) if(V.bOverride) C->SetVariableVec3(V.Name,V.Value);
    for(const auto& V:L.Bools) if(V.bOverride) C->SetVariableBool(V.Name,V.Value);
}
}

AMCPerkVFXCompositionPlayer::AMCPerkVFXCompositionPlayer()
{
    bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("EventFrame")));
}

bool AMCPerkVFXCompositionPlayer::StartPrepared(const FMCPreparedPerkVFXComposition& Prepared,const FTransform& Frame,
    USceneComponent* Subject,UNiagaraComponent* LoopAnchor,bool bHostLoop,const FMCPerkVFXGameplayValues& Values,FString& Error)
{
    Error.Reset();
    if(bRunning || !Prepared.Snapshot || Prepared.Systems.Num()!=Prepared.Snapshot->Layers.Num() || Frame.ContainsNaN())
    {Error=TEXT("Independent VFX player requires a valid immutable prepared snapshot.");return false;}
    for(int32 I=0;I<Prepared.Snapshot->Layers.Num();++I)
    {
        const auto& Layer=Prepared.Snapshot->Layers[I];if(!Layer.bEnabled) continue;
        if(!Prepared.Systems[I]) {Error=TEXT("A composition layer was not prepared before its first event.");return false;}
        if(Layer.Binding==EMCVFXCompositionBinding::Subject && (!IsValid(Subject) || Subject->GetWorld()!=GetWorld()
            || (!Layer.Socket.IsNone() && !Subject->DoesSocketExist(Layer.Socket))))
        {Error=FString::Printf(TEXT("Layer %s needs a valid subject/socket in this world."),*Layer.Name.ToString());return false;}
    }
    Data=Prepared;Gameplay=Values;SubjectComponent=Subject;NativeLoopAnchor=LoopAnchor;bHostDrivenLoop=bHostLoop;
    SetActorTransform(Frame);LastAnchor=Frame;
    if(LoopAnchor) AddTickPrerequisiteComponent(LoopAnchor);
    Components.SetNum(Data.Systems.Num());Started.Init(false,Components.Num());Stopped.Init(false,Components.Num());
    Age=0;StopAge=0;bStopping=false;bRunning=true;SetActorTickEnabled(true);Advance(0);
    return true;
}

void AMCPerkVFXCompositionPlayer::StartLayer(int32 Index)
{
    const auto& Layer=Data.Snapshot->Layers[Index];Started[Index]=true;
    if(!Layer.bEnabled) {Stopped[Index]=true;return;}
    auto* Subject=SubjectComponent.Get();
    if(Layer.Binding==EMCVFXCompositionBinding::Subject && (!Subject || (!Layer.Socket.IsNone() && !Subject->DoesSocketExist(Layer.Socket))))
    {Stopped[Index]=true;UE_LOG(LogMCPerkComposition,Warning,TEXT("Canceled VFX layer %s: subject/socket disappeared before delayed start."),*Layer.Name.ToString());return;}
    auto* C=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient);
    C->SetAutoActivate(false);C->SetAutoDestroy(false);C->SetAsset(Data.Systems[Index]);
    C->SetTickBehavior(ENiagaraTickBehavior::UsePrereqs);
    C->AddTickPrerequisiteActor(this);
    if(Layer.Binding==EMCVFXCompositionBinding::Subject) {C->SetupAttachment(Subject,Layer.Socket);C->SetRelativeTransform(Layer.Transform);}
    else if(Layer.Binding==EMCVFXCompositionBinding::Composition) {C->SetupAttachment(GetRootComponent());C->SetRelativeTransform(Layer.Transform);}
    else C->SetWorldTransform(Layer.Transform*LastAnchor);
    Components[Index]=C;
    ApplyValues(Index);AddInstanceComponent(C);C->RegisterComponentWithWorld(GetWorld());C->Activate(true);
}

void AMCPerkVFXCompositionPlayer::ApplyValues(int32 Index)
{
    auto* C=Components.IsValidIndex(Index)?Components[Index].Get():nullptr;
    if(!IsValid(C)) return;
    const auto* NS=Data.Systems[Index].Get();
    using namespace MCPerkComposition;
    if(Gameplay.Radius>0 && Has(NS,TEXT("User.Radius"),FNiagaraTypeDefinition::GetFloatDef())) C->SetVariableFloat(TEXT("User.Radius"),Gameplay.Radius);
    if(!Gameplay.Direction.IsNearlyZero() && Has(NS,TEXT("User.Direction"),FNiagaraTypeDefinition::GetVec3Def())) C->SetVariableVec3(TEXT("User.Direction"),Gameplay.Direction.GetSafeNormal());
    if(Gameplay.Fill>=0 && Has(NS,TEXT("User.Fill"),FNiagaraTypeDefinition::GetFloatDef())) C->SetVariableFloat(TEXT("User.Fill"),Gameplay.Fill);
    if(Gameplay.Intensity>=0 && Has(NS,TEXT("User.Intensity"),FNiagaraTypeDefinition::GetFloatDef())) C->SetVariableFloat(TEXT("User.Intensity"),Gameplay.Intensity);
    if(Gameplay.Duration>0 && Has(NS,TEXT("User.Duration"),FNiagaraTypeDefinition::GetFloatDef())) C->SetVariableFloat(TEXT("User.Duration"),Gameplay.Duration);
    if(Gameplay.bHasColor && Has(NS,TEXT("User.Color"),FNiagaraTypeDefinition::GetColorDef())) C->SetVariableLinearColor(TEXT("User.Color"),Gameplay.Color);
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
}

void AMCPerkVFXCompositionPlayer::UpdateGameplayValues(const FMCPerkVFXGameplayValues& Values)
{Gameplay=Values;for(int32 I=0;I<Components.Num();++I) ApplyValues(I);}

void AMCPerkVFXCompositionPlayer::Advance(float DeltaSeconds)
{
    if(!bRunning || !Data.Snapshot || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0) return;
    Age+=DeltaSeconds;
    if(!bStopping)
    {
        if(auto* Anchor=NativeLoopAnchor.Get())
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
            }
            if(!Stopped[I] && Started[I] && Age>=End)
            {if(IsValid(Components[I])) Components[I]->Deactivate();Stopped[I]=true;}
            if(Stopped[I] && Layer.EmissionSeconds>0 && Age>=End+Data.Snapshot->TailSeconds && IsValid(Components[I]))
            {Components[I]->DeactivateImmediate();Components[I]->DestroyComponent();Components[I]=nullptr;}
            if(IsValid(Components[I])) ApplyValues(I);
        }
        if(Age>=MCPerkComposition::SafetySeconds)
        {
            UE_LOG(LogMCPerkComposition,Warning,TEXT("Independent VFX composition exceeded the explicit %.0fs safety lifetime; stopping emission (preview DurationSeconds is not a runtime TTL)."),MCPerkComposition::SafetySeconds);
            StopEmission(false);
        }
    }
    if(bStopping)
    {
        if(GetLiveLayerCount()==0 || Age>=StopAge+Data.Snapshot->TailSeconds) {StopEmission(true);return;}
    }
    else if(!bHostDrivenLoop && GetPendingLayerCount()==0 && GetLiveLayerCount()==0) StopEmission(true);
}

void AMCPerkVFXCompositionPlayer::StopEmission(bool bImmediate)
{
    if(bImmediate)
    {
        bRunning=false;SetActorTickEnabled(false);
        for(UNiagaraComponent* C:Components) if(IsValid(C)) {C->DeactivateImmediate();C->DestroyComponent();}
        Components.Reset();Started.Reset();Stopped.Reset();
        if(!IsActorBeingDestroyed()) Destroy();return;
    }
    if(!bRunning || bStopping) return;
    bStopping=true;StopAge=Age;
    if(auto* Anchor=NativeLoopAnchor.Get())
    {
        RemoveTickPrerequisiteComponent(Anchor);
        if(auto* SourceAction=Anchor->GetOwner()) RemoveTickPrerequisiteActor(SourceAction);
    }
    NativeLoopAnchor.Reset();SubjectComponent.Reset();
    for(int32 I=0;I<Started.Num();++I)
    {
        Started[I]=true;Stopped[I]=true;
        if(auto* C=Components[I].Get();IsValid(C))
        {
            // A source action/hand may disappear immediately after cancellation.
            // Keep existing particles at their last real frame for their tail.
            C->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
            C->Deactivate();
        }
    }
}

int32 AMCPerkVFXCompositionPlayer::GetLiveLayerCount() const
{int32 Count=0;for(UNiagaraComponent* C:Components) if(IsValid(C) && !C->IsComplete()) ++Count;return Count;}
int32 AMCPerkVFXCompositionPlayer::GetPendingLayerCount() const
{int32 Count=0;for(bool Value:Started) if(!Value) ++Count;return Count;}
void AMCPerkVFXCompositionPlayer::Tick(float DeltaSeconds) {Super::Tick(DeltaSeconds);Advance(DeltaSeconds);}
void AMCPerkVFXCompositionPlayer::EndPlay(const EEndPlayReason::Type Reason)
{
    bRunning=false;
    for(UNiagaraComponent* C:Components) if(IsValid(C)) {C->DeactivateImmediate();C->DestroyComponent();}
    Components.Reset();Data=FMCPreparedPerkVFXComposition();Super::EndPlay(Reason);
}
