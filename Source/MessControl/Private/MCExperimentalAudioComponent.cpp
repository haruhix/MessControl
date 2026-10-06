#include "MCExperimentalAudioComponent.h"
#include "MCToothCharacter.h"
#include "MCDataAssets.h"
#include "AudioParameter.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarAudioExperiments(TEXT("mc.Audio.Experiments"),1,
        TEXT("0: original sound palette. 1: experimental MetaSounds with palette fallback."));
    TAutoConsoleVariable<int32> CVarAudioExperimentLog(TEXT("mc.Audio.ExperimentLog"),0,
        TEXT("Log experimental sound events and normalized graph inputs for editor checks."));
    const TCHAR* const ExperimentEvents[]={TEXT("Step"),TEXT("KnifeSwing"),TEXT("KnifeHit"),TEXT("PickaxeSwing"),TEXT("PickaxeHit"),TEXT("Brush")};

    void AudioMessage(const FString& Message,FColor Color=FColor::Cyan,float Seconds=5.f)
    {
        UE_LOG(LogTemp,Display,TEXT("MC_AUDIO %s"),*Message);
        if(GEngine) GEngine->AddOnScreenDebugMessage(-1,Seconds,Color,Message);
    }

    AMCToothCharacter* CurrentAudioHero(UWorld* World)
    {
        if(!World || !World->IsGameWorld() || World->GetNetMode()==NM_DedicatedServer) return nullptr;
        auto* Player=UGameplayStatics::GetPlayerController(World,0);
        return Player && Player->IsLocalController()?Cast<AMCToothCharacter>(Player->GetPawn()):nullptr;
    }

    FName LegacyEvent(FName Event)
    {
        if(Event==TEXT("KnifeSwing") || Event==TEXT("PickaxeSwing")) return TEXT("Whoosh");
        if(Event==TEXT("KnifeHit") || Event==TEXT("PickaxeHit")) return TEXT("Hit");
        return Event;
    }

    FAutoConsoleCommandWithWorldAndArgs AudioStatusCommand(TEXT("mc.Audio.Status"),
        TEXT("Show the current local character's experimental sound paths, classes and selection mode."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&,UWorld* World)
        {
            if(auto* Hero=CurrentAudioHero(World); Hero && Hero->ExperimentalAudio) Hero->ExperimentalAudio->ShowStatus();
            else AudioMessage(TEXT("Аудио: запустите игру / PIE с локальным персонажем."),FColor::Yellow);
        }));

    FAutoConsoleCommandWithWorldAndArgs AudioCompareCommand(TEXT("mc.Audio.Compare"),
        TEXT("Compare 2 original then 2 current experimental short sounds in 2D. Usage: mc.Audio.Compare Step|KnifeSwing|KnifeHit|PickaxeSwing|PickaxeHit|Brush"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
        {
            if(Args.Num()!=1)
            {
                AudioMessage(TEXT("mc.Audio.Compare Step | KnifeSwing | KnifeHit | PickaxeSwing | PickaxeHit | Brush"),FColor::Yellow);
                return;
            }
            if(auto* Hero=CurrentAudioHero(World); Hero && Hero->ExperimentalAudio)
                Hero->ExperimentalAudio->BeginComparison(FName(*Args[0]),Hero->SoundPalette);
            else AudioMessage(TEXT("Сравнение: запустите игру / PIE с локальным персонажем."),FColor::Yellow);
        }));
}

UMCExperimentalAudioComponent::UMCExperimentalAudioComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    for(const TCHAR* Event:ExperimentEvents)
    {
        const FString AssetName=FString(TEXT("MS_"))+Event;
        Sounds.Add(FName(Event),TSoftObjectPtr<USoundBase>(FSoftObjectPath(
            FString::Printf(TEXT("/Game/Audio/Experiments/%s.%s"),*AssetName,*AssetName))));
    }
}

bool UMCExperimentalAudioComponent::IsEnabled()
{
    return CVarAudioExperiments.GetValueOnGameThread()!=0;
}

