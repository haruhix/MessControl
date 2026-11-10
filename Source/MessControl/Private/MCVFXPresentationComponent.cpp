#include "MCVFXPresentationComponent.h"

#include "MCNutVFXCatalog.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCEventSFXPlayer.h"
#include "MCEventSFXReadiness.h"
#include "MCGameState.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "AudioDevice.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCEventPresentation,Log,All);

namespace MCEventPresentation
{
// Provenance belongs only to players allocated by this component, not active
// actions, artist-placed Niagara actors, or the existing default reaction path.
const FName OwnedPlayerTag(TEXT("MC.VFXPresentationComponent.Player"));
const FName OwnedAudioTag(TEXT("MC.VFXPresentationComponent.Audio"));
constexpr int32 MaxPendingReceipts=32;
constexpr double ReceiptLifetime=60.;
double Now(const UWorld* World)
{const auto* GS=World?World->GetGameState():nullptr;return GS?GS->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.;}
template<class T> T* Resolve(const TSoftObjectPtr<T>& Ref,bool bAllowLoad)
{return bAllowLoad?Ref.LoadSynchronous():Ref.Get();}
bool Matches(const FMCVFXPresentationCancel& Scope,FName ID,FGuid Episode)
{return (Scope.SourceID.IsNone() || Scope.SourceID==ID) && (!Scope.EpisodeID.IsValid() || Scope.EpisodeID==Episode);}
bool HasAudioOutput(const UWorld* World)
{return World && World->GetNetMode()!=NM_DedicatedServer && World->AllowAudioPlayback()
    && GEngine && GEngine->UseSound() && bool(World->GetAudioDevice());}
bool HasAudio(const FMCPreparedEventSFX& Data)
{return Data.Slot.HasCustomLayers() && Data.Slot.Layers.ContainsByPredicate([&](const auto& L){return L.bEnabled&&!Data.DisabledGroupIDs.Contains(L.GroupID);});}
bool Running(const FMCVFXPresentationPlayerHandle& H)
{return (H.Player.IsValid()&&H.Player->IsRunning()) || (H.AudioPlayer.IsValid()&&H.AudioPlayer->IsRunning());}
bool ValidCaptured(const TArray<FMCEchoToolPart>& Parts,double ContactAt)
{
    if(Parts.Num()>3 || !FMath::IsFinite(ContactAt))return false;
    for(const auto& Part:Parts)if(!IsValid(Part.Mesh) || Part.ContactPose.ContainsNaN()
        || !Part.ContactPose.GetRotation().IsNormalized() || Part.ContactPose.GetScale3D().GetAbsMin()<.0001)return false;
    return true;
}
void CaptureContext(const FMCPerkVFXGameplayValues& Values,FMCVFXPresentationReceipt& Receipt)
{
    if(auto* Subject=Values.VisualSubject.Get();IsValid(Subject)) {
        Receipt.bHasVisualSubject=true;Receipt.VisualSubjectActor=Subject->GetOwner();Receipt.VisualSubjectComponent=Subject->GetFName();
    }
    Receipt.CapturedMeshes=Values.CapturedMeshes;Receipt.bCapturedMeshOverhead=Values.bCapturedMeshOverhead;
    Receipt.CapturedContactAt=Values.CapturedContactAt;
    Receipt.NativeInputs=Values.NativeInputs;
}
USceneComponent* ResolveSubject(const FMCVFXPresentationReceipt& Receipt,AActor* DefaultOwner)
{
    if(!Receipt.bHasVisualSubject)return DefaultOwner?DefaultOwner->GetRootComponent():nullptr;
    if(!IsValid(Receipt.VisualSubjectActor))return nullptr;
    TInlineComponentArray<USceneComponent*> Components(Receipt.VisualSubjectActor);
    for(auto* Component:Components)if(IsValid(Component) && Component->GetFName()==Receipt.VisualSubjectComponent)return Component;
    return nullptr;
}
bool ProjectGroups(const FMCEventPresentationGroupSet* Set,FMCVFXEventGroupPolicy& Out,FString& Error)
{
    if(Set&&!Set->Validate(Error))return false;
    if(Set)for(const auto& Group:Set->Groups) {
        Out.SharedIDs.Add(Group.ID);if(!Group.bEnabled)Out.DisabledIDs.Add(Group.ID);
        if(Group.bMuteSFX)Out.MutedSFXIDs.Add(Group.ID);
    }
    return true;
}
bool ValidateProjection(const FMCVFXEventGroupPolicy& Policy,FString& Error)
{
    if(Policy.SharedIDs.Num()>32 || Policy.DisabledIDs.Num()>32 || Policy.MutedSFXIDs.Num()>32)
    {Error=TEXT("Shared presentation policy exceeds 32 groups.");return false;}
    FMCPreparedEventSFX Empty;
    return MCEventSFX::CompileSharedGroupPolicy(Policy.SharedIDs,Policy.DisabledIDs,Policy.MutedSFXIDs,Empty,Error);
}
}

UMCVFXPresentationComponent::UMCVFXPresentationComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=true;PrimaryComponentTick.TickInterval=.1f;
    Library=UMCVFXPresentationLibrary::MouthLibraryPath();
}

bool UMCVFXPresentationComponent::PrepareSlot(const FMCPerkVFXSlot& Slot,bool bAllowLoad,
    FMCPreparedPerkVFXComposition& Out,FString& Error,bool* bWaiting,const FMCVFXEventGroupPolicy* Groups)
{
    if(bWaiting)*bWaiting=false;
    if(!UMCPerkVFXProfile::ValidateSlot(Slot,Error))return false;
    Out=FMCPreparedPerkVFXComposition();Out.Outer=Slot;
    if(Slot.Mode==EMCPerkVFXSlotMode::UseDefault || Slot.Mode==EMCPerkVFXSlotMode::Disabled)return true;
    auto* Snapshot=NewObject<UMCVFXComposition>(GetTransientPackage(),NAME_None,RF_Transient);
    if(Slot.Mode==EMCPerkVFXSlotMode::Composition) {
        const auto* Source=MCEventPresentation::Resolve(Slot.Composition,bAllowLoad);
        if(!Source){if(bWaiting)*bWaiting=true;Error=TEXT("Runtime composition is not resident; prepare the location before gameplay.");return false;}
        // Editor recipe subclasses cannot be shipped as the runtime source.
        if(Source->GetClass()!=UMCVFXComposition::StaticClass()) {Error=TEXT("Presentation requires a native runtime composition, not an editor recipe subclass.");return false;}
        if(!Source->Validate(Error))return false;
        Snapshot->Layers=Source->Layers;Snapshot->DurationSeconds=Source->DurationSeconds;Snapshot->TailSeconds=Source->TailSeconds;
    } else {
        FMCVFXCompositionLayer Layer;Layer.Name=TEXT("System");Layer.System=Slot.System;
        Snapshot->Layers.Add(Layer);
    }
    Out.Snapshot=Snapshot;Out.Systems.SetNum(Snapshot->Layers.Num());
    for(int32 I=0;I<Snapshot->Layers.Num();++I) {
        auto& Layer=Snapshot->Layers[I];
        if(Groups) {
            if(Layer.GroupID.IsValid()&&!Groups->SharedIDs.Contains(Layer.GroupID))
            {Error=TEXT("VFX layer refers to a group outside its actual event.");return false;}
            if(Groups->DisabledIDs.Contains(Layer.GroupID))Layer.bEnabled=false;
        }
        auto* System=MCEventPresentation::Resolve(Layer.System,bAllowLoad);
        if(!Layer.bEnabled){Out.Systems[I]=System;continue;} // Retain authored roots, even when a group is disabled.
        if(!System){if(bWaiting)*bWaiting=true;Error=TEXT("Niagara source is not resident: ")+Layer.System.ToString();return false;}
        if(!UMCVFXComposition::ValidateParameters(Layer,System,Error)
            || !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),System,Error))return false;
        Out.Systems[I]=System;
    }
    return true;
}

bool UMCVFXPresentationComponent::PrepareAudioSlot(FName ID,FName Event,const FMCEventSFXSlot& Slot,const FMCVFXEventGroupPolicy& Groups,
    bool bAllowLoad,FMCPreparedEventSFX& Out,FString& Error,bool* bWaiting)
{
    if(!MCEventSFX::ValidateNativePolicy(Slot,MCVFXSourceContracts::CanSuppressOriginalSFX(ID,Event),Error)
        || !MCEventPresentation::ValidateProjection(Groups,Error))return false;
    auto Effective=Slot;
    for(auto& L:Effective.Layers)if(Groups.DisabledIDs.Contains(L.GroupID))L.bEnabled=false;
    FMCPreparedEventSFX Candidate;
    if(!MCEventSFX::Prepare(Effective,bAllowLoad,Candidate,Error,bWaiting))return false;
    Candidate.Slot=Slot; // Preserve the immutable authored child state; shared policy stays orthogonal.
    if(!MCEventSFX::CompileSharedGroupPolicy(Groups.SharedIDs,Groups.DisabledIDs,Groups.MutedSFXIDs,Candidate,Error))return false;
    const auto* Binding=AudioSocketSources.Find(ID);
    auto* SocketSource=Binding?Binding->Get():nullptr;
    for(const auto& L:Candidate.Slot.Layers)
        if(MCEventPresentation::HasAudio(Candidate) && L.bEnabled&&!Candidate.DisabledGroupIDs.Contains(L.GroupID)
            &&L.Space==EMCEventSFXSpace::SourceSocket
            &&(!IsValid(SocketSource)||!SocketSource->DoesSocketExist(L.Socket)))
        {Error=TEXT("This source has no provider-supplied native audio socket: ")+L.Socket.ToString();return false;}
    FString CertificateError;CertifyAudioSlot(Candidate,CertificateError); // Never manufactures a certificate.
    Out=MoveTemp(Candidate);return true;
}

