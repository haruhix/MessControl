#include "MCComposerAudioProbe.h"

#include "ActiveSound.h"
#include "Async/Async.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "AudioThread.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundBase.h"
#include "Templates/Atomic.h"

namespace
{
    constexpr int32 HistoryLimit=256;
    constexpr float SampleInterval=.1f;

    // Plain copied values, plus a weak object identity which is resolved only on GT.
    // No FActiveSound pointer or live audio-device collection crosses the thread boundary.
    struct FRawSound
    {
        TWeakObjectPtr<USoundBase> Source;
        uint32 PlayOrder=0;
        uint64 AudioComponentID=0;
        float PlaybackSeconds=0,ActiveVolume=0;
        bool bHadSource=false,bPlayingAudio=false,bPaused=false,bPreview=false,bHasVirtualized=false;
    };

    struct FObservedSound
    {
        FString SourcePath;
        uint32 PlayOrder=0;
        uint64 AudioComponentID=0;
        float PlaybackSeconds=0,ActiveVolume=0;
        double FirstObservedSeconds=0,LastObservedSeconds=0;
        uint32 ObservedSamples=0;
        bool bSourceExpired=false,bHadSource=false,bPlayingAudio=false,bEverPlayingAudio=false;
        bool bPaused=false,bPreview=false,bHasVirtualized=false,bInLastSample=false,bAlreadyActiveAtFirstSample=false;
        bool bNonFiniteVolume=false;
    };

    struct FProbeState
    {
        // Only these two fields are shared mutable state. All history/metadata below is GT-owned.
        TAtomic<bool> bRunning{false},bSamplePending{false};
        TWeakObjectPtr<UWorld> World;
        FString WorldPath,StartedUTC,StopReason,LastError;
        uint32 WorldID=0,DeviceID=0;
        uint64 SampleCount=0,TotalFirstObserved=0,DroppedHistory=0;
        double StartedAt=0,StoppedElapsed=0,LastSampleSeconds=0;
        int32 LastWorldActiveCount=0;
        bool bDeviceMuted=false,bDeviceMuteKnown=false,bActiveListTruncated=false;
        TArray<FObservedSound> History;
    };

    using FProbePtr=TSharedPtr<FProbeState,ESPMode::ThreadSafe>;
    FProbePtr CurrentProbe;
    FTSTicker::FDelegateHandle SampleTicker;
    FDelegateHandle EndPIEHandle;

    void StopInternal(const FString& Reason)
    {
        check(IsInGameThread());
        if(SampleTicker.IsValid()){FTSTicker::GetCoreTicker().RemoveTicker(SampleTicker);SampleTicker.Reset();}
        if(EndPIEHandle.IsValid()){FEditorDelegates::EndPIE.Remove(EndPIEHandle);EndPIEHandle.Reset();}
        if(CurrentProbe&&CurrentProbe->bRunning.Exchange(false))
        {
            CurrentProbe->StoppedElapsed=FPlatformTime::Seconds()-CurrentProbe->StartedAt;
            CurrentProbe->StopReason=Reason;
        }
        // Any pending command owns its own state and device handle. It will discard
        // results after cancellation and release the handle on GT, without a fence/wait.
    }

    void OnEndPIE(bool){StopInternal(TEXT("EndPIE"));}

