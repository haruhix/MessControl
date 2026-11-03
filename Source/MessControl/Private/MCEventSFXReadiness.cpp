#include "MCEventSFXReadiness.h"
#include "MCPreloadManifest.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

namespace MCEventSFXPrewarm
{
constexpr int32 MaxSources=128,MaxWaves=256;
constexpr uint64 MaxRetainedStartupBytes=64ull*1024ull*1024ull;
constexpr double MaxPreparationSeconds=120.;
}

void UMCEventSFXReadiness::Reset()
{
    // Releasing only our chunk handles never clears the shared streaming cache or alters source assets.
    Work.Reset();RetainedWaves.Reset();RetainedPlans.Reset();PreparedWorld.Reset();
    StartedAt=0;State=EMCEventSFXReadiness::Idle;Diagnostic.Reset();
}
void UMCEventSFXReadiness::Fail(const FString& Error)
{State=EMCEventSFXReadiness::Failed;Diagnostic=Error;}

bool UMCEventSFXReadiness::Begin(UWorld* World,const TArray<FMCEventSFXSamplePlan>& Plans,FString& Error)
{
    Reset();Error.Reset();
    if(!World || Plans.Num()>MCEventSFXPrewarm::MaxSources)
    {Error=TEXT("Audio readiness needs a live location and at most 128 scoped sources.");Fail(Error);return false;}
    PreparedWorld=World;RetainedPlans=Plans;
    if(!GEngine || !GEngine->UseSound() || !World->AllowAudioPlayback() || !World->GetAudioDevice()) {
        State=EMCEventSFXReadiness::OutputUnavailable;
        Diagnostic=TEXT("Audio output disabled/unavailable: visual readiness may proceed, playback-resource/audible readiness is unverified.");return true;
    }
    for(const auto& Plan:Plans) {
        if(!IsValid(Plan.Source)) {Error=TEXT("An audio preparation source is not resident.");Fail(Error);return false;}
        auto* DirectWave=Cast<USoundWave>(Plan.Source.Get());
        if(DirectWave && !DirectWave->IsProcedurallyGenerated())RetainedWaves.AddUnique(DirectWave);
        else if(!Plan.bDependencyAuditComplete || !Plan.bGraphPrepared) {
            Error=TEXT("Cue/MetaSound/procedural preparation needs an audited dependency list and prepared source-specific graph resources: ")+Plan.Source->GetPathName();
            Fail(Error);return false;
        }
        for(USoundWave* Wave:Plan.SampleWaves) {
            if(!IsValid(Wave) || Wave->IsProcedurallyGenerated())
            {Error=TEXT("Audio sample dependencies must be resident, nonprocedural SoundWaves.");Fail(Error);return false;}
            RetainedWaves.AddUnique(Wave);
        }
    }
    if(RetainedWaves.Num()>MCEventSFXPrewarm::MaxWaves)
    {Error=TEXT("Location audio preparation exceeds 256 sample waves.");Fail(Error);return false;}
    State=EMCEventSFXReadiness::Preparing;StartedAt=FPlatformTime::Seconds();
    auto Device=World->GetAudioDevice();
    for(USoundWave* Wave:RetainedWaves) {
        FWaveWork W;W.Wave=Wave;Work.Add(MoveTemp(W));
        // Engine Precache setup only: streaming Done can still mean compressed bytes rather than decoded first audio.
        // Initial DSP/decode may occur at Play; this barrier does not certify a decoded first buffer.
        Device->Precache(Wave,false,false,false);
    }
    Poll();if(State==EMCEventSFXReadiness::Failed){Error=Diagnostic;return false;}return true;
}