bool UMCVFXPresentationComponent::BuildPreparedSource(FName ID,const TArray<FName>& AllowedEvents,UMCPerkVFXProfile* Profile,
    bool bAllowLoad,FMCVFXPreparedSource& Out,FString& Error)
{
    Error.Reset();
    // Validate even an empty/default profile against this exact provider contract.
    auto* Effective=Profile?Profile:NewObject<UMCPerkVFXProfile>(GetTransientPackage());
    if(!Effective->ValidateForEvents(AllowedEvents,Error))return false;
    TMap<FName,FMCPreparedPerkVFXComposition> Candidate;
    TMap<FName,FMCPreparedEventSFX> AudioCandidate;
    TMap<FName,FMCVFXEventGroupPolicy> GroupCandidate;
    int32 Count=0;
    for(FName Event:AllowedEvents) {
        FMCPerkVFXSlot Default;const auto* Slot=Effective->Slots.Find(Event);
        FMCVFXEventGroupPolicy Groups;
        if(!MCEventPresentation::ProjectGroups(Effective->EventGroups.Find(Event),Groups,Error))return false;
        FMCPreparedPerkVFXComposition Prepared;
        if(!PrepareSlot(Slot?*Slot:Default,bAllowLoad,Prepared,Error,nullptr,&Groups)) {Error=Event.ToString()+TEXT(": ")+Error;return false;}
        FMCEventSFXSlot DefaultAudio;const auto* Audio=Effective->SFXSlots.Find(Event);FMCPreparedEventSFX AudioData;
        if(!PrepareAudioSlot(ID,Event,Audio?*Audio:DefaultAudio,Groups,bAllowLoad,AudioData,Error)) {Error=Event.ToString()+TEXT(": ")+Error;return false;}
        if(Prepared.Snapshot)for(int32 I=0;I<Prepared.Systems.Num();++I)
            if(Prepared.Snapshot->Layers[I].bEnabled&&Prepared.Systems[I])++Count;
        if(Count>128){Error=TEXT("A presentation component accepts at most 128 enabled layers across its events.");return false;}
        Candidate.Add(Event,MoveTemp(Prepared));
        AudioCandidate.Add(Event,MoveTemp(AudioData));GroupCandidate.Add(Event,MoveTemp(Groups));
    }
    FMCVFXPreparedNativePresentation Native;
    if(!Effective->NativePresentation.IsNull()) {
        auto* Source=MCEventPresentation::Resolve(Effective->NativePresentation,bAllowLoad);
        if(!Source){Error=TEXT("Native presentation settings are not resident: ")+Effective->NativePresentation.ToString();return false;}
        if(!Source->ValidateForSource(ID,AllowedEvents,Error))return false;
        auto* Loading=GetWorld()?GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr;
        if(!Loading || !Loading->GetPreparedNativeStyle(Source,Native)) {
            if(!bAllowLoad && GetNetMode()!=NM_DedicatedServer && GetWorld() && GetWorld()->IsGameWorld()
                && (!Loading || Loading->IsReady())) {
                Error=TEXT("Native material variants were not prepared by this location before readiness.");return false;
            }
            if(!FMCVFXPreparedNativePresentation::Prepare(ID,AllowedEvents,Source,this,bAllowLoad,Native,Error))return false;
        }
    }
    Out.AllowedEvents=AllowedEvents;Out.Events=MoveTemp(Candidate);Out.Native=MoveTemp(Native);
    Out.AudioEvents=MoveTemp(AudioCandidate);Out.GroupPolicies=MoveTemp(GroupCandidate);return true;
}
bool UMCVFXPresentationComponent::PrepareSourceProfile(FName ID,const TArray<FName>& AllowedEvents,
    UMCPerkVFXProfile* Profile,FString& Error,bool bAllowLoad)
{
    Error.Reset();
    if(ID.IsNone() || (!PreparedSources.Contains(ID) && PreparedSources.Num()>=64))
    {Error=TEXT("Presentation source must be named; a pawn accepts at most 64 prepared sources.");LastError=Error;return false;}
    if(const auto* Override=ScopedOverrides.Find(ID)) {
        if(AllowedEvents.Num()!=Override->AllowedEvents.Num()) {Error=TEXT("Scoped override event contract changed.");LastError=Error;return false;}
        for(FName Event:AllowedEvents)if(!Override->AllowedEvents.Contains(Event)){Error=TEXT("Scoped override event contract changed.");LastError=Error;return false;}
        PreparedSources.Add(ID,*Override);bPrepared=true;LastError.Reset();SetComponentTickEnabled(!PendingReceipts.IsEmpty()||!LocalSourceStates.IsEmpty()||!LocalEventPlayers.IsEmpty());return true;
    }
    FMCVFXPreparedSource Candidate;
    if(!BuildPreparedSource(ID,AllowedEvents,Profile,bAllowLoad,Candidate,Error)){LastError=Error;return false;}
    PreparedSources.Add(ID,MoveTemp(Candidate));bPrepared=true;LastError.Reset();
    SetComponentTickEnabled(!PendingReceipts.IsEmpty()||!LocalSourceStates.IsEmpty()||!LocalEventPlayers.IsEmpty());return true;
}
bool UMCVFXPresentationComponent::PrepareScopedOverride(FName ID,const TArray<FName>& AllowedEvents,
    UMCPerkVFXProfile* Profile,FString& Error)
{
    if(!Profile || ID.IsNone() || (!PreparedSources.Contains(ID) && PreparedSources.Num()>=64))
    {Error=TEXT("A scoped presentation override needs a named source and profile within the 64-source bound.");LastError=Error;return false;}
    FMCVFXPreparedSource Candidate;
    if(!BuildPreparedSource(ID,AllowedEvents,Profile,true,Candidate,Error)){LastError=Error;return false;}
    ScopedOverrides.Add(ID,Candidate);PreparedSources.Add(ID,MoveTemp(Candidate));
    bPrepared=true;LastError.Reset();SetComponentTickEnabled(!PendingReceipts.IsEmpty()||!LocalSourceStates.IsEmpty()||!LocalEventPlayers.IsEmpty());return true;
}
bool UMCVFXPresentationComponent::PrepareProfile(UMCPerkVFXProfile* Profile,bool bAllowLoad,FString& Error)
{return PrepareSourceProfile(SourceID,Events,Profile,Error,bAllowLoad);}
const FMCPreparedPerkVFXComposition* UMCVFXPresentationComponent::FindPreparedEvent(FName ID,FName Event) const
{
    const auto* Source=PreparedSources.Find(ID);return Source?Source->Events.Find(Event):nullptr;
}
bool UMCVFXPresentationComponent::IsSourceReadyForPlayback(FName ID) const
{
    const auto* Source=PreparedSources.Find(ID);if(!Source)return false;
    if(GetNetMode()==NM_DedicatedServer)return true;
    if(!Source->Native.IsReady())return false;
    if(MCEventPresentation::HasAudioOutput(GetWorld()))for(const auto& Audio:Source->AudioEvents)
        if(MCEventPresentation::HasAudio(Audio.Value)) {
            auto Check=Audio.Value;FString Error;if(!CertifyAudioSlot(Check,Error))return false;
        }
    for(const auto& Event:Source->Events)for(UNiagaraSystem* System:Event.Value.Systems)
        if(System && !System->IsReadyToRun())return false;
    return true;
}