    void AcceptSample(const FProbePtr& Probe,TArray<FRawSound>&& Sounds,double Seconds,int32 Total,bool Muted)
    {
        check(IsInGameThread());
        Probe->bSamplePending.Store(false);
        if(!Probe->bRunning.Load())return;
        TSet<uint32> CurrentOrders;
        for(const auto& Sound:Sounds)CurrentOrders.Add(Sound.PlayOrder);
        for(auto& Previous:Probe->History)Previous.bInLastSample=false;
        for(const auto& Raw:Sounds)
        {
            auto* Record=Probe->History.FindByPredicate([&Raw](const FObservedSound& Item){return Item.PlayOrder==Raw.PlayOrder;});
            if(!Record)
            {
                if(Probe->History.Num()>=HistoryLimit)
                {
                    // Keep every member of this whole active sample, including
                    // an existing sound which appears later in the copied array.
                    // History is bounded, so an old logical sound may be observed anew after eviction.
                    const int32 Old=Probe->History.IndexOfByPredicate([&CurrentOrders](const FObservedSound& Item){return !CurrentOrders.Contains(Item.PlayOrder);});
                    Probe->History.RemoveAt(Old==INDEX_NONE?0:Old);++Probe->DroppedHistory;
                }
                Record=&Probe->History.AddDefaulted_GetRef();Record->PlayOrder=Raw.PlayOrder;
                Record->FirstObservedSeconds=Seconds;Record->bAlreadyActiveAtFirstSample=Probe->SampleCount==0;
                ++Probe->TotalFirstObserved;
            }
            // A short-lived source may expire before GT receives the sample. Keep
            // the admitted-sound observation even when its path cannot be resolved.
            if(const auto* Source=Raw.Source.Get())Record->SourcePath=Source->GetPathName();
            Record->bSourceExpired=Raw.bHadSource&&!Raw.Source.IsValid();Record->bHadSource=Raw.bHadSource;
            Record->AudioComponentID=Raw.AudioComponentID;Record->PlaybackSeconds=Raw.PlaybackSeconds;
            Record->bNonFiniteVolume=!FMath::IsFinite(Raw.ActiveVolume);
            Record->ActiveVolume=Record->bNonFiniteVolume?0.f:Raw.ActiveVolume;
            Record->LastObservedSeconds=Seconds;++Record->ObservedSamples;
            Record->bPlayingAudio=Raw.bPlayingAudio;Record->bEverPlayingAudio|=Raw.bPlayingAudio;
            Record->bPaused=Raw.bPaused;Record->bPreview=Raw.bPreview;Record->bHasVirtualized=Raw.bHasVirtualized;
            Record->bInLastSample=true;
        }
        ++Probe->SampleCount;Probe->LastSampleSeconds=Seconds;Probe->LastWorldActiveCount=Total;
        Probe->bActiveListTruncated=Total>HistoryLimit;Probe->bDeviceMuted=Muted;Probe->bDeviceMuteKnown=true;
        Probe->LastError.Reset();
    }

    bool TickProbe(float)
    {
        check(IsInGameThread());
        const FProbePtr Probe=CurrentProbe;
        if(!Probe||!Probe->bRunning.Load())return false;
        UWorld* World=Probe->World.Get();
        if(!World||World->WorldType!=EWorldType::PIE||World->GetUniqueID()!=Probe->WorldID)
        {StopInternal(TEXT("PIE world no longer available"));return false;}
        FAudioDeviceHandle Device=World->GetAudioDevice();
        if(!Device){Probe->LastError=TEXT("У PIE-мира сейчас нет аудиоустройства; наблюдение не доказывает тишину.");return true;}
        if(Device.GetDeviceID()!=Probe->DeviceID)
        {StopInternal(TEXT("PIE audio device changed; start a new probe"));return false;}
        if(Probe->bSamplePending.Exchange(true))return true;
        const uint32 WorldID=Probe->WorldID;
        const double StartedAt=Probe->StartedAt;
        FAudioThread::RunCommandOnAudioThread([Probe,Device=MoveTemp(Device),WorldID,StartedAt]() mutable
        {
            TArray<FRawSound> Sounds;int32 Total=0;bool Muted=false;
            const double Seconds=FPlatformTime::Seconds()-StartedAt;
            if(Probe->bRunning.Load()&&Device)
            {
                Muted=Device->IsAudioDeviceMuted();
                for(const FActiveSound* Active:Device->GetActiveSounds())
                {
                    // WorldID is cached by FActiveSound::SetWorld on GT. Do not resolve its UWorld on AT.
                    if(!Active||Active->GetWorldID()!=WorldID)continue;
                    ++Total;if(Sounds.Num()>=HistoryLimit)continue;
                    auto& Raw=Sounds.AddDefaulted_GetRef();
                    Raw.PlayOrder=Active->GetPlayOrder();Raw.AudioComponentID=Active->GetAudioComponentID();
                    Raw.PlaybackSeconds=Active->PlaybackTime;Raw.ActiveVolume=Active->GetVolume();
                    Raw.bPlayingAudio=Active->IsPlayingAudio();Raw.bPaused=Active->bIsPaused;
                    Raw.bPreview=Active->bIsPreviewSound;Raw.bHasVirtualized=Active->bHasVirtualized;
                    Raw.bHadSource=Active->GetSound()!=nullptr;
                    // FActiveSound retains Sound for this command. Creating a new local
                    // weak identity uses atomic serial allocation; resolve/name access is GT-only.
                    Raw.Source=TWeakObjectPtr<USoundBase>(Active->GetSound());
                }
            }
            // A last FAudioDeviceHandle release can tear down its device. Move it
            // back to GT even for cancelled samples; never release the last handle on AT.
            AsyncTask(ENamedThreads::GameThread,[Probe,Device=MoveTemp(Device),Sounds=MoveTemp(Sounds),Seconds,Total,Muted]() mutable
            {
                AcceptSample(Probe,MoveTemp(Sounds),Seconds,Total,Muted);
                Device.Reset();
            });
        });
        return true;
    }

