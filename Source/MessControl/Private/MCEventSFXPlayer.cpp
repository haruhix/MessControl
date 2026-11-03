#include "MCEventSFXPlayer.h"

#include "MCGameState.h"
#include "AudioDevice.h"
#include "AudioParameter.h"
#include "Audio/ActorSoundParameterInterface.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWave.h"

namespace MCEventSFXPlayback
{
constexpr int32 MaxWorldPlayers=64,MaxWorldLayers=256;
constexpr float MissedOnsetTolerance=.05f;
bool FiniteFrame(const FTransform& Frame)
{return !Frame.ContainsNaN() && Frame.GetRotation().IsNormalized() && Frame.GetScale3D().GetAbsMin()>=.0001;}
bool RequiresNativeOwnerConcurrency(const USoundBase* Sound,const USoundConcurrency* Extra)
{
    if(Extra && Extra->Concurrency.bLimitToOwner)return true;
    if(!Sound)return false;
    if(Sound->bOverrideConcurrency)return Sound->ConcurrencyOverrides.bLimitToOwner;
    for(const USoundConcurrency* C:Sound->ConcurrencySet)if(C && C->Concurrency.bLimitToOwner)return true;
    return false;
}
}

AMCEventSFXPlayer::AMCEventSFXPlayer()
{
    bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("AcceptedEventFrame")));
    SetActorEnableCollision(false);
}

AMCEventSFXPlayer* AMCEventSFXPlayer::SpawnPrepared(UWorld* World,const FMCPreparedEventSFX& Prepared,
    const FTransform& Frame,USceneComponent* SocketSource,const FMCEventSFXContext& Context,
    FName OwnershipTag,FString& Error,float InitialElapsedSeconds,const FMCEventSFXPreviewFilter* InitialFilter)
{
    Error.Reset();
    if(!IsInGameThread() || !World || World->bIsTearingDown || World->GetNetMode()==NM_DedicatedServer || OwnershipTag.IsNone())
    {Error=TEXT("SFX player needs a live local world and explicit factory ownership tag.");return nullptr;}
    int32 Players=0,Layers=0;
    for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)if(It->IsRunning())
    {++Players;Layers+=It->GetPendingLayerCount()+It->GetVoiceComponents().Num();}
    int32 Requested=0;for(const auto& L:Prepared.Slot.Layers)
        if(L.bEnabled && !Prepared.DisabledGroupIDs.Contains(L.GroupID))++Requested;
    if(Players>=MCEventSFXPlayback::MaxWorldPlayers || Layers+Requested>MCEventSFXPlayback::MaxWorldLayers)
    {Error=TEXT("SFX world budget of 64 players / 256 scheduled or active layers exceeded.");return nullptr;}
    FActorSpawnParameters Spawn;Spawn.Owner=World->GetWorldSettings();Spawn.ObjectFlags=RF_Transient;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Player=World->SpawnActor<AMCEventSFXPlayer>(AMCEventSFXPlayer::StaticClass(),FTransform::Identity,Spawn);
    if(!Player){Error=TEXT("Could not allocate the local SFX player.");return nullptr;}
    Player->Tags.AddUnique(OwnershipTag);
    if(!Player->StartPrepared(Prepared,Frame,SocketSource,Context,Error,InitialElapsedSeconds,InitialFilter))
    {Player->Destroy();return nullptr;}
    return Player;
}