bool UMCVFXPresentationComponent::GetPreparedEventMode(FName ID,FName Event,EMCPerkVFXSlotMode& OutMode) const
{
    if(const auto* Prepared=FindPreparedEvent(ID,Event)){OutMode=Prepared->Outer.Mode;return true;}
    return false;
}
bool UMCVFXPresentationComponent::GetPreparedAudioMode(FName ID,FName Event,EMCEventSFXMode& OutMode) const
{
    if(const auto* Source=PreparedSources.Find(ID))if(const auto* Audio=Source->AudioEvents.Find(Event))
    {OutMode=Audio->Slot.Mode;return true;}
    return false;
}
bool UMCVFXPresentationComponent::CopyPreparedSourceTo(FName ID,UMCVFXPresentationComponent* Target,FString& Error) const
{
    Error.Reset();const auto* Source=PreparedSources.Find(ID);
    if(!Source || !IsValid(Target) || Target==this || !GetWorld() || Target->GetWorld()!=GetWorld()
        || Target->SourceID!=ID || Target->Events.Num()!=Source->AllowedEvents.Num()
        || Target->IssuedSerial || Target->ReceivedSerial || Target->IsPresentationRunning())
    {Error=TEXT("Prepared handoff needs a matching, uncommitted presentation owner in the same world.");return false;}
    TSet<FName> Seen;
    for(FName Event:Target->Events) {
        if(Seen.Contains(Event) || !Source->AllowedEvents.Contains(Event))
        {Error=TEXT("Prepared handoff event contracts differ.");return false;}
        Seen.Add(Event);
    }
    if(!IsSourceReadyForPlayback(ID))
    {Error=TEXT("Prepared handoff cannot commit an effect which is not ready.");return false;}
    // Snapshots/original systems are strong immutable data. Do not rediscover a
    // potentially edited profile or copy the source actor's transport state.
    const FMCVFXPreparedSource Copy=*Source;
    Target->ScopedOverrides.Add(ID,Copy);Target->PreparedSources.Add(ID,Copy);
    // The local preparation owner is shared only inside this same world.
    // Its certificate is never serialized into an event/network payload.
    Target->LocalAudioReadiness=LocalAudioReadiness;
    Target->bPrepared=true;Target->bHasOverride=true;Target->LastError.Reset();
    Target->SetComponentTickEnabled(!Target->PendingReceipts.IsEmpty()||!Target->LocalSourceStates.IsEmpty()||!Target->LocalEventPlayers.IsEmpty());return true;
}
bool UMCVFXPresentationComponent::BindSourceEpisodeTickSource(FName ID,FGuid Episode,AActor* TickSource,FString& Error)
{
    Error.Reset();
    if(ID.IsNone() || !Episode.IsValid() || !IsValid(TickSource) || TickSource->IsActorBeingDestroyed()
        || !GetWorld() || TickSource->GetWorld()!=GetWorld() || TickSource->IsA<AMCPerkVFXCompositionPlayer>()
        || (!SourceID.IsNone() && SourceID!=ID))
    {Error=TEXT("A local frame dependency needs a native actor and matching episode in this world.");return false;}
    EpisodeTickSources.RemoveAll([](const auto& V){return !V.Actor.IsValid() || V.Actor->IsActorBeingDestroyed();});
    auto* Entry=EpisodeTickSources.FindByPredicate([&](const auto& V){return V.SourceID==ID && V.EpisodeID==Episode;});
    if(!Entry) {
        if(EpisodeTickSources.Num()>=128){Error=TEXT("Native frame dependency capacity is 128 active episodes.");return false;}
        FMCVFXPresentationTickSource V;V.SourceID=ID;V.EpisodeID=Episode;V.Actor=TickSource;
        Entry=&EpisodeTickSources.Add_GetRef(V);
    } else if(Entry->Actor.Get()!=TickSource) {
        for(const auto& Pair:Players)if(Pair.Value.SourceID==ID && Pair.Value.EpisodeID==Episode)
            if(auto* Previous=Entry->Actor.Get();Previous && Previous!=GetOwner()) {
                if(auto* Player=Pair.Value.Player.Get())Player->RemoveTickPrerequisiteActor(Previous);
                if(auto* Audio=Pair.Value.AudioPlayer.Get())Audio->RemoveTickPrerequisiteActor(Previous);
            }
        for(const auto& Local:LocalEventPlayers)if(Local.Handle.SourceID==ID && Local.Handle.EpisodeID==Episode)
            if(auto* Previous=Entry->Actor.Get();Previous && Previous!=GetOwner()) {
                if(auto* Player=Local.Handle.Player.Get())Player->RemoveTickPrerequisiteActor(Previous);
                if(auto* Audio=Local.Handle.AudioPlayer.Get())Audio->RemoveTickPrerequisiteActor(Previous);
            }
        Entry->Actor=TickSource;
    }
    for(const auto& Pair:Players)if(Pair.Value.SourceID==ID && Pair.Value.EpisodeID==Episode)
    {
        if(auto* Player=Pair.Value.Player.Get())Player->AddTickPrerequisiteActor(TickSource);
        if(auto* Audio=Pair.Value.AudioPlayer.Get())Audio->AddTickPrerequisiteActor(TickSource);
    }
    for(const auto& Local:LocalEventPlayers)if(Local.Handle.SourceID==ID && Local.Handle.EpisodeID==Episode) {
        if(auto* Player=Local.Handle.Player.Get())Player->AddTickPrerequisiteActor(TickSource);
        if(auto* Audio=Local.Handle.AudioPlayer.Get())Audio->AddTickPrerequisiteActor(TickSource);
    }
    return true;
}

bool UMCVFXPresentationComponent::Prepare(FString& Error,bool bAllowLoad)
{
    Error.Reset();
    if(SourceID==MCNutVFX::ChainSource())return MCNutVFX::Prepare(this,SourceID,Error,bAllowLoad);
    if(ScopedOverrides.Contains(SourceID) && IsSourcePrepared(SourceID))return true;
    if(SourceID.IsNone()){Error=TEXT("Presentation source ID is empty.");LastError=Error;return false;}
    auto* Source=MCEventPresentation::Resolve(Library,bAllowLoad);
    if(!Source){Error=TEXT("Presentation library is not resident: ")+Library.ToString();LastError=Error;return false;}
    const auto* Ref=Source->Profiles.Find(SourceID);
    auto* Profile=Ref && !Ref->IsNull()?MCEventPresentation::Resolve(*Ref,bAllowLoad):nullptr;
    if(Ref && !Ref->IsNull() && !Profile){Error=TEXT("Presentation profile is not resident: ")+Ref->ToString();LastError=Error;return false;}
    if(!PrepareProfile(Profile,bAllowLoad,Error)){LastError=Error;return false;}
    RetainedLibrary=Source;LastError.Reset();return true;
}

bool UMCVFXPresentationComponent::PrepareProfileOverride(UMCPerkVFXProfile* Profile,FString& Error)
{
    if(!Profile){Error=TEXT("An explicit presentation override needs a profile; use ClearProfileOverride to return to the library.");LastError=Error;return false;}
    if(!PrepareScopedOverride(SourceID,Events,Profile,Error)){LastError=Error;return false;}
    bHasOverride=true;LastError.Reset();return true;
}

void UMCVFXPresentationComponent::ClearProfileOverride()
{StopLocalSourceStates(SourceID);bHasOverride=false;ScopedOverrides.Remove(SourceID);PreparedSources.Remove(SourceID);bPrepared=!PreparedSources.IsEmpty();RetainedProfile=nullptr;SetComponentTickEnabled(true);}
void UMCVFXPresentationComponent::ClearScopedOverride(FName ID)
{StopSource(ID,true);StopLocalSourceStates(ID);ScopedOverrides.Remove(ID);PreparedSources.Remove(ID);bPrepared=!PreparedSources.IsEmpty();}
TArray<FName> UMCVFXPresentationComponent::GetPreparedSourceIDs() const
{TArray<FName> IDs;PreparedSources.GetKeys(IDs);IDs.Sort([](FName A,FName B){return A.LexicalLess(B);});return IDs;}

void UMCVFXPresentationComponent::GetPreparedResources(TArray<UNiagaraSystem*>& OutSystems) const
{
    OutSystems.Reset();for(const auto& Source:PreparedSources)for(const auto& Pair:Source.Value.Events)
        for(UNiagaraSystem* System:Pair.Value.Systems)if(System)OutSystems.AddUnique(System);
    for(const auto& Source:PreparedSources)Source.Value.Native.GetNiagaraSystems(OutSystems);
}
const FMCVFXPreparedNativePresentation* UMCVFXPresentationComponent::GetPreparedNativeSource(FName ID) const
{const auto* Source=PreparedSources.Find(ID);return Source && Source->Native.Snapshot?&Source->Native:nullptr;}
void UMCVFXPresentationComponent::GetPreparedNativeResources(TArray<UObject*>& OutObjects) const
{
    OutObjects.Reset();for(const auto& Source:PreparedSources)
        for(UObject* Resource:Source.Value.Native.Resources)if(Resource)OutObjects.AddUnique(Resource);
}
void UMCVFXPresentationComponent::GetPreparedAudioResources(TArray<UObject*>& OutObjects) const
{
    OutObjects.Reset();for(const auto& Source:PreparedSources)for(const auto& Event:Source.Value.AudioEvents) {
        for(USoundBase* Sound:Event.Value.Sounds)if(Sound)OutObjects.AddUnique(Sound);
        for(USoundAttenuation* Attenuation:Event.Value.Attenuations)if(Attenuation)OutObjects.AddUnique(Attenuation);
        for(USoundConcurrency* Concurrency:Event.Value.Concurrencies)if(Concurrency)OutObjects.AddUnique(Concurrency);
    }
}
void UMCVFXPresentationComponent::GetPreparedAudioSlots(TArray<FMCPreparedEventSFX>& OutSlots) const
{OutSlots.Reset();for(const auto& Source:PreparedSources)for(const auto& Event:Source.Value.AudioEvents)OutSlots.Add(Event.Value);}
bool UMCVFXPresentationComponent::CertifyAudioSlot(FMCPreparedEventSFX& Slot,FString& Error) const
{
    if(!MCEventPresentation::HasAudio(Slot)){Slot.bPlaybackResourcesPrepared=false;Error.Reset();return true;}
    const UMCEventSFXReadiness* Service=LocalAudioReadiness;
    if(!Service)if(const auto* Loading=GetWorld()?GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr)
        Service=Loading->GetEventAudioReadiness();
    if(!Service){Slot.bPlaybackResourcesPrepared=false;Error=TEXT("No location audio resource readiness service is available.");return false;}
    return Service->CertifyPrepared(Slot,Error);
}
bool UMCVFXPresentationComponent::CertifyPreparedAudio(const UMCEventSFXReadiness* Readiness,FString& Error)
{
    Error.Reset();
    if(!Readiness || !Readiness->IsReady())
    {Error=Readiness?Readiness->GetDiagnostic():TEXT("Audio readiness service is absent.");return false;}
    auto Sources=PreparedSources;auto Overrides=ScopedOverrides;auto Pending=PendingReceipts;
    auto CertifyMap=[&](auto& Map) {
        for(auto& Source:Map)for(auto& Event:Source.Value.AudioEvents)
            if(MCEventPresentation::HasAudio(Event.Value)&&!Readiness->CertifyPrepared(Event.Value,Error))return false;
        return true;
    };
    if(!CertifyMap(Sources)||!CertifyMap(Overrides))return false;
    for(auto& Receipt:Pending)if(Receipt.Receipt.bPlaySFX&&Receipt.bAudioPrepared
        &&MCEventPresentation::HasAudio(Receipt.PreparedAudio)&&!Readiness->CertifyPrepared(Receipt.PreparedAudio,Error))return false;
    PreparedSources=MoveTemp(Sources);ScopedOverrides=MoveTemp(Overrides);PendingReceipts=MoveTemp(Pending);
    LocalAudioReadiness=const_cast<UMCEventSFXReadiness*>(Readiness);return true;
}
bool UMCVFXPresentationComponent::BindSourceAudioSocket(FName ID,USceneComponent* Component,FString& Error)
{
    Error.Reset();
    if(ID.IsNone()||!IsValid(Component)||Component->GetWorld()!=GetWorld()
        ||(!SourceID.IsNone()&&SourceID!=ID))
    {Error=TEXT("An audio socket provider must explicitly resolve the actual native component in this world.");return false;}
    AudioSocketSources.Add(ID,Component);return true;
}