USoundBase* UMCExperimentalAudioComponent::ResolveSound(FName Event,FString& FailureReason)
{
    FailureReason.Reset();
    const auto* Path=Sounds.Find(Event);
    if(!Path || Path->IsNull())
    {
        LoadedSounds.Remove(Event); LoadedPaths.Remove(Event);
        FailureReason=Path?TEXT("empty_reference"):TEXT("missing_event");
        return nullptr;
    }
    const FSoftObjectPath RequestedPath=Path->ToSoftObjectPath();
    const auto* CachedPath=LoadedPaths.Find(Event);
    if(!CachedPath || *CachedPath!=RequestedPath)
    {
        LoadedSounds.Remove(Event); LoadedPaths.Remove(Event);
    }
    USoundBase* Sound=LoadedSounds.FindRef(Event);
    if(!Sound)
    {
        // An experiment may be authored after this actor's CDO was constructed.
        // Do not cache a failed load or warn every time its original sound plays.
        if(!FPackageName::DoesPackageExist(RequestedPath.GetLongPackageName()))
        {
            FailureReason=TEXT("package_missing");
            return nullptr;
        }
        Sound=Path->LoadSynchronous();
        if(!Sound)
        {
            FailureReason=TEXT("load_failed");
            return nullptr;
        }
        LoadedSounds.Add(Event,Sound);
        LoadedPaths.Add(Event,RequestedPath);
    }
    return Sound;
}

bool UMCExperimentalAudioComponent::Play(FName Event,FVector Location,float Intensity,float Speed)
{
    if(!IsEnabled() || !GetWorld() || GetNetMode()==NM_DedicatedServer) return false;
    FString FailureReason;
    USoundBase* Sound=ResolveSound(Event,FailureReason);
    if(!Sound)
    {
        if(CVarAudioExperimentLog.GetValueOnGameThread()!=0)
        {
            const auto* Path=Sounds.Find(Event);
            UE_LOG(LogTemp,Display,TEXT("MC_AUDIO_FALLBACK event=%s reason=%s path=%s owner=%s"),
                *Event.ToString(),*FailureReason,Path?*Path->ToString():TEXT("<none>"),*GetNameSafe(GetOwner()));
        }
        return false;
    }
    if(!InitialParameters) InitialParameters=NewObject<UInitialActiveSoundParams>(this);
    InitialParameters->Reset(2);
    InitialParameters->AudioParams.Emplace(FName(TEXT("Intensity")),FMath::Clamp(Intensity,0.f,1.f));
    InitialParameters->AudioParams.Emplace(FName(TEXT("Speed")),FMath::Clamp(Speed,0.f,1.f));
    UGameplayStatics::PlaySoundAtLocation(this,Sound,Location,FRotator::ZeroRotator,
        1.f,1.f,0.f,nullptr,nullptr,GetOwner(),InitialParameters);
    if(CVarAudioExperimentLog.GetValueOnGameThread()!=0)
        UE_LOG(LogTemp,Display,TEXT("MC_AUDIO_EXPERIMENT event=%s intensity=%.3f speed=%.3f owner=%s net=%d path=%s class=%s"),
            *Event.ToString(),FMath::Clamp(Intensity,0.f,1.f),FMath::Clamp(Speed,0.f,1.f),*GetNameSafe(GetOwner()),int32(GetNetMode()),
            *Sound->GetPathName(),*Sound->GetClass()->GetName());
    return true;
}

void UMCExperimentalAudioComponent::ShowStatus()
{
    AudioMessage(FString::Printf(TEXT("Аудио: mc.Audio.Experiments=%d; игрок %s"),
        CVarAudioExperiments.GetValueOnGameThread(),*GetNameSafe(GetOwner())),FColor::Cyan,10.f);
    for(const TCHAR* Event:ExperimentEvents)
    {
        FString FailureReason;
        if(USoundBase* Sound=ResolveSound(FName(Event),FailureReason))
            AudioMessage(FString::Printf(TEXT("%s → %s [%s]"),Event,*Sound->GetPathName(),*Sound->GetClass()->GetName()),FColor::Cyan,10.f);
        else
        {
            const auto* Path=Sounds.Find(FName(Event));
            AudioMessage(FString::Printf(TEXT("%s → старая палитра (%s; %s)"),Event,*FailureReason,
                Path?*Path->ToString():TEXT("<none>")),FColor::Yellow,10.f);
        }
    }
}

void UMCExperimentalAudioComponent::BeginComparison(FName Event,UMCSoundPalette* Palette)
{
    bool KnownEvent=false;
    for(const TCHAR* Name:ExperimentEvents) if(Event==FName(Name)) {Event=FName(Name);KnownEvent=true;break;}
    if(!KnownEvent)
    {
        AudioMessage(TEXT("Событие: Step, KnifeSwing, KnifeHit, PickaxeSwing, PickaxeHit или Brush."),FColor::Yellow);
        return;
    }
    if(!GetWorld() || !GetWorld()->IsGameWorld() || GetNetMode()==NM_DedicatedServer) return;
    const auto* Legacy=Palette?Palette->Events.Find(LegacyEvent(Event)):nullptr;
    if(!Legacy || !Legacy->Sounds.ContainsByPredicate([](const auto& Sound){return IsValid(Sound.Get());}))
    {
        AudioMessage(TEXT("Сравнение: в текущей старой палитре нет звука для этого события."),FColor::Yellow);
        return;
    }
    FString FailureReason;
    if(!ResolveSound(Event,FailureReason))
    {
        AudioMessage(FString::Printf(TEXT("Сравнение: новый %s недоступен (%s)."),*Event.ToString(),*FailureReason),FColor::Yellow);
        return;
    }
    StopComparison();
    ComparisonEvent=Event; ComparisonPalette=Palette; ComparisonStep=0;
    AdvanceComparison();
}

