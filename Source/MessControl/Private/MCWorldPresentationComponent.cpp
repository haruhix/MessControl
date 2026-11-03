#include "MCWorldPresentationComponent.h"

#include "MCEventSFX.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXSourceContracts.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCWorldPresentation,Log,All);

UMCWorldPresentationComponent::UMCWorldPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetIsReplicatedByDefault(false); // Existing producer state/OnRep is the transport.
}

void UMCWorldPresentationComponent::BeginPlay()
{
    Super::BeginPlay();if(GetOwner())AddTickPrerequisiteActor(GetOwner());FString Error;PrepareSources(Error);
}

bool UMCWorldPresentationComponent::PrepareSources(FString& Error,bool bAllowLoad)
{
    Error.Reset();auto* Owner=GetOwner();
    if(!IsValid(Owner)||!GetWorld()){Error=TEXT("Presentation source has no live owner/world.");LastError=Error;return false;}
    bool bAllReady=true;
    for(const FName ID:Sources)
    {
        if(Channels.Contains(ID))continue;
        const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!Contract){bAllReady=false;Error=TEXT("Unregistered presentation source: ")+ID.ToString();continue;}
        auto* Library=Cast<UMCVFXPresentationLibrary>(bAllowLoad?Contract->Library.TryLoad():Contract->Library.ResolveObject());
        if(!Library){bAllReady=false;Error=TEXT("Saved location presentation library is not available: ")+Contract->Library.ToString();continue;}
        auto* Channel=NewObject<UMCVFXPresentationComponent>(Owner,NAME_None,RF_Transient);
        Channel->SetIsReplicated(false);Channel->SourceID=ID;Channel->Events=Contract->Events;Channel->Library=Library;
        FString PrepareError;
        // Prepare before registration so even dedicated-server BeginPlay cannot
        // enter the generic component's optional loading path for this adapter.
        if(!Channel->Prepare(PrepareError,bAllowLoad))
        {
            Channel->DestroyComponent();bAllReady=false;Error=PrepareError;continue;
        }
        Owner->AddInstanceComponent(Channel);Channel->RegisterComponent();
        RetainedLibraries.AddUnique(Library);Channels.Add(ID,Channel);
    }
    LastError=Error;return bAllReady;
}

bool UMCWorldPresentationComponent::UpdateState(FName SourceID,FName Event,bool bActive,const FTransform& Frame,
    const FMCPerkVFXGameplayValues& Values,UActorComponent* TickPrerequisite,FString& Error)
{
    const FString Key=SourceID.ToString()+TEXT("/")+Event.ToString();
    auto* Channel=GetSource(SourceID);
    if(!Channel){Error=TEXT("Presentation source was not prepared before this native state: ")+SourceID.ToString();
        if(bActive&&!FailedStateEvents.Contains(Key))UE_LOG(LogMCWorldPresentation,Warning,TEXT("%s: %s"),*Key,*Error);
        FailedStateEvents.Add(Key);LastError=Error;return false;}
    const bool Result=Channel->UpdateLocalSourceState(SourceID,Event,bActive,Frame,Values,TickPrerequisite,Error);
    if(Result)FailedStateEvents.Remove(Key);
    else {if(bActive&&!FailedStateEvents.Contains(Key))UE_LOG(LogMCWorldPresentation,Warning,TEXT("%s: %s"),*Key,*Error);FailedStateEvents.Add(Key);}
    LastError=Error;return Result;
}

void UMCWorldPresentationComponent::StopStates(FName SourceID)
{
    for(auto It=Pulses.CreateIterator();It;++It)if(SourceID.IsNone()||It.Value().SourceID==SourceID)It.RemoveCurrent();
    if(Pulses.IsEmpty())SetComponentTickEnabled(false);
    for(const auto& Pair:Channels)if((SourceID.IsNone()||Pair.Key==SourceID)&&Pair.Value)
        Pair.Value->StopLocalSourceStates(Pair.Key);
}

bool UMCWorldPresentationComponent::Pulse(FName SourceID,FName Event,const FTransform& Frame,
    const FMCPerkVFXGameplayValues& Values,float NativeLifeSeconds,FString& Error)
{
    if(!GetWorld()||!FMath::IsFinite(NativeLifeSeconds)||NativeLifeSeconds<=0.f)
    {Error=TEXT("Native presentation pulse needs a finite positive lifetime.");return false;}
    FNativePulse State;State.SourceID=SourceID;State.Event=Event;State.Frame=Frame;State.Values=Values;
    State.Life=FMath::Clamp(NativeLifeSeconds,.01f,60.f);
    const float Age=Values.LocalStateAgeSeconds>=0?Values.LocalStateAgeSeconds:0.f;
    State.Started=GetWorld()->GetTimeSeconds()-Age;
    State.Values.Duration=State.Life;State.Values.LocalStateAgeSeconds=Age;
    if(!State.Values.EpisodeID.IsValid())State.Values.EpisodeID=FGuid::NewGuid();
    const bool Result=UpdateState(SourceID,Event,Age<State.Life,Frame,State.Values,this,Error);
    if(Result&&Age<State.Life){Pulses.Add(SourceID.ToString()+TEXT("/")+Event.ToString(),MoveTemp(State));SetComponentTickEnabled(true);}
    return Result;
}

void UMCWorldPresentationComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if(!GetWorld())return;
    const double Now=GetWorld()->GetTimeSeconds();
    for(auto It=Pulses.CreateIterator();It;++It)
    {
        auto& State=It.Value();const float Age=FMath::Max(0.f,float(Now-State.Started));
        State.Values.LocalStateAgeSeconds=Age;State.Values.Fill=FMath::Clamp(Age/State.Life,0.f,1.f);
        FString Error;UpdateState(State.SourceID,State.Event,Age<State.Life,State.Frame,State.Values,this,Error);
        if(Age>=State.Life)It.RemoveCurrent();
    }
    if(Pulses.IsEmpty())SetComponentTickEnabled(false);
}

bool UMCWorldPresentationComponent::UsesNativeVisuals(FName SourceID,FName Event) const
{
    const auto* Channel=GetSource(SourceID);EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    if(FailedStateEvents.Contains(SourceID.ToString()+TEXT("/")+Event.ToString())
        || !Channel||!Channel->GetPreparedEventMode(SourceID,Event,Mode))return true;
    if(Mode==EMCPerkVFXSlotMode::UseDefault)return true;
    if(Mode==EMCPerkVFXSlotMode::Disabled)return false;
    const auto* Native=Channel->GetPreparedNativeSource(SourceID);
    return Native&&Native->ShouldRenderNativeAccents(Event,Mode);
}

bool UMCWorldPresentationComponent::UsesOriginalAudio(FName SourceID,FName Event) const
{
    if(!MCVFXSourceContracts::CanSuppressOriginalSFX(SourceID,Event))return true;
    const auto* Channel=GetSource(SourceID);EMCEventSFXMode Mode=EMCEventSFXMode::UseDefault;
    if(!Channel||!Channel->GetPreparedAudioMode(SourceID,Event,Mode))return true;
    return Mode==EMCEventSFXMode::UseDefault||Mode==EMCEventSFXMode::Add;
}

UMCVFXPresentationComponent* UMCWorldPresentationComponent::GetSource(FName SourceID) const
{const auto* Found=Channels.Find(SourceID);return Found?Found->Get():nullptr;}
void UMCWorldPresentationComponent::GetPresentationComponents(TArray<UMCVFXPresentationComponent*>& Out) const
{for(const auto& Pair:Channels)if(Pair.Value)Out.AddUnique(Pair.Value.Get());}

void UMCWorldPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopStates();
    // Match the shared component: accepted world-space one-shots outlive
    // ordinary source destruction. Explicit cancellation/reset owns their stop.
    if(Reason!=EEndPlayReason::Destroyed)
        for(const auto& Pair:Channels)if(Pair.Value)Pair.Value->StopPresentation(true);
    Channels.Reset();RetainedLibraries.Reset();Super::EndPlay(Reason);
}

bool MCWorldPresentation::UsesOriginalAudio(const AActor* Owner,FName SourceID,FName Event)
{
    const auto* Adapter=IsValid(Owner)?Owner->FindComponentByClass<UMCWorldPresentationComponent>():nullptr;
    return !Adapter||Adapter->UsesOriginalAudio(SourceID,Event);
}

UMCWorldPresentationComponent* MCWorldPresentation::PrepareOwner(AActor* Owner,const TArray<FName>& Sources)
{
    if(!IsValid(Owner)||!Owner->GetWorld())return nullptr;
    auto* Adapter=Owner->FindComponentByClass<UMCWorldPresentationComponent>();
    const bool bNew=!Adapter;
    if(!Adapter)Adapter=NewObject<UMCWorldPresentationComponent>(Owner,NAME_None,RF_Transient);
    for(FName Source:Sources)Adapter->Sources.AddUnique(Source);
    if(bNew){Owner->AddInstanceComponent(Adapter);Adapter->RegisterComponent();}
    FString Error;Adapter->PrepareSources(Error);return Adapter;
}

bool MCWorldPresentation::Pulse(AActor* Owner,FName SourceID,FName Event,const FTransform& Frame,
    const FMCPerkVFXGameplayValues& Values,float NativeLifeSeconds)
{
    auto* Adapter=IsValid(Owner)?Owner->FindComponentByClass<UMCWorldPresentationComponent>():nullptr;
    if(!Adapter)return false;
    FString Error;return Adapter->Pulse(SourceID,Event,Frame,Values,NativeLifeSeconds,Error);
}