EMCVFXPresentationResult UMCVFXPresentationComponent::PlayEvent(FName Event,const FTransform& Frame,const FMCPerkVFXGameplayValues& Values,bool bDispatchVFX)
{
    if(!IsSourcePrepared(SourceID)){FString Error;if(!Prepare(Error,false)) {
        LastError=Error;OnEventAccepted.Broadcast(Event,EMCVFXPresentationResult::Rejected);OnSourceEventAccepted.Broadcast(SourceID,Event,EMCVFXPresentationResult::Rejected);return EMCVFXPresentationResult::Rejected;
    }}
    return PlaySourceEvent(SourceID,Event,Frame,Values,bDispatchVFX);
}

bool UMCVFXPresentationComponent::ValidatePreparedEventInput(const FMCPreparedPerkVFXComposition* Prepared,
    const FTransform& Frame,const FMCPerkVFXGameplayValues& Values,bool bDispatchVFX,FString& Error) const
{
    Error.Reset();
    if(!GetOwner() || !GetWorld() || !Prepared || Frame.ContainsNaN()
        || !Frame.GetRotation().IsNormalized() || Frame.GetScale3D().GetAbsMin()<.0001
        || !MCEventPresentation::ValidCaptured(Values.CapturedMeshes,Values.CapturedContactAt)
        || (Values.VisualSubject.IsValid() && Values.VisualSubject->GetWorld()!=GetWorld())
        || Values.Direction.ContainsNaN() || !FMath::IsFinite(Values.Radius) || !FMath::IsFinite(Values.Fill)
        || !FMath::IsFinite(Values.Intensity) || !FMath::IsFinite(Values.Duration)
        || (Values.bHasEndpoints && (Values.SourcePosition.ContainsNaN() || Values.TargetPosition.ContainsNaN()))
        || (Values.bHasColor && (!FMath::IsFinite(Values.Color.R) || !FMath::IsFinite(Values.Color.G)
            || !FMath::IsFinite(Values.Color.B) || !FMath::IsFinite(Values.Color.A))))
    {Error=TEXT("Presentation requires a prepared declared event and finite frame/values.");return false;}
    const bool Visual=Prepared->Outer.Mode==EMCPerkVFXSlotMode::Replace || Prepared->Outer.Mode==EMCPerkVFXSlotMode::Composition;
    if(!Values.NativeInputs.Validate(Error))return false;
    if(Visual && bDispatchVFX && Prepared->Snapshot) {
        for(int32 I=0;I<Prepared->Snapshot->Layers.Num();++I)if(Prepared->Snapshot->Layers[I].bEnabled
            && (!Prepared->Systems.IsValidIndex(I) || !Values.NativeInputs.ValidateForSystem(Prepared->Systems[I],Error)))
        {if(Error.IsEmpty())Error=TEXT("Native event data requires a prepared Niagara layer.");return false;}
    }
    if(Visual && bDispatchVFX && !Values.CapturedMeshes.IsEmpty() && GetWorld()->IsGameWorld() && GetNetMode()!=NM_DedicatedServer) {
        const auto* Loading=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
        for(const auto& Part:Values.CapturedMeshes)if(!Loading || !Loading->IsVisualResourceRetained(Part.Mesh.Get()))
        {Error=TEXT("Captured visual mesh was not retained by this location's readiness barrier.");return false;}
    }
    return true;
}

FMCVFXPresentationReceipt UMCVFXPresentationComponent::MakePreparedEventReceipt(FName ID,FName Event,
    const FTransform& Frame,const FMCPerkVFXGameplayValues& Values,const FMCVFXPreparedSource& Source,
    const FMCPreparedPerkVFXComposition& Prepared,bool bDispatchVFX) const
{
    FMCVFXPresentationReceipt Receipt;
    Receipt.SourceID=ID;Receipt.Event=Event;Receipt.Slot=Prepared.Outer;Receipt.Frame=Frame;Receipt.StartedAt=MCEventPresentation::Now(GetWorld());
    const bool Visual=Prepared.Outer.Mode==EMCPerkVFXSlotMode::Replace || Prepared.Outer.Mode==EMCPerkVFXSlotMode::Composition;
    Receipt.bPlayVFX=Visual&&bDispatchVFX;
    if(const auto* Audio=Source.AudioEvents.Find(Event)){Receipt.bPlaySFX=MCEventPresentation::HasAudio(*Audio);Receipt.SFXSlot=Audio->Slot;}
    MCEventPresentation::CaptureContext(Values,Receipt);
    if(const auto* Groups=Source.GroupPolicies.Find(Event))Receipt.GroupPolicy=*Groups;
    if(const auto* State=GetWorld()->GetGameState<AMCGameState>())Receipt.GameplayEpoch=State->GameplayLoadId;
    Receipt.Direction=Values.Direction;Receipt.Radius=Values.Radius;Receipt.Fill=Values.Fill;Receipt.Intensity=Values.Intensity;
    Receipt.Duration=Values.Duration;Receipt.Color=Values.Color;Receipt.bHasColor=Values.bHasColor;
    Receipt.SourcePosition=Values.SourcePosition;Receipt.TargetPosition=Values.TargetPosition;Receipt.bHasEndpoints=Values.bHasEndpoints;
    Receipt.Seed=Values.Seed;Receipt.bHasSeed=Values.bHasSeed;Receipt.EpisodeID=Values.EpisodeID;
    return Receipt;
}

EMCVFXPresentationResult UMCVFXPresentationComponent::PlaySourceEvent(FName ID,FName Event,const FTransform& Frame,const FMCPerkVFXGameplayValues& Values,bool bDispatchVFX)
{
    auto Reject=[this,ID,Event](const FString& Error){LastError=Error;UE_LOG(LogMCEventPresentation,Warning,TEXT("%s / %s: %s"),*ID.ToString(),*Event.ToString(),*Error);OnEventAccepted.Broadcast(Event,EMCVFXPresentationResult::Rejected);OnSourceEventAccepted.Broadcast(ID,Event,EMCVFXPresentationResult::Rejected);return EMCVFXPresentationResult::Rejected;};
    const auto* Prepared=FindPreparedEvent(ID,Event);const auto* Source=PreparedSources.Find(ID);
    if(!GetOwner() || !GetOwner()->HasAuthority() || !Source)
        return Reject(TEXT("Presentation requires an authority-accepted declared event and finite frame/values."));
    FString Error;
    if(!ValidatePreparedEventInput(Prepared,Frame,Values,bDispatchVFX,Error))return Reject(Error);
    const bool Visual=Prepared->Outer.Mode==EMCPerkVFXSlotMode::Replace || Prepared->Outer.Mode==EMCPerkVFXSlotMode::Composition;
    const auto Result=Visual?EMCVFXPresentationResult::Played:Prepared->Outer.Mode==EMCPerkVFXSlotMode::Disabled?
        EMCVFXPresentationResult::Suppressed:EMCVFXPresentationResult::UseDefault;
    const auto* Audio=Source->AudioEvents.Find(Event);
    const bool Sound=Audio&&MCEventPresentation::HasAudio(*Audio);LastAudioError.Reset();
    if((Visual && bDispatchVFX) || Sound) {
        if(IssuedSerial==MAX_uint32)return Reject(TEXT("Presentation receipt serial budget exhausted for this owner."));
        auto Receipt=MakePreparedEventReceipt(ID,Event,Frame,Values,*Source,*Prepared,bDispatchVFX);Receipt.Serial=++IssuedSerial;
        PruneScopedCancels();
        if(Receipt.EpisodeID.IsValid() && ScopedCancels.Num()>=128)return Reject(TEXT("Episode cancellation ledger reached its explicit 128-scope capacity."));
        // ONE transport owns both channels. Audio-only receipts never replay native
        // Default/Disabled visuals, and an audio output error cannot select a VFX fallback.
        if(!ReceiveReceipt(Receipt,Prepared,Error,false,Audio))return Reject(Error);
        LastReceipt=Receipt;MulticastReceipt(Receipt);GetOwner()->ForceNetUpdate();
    }
    OnEventAccepted.Broadcast(Event,Result);OnSourceEventAccepted.Broadcast(ID,Event,Result);return Result;
}