void UMCEventSFXReadiness::Poll()
{
    if(State!=EMCEventSFXReadiness::Preparing)return;
    if(!PreparedWorld.IsValid() || PreparedWorld->bIsTearingDown)
    {Fail(TEXT("Audio location was destroyed during preparation."));return;}
    if(FPlatformTime::Seconds()-StartedAt>MCEventSFXPrewarm::MaxPreparationSeconds)
    {Fail(TEXT("Audio preparation timed out; remaining Precache/startup-resource work was not certified ready."));return;}
    uint64 RetainedBytes=0;int32 Ready=0;
    for(auto& Item:Work) {
        auto* Wave=Item.Wave.Get();if(!Wave){Fail(TEXT("A retained audio sample became invalid."));return;}
        if(Item.bReady){++Ready;RetainedBytes+=Item.StartupChunk.Num();continue;}
        if(Wave->GetPrecacheState()!=ESoundWavePrecacheState::Done)continue;
        if(Wave->DecompressionType==DTYPE_Invalid)
        {Fail(TEXT("Audio decode setup failed: ")+Wave->GetPathName());return;}
        if(!Item.WaveData.IsValid())Item.WaveData=Wave->GetSoundWaveProxy()->GetSoundWaveDataRef();
        if(Wave->IsStreaming()) {
            if(Item.WaveData->GetNumChunks()==0 || Item.WaveData->GetZerothChunk(false).IsEmpty())
            {Fail(TEXT("Streaming source has no prepared initial chunk: ")+Wave->GetPathName());return;}
            if(Item.WaveData->GetNumChunks()>1) {
                auto& Streaming=IStreamingManager::Get().GetAudioStreamingManager();
                Item.StartupChunk=Streaming.GetLoadedChunk(Item.WaveData.ToSharedRef(),1,false,false);
                if(!Item.StartupChunk.IsValid()) {
                    if(!Item.bChunkRequested) {
                        Item.bChunkRequested=true;
                        if(!Streaming.RequestChunk(Item.WaveData.ToSharedRef(),1))
                        {Fail(TEXT("Audio first streaming-chunk request failed: ")+Wave->GetPathName());return;}
                    }
                    continue;
                }
                RetainedBytes+=Item.StartupChunk.Num();
            }
        }
        Item.bReady=true;++Ready;
    }
    if(RetainedBytes>MCEventSFXPrewarm::MaxRetainedStartupBytes)
    {Fail(TEXT("Retained audio startup chunks exceeded the explicit 64MiB budget."));return;}
    if(Ready==Work.Num()) {
        State=EMCEventSFXReadiness::Ready;
        Diagnostic=FString::Printf(TEXT("Prepared playback resources for %d scoped sources / %d sample waves; retained compressed startup chunks %.2fMiB. Decoded first buffer/DSP and later streaming are not certified."),
            RetainedPlans.Num(),RetainedWaves.Num(),double(RetainedBytes)/(1024.*1024.));
    }
}

bool UMCEventSFXReadiness::CertifyPrepared(FMCPreparedEventSFX& Prepared,FString& Error) const
{
    Error.Reset();Prepared.bPlaybackResourcesPrepared=false;
    if(!IsReady()){Error=State==EMCEventSFXReadiness::Ready?TEXT("Audio playback resources changed; explicitly reprepare before playback."):
        Diagnostic.IsEmpty()?TEXT("Audio playback-resource preparation is not complete."):Diagnostic;return false;}
    for(int32 I=0;I<Prepared.Slot.Layers.Num();++I) {
        const auto& L=Prepared.Slot.Layers[I];if(!L.bEnabled || Prepared.DisabledGroupIDs.Contains(L.GroupID))continue;
        const auto* Sound=Prepared.Sounds.IsValidIndex(I)?Prepared.Sounds[I].Get():nullptr;
        if(!Sound || !RetainedPlans.ContainsByPredicate([Sound](const auto& P){return P.Source==Sound;}))
        {Error=TEXT("Prepared SFX source was outside the scoped audio readiness manifest.");return false;}
    }
    Prepared.bPlaybackResourcesPrepared=true;return true;
}
int32 UMCEventSFXReadiness::GetPendingWaveCount() const
{int32 Count=0;for(const auto& W:Work)if(!W.bReady)++Count;return Count;}

bool UMCEventSFXReadiness::ArePlaybackResourcesCurrent() const
{
    if(!PreparedWorld.IsValid() || PreparedWorld->bIsTearingDown)return false;
    for(const auto& Item:Work) {
        auto* Wave=Item.Wave.Get();
        if(!IsValid(Wave) || !Item.bReady || Wave->GetPrecacheState()!=ESoundWavePrecacheState::Done
            || Wave->DecompressionType==DTYPE_Invalid || !Item.WaveData.IsValid())return false;
        const auto Current=Wave->GetSoundWaveProxy()->GetSoundWaveDataRef();
        if(&Current.Get()!=Item.WaveData.Get())return false;
        if(Wave->IsStreaming() && Item.WaveData->GetNumChunks()>1) {
            if(!Item.StartupChunk.IsValid())return false;
#if WITH_EDITOR
            if(Item.StartupChunk.IsStale())return false;
#endif
        }
    }
    for(const auto& Previous:RetainedPlans) {
        FMCEventSFXSamplePlan Current;FString Error;
        if(!FMCPreloadManifest::BuildEventAudioPlan(Previous.Source,Current,Error) || Current.SampleWaves.Num()!=Previous.SampleWaves.Num())return false;
        for(USoundWave* Wave:Current.SampleWaves)if(!Previous.SampleWaves.Contains(Wave))return false;
    }
    return true;
}

bool UMCEventSFXReadiness::IsReady() const
{return State==EMCEventSFXReadiness::Ready && ArePlaybackResourcesCurrent();}