    TSharedRef<FJsonObject> SoundJson(const FObservedSound& Sound)
    {
        auto Result=MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("source_path"),Sound.SourcePath);
        Result->SetBoolField(TEXT("source_expired_before_resolution"),Sound.bSourceExpired);
        Result->SetBoolField(TEXT("source_path_known"),!Sound.SourcePath.IsEmpty());
        Result->SetBoolField(TEXT("had_source"),Sound.bHadSource);
        Result->SetNumberField(TEXT("play_order"),Sound.PlayOrder);
        Result->SetStringField(TEXT("audio_component_id"),LexToString(Sound.AudioComponentID));
        Result->SetNumberField(TEXT("playback_seconds"),FMath::IsFinite(Sound.PlaybackSeconds)?Sound.PlaybackSeconds:0.f);
        Result->SetNumberField(TEXT("active_volume"),Sound.ActiveVolume);
        Result->SetBoolField(TEXT("non_finite_volume"),Sound.bNonFiniteVolume);
        Result->SetNumberField(TEXT("first_observed_seconds"),Sound.FirstObservedSeconds);
        Result->SetNumberField(TEXT("last_observed_seconds"),Sound.LastObservedSeconds);
        Result->SetNumberField(TEXT("observed_samples"),Sound.ObservedSamples);
        Result->SetBoolField(TEXT("in_last_active_sample"),Sound.bInLastSample);
        Result->SetBoolField(TEXT("playing_audio_flag"),Sound.bPlayingAudio);
        Result->SetBoolField(TEXT("ever_playing_audio_flag"),Sound.bEverPlayingAudio);
        Result->SetBoolField(TEXT("paused"),Sound.bPaused);Result->SetBoolField(TEXT("preview_sound"),Sound.bPreview);
        Result->SetBoolField(TEXT("has_been_virtualized"),Sound.bHasVirtualized);
        Result->SetBoolField(TEXT("already_active_at_first_sample"),Sound.bAlreadyActiveAtFirstSample);
        return Result;
    }
}

bool MCComposerAudioProbe::Start(FString& Error)
{
    Error.Reset();
    if(!IsInGameThread()){Error=TEXT("Запускайте аудиопроверку на потоке редактора.");return false;}
    TArray<UWorld*> Worlds;
    if(GEngine)for(const auto& Context:GEngine->GetWorldContexts())
        if(Context.WorldType==EWorldType::PIE)if(UWorld* World=Context.World())Worlds.AddUnique(World);
    if(Worlds.Num()!=1){Error=TEXT("Нужен ровно один запущенный PIE-мир для отдельной аудиопроверки.");return false;}
    UWorld* World=Worlds[0];
    if(!World->bAllowAudioPlayback){Error=TEXT("В этом PIE-мире воспроизведение звука выключено.");return false;}
    const FAudioDeviceHandle Device=World->GetAudioDevice();
    if(!Device){Error=TEXT("У PIE-мира нет аудиоустройства. Наблюдение не запущено.");return false;}
    StopInternal(TEXT("Replaced by a new explicit probe"));
    CurrentProbe=MakeShared<FProbeState,ESPMode::ThreadSafe>();
    CurrentProbe->World=World;CurrentProbe->WorldPath=World->GetPathName();CurrentProbe->WorldID=World->GetUniqueID();
    CurrentProbe->DeviceID=Device.GetDeviceID();CurrentProbe->StartedAt=FPlatformTime::Seconds();
    CurrentProbe->StartedUTC=FDateTime::UtcNow().ToIso8601();CurrentProbe->bRunning.Store(true);
    EndPIEHandle=FEditorDelegates::EndPIE.AddStatic(&OnEndPIE);
    SampleTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickProbe),SampleInterval);
    TickProbe(0.f);
    return true;
}