EMCVFXPresentationResult UMCVFXPresentationComponent::PlayLocalSourceEvent(FName ID,FName Event,const FTransform& Frame,
    const FMCPerkVFXGameplayValues& Values,bool bDispatchVFX)
{
    auto Reject=[this,ID,Event](const FString& Error){LastError=Error;OnEventAccepted.Broadcast(Event,EMCVFXPresentationResult::Rejected);OnSourceEventAccepted.Broadcast(ID,Event,EMCVFXPresentationResult::Rejected);return EMCVFXPresentationResult::Rejected;};
    const auto* Prepared=FindPreparedEvent(ID,Event);const auto* Source=PreparedSources.Find(ID);FString Error;
    LastAudioError.Reset();
    if(!Source || !ValidatePreparedEventInput(Prepared,Frame,Values,bDispatchVFX,Error)
        || !FMath::IsFinite(Values.LocalStateAgeSeconds) || GetOwner()->IsActorBeingDestroyed() || GetWorld()->bIsTearingDown)
        return Reject(Error.IsEmpty()?TEXT("Local one-shot requires a live prepared source and finite accepted age."):Error);
    if(!Source->Native.IsReady())return Reject(TEXT("Local native presentation resources were not prepared."));
    const bool Visual=Prepared->Outer.Mode==EMCPerkVFXSlotMode::Replace || Prepared->Outer.Mode==EMCPerkVFXSlotMode::Composition;
    const auto Result=Visual?EMCVFXPresentationResult::Played:Prepared->Outer.Mode==EMCPerkVFXSlotMode::Disabled?
        EMCVFXPresentationResult::Suppressed:EMCVFXPresentationResult::UseDefault;
    auto Receipt=MakePreparedEventReceipt(ID,Event,Frame,Values,*Source,*Prepared,bDispatchVFX);
    Receipt.StartedAt-=FMath::Max(0.f,Values.LocalStateAgeSeconds);
    if(Receipt.bPlayVFX || Receipt.bPlaySFX) {
        LocalEventPlayers.RemoveAll([](const auto& State){return !MCEventPresentation::Running(State.Handle);});
        if(LocalEventPlayers.Num()>=64)return Reject(TEXT("Local one-shot capacity of 64 active events exceeded."));
        FMCVFXLocalSourceState State;State.SourceID=ID;State.Event=Event;State.Epoch=Receipt.GameplayEpoch;
        const auto* Audio=Source->AudioEvents.Find(Event);
        // Serial zero is never admitted into ReceiveReceipt or its remote/cancel ledgers.
        // The caller already owns this local accepted boundary; every invocation is independent.
        if(!PresentReceipt(Receipt,Prepared,Error,Audio,&State.Handle))return Reject(Error);
        if(MCEventPresentation::Running(State.Handle)){LocalEventPlayers.Add(MoveTemp(State));SetComponentTickEnabled(true);}
    }
    LastError.Reset();OnEventAccepted.Broadcast(Event,Result);OnSourceEventAccepted.Broadcast(ID,Event,Result);return Result;
}

bool UMCVFXPresentationComponent::PresentReceipt(const FMCVFXPresentationReceipt& Receipt,
    const FMCPreparedPerkVFXComposition* Prepared,FString& Error,const FMCPreparedEventSFX* PreparedAudio,
    FMCVFXPresentationPlayerHandle* LocalHandle)
{
    if(GetNetMode()==NM_DedicatedServer)return true;
    if(!GetWorld() || !FMath::IsFinite(Receipt.StartedAt) || Receipt.Frame.ContainsNaN()
        || !Receipt.Frame.GetRotation().IsNormalized() || Receipt.Frame.GetScale3D().GetAbsMin()<.0001)
    {Error=TEXT("Presentation receipt contains an invalid server frame/time.");return false;}
    if(Receipt.GameplayEpoch.IsValid()) {
        const auto* State=GetWorld()->GetGameState<AMCGameState>();
        if(!State || State->GameplayLoadId!=Receipt.GameplayEpoch)
        {Error=TEXT("Presentation is waiting for its matching replicated run identity.");return false;}
    }
    const float Elapsed=FMath::Max(0.f,float(MCEventPresentation::Now(GetWorld())-Receipt.StartedAt));
    if(Elapsed>=60.f)return true; // Consumed old onset, never a fresh replay.
    FMCVFXPresentationPlayerHandle Handle;Handle.SourceID=Receipt.SourceID;Handle.EpisodeID=Receipt.EpisodeID;Handle.Offset=Receipt.Slot.Transform;
    if(Receipt.bPlayVFX) {
        FMCPreparedPerkVFXComposition Local;
        if(!Prepared){if(!PrepareSlot(Receipt.Slot,false,Local,Error,nullptr,&Receipt.GroupPolicy))return false;Prepared=&Local;}
        if(!Prepared->Snapshot){Error=TEXT("Custom presentation receipt has no prepared runtime snapshot.");return false;}
        int32 Needed=0;
        for(int32 I=0;I<Prepared->Snapshot->Layers.Num();++I)if(Prepared->Snapshot->Layers[I].bEnabled) {
            ++Needed;auto* System=Prepared->Systems.IsValidIndex(I)?Prepared->Systems[I].Get():nullptr;
            if(!System||!System->IsReadyToRun()){Error=TEXT("Niagara source was not ready at the accepted event.");return false;}
        }
        if(Needed) {
            int32 PlayerCount=0,LayerCount=0;
            for(TActorIterator<AMCPerkVFXCompositionPlayer> It(GetWorld());It;++It)if(It->IsRunning()) {
                ++PlayerCount;LayerCount+=It->GetLiveLayerCount()+It->GetPendingLayerCount();
            }
            if(PlayerCount>=64 || LayerCount+Needed>512){Error=TEXT("World presentation budget of 64 players / 512 layers exceeded.");return false;}
            FMCPerkVFXGameplayValues Values;Values.Direction=Receipt.Direction;Values.Radius=Receipt.Radius;Values.Fill=Receipt.Fill;
            Values.Intensity=Receipt.Intensity;Values.Duration=Receipt.Duration;Values.Color=Receipt.Color;Values.bHasColor=Receipt.bHasColor;
            Values.SourcePosition=Receipt.SourcePosition;Values.TargetPosition=Receipt.TargetPosition;Values.bHasEndpoints=Receipt.bHasEndpoints;
            Values.Seed=Receipt.Seed;Values.bHasSeed=Receipt.bHasSeed;Values.EpisodeID=Receipt.EpisodeID;
            Values.VisualSubject=MCEventPresentation::ResolveSubject(Receipt,GetOwner());
            if(Receipt.bHasVisualSubject&&!Values.VisualSubject.IsValid()) {Error=TEXT("The accepted visual subject is unavailable; refusing an unrelated owner fallback.");return false;}
            Values.CapturedMeshes=Receipt.CapturedMeshes;Values.bCapturedMeshOverhead=Receipt.bCapturedMeshOverhead;
            Values.NativeInputs=Receipt.NativeInputs;
            Values.CapturedContactAt=Receipt.CapturedContactAt;
            FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.Owner=GetWorld()->GetWorldSettings();
            Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FTransform Frame=Receipt.Slot.Transform*Receipt.Frame;
            auto* Player=GetWorld()->SpawnActor<AMCPerkVFXCompositionPlayer>(Frame.GetLocation(),Frame.Rotator(),Spawn);
            if(!Player){Error=TEXT("Could not allocate the local presentation player.");return false;}
            Player->Tags.AddUnique(MCEventPresentation::OwnedPlayerTag);
            if(GetOwner())Player->AddTickPrerequisiteActor(GetOwner());
            for(const auto& Binding:EpisodeTickSources)
                if(Binding.SourceID==Receipt.SourceID && Binding.EpisodeID==Receipt.EpisodeID)
                    if(auto* Native=Binding.Actor.Get())Player->AddTickPrerequisiteActor(Native);
            if(Receipt.GameplayEpoch.IsValid() && !Player->BindToPresentationEpoch(Receipt.GameplayEpoch,Error))
            {Player->Destroy();return false;}
            if(!Player->StartPrepared(*Prepared,Frame,Values.VisualSubject.Get(),nullptr,false,Values,Error,Elapsed))
            {Player->Destroy();return false;}
            Handle.Player=Player;
        }
    }
    PresentAudio(Receipt,PreparedAudio,Handle,Elapsed);
    if(LocalHandle)*LocalHandle=Handle;
    else {
        for(auto It=Players.CreateIterator();It;++It)if(!MCEventPresentation::Running(It.Value()))It.RemoveCurrent();
        if(MCEventPresentation::Running(Handle))Players.Add(Receipt.Serial,Handle);
    }
    return true;
}

void UMCVFXPresentationComponent::PresentAudio(const FMCVFXPresentationReceipt& Receipt,
    const FMCPreparedEventSFX* PreparedAudio,FMCVFXPresentationPlayerHandle& Handle,float Elapsed)
{
    if(!Receipt.bPlaySFX)return;
    auto AudioError=[&](const FString& Error) {
        LastAudioError=Error;UE_LOG(LogMCEventPresentation,Warning,TEXT("SFX %s / %s / %u: %s"),
            *Receipt.SourceID.ToString(),*Receipt.Event.ToString(),Receipt.Serial,*Error);
    };
    FMCPreparedEventSFX Local;FString Error;
    if(PreparedAudio)Local=*PreparedAudio;
    else if(!PrepareAudioSlot(Receipt.SourceID,Receipt.Event,Receipt.SFXSlot,Receipt.GroupPolicy,false,Local,Error)) {AudioError(Error);return;}
    if(!MCEventPresentation::HasAudio(Local))return;
    // Only a local retained readiness service may supply this certificate.
    // No value from the network payload can promote an unprepared sound source.
    if(MCEventPresentation::HasAudioOutput(GetWorld())&&!CertifyAudioSlot(Local,Error)) {AudioError(Error);return;}
    FMCEventSFXContext Context;Context.SourceID=Receipt.SourceID;Context.Event=Receipt.Event;
    Context.EpisodeID=Receipt.EpisodeID;Context.GameplayEpoch=Receipt.GameplayEpoch;Context.ParameterOwner=GetOwner();
    if(Receipt.Intensity>=0.f)Context.Intensity=Receipt.Intensity;
    const auto* Binding=AudioSocketSources.Find(Receipt.SourceID);
    auto* SocketSource=Binding?Binding->Get():nullptr;
    auto* Player=AMCEventSFXPlayer::SpawnPrepared(GetWorld(),Local,Receipt.Frame,SocketSource,Context,
        MCEventPresentation::OwnedAudioTag,Error,Elapsed);
    if(!Player){AudioError(Error);return;}
    if(GetOwner())Player->AddTickPrerequisiteActor(GetOwner());
    for(const auto& BindingEntry:EpisodeTickSources)
        if(BindingEntry.SourceID==Receipt.SourceID&&BindingEntry.EpisodeID==Receipt.EpisodeID)
            if(auto* Native=BindingEntry.Actor.Get())Player->AddTickPrerequisiteActor(Native);
    Handle.AudioPlayer=Player;LastAudioError.Reset();
}