bool AMCEventSFXPlayer::StartPrepared(const FMCPreparedEventSFX& Prepared,const FTransform& Frame,
    USceneComponent* SocketSource,const FMCEventSFXContext& Context,FString& Error,float InitialElapsedSeconds,
    const FMCEventSFXPreviewFilter* InitialFilter)
{
    Error.Reset();const int32 Count=Prepared.Slot.Layers.Num();
    if(!IsInGameThread() || bStartedOnce || !GetWorld() || !Prepared.Slot.Validate(Error) || !MCEventSFXPlayback::FiniteFrame(Frame)
        || Context.SourceID.IsNone() || Context.Event.IsNone() || !FMath::IsFinite(Context.Intensity)
        || !FMath::IsFinite(Context.Speed) || !FMath::IsFinite(InitialElapsedSeconds) || InitialElapsedSeconds<0.f
        || Prepared.Sounds.Num()!=Count || Prepared.Attenuations.Num()!=Count || Prepared.Concurrencies.Num()!=Count)
    {if(Error.IsEmpty())Error=TEXT("SFX requires a new player, immutable resident snapshot and finite accepted context.");return false;}
    if(Context.GameplayEpoch.IsValid()) {
        auto* State=GetWorld()->GetGameState<AMCGameState>();
        if(!State || State->GameplayLoadId!=Context.GameplayEpoch)
        {Error=TEXT("SFX event is not in the world's current accepted run identity.");return false;}
        EpochState=State;
    }
    if(HasUsableOutput() && Prepared.Slot.HasCustomLayers() && !Prepared.bPlaybackResourcesPrepared)
    {Error=TEXT("SFX playback resources were not prepared by the local location readiness barrier.");return false;}
    for(int32 I=0;I<Count;++I) {
        const auto& L=Prepared.Slot.Layers[I];
        if(!Prepared.Slot.HasCustomLayers() || !L.bEnabled || Prepared.DisabledGroupIDs.Contains(L.GroupID))continue;
        if(!Prepared.Sounds[I] || (!L.Attenuation.IsNull() && !Prepared.Attenuations[I])
            || (!L.Concurrency.IsNull() && !Prepared.Concurrencies[I]))
        {Error=TEXT("A required SFX layer was not resident before the accepted event.");return false;}
        if(MCEventSFXPlayback::RequiresNativeOwnerConcurrency(Prepared.Sounds[I],Prepared.Concurrencies[I]))
        {Error=TEXT("This custom source requires native-actor owner concurrency; the world-owned event player cannot silently substitute its own owner.");return false;}
        if(L.Space==EMCEventSFXSpace::SourceSocket && (!IsValid(SocketSource) || SocketSource->GetWorld()!=GetWorld()
            || !SocketSource->DoesSocketExist(L.Socket)))
        {Error=TEXT("SFX requires its exact native source component/socket binding: ")+L.Socket.ToString();return false;}
    }
    Data=Prepared;EventContext=Context;Subject=SocketSource;EventFrame=Frame;SetActorTransform(Frame);
    Voices.SetNum(Count);Age=InitialElapsedSeconds;InitialReceiptAge=InitialElapsedSeconds;
    bStartedOnce=true;bRunning=true;bStopping=false;if(InitialFilter)SetPreviewFilter(*InitialFilter);
    OutputStatus=HasUsableOutput()?TEXT("Audio device available; audibility still depends on source/concurrency/mix."):
        TEXT("Audio output unavailable or disabled; silent lifecycle only, no audible preview pass.");
    if(SocketSource)AddTickPrerequisiteComponent(SocketSource);
    Advance(0.f);SetActorTickEnabled(bRunning);return true;
}

bool AMCEventSFXPlayer::IsLayerEnabled(int32 Index) const
{return Data.Slot.HasCustomLayers() && Data.Slot.Layers.IsValidIndex(Index) && Data.Slot.Layers[Index].bEnabled
    && !Data.DisabledGroupIDs.Contains(Data.Slot.Layers[Index].GroupID);}
bool AMCEventSFXPlayer::HasUsableOutput() const
{return GetNetMode()!=NM_DedicatedServer && GetWorld() && GetWorld()->AllowAudioPlayback()
    && GEngine && GEngine->UseSound() && bool(GetWorld()->GetAudioDevice());}

float AMCEventSFXPlayer::EffectiveGain(int32 Index) const
{
    const auto& L=Data.Slot.Layers[Index];
    const bool bMuted=PreviewFilter.bOutputMuted || Data.MutedGroupIDs.Contains(L.GroupID)
        || PreviewFilter.MutedGroupIDs.Contains(L.GroupID)
        || (!PreviewFilter.SoloGroupIDs.IsEmpty() && !PreviewFilter.SoloGroupIDs.Contains(L.GroupID));
    return bMuted?0.f:L.Volume*PreviewFilter.OutputGain;
}

void AMCEventSFXPlayer::ApplyGainAndInputs(int32 Index)
{
    if(!Voices.IsValidIndex(Index) || !IsValid(Voices[Index].Component))return;
    auto* C=Voices[Index].Component.Get();
    C->SetVolumeMultiplier(EffectiveGain(Index));C->SetPitchMultiplier(Data.Slot.Layers[Index].Pitch);
    C->SetFloatParameter(TEXT("Intensity"),FMath::Clamp(EventContext.Intensity,0.f,1.f));
    C->SetFloatParameter(TEXT("Speed"),FMath::Clamp(EventContext.Speed,0.f,1.f));
}