void MCComposerAudioProbe::Stop(){StopInternal(TEXT("Stopped explicitly"));}

FString MCComposerAudioProbe::SnapshotJson()
{
    if(!IsInGameThread())return TEXT("{\"error\":\"Game-thread API only\"}");
    auto Root=MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("mode"),TEXT("sampled_engine_active_sounds"));
    Root->SetNumberField(TEXT("sample_interval_ms"),100);Root->SetNumberField(TEXT("history_limit"),HistoryLimit);
    Root->SetBoolField(TEXT("running"),CurrentProbe&&CurrentProbe->bRunning.Load());
    Root->SetBoolField(TEXT("sample_pending"),CurrentProbe&&CurrentProbe->bSamplePending.Load());
    Root->SetStringField(TEXT("limitations"),TEXT("Опрос 100 мс: короткие звуки могут быть пропущены. Наблюдение ActiveSound подтверждает попадание в активный список движка, а не слышимый PCM. Виртуализированные без реализации звуки и отклонённые попытки сюда не входят. active_volume не является финальной громкостью после пространственного микса и submix."));
    TArray<TSharedPtr<FJsonValue>> History,Active;TSet<FString> Paths;
    if(const auto Probe=CurrentProbe)
    {
        Root->SetStringField(TEXT("world"),Probe->WorldPath);Root->SetNumberField(TEXT("world_id"),Probe->WorldID);
        Root->SetNumberField(TEXT("audio_device_id"),Probe->DeviceID);Root->SetStringField(TEXT("started_utc"),Probe->StartedUTC);
        Root->SetNumberField(TEXT("elapsed_seconds"),Probe->bRunning.Load()?FPlatformTime::Seconds()-Probe->StartedAt:Probe->StoppedElapsed);
        Root->SetStringField(TEXT("stop_reason"),Probe->StopReason);Root->SetStringField(TEXT("last_error"),Probe->LastError);
        Root->SetNumberField(TEXT("completed_samples"),double(Probe->SampleCount));
        Root->SetNumberField(TEXT("last_sample_seconds"),Probe->LastSampleSeconds);
        Root->SetNumberField(TEXT("last_world_active_count"),Probe->LastWorldActiveCount);
        Root->SetBoolField(TEXT("active_list_truncated"),Probe->bActiveListTruncated);
        Root->SetBoolField(TEXT("device_mute_known"),Probe->bDeviceMuteKnown);
        if(Probe->bDeviceMuteKnown)Root->SetBoolField(TEXT("device_muted"),Probe->bDeviceMuted);
        else Root->SetField(TEXT("device_muted"),MakeShared<FJsonValueNull>());
        Root->SetNumberField(TEXT("history_records_created"),double(Probe->TotalFirstObserved));
        Root->SetStringField(TEXT("deduplication_scope"),TEXT("retained_history_by_device_world_play_order"));
        Root->SetNumberField(TEXT("dropped_history_records"),double(Probe->DroppedHistory));
        for(const auto& Sound:Probe->History)
        {
            const auto Data=MakeShared<FJsonValueObject>(SoundJson(Sound));History.Add(Data);
            if(Sound.bInLastSample)Active.Add(Data);
            if(!Sound.SourcePath.IsEmpty())Paths.Add(Sound.SourcePath);
        }
    }
    Root->SetNumberField(TEXT("known_source_path_count"),Paths.Num());
    Root->SetArrayField(TEXT("history"),History);Root->SetArrayField(TEXT("last_active_sample"),Active);
    FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
}