void UMCVFXPresentationComponent::ObserveRunEpoch()
{
    const auto* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!State || !State->GameplayLoadId.IsValid())return;
    if(ObservedEpoch.IsValid() && ObservedEpoch!=State->GameplayLoadId) {
        RetiredEpochs.AddUnique(ObservedEpoch);
        if(RetiredEpochs.Num()>8)RetiredEpochs.RemoveAt(0);
    }
    ObservedEpoch=State->GameplayLoadId;
}

bool UMCVFXPresentationComponent::ReceiveReceipt(const FMCVFXPresentationReceipt& Receipt,
    const FMCPreparedPerkVFXComposition* Prepared,FString& Error,bool bMayWait,const FMCPreparedEventSFX* PreparedAudio)
{
    Error.Reset();ObserveRunEpoch();
    const uint32 Cancelled=GetCancellationFloor(Receipt);
    if(Receipt.Serial==0 || Receipt.Serial<=ReceivedSerial || Receipt.Serial<=Cancelled)return true;
    const auto* Source=PreparedSources.Find(Receipt.SourceID);
    if(Receipt.SourceID.IsNone() || Receipt.Event.IsNone()
        || (Source && !Source->AllowedEvents.Contains(Receipt.Event))
        || (!Source && !SourceID.IsNone() && (Receipt.SourceID!=SourceID || !Events.Contains(Receipt.Event)))
        || (!Receipt.bPlayVFX&&!Receipt.bPlaySFX)
        || (Receipt.bPlayVFX&&Receipt.Slot.Mode!=EMCPerkVFXSlotMode::Replace && Receipt.Slot.Mode!=EMCPerkVFXSlotMode::Composition)
        || !UMCPerkVFXProfile::ValidateSlot(Receipt.Slot,Error)
        || !MCEventPresentation::ValidateProjection(Receipt.GroupPolicy,Error)
        || (Receipt.bPlaySFX&&(!Receipt.SFXSlot.HasCustomLayers()||!MCEventSFX::ValidateNativePolicy(Receipt.SFXSlot,MCVFXSourceContracts::CanSuppressOriginalSFX(Receipt.SourceID,Receipt.Event),Error)))
        || !MCEventPresentation::ValidCaptured(Receipt.CapturedMeshes,Receipt.CapturedContactAt)
        || !Receipt.NativeInputs.Validate(Error)
        || !FMath::IsFinite(Receipt.StartedAt) || Receipt.Frame.ContainsNaN()
        || !Receipt.Frame.GetRotation().IsNormalized() || Receipt.Frame.GetScale3D().GetAbsMin()<.0001
        || Receipt.Direction.ContainsNaN() || !FMath::IsFinite(Receipt.Radius) || !FMath::IsFinite(Receipt.Fill)
        || !FMath::IsFinite(Receipt.Duration) || !FMath::IsFinite(Receipt.Intensity)
        || (Receipt.bHasEndpoints && (Receipt.SourcePosition.ContainsNaN() || Receipt.TargetPosition.ContainsNaN()))
        || (Receipt.bHasColor && (!FMath::IsFinite(Receipt.Color.R) || !FMath::IsFinite(Receipt.Color.G)
            || !FMath::IsFinite(Receipt.Color.B) || !FMath::IsFinite(Receipt.Color.A)))) {
        if(Error.IsEmpty())Error=TEXT("Invalid custom presentation receipt.");
        return false;
    }
    if(Receipt.bPlaySFX) {
        FMCPreparedEventSFX Probe;Probe.Slot=Receipt.SFXSlot;
        if(!MCEventSFX::CompileSharedGroupPolicy(Receipt.GroupPolicy.SharedIDs,Receipt.GroupPolicy.DisabledIDs,
            Receipt.GroupPolicy.MutedSFXIDs,Probe,Error))return false;
    }
    if(!bMayWait || GetNetMode()==NM_DedicatedServer) {
        if(GetNetMode()!=NM_DedicatedServer && !PresentReceipt(Receipt,Prepared,Error,PreparedAudio))return false;
        ReceivedSerial=Receipt.Serial;LatestReceivedReceipt=Receipt;return true;
    }
    if(PendingReceipts.Num()>=MCEventPresentation::MaxPendingReceipts) {
        Error=TEXT("Ordered presentation queue reached its explicit 32-receipt capacity.");return false;
    }
    FMCPendingVFXPresentationReceipt Pending;Pending.Receipt=Receipt;
    Pending.bPrepared=!Receipt.bPlayVFX;Pending.bAudioPrepared=!Receipt.bPlaySFX;
    if(Receipt.bPlayVFX&&Prepared){Pending.Prepared=*Prepared;Pending.bPrepared=true;}
    else if(Receipt.bPlayVFX) {
        // Capture any already-resident authoring data immediately, even when
        // GameState's epoch notification has not arrived yet. Later edits must
        // not rewrite an accepted queued event's layer snapshot.
        bool Waiting=false;
        Pending.bPrepared=PrepareSlot(Receipt.Slot,false,Pending.Prepared,Error,&Waiting,&Receipt.GroupPolicy);
        if(!Pending.bPrepared && !Waiting)return false;
        Error.Reset();
    }
    if(Receipt.bPlaySFX) {
        if(PreparedAudio){Pending.PreparedAudio=*PreparedAudio;Pending.bAudioPrepared=true;}
        else {Pending.bAudioPrepared=PrepareAudioSlot(Receipt.SourceID,Receipt.Event,Receipt.SFXSlot,Receipt.GroupPolicy,false,Pending.PreparedAudio,Error);Error.Reset();}
    }
    PendingReceipts.Add(MoveTemp(Pending));ReceivedSerial=Receipt.Serial;LatestReceivedReceipt=Receipt;
    ProcessPendingReceipts();return true;
}

void UMCVFXPresentationComponent::ProcessPendingReceipts()
{
    ObserveRunEpoch();
    const auto* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    uint32 LatestCurrentEpochSerial=0;
    if(State && State->GameplayLoadId.IsValid())for(const auto& Pending:PendingReceipts)
        if(Pending.Receipt.GameplayEpoch==State->GameplayLoadId)
            LatestCurrentEpochSerial=FMath::Max(LatestCurrentEpochSerial,Pending.Receipt.Serial);
    int32 Expired=0;
    PendingReceipts.RemoveAll([&](const auto& Pending) {
        if(MCEventPresentation::Now(GetWorld())-Pending.Receipt.StartedAt>=MCEventPresentation::ReceiptLifetime){++Expired;return true;}
        return Pending.Receipt.Serial<=GetCancellationFloor(Pending.Receipt)
            // The component may have been idle for an entire run. A later
            // ordered receipt for the known current run proves an earlier
            // mismatched receipt is obsolete even if that epoch was not observed.
            || (LatestCurrentEpochSerial && Pending.Receipt.Serial<LatestCurrentEpochSerial
                && Pending.Receipt.GameplayEpoch!=State->GameplayLoadId)
            || (Pending.Receipt.GameplayEpoch.IsValid() && RetiredEpochs.Contains(Pending.Receipt.GameplayEpoch));
    });
    if(Expired) {
        LastError=TEXT("Deferred presentation expired after the explicit 60-second delivery lifetime.");
        UE_LOG(LogMCEventPresentation,Warning,TEXT("%s: %d ordered custom receipts expired while waiting for their run/resources."),*SourceID.ToString(),Expired);
    }
    while(!PendingReceipts.IsEmpty()) {
        auto& Pending=PendingReceipts[0];const auto& Receipt=Pending.Receipt;
        if(Receipt.GameplayEpoch.IsValid() && (!State || State->GameplayLoadId!=Receipt.GameplayEpoch))break;
        FString Error;bool Waiting=false;
        if(!Pending.bPrepared) {
            if(!PrepareSlot(Receipt.Slot,false,Pending.Prepared,Error,&Waiting,&Receipt.GroupPolicy)) {
                if(Waiting)break;
                LastError=Error;UE_LOG(LogMCEventPresentation,Warning,TEXT("Ordered presentation %s / %u refused: %s"),*SourceID.ToString(),Receipt.Serial,*Error);
                PendingReceipts.RemoveAt(0);continue;
            }
            Pending.bPrepared=true;
        }
        bool Ready=true;
        if(Receipt.bPlayVFX&&Pending.Prepared.Snapshot)for(int32 I=0;I<Pending.Prepared.Systems.Num();++I)
            if(Pending.Prepared.Snapshot->Layers[I].bEnabled)
                if(UNiagaraSystem* System=Pending.Prepared.Systems[I];System&&!System->IsReadyToRun()){Ready=false;break;}
        if(!Ready)break;
        // Audio resource/output failure is a separate channel. It cannot
        // indefinitely block this event's valid visuals or later FIFO events.
        if(Receipt.bPlaySFX&&!Pending.bAudioPrepared)
            Pending.bAudioPrepared=PrepareAudioSlot(Receipt.SourceID,Receipt.Event,Receipt.SFXSlot,Receipt.GroupPolicy,false,Pending.PreparedAudio,Error);
        Error.Reset();
        if(!PresentReceipt(Receipt,Receipt.bPlayVFX?&Pending.Prepared:nullptr,Error,
            Receipt.bPlaySFX&&Pending.bAudioPrepared?&Pending.PreparedAudio:nullptr)) {
            LastError=Error;UE_LOG(LogMCEventPresentation,Warning,TEXT("Ordered presentation %s / %u refused: %s"),*SourceID.ToString(),Receipt.Serial,*Error);
        }
        PendingReceipts.RemoveAt(0);
    }
    SetComponentTickEnabled(!bPrepared || !PendingReceipts.IsEmpty() || !LocalSourceStates.IsEmpty() || !LocalEventPlayers.IsEmpty());
}

