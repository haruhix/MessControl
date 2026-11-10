#include "MCVFXPresentationComponent.h"

#include "MCEventSFXPlayer.h"
#include "MCGameState.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraSystem.h"

namespace
{
void StopState(FMCVFXLocalSourceState& State,bool bImmediate=true)
{
    if(auto* Player=State.Handle.Player.Get())Player->StopEmission(bImmediate);
    if(auto* Audio=State.Handle.AudioPlayer.Get())Audio->Stop(bImmediate);
}
bool FiniteState(const FTransform& Frame,const FMCPerkVFXGameplayValues& Values)
{
    return !Frame.ContainsNaN() && Frame.GetRotation().IsNormalized() && Frame.GetScale3D().GetAbsMin()>=.0001
        && !Values.Direction.ContainsNaN() && FMath::IsFinite(Values.Radius) && FMath::IsFinite(Values.Fill)
        && FMath::IsFinite(Values.LocalStateAgeSeconds)
        && FMath::IsFinite(Values.Intensity) && FMath::IsFinite(Values.Duration)
        && (!Values.bHasEndpoints || (!Values.SourcePosition.ContainsNaN()&&!Values.TargetPosition.ContainsNaN()))
        && (!Values.bHasColor || (FMath::IsFinite(Values.Color.R)&&FMath::IsFinite(Values.Color.G)
            &&FMath::IsFinite(Values.Color.B)&&FMath::IsFinite(Values.Color.A)))
        && Values.CapturedMeshes.IsEmpty(); // Captured one-shot tools use the accepted receipt path.
}
}

void UMCVFXPresentationComponent::StopLocalSourceStates(FName ID,FName Event)
{
    const auto Matches=[ID,Event](const auto& State){return (ID.IsNone()||State.SourceID==ID)&&(Event.IsNone()||State.Event==Event);};
    for(auto& State:LocalSourceStates)if(Matches(State))StopState(State);
    LocalSourceStates.RemoveAll(Matches);
}