void AMCEventSFXPlayer::StartLayer(int32 Index)
{
    auto& State=Voices[Index];const auto& L=Data.Slot.Layers[Index];auto* Sound=Data.Sounds[Index].Get();
    State.bStarted=true;
    if(!Sound){State.bFinished=true;return;}
    const bool bFinite=Sound->IsOneShot() && !Sound->IsLooping() && FMath::IsFinite(Sound->GetDuration()) && Sound->GetDuration()>0.f;
    State.SourceDuration=bFinite?Sound->GetDuration():0.f;
    const float SourceAge=FMath::Max(0.f,Age-L.DelaySeconds)*L.Pitch;
    float Start=SourceAge;
    if(bFinite && !L.bRepeatOnFinish && SourceAge>=State.SourceDuration){State.bFinished=true;++SkippedLateLayers;return;}
    if(bFinite && L.bRepeatOnFinish)Start=FMath::Fmod(SourceAge,State.SourceDuration);
    const auto* Wave=Cast<USoundWave>(Sound);
    // Decoder capability is required even for an ordinary nonprocedural wave.
    // A nonseekable source would otherwise replay its onset with an aged deadline.
    const bool bSeekable=Wave && !Wave->IsProcedurallyGenerated() && Wave->IsSeekable();
    if(InitialReceiptAge-L.DelaySeconds>MCEventSFXPlayback::MissedOnsetTolerance && !bSeekable)
    {State.bFinished=true;++SkippedLateLayers;return;}
    if(!bSeekable)Start=0.f;
    auto* C=NewObject<UAudioComponent>(this,NAME_None,RF_Transient);
    C->bAutoActivate=false;C->bAutoDestroy=false;C->bStopWhenOwnerDestroyed=true;
    C->bIsUISound=L.bIsUISound;C->SetSound(Sound);C->AttenuationSettings=Data.Attenuations[Index];
    if(EventContext.ParameterOwner.IsValid())UActorSoundParameterInterface::Fill(EventContext.ParameterOwner.Get(),C->DefaultParameters);
    if(Data.Concurrencies[Index])C->ConcurrencySet.Add(Data.Concurrencies[Index]);
    const FTransform Placement=L.Offset*EventFrame;
    if(L.Space==EMCEventSFXSpace::SourceSocket) {
        auto* Bound=Subject.Get();
        if(!Bound || !Bound->DoesSocketExist(L.Socket)) {State.bFinished=true;C->DestroyComponent();return;}
        C->SetupAttachment(Bound,L.Socket);C->SetRelativeTransform(L.Offset);
    } else {
        C->SetupAttachment(GetRootComponent());
        if(L.Space!=EMCEventSFXSpace::FollowEventFrame)C->SetAbsolute(true,true,true);
        C->SetWorldTransform(Placement);
    }
    if(L.Space==EMCEventSFXSpace::NonSpatial2D) {
        C->bAllowSpatialization=false;C->bOverrideAttenuation=true;
        C->AttenuationOverrides.bAttenuate=false;C->AttenuationOverrides.bSpatialize=false;
    }
    AddInstanceComponent(C);C->RegisterComponent();State.Component=C;
    C->OnAudioFinishedNative.AddUObject(this,&AMCEventSFXPlayer::FinishVoice);
    ApplyGainAndInputs(Index);PlayVoice(Index,Start);
}

void AMCEventSFXPlayer::PlayVoice(int32 Index,float SourceStartSeconds)
{PlayVoiceWithOutputState(Index,SourceStartSeconds,HasUsableOutput());}

void AMCEventSFXPlayer::PlayVoiceWithOutputState(int32 Index,float SourceStartSeconds,bool bOutputAvailable)
{
    auto& State=Voices[Index];const auto& L=Data.Slot.Layers[Index];auto* C=State.Component.Get();if(!C)return;
    State.bAwaitingRepeat=false;
    // Re-evaluate the local immutable readiness certificate on EVERY start/repeat.
    // Enabling/recovering output cannot promote an uncertified silent snapshot.
    State.bSilentClock=!bOutputAvailable || !Data.bPlaybackResourcesPrepared;
    if(bOutputAvailable && !Data.bPlaybackResourcesPrepared)
        OutputStatus=TEXT("Output became available; this snapshot remains silent. Rebuild and certify audio preparation before audition.");
    State.NextBoundaryAge=State.SourceDuration>0.f?Age+FMath::Max(.001f,(State.SourceDuration-SourceStartSeconds)/L.Pitch):Data.Slot.MaxDurationSeconds;
    ApplyGainAndInputs(Index);
    // Device mute still plays at zero gain to preserve phase. No-device mode does not pretend to play.
    if(!State.bSilentClock){++PlaybackRequests;C->FadeIn(L.FadeInSeconds,1.f,SourceStartSeconds);}
}

