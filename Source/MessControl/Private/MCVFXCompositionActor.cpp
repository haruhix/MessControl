#include "MCVFXCompositionActor.h"

#include "MCVFXComposition.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCVFXComposition,Log,All);

AMCVFXCompositionActor::AMCVFXCompositionActor()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=true;
    bReplicates=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("CompositionAnchor")));
}

bool AMCVFXCompositionActor::PrepareComposition(UMCVFXComposition* InComposition,FString& OutError)
{
    StopComposition(true);Prepared=nullptr;PreparedSystems.Reset();LastError.Reset();OutError.Reset();
    if(!IsValid(InComposition) || !InComposition->Validate(OutError))
    {
        if(OutError.IsEmpty()) OutError=TEXT("Choose a valid VFX composition.");
        LastError=OutError;return false;
    }
    // Snapshot the recipe: editing the composer's working copy cannot invalidate a playing schedule.
    Prepared=DuplicateObject<UMCVFXComposition>(InComposition,this,
        MakeUniqueObjectName(this,UMCVFXComposition::StaticClass(),TEXT("PreparedComposition")));
    Prepared->ClearFlags(RF_Public|RF_Standalone);
    Prepared->SetFlags(RF_Transient);
    PreparedSystems.SetNum(Prepared->Layers.Num());
    for(int32 Index=0;Index<Prepared->Layers.Num();++Index)
    {
        const auto& Layer=Prepared->Layers[Index];
        if(!Layer.bEnabled) continue;
        UNiagaraSystem* System=Layer.System.LoadSynchronous();
        if(!System || !UMCVFXComposition::ValidateParameters(Layer,System,OutError))
        {
            if(OutError.IsEmpty()) OutError=FString::Printf(TEXT("Could not prepare layer %d."),Index+1);
            LastError=OutError;Prepared=nullptr;PreparedSystems.Reset();return false;
        }
        PreparedSystems[Index]=System;
    }
    Composition=InComposition;
    return true;
}

bool AMCVFXCompositionActor::PlayPrepared(USceneComponent* Subject,bool bInHonorSolo)
{
    StopComposition(true);LastError.Reset();
    if(!Prepared || PreparedSystems.Num()!=Prepared->Layers.Num())
    {LastError=TEXT("Prepare the composition before playing.");return false;}
    bHonorSolo=bInHonorSolo;bHasSolo=false;
    for(const auto& Layer:Prepared->Layers) bHasSolo|=Layer.bEnabled && Layer.bSolo;
    for(int32 Index=0;Index<Prepared->Layers.Num();++Index)
    {
        const auto& Layer=Prepared->Layers[Index];
        if(!Layer.bEnabled || (bHonorSolo && bHasSolo && !Layer.bSolo)) continue;
        if(!PreparedSystems[Index]) {LastError=TEXT("A prepared system is unavailable; prepare again during loading.");return false;}
        if(Layer.Binding==EMCVFXCompositionBinding::Subject && (!IsValid(Subject) || Subject->GetWorld()!=GetWorld()
            || (!Layer.Socket.IsNone() && !Subject->DoesSocketExist(Layer.Socket))))
        {LastError=TEXT("Subject layer needs a valid component and an existing socket in this world.");return false;}
    }
    SubjectComponent=Subject;Components.SetNum(Prepared->Layers.Num());
    Started.Init(false,Prepared->Layers.Num());Stopped.Init(false,Prepared->Layers.Num());
    PlaybackTime=0;bRunning=true;bPaused=false;bEmissionStopped=false;
    AdvanceComposition(0);
    return true;
}