void UMCExperimentalAudioComponent::AdvanceComparison()
{
    if(ComparisonAudio)
    {
        ComparisonAudio->Stop(); ComparisonAudio->DestroyComponent(); ComparisonAudio=nullptr;
    }
    if(!GetWorld() || ComparisonStep>=4)
    {
        AudioMessage(TEXT("Сравнение завершено: два старых, затем два новых. Режим игры не менялся."));
        StopComparison();
        return;
    }
    const bool Original=ComparisonStep<2;
    float Volume=1.f,Pitch=1.f;
    USoundBase* Sound=nullptr;
    FString FailureReason;
    if(Original)
    {
        const auto* Entry=ComparisonPalette?ComparisonPalette->Events.Find(LegacyEvent(ComparisonEvent)):nullptr;
        if(Entry)
        {
            TArray<USoundBase*> ValidSounds;
            for(const auto& Candidate:Entry->Sounds) if(IsValid(Candidate.Get())) ValidSounds.Add(Candidate.Get());
            if(!ValidSounds.IsEmpty()) Sound=ValidSounds[FMath::RandRange(0,ValidSounds.Num()-1)];
            Volume=Entry->Volume;
            Pitch=(Entry->PitchMin+FMath::Max(Entry->PitchMin,Entry->PitchMax))*.5f;
        }
    }
    else Sound=ResolveSound(ComparisonEvent,FailureReason);
    if(!Sound)
    {
        AudioMessage(FString::Printf(TEXT("Сравнение остановлено: %s (%s)."),*ComparisonEvent.ToString(),*FailureReason),FColor::Yellow);
        StopComparison();
        return;
    }
    ComparisonAudio=UGameplayStatics::CreateSound2D(this,Sound,Volume,Pitch,0.f,nullptr,false,false);
    if(!ComparisonAudio)
    {
        AudioMessage(TEXT("Сравнение остановлено: аудиоустройство недоступно."),FColor::Yellow);
        StopComparison();
        return;
    }
    ComparisonAudio->bAllowSpatialization=false;
    ComparisonAudio->bOverrideAttenuation=true;
    ComparisonAudio->AttenuationOverrides.bAttenuate=false;
    ComparisonAudio->AttenuationOverrides.bSpatialize=false;
    ComparisonAudio->SetFloatParameter(TEXT("Intensity"),.65f);
    ComparisonAudio->SetFloatParameter(TEXT("Speed"),.5f);
    const TCHAR* Label=Original?TEXT("Старый"):TEXT("Новый");
    AudioMessage(FString::Printf(TEXT("%s %d/2: %s"),Label,ComparisonStep%2+1,*ComparisonEvent.ToString()),
        Original?FColor::Yellow:FColor::Green,.9f);
    UE_LOG(LogTemp,Display,TEXT("MC_AUDIO_COMPARE event=%s phase=%s sample=%d path=%s class=%s intensity=.65 speed=.5 volume=%.3f pitch=%.3f"),
        *ComparisonEvent.ToString(),Original?TEXT("old"):TEXT("new"),ComparisonStep%2+1,
        *Sound->GetPathName(),*Sound->GetClass()->GetName(),Volume,Pitch);
    ComparisonAudio->Play();
    ++ComparisonStep;
    // Procedural sources may report an infinite GetDuration despite a finite DSP graph.
    // Fixed one-second slots bound the audition and leave a gap after these short SFX.
    const TWeakObjectPtr<UMCExperimentalAudioComponent> WeakThis(this);
    GetWorld()->GetTimerManager().SetTimer(ComparisonTimer,FTimerDelegate::CreateLambda([WeakThis]()
    {
        if(auto* Component=WeakThis.Get()) Component->AdvanceComparison();
    }),1.f,false);
}

void UMCExperimentalAudioComponent::StopComparison()
{
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ComparisonTimer);
    if(ComparisonAudio)
    {
        ComparisonAudio->Stop(); ComparisonAudio->DestroyComponent(); ComparisonAudio=nullptr;
    }
    ComparisonPalette=nullptr; ComparisonEvent=NAME_None; ComparisonStep=0;
}

void UMCExperimentalAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopComparison();
    Super::EndPlay(Reason);
}