void AMCEventSFXPlayer::FinishVoice(UAudioComponent* Component)
{
    for(int32 I=0;I<Voices.Num();++I)if(Voices[I].Component==Component) {
        auto& State=Voices[I];
        if(State.bRetiring || bStopping || !bRunning || !IsLayerEnabled(I))State.bFinished=true;
        else if(Data.Slot.Layers[I].bRepeatOnFinish && Age<Data.Slot.MaxDurationSeconds)
        {State.bAwaitingRepeat=true;State.NextBoundaryAge=Age+.01f;}
        else State.bFinished=true;
        return;
    }
}

void AMCEventSFXPlayer::ReleaseVoice(int32 Index)
{
    auto* C=Voices[Index].Component.Get();Voices[Index].Component=nullptr;
    if(!IsValid(C))return;
    C->OnAudioFinishedNative.RemoveAll(this);C->Stop();C->DestroyComponent();
}

void AMCEventSFXPlayer::CancelLayer(int32 Index,bool bImmediate)
{
    auto& State=Voices[Index];const auto& L=Data.Slot.Layers[Index];
    if(State.bFinished){ReleaseVoice(Index);return;}
    State.bStarted=true;State.bAwaitingRepeat=false;
    if(bImmediate || !IsValid(State.Component) || Data.Slot.TailSeconds<=0.f)
    {State.bFinished=true;ReleaseVoice(Index);return;}
    if(State.bRetiring)return;
    State.bRetiring=true;auto* C=State.Component.Get();C->OnAudioFinishedNative.RemoveAll(this);
    C->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    State.bNaturalTail=L.bAllowNaturalTail && !L.bRepeatOnFinish && State.SourceDuration>0.f;
    const float Fade=FMath::Min(L.FadeOutSeconds,Data.Slot.TailSeconds);
    State.RetireAtAge=Age+(State.bNaturalTail?Data.Slot.TailSeconds:Fade);
    if(!State.bNaturalTail && !State.bSilentClock && Fade>0.f)C->FadeOut(Fade,0.f);
    if(!State.bNaturalTail && Fade<=0.f){State.bFinished=true;ReleaseVoice(Index);}
}

void AMCEventSFXPlayer::Advance(float DeltaSeconds)
{
    if(!bRunning || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0.f)return;
    if(EventContext.GameplayEpoch.IsValid() && (!EpochState.IsValid() || EpochState->GameplayLoadId!=EventContext.GameplayEpoch))
    {Stop(true);return;}
    Age+=DeltaSeconds;
    if(!bStopping && Age>=Data.Slot.MaxDurationSeconds)Stop(false);
    for(int32 I=0;I<Voices.Num() && bRunning;++I) {
        auto& State=Voices[I];const auto& L=Data.Slot.Layers[I];
        if(State.bFinished){ReleaseVoice(I);continue;}
        if(!IsLayerEnabled(I)){State.bStarted=true;State.bFinished=true;continue;}
        if(!State.bStarted && !bStopping && Age>=L.DelaySeconds)StartLayer(I);
        if(State.bFinished){ReleaseVoice(I);continue;}
        auto* C=State.Component.Get();
        if(!C)continue;
        if(L.Space==EMCEventSFXSpace::SourceSocket && !State.bRetiring && (!Subject.IsValid()
            || (Subject->GetOwner() && Subject->GetOwner()->IsActorBeingDestroyed())))
        {CancelLayer(I,true);continue;}
        if(State.bRetiring) {
            if(Age>=State.RetireAtAge || (State.bNaturalTail && State.SourceDuration>0.f && Age>=State.NextBoundaryAge))
            {State.bFinished=true;ReleaseVoice(I);}continue;
        }
        if(State.bAwaitingRepeat && Age>=State.NextBoundaryAge && !bStopping)PlayVoice(I,0.f);
        else if(State.bSilentClock && State.SourceDuration>0.f && Age>=State.NextBoundaryAge) {
            if(L.bRepeatOnFinish && !bStopping)PlayVoice(I,0.f);
            else {State.bFinished=true;ReleaseVoice(I);continue;}
        }
        else if(!State.bSilentClock && State.SourceDuration>0.f && Age>=State.NextBoundaryAge && !C->IsPlaying()) {
            // Failed-to-start concurrency voices do not broadcast OnAudioFinished.
            // Retire/retry only at the finite source boundary, never every frame.
            if(L.bRepeatOnFinish && !bStopping)PlayVoice(I,0.f);
            else {State.bFinished=true;ReleaseVoice(I);continue;}
        }
        if(L.Space==EMCEventSFXSpace::FollowEventFrame)C->SetWorldTransform(L.Offset*EventFrame);
    }
    if(GetPendingLayerCount()==0 && GetVoiceComponents().IsEmpty())Stop(true);
}