uint32 UMCVFXPresentationComponent::GetCancellationFloor(const FMCVFXPresentationReceipt& Receipt) const
{
    uint32 Floor=FMath::Max(CancelledThroughSerial,LastCancel.ThroughSerial);
    for(const auto& Scope:ScopedCancels)if(MCEventPresentation::Matches(Scope,Receipt.SourceID,Receipt.EpisodeID))Floor=FMath::Max(Floor,Scope.ThroughSerial);
    return Floor;
}
void UMCVFXPresentationComponent::PruneScopedCancels()
{
    const auto Snapshot=ScopedCancels;
    ScopedCancels.RemoveAll([&](const auto& Scope) {
        if(Scope.ThroughSerial<=FMath::Max(CancelledThroughSerial,LastCancel.ThroughSerial))return true;
        if(Scope.EpisodeID.IsValid())for(const auto& Other:Snapshot)
            if(Other.SourceID==Scope.SourceID && !Other.EpisodeID.IsValid() && Other.ThroughSerial>=Scope.ThroughSerial)return true;
        auto Protects=[&](const FMCVFXPresentationReceipt& R){return R.Serial && R.Serial<=Scope.ThroughSerial && MCEventPresentation::Matches(Scope,R.SourceID,R.EpisodeID);};
        if(Protects(LastReceipt) || Protects(LatestReceivedReceipt))return false;
        for(const auto& Pending:PendingReceipts)if(Protects(Pending.Receipt))return false;
        for(const auto& Pair:Players)if(Pair.Key<=Scope.ThroughSerial && MCEventPresentation::Running(Pair.Value)
            && MCEventPresentation::Matches(Scope,Pair.Value.SourceID,Pair.Value.EpisodeID))return false;
        return true;
    });
}
void UMCVFXPresentationComponent::ReceiveCancel(const FMCVFXPresentationCancel& Cancel)
{
    if(Cancel.SourceID.IsNone())CancelledThroughSerial=FMath::Max(CancelledThroughSerial,Cancel.ThroughSerial);
    PendingReceipts.RemoveAll([&](const auto& Pending){return Pending.Receipt.Serial<=Cancel.ThroughSerial
        && MCEventPresentation::Matches(Cancel,Pending.Receipt.SourceID,Pending.Receipt.EpisodeID);});
    for(auto It=Players.CreateIterator();It;++It) {
        if(It.Key()<=Cancel.ThroughSerial&&MCEventPresentation::Matches(Cancel,It.Value().SourceID,It.Value().EpisodeID)) {
            if(auto* Player=It.Value().Player.Get())Player->StopEmission(Cancel.bImmediate);
            if(auto* Audio=It.Value().AudioPlayer.Get())Audio->Stop(Cancel.bImmediate);
        }
        if(!MCEventPresentation::Running(It.Value()))It.RemoveCurrent();
    }
    EpisodeTickSources.RemoveAll([&](const auto& V){return MCEventPresentation::Matches(Cancel,V.SourceID,V.EpisodeID);});
    if(!Cancel.SourceID.IsNone()) {
        auto* Existing=ScopedCancels.FindByPredicate([&](const auto& V){return V.SourceID==Cancel.SourceID && V.EpisodeID==Cancel.EpisodeID;});
        if(Existing) {
            if(Cancel.ThroughSerial>Existing->ThroughSerial)*Existing=Cancel;
            else if(Cancel.ThroughSerial==Existing->ThroughSerial)Existing->bImmediate|=Cancel.bImmediate;
        } else {
            PruneScopedCancels();
            if(ScopedCancels.Num()<128)ScopedCancels.Add(Cancel);
            else {
                // Cancellation itself has already executed. Keep late joiners
                // from replaying a matching retained event without broadening
                // cancellation to unrelated native episodes.
                if(LastReceipt.Serial<=Cancel.ThroughSerial && MCEventPresentation::Matches(Cancel,LastReceipt.SourceID,LastReceipt.EpisodeID)) {
                    LastReceipt.Slot.Mode=EMCPerkVFXSlotMode::Disabled;LastReceipt.bPlayVFX=false;LastReceipt.bPlaySFX=false;
                }
                LastError=TEXT("Scoped cancellation ledger capacity exceeded; cancellation executed, matching late snapshot suppressed.");
                UE_LOG(LogMCEventPresentation,Error,TEXT("%s"),*LastError);
            }
        }
    }
    SetComponentTickEnabled(!bPrepared || !PendingReceipts.IsEmpty() || !LocalSourceStates.IsEmpty() || !LocalEventPlayers.IsEmpty());
}

void UMCVFXPresentationComponent::MulticastReceipt_Implementation(const FMCVFXPresentationReceipt& Receipt)
{
    FString Error;if(!ReceiveReceipt(Receipt,nullptr,Error,true)) {
        LastError=Error;UE_LOG(LogMCEventPresentation,Warning,TEXT("Custom presentation delivery %s / %u refused: %s"),*SourceID.ToString(),Receipt.Serial,*Error);
    }
}
void UMCVFXPresentationComponent::MulticastCancel_Implementation(const FMCVFXPresentationCancel& Cancel)
{ReceiveCancel(Cancel);}
void UMCVFXPresentationComponent::OnRep_Receipt()
{
    if(LastReceipt.Serial && (LastReceipt.bPlaySFX || (LastReceipt.bPlayVFX
        && LastReceipt.Slot.Mode!=EMCPerkVFXSlotMode::UseDefault && LastReceipt.Slot.Mode!=EMCPerkVFXSlotMode::Disabled)))
        MulticastReceipt_Implementation(LastReceipt);
}
void UMCVFXPresentationComponent::OnRep_Cancel()
{ReceiveCancel(LastCancel);}
void UMCVFXPresentationComponent::OnRep_ScopedCancels()
{const auto Snapshot=ScopedCancels;for(const auto& Cancel:Snapshot)ReceiveCancel(Cancel);}

void UMCVFXPresentationComponent::BeginPlay()
{
    Super::BeginPlay();ObserveRunEpoch();FString Error;
    // A dedicated server has no local rendering/readiness controller. Prepare
    // immutable event data during actor setup, never from the accepted cue.
    if(!SourceID.IsNone() && !IsSourcePrepared(SourceID))Prepare(Error,GetNetMode()==NM_DedicatedServer);
    if(SourceID.IsNone() && PendingReceipts.IsEmpty() && LocalSourceStates.IsEmpty() && LocalEventPlayers.IsEmpty())SetComponentTickEnabled(false);
}
void UMCVFXPresentationComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,TickType,Function);
    const auto* CurrentState=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    const FGuid CurrentEpoch=CurrentState?CurrentState->GameplayLoadId:FGuid();
    LocalSourceStates.RemoveAll([&](auto& State) {
        if(State.Epoch!=CurrentEpoch || (State.bHasTickPrerequisite&&!State.TickPrerequisite.IsValid())) {
            if(auto* Player=State.Handle.Player.Get())Player->StopEmission(true);
            if(auto* Audio=State.Handle.AudioPlayer.Get())Audio->Stop(true);
            return true;
        }
        return false;
    });
    LocalEventPlayers.RemoveAll([&](auto& State) {
        if(State.Epoch!=CurrentEpoch) {
            if(auto* Player=State.Handle.Player.Get())Player->StopEmission(true);
            if(auto* Audio=State.Handle.AudioPlayer.Get())Audio->Stop(true);
            return true;
        }
        return !MCEventPresentation::Running(State.Handle);
    });
    if(!SourceID.IsNone() && !IsSourcePrepared(SourceID)){FString Error;Prepare(Error,false);}
    if(!PendingReceipts.IsEmpty())ProcessPendingReceipts();
    SetComponentTickEnabled(!bPrepared || !PendingReceipts.IsEmpty() || !LocalSourceStates.IsEmpty() || !LocalEventPlayers.IsEmpty());
}

void UMCVFXPresentationComponent::ForgetPreparedSourceSnapshot(FName ID)
{
    if(ID.IsNone())return;PreparedSources.Remove(ID);bPrepared=!PreparedSources.IsEmpty();
    SetComponentTickEnabled((!SourceID.IsNone()&&!bPrepared)||!PendingReceipts.IsEmpty()||!LocalSourceStates.IsEmpty()||!LocalEventPlayers.IsEmpty());
}

void UMCVFXPresentationComponent::RetireLocalSourceAudioEvent(FName ID,FName Event)
{
    if(ID.IsNone()||Event.IsNone())return;
    for(auto& State:LocalEventPlayers)if(State.SourceID==ID&&State.Event==Event&&State.Handle.SourceID==ID)
        if(auto* A=State.Handle.AudioPlayer.Get();A&&A->GetWorld()==GetWorld()&&A->GetContext().SourceID==ID&&A->GetContext().Event==Event)
            A->Stop(false); // Existing authored finite natural-tail/fade bound; loops and pending repeats end.
}