void AMCVFXCompositionActor::StartLayer(int32 Index)
{
    const auto& Layer=Prepared->Layers[Index];
    Started[Index]=true;
    if(!Layer.bEnabled || (bHonorSolo && bHasSolo && !Layer.bSolo)) {Stopped[Index]=true;return;}
    if(Layer.Binding==EMCVFXCompositionBinding::Subject && !SubjectComponent.IsValid())
    {
        LastError=TEXT("Subject disappeared; its layer was cancelled.");Stopped[Index]=true;return;
    }
    auto* Component=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient);
    Component->SetAutoActivate(false);Component->SetAutoDestroy(false);
    Component->SetAsset(PreparedSystems[Index]);
    if(Layer.Binding==EMCVFXCompositionBinding::Subject) Component->SetupAttachment(SubjectComponent.Get(),Layer.Socket);
    else if(Layer.Binding==EMCVFXCompositionBinding::Composition) Component->SetupAttachment(GetRootComponent());
    if(Layer.Binding==EMCVFXCompositionBinding::World) Component->SetWorldTransform(Layer.Transform*GetActorTransform());
    else Component->SetRelativeTransform(Layer.Transform);
    // Values are applied to this instance before activation. Original systems remain untouched.
    for(const auto& Item:Layer.Floats) if(Item.bOverride) Component->SetVariableFloat(Item.Name,Item.Value);
    for(const auto& Item:Layer.Colors) if(Item.bOverride) Component->SetVariableLinearColor(Item.Name,Item.Value);
    for(const auto& Item:Layer.Vectors) if(Item.bOverride) Component->SetVariableVec3(Item.Name,Item.Value);
    for(const auto& Item:Layer.Bools) if(Item.bOverride) Component->SetVariableBool(Item.Name,Item.Value);
    AddInstanceComponent(Component);Component->RegisterComponentWithWorld(GetWorld());
    Components[Index]=Component;Component->Activate(true);
}

void AMCVFXCompositionActor::AdvanceComposition(float DeltaSeconds)
{
    if(!bRunning || bPaused || !Prepared || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0) return;
    PlaybackTime=FMath::Min(PlaybackTime+DeltaSeconds,Prepared->DurationSeconds+Prepared->TailSeconds);
    if(!bEmissionStopped)
    {
        for(int32 Index=0;Index<Prepared->Layers.Num();++Index)
        {
            const auto& Layer=Prepared->Layers[Index];
            const float End=Layer.EmissionSeconds>0?FMath::Min(Layer.StartSeconds+Layer.EmissionSeconds,Prepared->DurationSeconds):Prepared->DurationSeconds;
            // A frame which skips an entire layer must not flash that layer after its scheduled end.
            if(!Started[Index] && PlaybackTime>=Layer.StartSeconds)
            {
                if(PlaybackTime<End) StartLayer(Index);
                else {Started[Index]=true;Stopped[Index]=true;}
            }
            if(!Stopped[Index] && PlaybackTime>=End)
            {
                if(IsValid(Components[Index])) Components[Index]->Deactivate();
                Stopped[Index]=true;
            }
        }
        if(PlaybackTime>=Prepared->DurationSeconds) bEmissionStopped=true;
    }
    if(bEmissionStopped)
    {
        bool bComplete=true;
        for(UNiagaraComponent* Component:Components) if(IsValid(Component) && !Component->IsComplete()) {bComplete=false;break;}
        if(bComplete || PlaybackTime>=Prepared->DurationSeconds+Prepared->TailSeconds) StopComposition(true);
    }
}

void AMCVFXCompositionActor::PauseComposition(bool bInPaused)
{
    bPaused=bInPaused;
    for(UNiagaraComponent* Component:Components) if(IsValid(Component)) Component->SetPaused(bPaused);
}

void AMCVFXCompositionActor::StopComposition(bool bImmediate)
{
    if(bImmediate)
    {
        for(UNiagaraComponent* Component:Components) if(IsValid(Component)) {Component->DeactivateImmediate();Component->DestroyComponent();}
        Components.Reset();Started.Reset();Stopped.Reset();bRunning=false;bPaused=false;SubjectComponent.Reset();
    }
    else if(bRunning)
    {
        bEmissionStopped=true;PlaybackTime=FMath::Max(PlaybackTime,Prepared?Prepared->DurationSeconds:0.f);
        for(UNiagaraComponent* Component:Components) if(IsValid(Component)) Component->Deactivate();
    }
}

int32 AMCVFXCompositionActor::GetLiveLayerCount() const
{
    int32 Result=0;for(UNiagaraComponent* Component:Components) if(IsValid(Component) && !Component->IsComplete()) ++Result;
    return Result;
}

void AMCVFXCompositionActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!bExternallyTicked) AdvanceComposition(DeltaSeconds);
}

void AMCVFXCompositionActor::EndPlay(const EEndPlayReason::Type Reason)
{
    StopComposition(true);PreparedSystems.Reset();Prepared=nullptr;
    Super::EndPlay(Reason);
}