void AMCEventSFXPlayer::UpdateEventFrame(const FTransform& Frame)
{if(MCEventSFXPlayback::FiniteFrame(Frame)){EventFrame=Frame;SetActorTransform(Frame);}}
bool AMCEventSFXPlayer::BindEventFrameSource(USceneComponent* Source)
{
    if(!Source){FrameSource.Reset();return true;}
    if(!IsValid(Source) || !GetWorld() || Source->GetWorld()!=GetWorld() || Source->GetOwner()==this
        || (Source->GetOwner() && Source->GetOwner()->IsActorBeingDestroyed()))return false;
    FrameSource=Source;return true;
}
void AMCEventSFXPlayer::UpdateInputs(float Intensity,float Speed)
{
    if(!FMath::IsFinite(Intensity) || !FMath::IsFinite(Speed))return;
    EventContext.Intensity=Intensity;EventContext.Speed=Speed;
    for(int32 I=0;I<Voices.Num();++I)ApplyGainAndInputs(I);
}
void AMCEventSFXPlayer::SetPreviewFilter(const FMCEventSFXPreviewFilter& Filter)
{
    PreviewFilter=Filter;PreviewFilter.OutputGain=FMath::IsFinite(Filter.OutputGain)?FMath::Clamp(Filter.OutputGain,0.f,1.f):0.f;
    for(int32 I=0;I<Voices.Num();++I)ApplyGainAndInputs(I);
}

void AMCEventSFXPlayer::ClearOwnedVoices()
{for(int32 I=0;I<Voices.Num();++I){Voices[I].bStarted=true;Voices[I].bFinished=true;ReleaseVoice(I);}}
void AMCEventSFXPlayer::Stop(bool bImmediate)
{
    FrameSource.Reset();
    if(bImmediate) {
        bRunning=false;bStopping=true;SetActorTickEnabled(false);ClearOwnedVoices();
        if(Subject.IsValid())RemoveTickPrerequisiteComponent(Subject.Get());Subject.Reset();
        if(!IsActorBeingDestroyed())Destroy();return;
    }
    if(!bRunning || bStopping)return;bStopping=true;
    for(int32 I=0;I<Voices.Num();++I)CancelLayer(I,false);
}
void AMCEventSFXPlayer::StopGroup(FGuid GroupID,bool bImmediate)
{for(int32 I=0;I<Voices.Num();++I)if(Data.Slot.Layers[I].GroupID==GroupID)CancelLayer(I,bImmediate);}
void AMCEventSFXPlayer::StopWorldPlayers(UWorld* World,FName OwnershipTag)
{if(World && !OwnershipTag.IsNone())for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)if(It->Tags.Contains(OwnershipTag))It->Stop(true);}
int32 AMCEventSFXPlayer::GetPendingLayerCount() const
{int32 Count=0;for(int32 I=0;I<Voices.Num();++I)if(!Voices[I].bStarted && !Voices[I].bFinished && IsLayerEnabled(I))++Count;return Count;}
int32 AMCEventSFXPlayer::GetStartedLayerCount() const
{int32 Count=0;for(const auto& V:Voices)if(V.bStarted)++Count;return Count;}
TArray<UAudioComponent*> AMCEventSFXPlayer::GetVoiceComponents() const
{TArray<UAudioComponent*> Result;for(const auto& V:Voices)if(IsValid(V.Component))Result.Add(V.Component.Get());return Result;}
void AMCEventSFXPlayer::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(bRunning && !bStopping) {
        if(auto* Anchor=FrameSource.Get();Anchor && Anchor->GetWorld()==GetWorld()
            && (!Anchor->GetOwner() || !Anchor->GetOwner()->IsActorBeingDestroyed()))UpdateEventFrame(Anchor->GetComponentTransform());
    }
    Advance(DeltaSeconds);
}
void AMCEventSFXPlayer::EndPlay(const EEndPlayReason::Type Reason)
{bRunning=false;bStopping=true;FrameSource.Reset();ClearOwnedVoices();Super::EndPlay(Reason);}