void UMCVFXPresentationComponent::StopPresentationWithLocalSourceAudioTails(FName ID)
{
    StopLocalSourceStates(); // Persistent loops never become death tails.
    StopScope(NAME_None,FGuid(),true,ID);
}

void UMCVFXPresentationComponent::StopPresentation(bool bImmediate)
{StopScope(NAME_None,FGuid(),bImmediate);}
void UMCVFXPresentationComponent::StopSource(FName ID,bool bImmediate)
{if(!ID.IsNone())StopScope(ID,FGuid(),bImmediate);}
void UMCVFXPresentationComponent::StopSourceEpisode(FName ID,FGuid Episode,bool bImmediate)
{if(!ID.IsNone() && Episode.IsValid())StopScope(ID,Episode,bImmediate);}
bool UMCVFXPresentationComponent::UpdateSourceEpisode(FName ID,FGuid Episode,const FTransform& Frame,const FMCPerkVFXGameplayValues& Values)
{
    if(ID.IsNone() || !Episode.IsValid() || Frame.ContainsNaN() || !Frame.GetRotation().IsNormalized()
        || Frame.GetScale3D().GetAbsMin()<.0001 || Values.Direction.ContainsNaN()
        || !FMath::IsFinite(Values.Radius) || !FMath::IsFinite(Values.Fill) || !FMath::IsFinite(Values.Intensity)
        || !FMath::IsFinite(Values.Duration) || (Values.bHasEndpoints && (Values.SourcePosition.ContainsNaN() || Values.TargetPosition.ContainsNaN()))
        || (Values.bHasColor && (!FMath::IsFinite(Values.Color.R) || !FMath::IsFinite(Values.Color.G)
            || !FMath::IsFinite(Values.Color.B) || !FMath::IsFinite(Values.Color.A))))return false;
    FString NativeError;
    if(!Values.NativeInputs.Validate(NativeError)) {LastError=NativeError;return false;}
    // Validate every recipient before replacing any frame or last-good input set.
    for(const auto& Pair:Players)if(Pair.Value.SourceID==ID && Pair.Value.EpisodeID==Episode)
        if(const auto* Player=Pair.Value.Player.Get();Player&&Player->IsRunning()&&!Player->IsStopping()
            && !Player->ValidateGameplayValues(Values,NativeError)) {LastError=NativeError;return false;}
    for(const auto& Local:LocalEventPlayers)if(Local.Handle.SourceID==ID && Local.Handle.EpisodeID==Episode)
        if(const auto* Player=Local.Handle.Player.Get();Player&&Player->IsRunning()&&!Player->IsStopping()
            && !Player->ValidateGameplayValues(Values,NativeError)) {LastError=NativeError;return false;}
    bool Updated=false;
    for(auto& Pair:Players)if(Pair.Value.SourceID==ID && Pair.Value.EpisodeID==Episode) {
        if(auto* Player=Pair.Value.Player.Get();Player && Player->IsRunning() && !Player->IsStopping()) {
            Player->UpdateEventFrame(Pair.Value.Offset*Frame);
            if(!Player->UpdateGameplayValues(Values,&NativeError)) {LastError=NativeError;return false;}Updated=true;
        }
        if(auto* Audio=Pair.Value.AudioPlayer.Get();Audio&&Audio->IsRunning()) {
            Audio->UpdateEventFrame(Frame);
            Audio->UpdateInputs(Values.Intensity>=0?Values.Intensity:Audio->GetContext().Intensity,Audio->GetContext().Speed);Updated=true;
        }
    }
    for(auto& Local:LocalEventPlayers)if(Local.Handle.SourceID==ID && Local.Handle.EpisodeID==Episode) {
        if(auto* Player=Local.Handle.Player.Get();Player&&Player->IsRunning()&&!Player->IsStopping()) {
            Player->UpdateEventFrame(Local.Handle.Offset*Frame);
            if(!Player->UpdateGameplayValues(Values,&NativeError)){LastError=NativeError;return false;}Updated=true;
        }
        if(auto* Audio=Local.Handle.AudioPlayer.Get();Audio&&Audio->IsRunning()) {
            Audio->UpdateEventFrame(Frame);
            Audio->UpdateInputs(Values.Intensity>=0?Values.Intensity:Audio->GetContext().Intensity,Audio->GetContext().Speed);Updated=true;
        }
    }
    return Updated;
}
void UMCVFXPresentationComponent::StopScope(FName ID,FGuid Episode,bool bImmediate,FName LocalAudioTailSource)
{
    // Local events have no serial relationship with remote cancellation watermarks.
    // Only an explicit component/source/episode stop owns these independent handles.
    LocalEventPlayers.RemoveAll([&](auto& State) {
        if((ID.IsNone()||State.Handle.SourceID==ID)&&(!Episode.IsValid()||State.Handle.EpisodeID==Episode)) {
            if(auto* Player=State.Handle.Player.Get())Player->StopEmission(bImmediate);
            if(auto* Audio=State.Handle.AudioPlayer.Get()) {
                const bool Tail=!LocalAudioTailSource.IsNone()&&State.SourceID==LocalAudioTailSource
                    &&State.Handle.SourceID==LocalAudioTailSource&&Audio->GetWorld()==GetWorld()
                    &&Audio->GetContext().SourceID==LocalAudioTailSource&&Audio->GetContext().Event==State.Event
                    &&Audio->GetContext().GameplayEpoch==State.Epoch;
                Audio->Stop(Tail?false:bImmediate);
            }
            return !MCEventPresentation::Running(State.Handle);
        }
        return false;
    });
    FMCVFXPresentationCancel Cancel;
    Cancel.SourceID=ID;Cancel.EpisodeID=Episode;Cancel.ThroughSerial=FMath::Max(IssuedSerial,ReceivedSerial);Cancel.bImmediate=bImmediate;
    ReceiveCancel(Cancel);
    if(GetOwner() && GetOwner()->HasAuthority() && Cancel.ThroughSerial) {
        if(ID.IsNone()) {
            if(Cancel.ThroughSerial>LastCancel.ThroughSerial)LastCancel=Cancel;
            else if(Cancel.ThroughSerial==LastCancel.ThroughSerial)LastCancel.bImmediate|=bImmediate;
        }
        MulticastCancel(Cancel);GetOwner()->ForceNetUpdate();
    }
}
void UMCVFXPresentationComponent::StopWorldPresentations(UWorld* World)
{
    if(!World)return;
    TArray<TWeakObjectPtr<AMCPerkVFXCompositionPlayer>> Owned;
    for(TActorIterator<AMCPerkVFXCompositionPlayer> It(World);It;++It)
        if(IsValid(*It) && It->ActorHasTag(MCEventPresentation::OwnedPlayerTag))Owned.Add(*It);
    for(const auto& Weak:Owned)if(auto* Player=Weak.Get())Player->StopEmission(true);
    AMCEventSFXPlayer::StopWorldPlayers(World,MCEventPresentation::OwnedAudioTag);
}
bool UMCVFXPresentationComponent::IsPresentationRunning() const
{for(const auto& Pair:Players)if(MCEventPresentation::Running(Pair.Value))return true;
for(const auto& State:LocalEventPlayers)if(MCEventPresentation::Running(State.Handle))return true;
for(const auto& State:LocalSourceStates)if(MCEventPresentation::Running(State.Handle))return true;return !PendingReceipts.IsEmpty();}
bool UMCVFXPresentationComponent::IsSourceEpisodeRunning(FName ID,FGuid Episode) const
{
    for(const auto& Pair:Players)if(Pair.Value.SourceID==ID&&Pair.Value.EpisodeID==Episode&&MCEventPresentation::Running(Pair.Value))return true;
    for(const auto& State:LocalEventPlayers)if(State.Handle.SourceID==ID&&State.Handle.EpisodeID==Episode&&MCEventPresentation::Running(State.Handle))return true;
    for(const auto& State:LocalSourceStates)if(State.SourceID==ID&&State.Handle.EpisodeID==Episode&&MCEventPresentation::Running(State.Handle))return true;
    return PendingReceipts.ContainsByPredicate([&](const auto& Pending){return Pending.Receipt.SourceID==ID&&Pending.Receipt.EpisodeID==Episode;});
}
bool UMCVFXPresentationComponent::IsSourceEpisodeVisualActive(FName ID,FGuid Episode) const
{
    auto Active=[&](const FMCVFXPresentationPlayerHandle& Handle) {
        const auto* Player=Handle.Player.Get();
        return Handle.SourceID==ID && Handle.EpisodeID==Episode && Player
            && Player->IsRunning() && !Player->IsStopping() && Player->GetLiveLayerCount()>0;
    };
    for(const auto& Pair:Players)if(Active(Pair.Value))return true;
    for(const auto& State:LocalEventPlayers)if(Active(State.Handle))return true;
    for(const auto& State:LocalSourceStates)if(Active(State.Handle))return true;
    return false;
}

void UMCVFXPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    // Ordinary enemy destruction is not cancellation of its accepted world-space death burst.
    // The world-owned prepared players retain resources and their own scheduled tail.
    if(Reason!=EEndPlayReason::Destroyed)StopPresentation(true);
    StopLocalSourceStates();
    PendingReceipts.Reset();Players.Reset();LocalEventPlayers.Reset();EpisodeTickSources.Reset();Super::EndPlay(Reason);
}
void UMCVFXPresentationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UMCVFXPresentationComponent,LastReceipt,COND_InitialOnly);
    DOREPLIFETIME_CONDITION(UMCVFXPresentationComponent,LastCancel,COND_InitialOnly);
    DOREPLIFETIME_CONDITION(UMCVFXPresentationComponent,ScopedCancels,COND_InitialOnly);
}