bool UMCVFXPresentationComponent::UpdateLocalSourceState(FName ID,FName Event,bool bActive,const FTransform& Frame,
    const FMCPerkVFXGameplayValues& Values,UActorComponent* TickPrerequisite,FString& Error,bool bDispatchAudio)
{
    Error.Reset();
    if(ID.IsNone()||Event.IsNone()) {Error=TEXT("A persistent state needs its real source/event identity.");return false;}
    auto* Existing=LocalSourceStates.FindByPredicate([&](const auto& S){return S.SourceID==ID&&S.Event==Event;});
    if(!bActive) {
        if(Existing)StopState(*Existing,false);
        LocalSourceStates.RemoveAll([&](const auto& S){return S.SourceID==ID&&S.Event==Event;});return true;
    }
    auto ClearExisting=[&]() {
        if(Existing)StopState(*Existing);
        LocalSourceStates.RemoveAll([&](const auto& S){return S.SourceID==ID&&S.Event==Event;});Existing=nullptr;
    };
    auto Fail=[&](const TCHAR* Message){ClearExisting();Error=Message;LastError=Error;return false;};
    auto RejectNative=[&]() {
        // A rejected update returns control to native presentation. Retaining
        // an old custom state here would draw both at different contact frames.
        ClearExisting();
        LastError=Error;return false;
    };
    if(!GetWorld() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() || !FiniteState(Frame,Values)
        || (Values.VisualSubject.IsValid() && Values.VisualSubject->GetWorld()!=GetWorld())
        || (TickPrerequisite && (!IsValid(TickPrerequisite) || TickPrerequisite->GetWorld()!=GetWorld())))
        return Fail(TEXT("Persistent presentation needs a live local owner and finite native inputs."));
    const auto* Source=PreparedSources.Find(ID);
    const auto* Prepared=FindPreparedEvent(ID,Event);
    if(!Source || !Prepared)return Fail(TEXT("Persistent source/event must be prepared before readiness; runtime playback never loads."));
    if(!Values.NativeInputs.Validate(Error))return RejectNative();
    if(Prepared->Snapshot)for(int32 I=0;I<Prepared->Snapshot->Layers.Num();++I)
        if(Prepared->Snapshot->Layers[I].bEnabled && (!Prepared->Systems.IsValidIndex(I)
            || !Values.NativeInputs.ValidateForSystem(Prepared->Systems[I],Error)))return RejectNative();
    if(GetNetMode()==NM_DedicatedServer)return true;
    const auto* GameState=GetWorld()->GetGameState<AMCGameState>();
    const FGuid Epoch=GameState?GameState->GameplayLoadId:FGuid();
    const double NativeNow=GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(!FMath::IsFinite(NativeNow))return Fail(TEXT("Persistent presentation needs a finite native clock."));
    const auto* PreparedStateAudio=Source->AudioEvents.Find(Event);
    const bool HasCustomAudio=PreparedStateAudio&&PreparedStateAudio->Slot.HasCustomLayers();
    auto StartStateAudio=[&](FMCVFXLocalSourceState& State) {
        if(!bDispatchAudio||!HasCustomAudio)return;
        FMCVFXPresentationReceipt Receipt;Receipt.SourceID=ID;Receipt.Event=Event;Receipt.Frame=Frame;
        Receipt.bPlayVFX=false;Receipt.bPlaySFX=true;Receipt.SFXSlot=PreparedStateAudio->Slot;Receipt.EpisodeID=Values.EpisodeID;
        Receipt.GameplayEpoch=Epoch;Receipt.Intensity=Values.Intensity;
        if(const auto* Policy=Source->GroupPolicies.Find(Event))Receipt.GroupPolicy=*Policy;
        const float Elapsed=FMath::Max(0.f,float(NativeNow-State.NativeStartedAt));
        PresentAudio(Receipt,PreparedStateAudio,State.Handle,Elapsed);
        if(auto* Player=State.Handle.AudioPlayer.Get();Player&&TickPrerequisite)Player->AddTickPrerequisiteComponent(TickPrerequisite);
    };
    if(Existing && (Existing->Epoch!=Epoch || Existing->Handle.EpisodeID!=Values.EpisodeID
        || (Existing->bHasTickPrerequisite && !Existing->TickPrerequisite.IsValid()))) {
        StopState(*Existing);LocalSourceStates.RemoveAll([&](const auto& S){return S.SourceID==ID&&S.Event==Event;});Existing=nullptr;
    }
    if(Existing) {
        if(Values.LocalStateAgeSeconds>=0.f)Existing->NativeStartedAt=NativeNow-Values.LocalStateAgeSeconds;
        if(auto* Player=Existing->Handle.Player.Get();Player&&Player->IsRunning()&&!Player->IsStopping()) {
            if(!Player->ValidateGameplayValues(Values,Error))return RejectNative();
            Player->UpdateEventFrame(Existing->Handle.Offset*Frame);
            if(!Player->UpdateGameplayValues(Values,&Error))return RejectNative();
        }
        if(Existing->TickPrerequisite.Get()!=TickPrerequisite) {
            auto* Previous=Existing->TickPrerequisite.Get();
            if(auto* Player=Existing->Handle.Player.Get()) {
                if(Previous)Player->RemoveTickPrerequisiteComponent(Previous);
                if(TickPrerequisite)Player->AddTickPrerequisiteComponent(TickPrerequisite);
            }
            if(auto* Player=Existing->Handle.AudioPlayer.Get()) {
                if(Previous)Player->RemoveTickPrerequisiteComponent(Previous);
                if(TickPrerequisite)Player->AddTickPrerequisiteComponent(TickPrerequisite);
            }
            Existing->TickPrerequisite=TickPrerequisite;Existing->bHasTickPrerequisite=TickPrerequisite!=nullptr;
        }
        if(!bDispatchAudio) {
            if(auto* Player=Existing->Handle.AudioPlayer.Get())Player->Stop(true);
            Existing->Handle.AudioPlayer.Reset();
        }
        else if(!Existing->bDispatchAudio)StartStateAudio(*Existing);
        Existing->bDispatchAudio=bDispatchAudio;
        if(auto* Audio=Existing->Handle.AudioPlayer.Get();Audio&&Audio->IsRunning()) {
            Audio->UpdateEventFrame(Frame);
            Audio->UpdateInputs(Values.Intensity>=0.f?Values.Intensity:Audio->GetContext().Intensity,Audio->GetContext().Speed);
        }
        // A finished one-shot sound is not retriggered by every state tick.
        return true;
    }
    if(LocalSourceStates.Num()>=32)return Fail(TEXT("Persistent source-state budget of 32 per owner exceeded."));
    const bool Visual=Prepared->Outer.Mode==EMCPerkVFXSlotMode::Replace||Prepared->Outer.Mode==EMCPerkVFXSlotMode::Composition;
    if(!Visual&&!HasCustomAudio)return true;
    FMCVFXLocalSourceState State;State.SourceID=ID;State.Event=Event;State.Epoch=Epoch;
    State.bDispatchAudio=bDispatchAudio;State.NativeStartedAt=NativeNow-FMath::Max(0.f,Values.LocalStateAgeSeconds);
    State.TickPrerequisite=TickPrerequisite;State.bHasTickPrerequisite=TickPrerequisite!=nullptr;
    State.Handle.SourceID=ID;State.Handle.EpisodeID=Values.EpisodeID;State.Handle.Offset=Prepared->Outer.Transform;
    if(Visual) {
        if(!Prepared->Snapshot)return Fail(TEXT("Persistent visual event has no prepared composition."));
        int32 Needed=0;
        for(int32 I=0;I<Prepared->Snapshot->Layers.Num();++I)if(Prepared->Snapshot->Layers[I].bEnabled) {
            ++Needed;const auto* System=Prepared->Systems.IsValidIndex(I)?Prepared->Systems[I].Get():nullptr;
            if(!System||!System->IsReadyToRun())return Fail(TEXT("Persistent Niagara source was not ready at activation."));
        }
        if(Needed) {
            int32 PlayersInWorld=0,LayersInWorld=0;
            for(TActorIterator<AMCPerkVFXCompositionPlayer> It(GetWorld());It;++It)if(It->IsRunning()) {
                ++PlayersInWorld;LayersInWorld+=It->GetLiveLayerCount()+It->GetPendingLayerCount();
            }
            if(PlayersInWorld>=64||LayersInWorld+Needed>512)return Fail(TEXT("World presentation budget of 64 players / 512 layers exceeded."));
            FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.Owner=GetWorld()->GetWorldSettings();
            Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FTransform Effective=State.Handle.Offset*Frame;
            auto* Player=GetWorld()->SpawnActor<AMCPerkVFXCompositionPlayer>(Effective.GetLocation(),Effective.Rotator(),Spawn);
            if(!Player)return Fail(TEXT("Could not allocate persistent local presentation."));
            Player->Tags.AddUnique(TEXT("MC.VFXPresentationComponent.Player"));Player->AddTickPrerequisiteActor(GetOwner());
            if(TickPrerequisite)Player->AddTickPrerequisiteComponent(TickPrerequisite);
            if(Epoch.IsValid()&&!Player->BindToPresentationEpoch(Epoch,Error)) {Player->Destroy();return false;}
            auto* Subject=Values.VisualSubject.IsValid()?Values.VisualSubject.Get():GetOwner()->GetRootComponent();
            if(!Player->StartPrepared(*Prepared,Effective,Subject,nullptr,false,Values,Error,FMath::Max(0.f,Values.LocalStateAgeSeconds),GetOwner()))
            {Player->Destroy();return false;}
            State.Handle.Player=Player;
        }
    }
    StartStateAudio(State);
    LocalSourceStates.Add(MoveTemp(State));SetComponentTickEnabled(true);return true;
}
