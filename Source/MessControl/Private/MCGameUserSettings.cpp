#include "MCGameUserSettings.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioThread.h"
#include "ActiveSound.h"
#include "Components/AudioComponent.h"
#include "MCVoiceChatSubsystem.h"
#include "MCVoiceSoundClass.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

namespace
{
    float Volume(float Value) { return FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):1.f; }
    float Sensitivity(float Value) { return FMath::IsFinite(Value)?FMath::Clamp(Value,.1f,5.f):1.f; }
}

UMCGameUserSettings* UMCGameUserSettings::Get()
{
    return GEngine?Cast<UMCGameUserSettings>(GEngine->GetGameUserSettings()):nullptr;
}

void UMCGameUserSettings::SetMasterVolume(float Value) { MasterVolume=Volume(Value); }
void UMCGameUserSettings::SetMusicVolume(float Value) { MusicVolume=Volume(Value); }
void UMCGameUserSettings::SetAmbienceVolume(float Value) { AmbienceVolume=Volume(Value); }
void UMCGameUserSettings::SetSFXVolume(float Value) { SFXVolume=Volume(Value); }
float UMCGameUserSettings::GetEffectiveSFXVolume() const { return Volume(MasterVolume)*Volume(SFXVolume); }
float UMCGameUserSettings::GetEffectiveMusicVolume() const { return Volume(MasterVolume)*Volume(MusicVolume); }
float UMCGameUserSettings::GetEffectiveAmbienceVolume() const { return Volume(MasterVolume)*Volume(AmbienceVolume); }
void UMCGameUserSettings::SetMouseSensitivity(float Value) { MouseSensitivity=Sensitivity(Value); }
void UMCGameUserSettings::SetGamepadSensitivity(float Value) { GamepadSensitivity=Sensitivity(Value); }
void UMCGameUserSettings::SetSavedBagColor(FLinearColor Value)
{
    if(!FMath::IsFinite(Value.R) || !FMath::IsFinite(Value.G) || !FMath::IsFinite(Value.B) || !FMath::IsFinite(Value.A)) return;
    SavedBagColor=Value.GetClamped(0.f,1.f);SavedBagColor.A=1.f;bHasCustomBagColor=true;
}
void UMCGameUserSettings::SetVoiceInputMode(EMCVoiceInputMode Value)
{ VoiceInputMode=Value==EMCVoiceInputMode::OpenMic?Value:EMCVoiceInputMode::PushToTalk; }
bool UMCGameUserSettings::SetPushToTalkKey(FKey Value)
{
    if(!Value.IsValid() || Value.IsAnalog() || Value.IsModifierKey()) return false;
    PushToTalkKey=Value;return true;
}
void UMCGameUserSettings::SetMicrophoneGain(float Value)
{ MicrophoneGain=FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,3.f):1.f; }
void UMCGameUserSettings::SetVoiceActivationThreshold(float Value)
{ VoiceActivationThreshold=FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):.08f; }
void UMCGameUserSettings::SetVoiceVolume(float Value) { VoiceVolume=Volume(Value); }
float UMCGameUserSettings::GetEffectiveVoiceVolume() const { return bVoiceEnabled?Volume(MasterVolume)*Volume(VoiceVolume):0.f; }
bool UMCGameUserSettings::IsVoicePlayerMuted(const FString& Id) const { return !Id.IsEmpty() && MutedVoicePlayerIds.Contains(Id); }
void UMCGameUserSettings::SetVoicePlayerMuted(const FString& Id,bool bMuted)
{
    if(Id.IsEmpty() || Id.Len()>256) return;
    if(bMuted) MutedVoicePlayerIds.AddUnique(Id);else MutedVoicePlayerIds.Remove(Id);
}

FVector2D UMCGameUserSettings::ScaleLookInput(FVector2D Delta,bool bGamepad) const
{
    Delta*=Sensitivity(bGamepad?GamepadSensitivity:MouseSensitivity);
    if(bInvertY) Delta.Y=-Delta.Y;
    return Delta;
}

TArray<FString> UMCGameUserSettings::GetAvailableLanguageCodes()
{
    // Ten editable FMCUILocalizationRow columns. Missing keys use the table's fallback.
    return {TEXT("ru"),TEXT("en"),TEXT("de"),TEXT("fr"),TEXT("es"),TEXT("pt-BR"),
        TEXT("zh-Hans"),TEXT("zh-Hant"),TEXT("ja"),TEXT("ko")};
}

bool UMCGameUserSettings::SetLanguageCode(const FString& Value)
{
    const FString Requested=Value.TrimStartAndEnd().Replace(TEXT("_"),TEXT("-"));
    if(Requested.IsEmpty()) { LanguageCode.Reset();return true; }
    for(const FString& Code:GetAvailableLanguageCodes())
        if(Code.Equals(Requested,ESearchCase::IgnoreCase) && FInternationalization::Get().GetCulture(Code))
        { LanguageCode=Code;return true; }
    return false;
}

void UMCGameUserSettings::NormalizePreferences()
{
    MasterVolume=Volume(MasterVolume);MusicVolume=Volume(MusicVolume);
    AmbienceVolume=Volume(AmbienceVolume);SFXVolume=Volume(SFXVolume);
    MouseSensitivity=Sensitivity(MouseSensitivity);GamepadSensitivity=Sensitivity(GamepadSensitivity);
    if(!FMath::IsFinite(SavedBagColor.R) || !FMath::IsFinite(SavedBagColor.G)
        || !FMath::IsFinite(SavedBagColor.B) || !FMath::IsFinite(SavedBagColor.A)) {
        SavedBagColor=FLinearColor(.24f,.65f,1.f);bHasCustomBagColor=false;
    }
    else {SavedBagColor=SavedBagColor.GetClamped(0.f,1.f);SavedBagColor.A=1.f;}
    SetMicrophoneGain(MicrophoneGain);SetVoiceActivationThreshold(VoiceActivationThreshold);SetVoiceVolume(VoiceVolume);
    SetVoiceInputMode(VoiceInputMode);
    if(!SetPushToTalkKey(PushToTalkKey)) PushToTalkKey=EKeys::V;
    MutedVoicePlayerIds.RemoveAll([](const FString& Id) { return Id.IsEmpty() || Id.Len()>256; });
    if(!SetLanguageCode(LanguageCode)) LanguageCode.Reset();
}

void UMCGameUserSettings::SetToDefaults()
{
    Super::SetToDefaults();
    MasterVolume=MusicVolume=AmbienceVolume=SFXVolume=1.f;
    MouseSensitivity=GamepadSensitivity=1.f;bInvertY=false;LanguageCode.Reset();
    bHasCustomBagColor=false;SavedBagColor=FLinearColor(.24f,.65f,1.f);
    bVoiceEnabled=true;VoiceInputMode=EMCVoiceInputMode::PushToTalk;PushToTalkKey=EKeys::V;
    MicrophoneGain=1.f;VoiceActivationThreshold=.08f;VoiceVolume=1.f;bSelfMuted=false;MutedVoicePlayerIds.Reset();
}

void UMCGameUserSettings::ValidateSettings()
{
    Super::ValidateSettings();NormalizePreferences();
}

void UMCGameUserSettings::LoadSettings(bool bForceReload)
{
    // Mark this before validation can reload settings recursively. Runtime
    // config reloads must never unexpectedly resize an already running game.
    const bool bInitialLoad=!bHasLoadedSettings;
    bHasLoadedSettings=true;
    // The previous release used the engine class. Preserve its resolution,
    // quality, FPS and window preferences on the first launch of this subclass.
    if(bForceReload) LoadConfigIni(true);
    const bool bMigratingLegacy=GConfig
        && !GConfig->DoesSectionExist(*GetClass()->GetPathName(),GGameUserSettingsIni)
        && GConfig->DoesSectionExist(*UGameUserSettings::StaticClass()->GetPathName(),GGameUserSettingsIni);
    if(bMigratingLegacy)
        LoadConfig(UGameUserSettings::StaticClass(),*GGameUserSettingsIni);
    Super::LoadSettings(bForceReload);NormalizePreferences();
    // UE preloads only the configured class's resolution section. On the first
    // upgrade it has not seen the legacy section yet: update GSystemResolution
    // before GameEngine creates its window, respecting -ResX/-ResY/-windowed.
    if(bInitialLoad && bMigratingLegacy && !GIsEditor && !IsRunningCommandlet()
        && !IsRunningDedicatedServer() && !IsTemplate() && GEngine && Get()==this && !GEngine->GameViewport)
        ApplyResolutionSettings(true);
    // Config/CDO/Designer inspection must not change editor culture or audio.
    if(!IsTemplate() && !WorldInitializedHandle.IsValid())
        WorldInitializedHandle=FWorldDelegates::OnPostWorldInitialization.AddWeakLambda(this,
            [this](UWorld* World,const UWorld::InitializationValues) { InitializeForWorld(World); });
    if(!IsTemplate() && !WorldCleanupHandle.IsValid())
        WorldCleanupHandle=FWorldDelegates::OnWorldCleanup.AddWeakLambda(this,
            [this](UWorld* World,bool,bool) { CleanupWorldAudio(World); });
}

void UMCGameUserSettings::InitializeForWorld(UWorld* World)
{
    if(Get()!=this || !World || !World->IsGameWorld() || World->GetNetMode()==NM_DedicatedServer || IsRunningCommandlet()) return;
    if(auto Device=World->GetAudioDevice()) ApplyAudioToDevice(Device.GetAudioDevice());
    if(!LanguageCode.IsEmpty()) FInternationalization::Get().SetCurrentLanguage(LanguageCode);
}

void UMCGameUserSettings::CleanupWorldAudio(UWorld* World)
{
    // A settings singleton outlives PIE. Its mix must not leave a shared editor
    // audio device muted after the last game world on that device has gone away.
    if(!GIsEditor || !GEngine || !World || !World->IsGameWorld() || !PreferencesMix) return;
    FAudioDeviceHandle Device=World->GetAudioDevice();
    if(!Device) return;
    for(const FWorldContext& Context:GEngine->GetWorldContexts())
        if(UWorld* Other=Context.World(); Other && Other!=World && Other->IsGameWorld() && !Other->bIsTearingDown)
            if(const auto OtherDevice=Other->GetAudioDevice(); OtherDevice && OtherDevice.GetDeviceID()==Device.GetDeviceID())
                return;
    Device->ClearSoundMixModifier(PreferencesMix);
}

void UMCGameUserSettings::ApplyGameplaySettings()
{
    if(IsTemplate() || Get()!=this || IsRunningCommandlet()) return;
    NormalizePreferences();
    bool bPlaying=false;
    for(const FWorldContext& Context:GEngine->GetWorldContexts())
        if(UWorld* World=Context.World(); World && World->IsGameWorld() && World->GetNetMode()!=NM_DedicatedServer)
        {
            bPlaying=true;InitializeForWorld(World);
            if(UGameInstance* Instance=World->GetGameInstance())
                if(auto* Chat=Instance->GetSubsystem<UMCVoiceChatSubsystem>()) Chat->ApplyPreferences();
        }
    if(!GIsEditor && !bPlaying && !LanguageCode.IsEmpty()) FInternationalization::Get().SetCurrentLanguage(LanguageCode);
}

void UMCGameUserSettings::ApplyNonResolutionSettings()
{
    Super::ApplyNonResolutionSettings();ApplyGameplaySettings();
}

void UMCGameUserSettings::EnsureAudioRouting()
{
    if(PreferencesMix) return;
    PreferencesMix=NewObject<USoundMix>(this,TEXT("LocalPreferencesMix"),RF_Transient);
    PreferencesMix->Duration=-1.f;PreferencesMix->FadeInTime=0.f;PreferencesMix->FadeOutTime=0.f;
    // Independent roots: the SFX tree must never multiply music or ambience gain.
    MusicClass=NewObject<USoundClass>(this,TEXT("LocalMusicClass"),RF_Transient);
    AmbienceClass=NewObject<USoundClass>(this,TEXT("LocalAmbienceClass"),RF_Transient);
    VoiceClass=GetMutableDefault<UMCVoiceSoundClass>();
    if(const USoundClass* Default=GetDefault<UAudioSettings>()->GetDefaultSoundClass())
    {
        MusicClass->Properties=Default->Properties;
        AmbienceClass->Properties=Default->Properties;
    }
    MusicClass->Properties.bIsMusic=true;
}

void UMCGameUserSettings::ApplyAudioToDevice(FAudioDevice* Device)
{
    if(!Device || !GEngine->UseSound()) return;
    EnsureAudioRouting();
    USoundClass* EffectsClass=GetDefault<UAudioSettings>()->GetDefaultSoundClass();
    Device->RegisterSoundClass(MusicClass);Device->RegisterSoundClass(AmbienceClass);Device->RegisterSoundClass(VoiceClass);
    USoundMix* Mix=PreferencesMix;
    USoundClass* Music=MusicClass;USoundClass* Ambience=AmbienceClass;USoundClass* VoiceRoot=VoiceClass;
    const float EffectsGain=GetEffectiveSFXVolume(),MusicGain=GetEffectiveMusicVolume(),AmbienceGain=GetEffectiveAmbienceVolume(),VoiceGain=GetEffectiveVoiceVolume();
    // Atomic audio-thread check: repeated Apply never stacks mix references.
    FAudioThread::RunCommandOnAudioThread([Device,Mix,EffectsClass,Music,Ambience,VoiceRoot,EffectsGain,MusicGain,AmbienceGain,VoiceGain]()
    {
        const FSoundMixState* Existing=Device->GetSoundMixModifiers().Find(Mix);
        if(!Existing || Existing->ActiveRefCount==0) Device->PushSoundMixModifier(Mix);
        Device->SetSoundMixClassOverride(Mix,EffectsClass,EffectsGain,1.f,.05f,true);
        Device->SetSoundMixClassOverride(Mix,Music,MusicGain,1.f,.05f,false);
        Device->SetSoundMixClassOverride(Mix,Ambience,AmbienceGain,1.f,.05f,false);
        Device->SetSoundMixClassOverride(Mix,VoiceRoot,VoiceGain,1.f,.05f,false);
    });
}

USoundClass* UMCGameUserSettings::GetSceneSoundClass(UWorld* World,bool bMusic)
{
    if(!World || !World->IsGameWorld() || World->GetNetMode()==NM_DedicatedServer || Get()!=this) return nullptr;
    EnsureAudioRouting();
    if(auto Device=World->GetAudioDevice()) ApplyAudioToDevice(Device.GetAudioDevice());
    return bMusic?MusicClass.Get():AmbienceClass.Get();
}

void UMCGameUserSettings::RouteVoiceAudio(UAudioComponent* Component)
{
    if(!IsValid(Component) || Get()!=this || !Component->GetWorld() || !Component->GetWorld()->IsGameWorld()) return;
    EnsureAudioRouting();
    Component->SoundClassOverride=VoiceClass;
    if(auto Device=Component->GetWorld()->GetAudioDevice())
    {
        ApplyAudioToDevice(Device.GetAudioDevice());
        // OSS notifies UVOIPTalker just after Synth::Start. Update that active
        // sound too, before its first decoded packet, without stopping playback.
        USoundClass* Root=VoiceClass;
        Device->SendCommandToActiveSounds(Component->GetAudioComponentID(),
            [Root](FActiveSound& Sound) { Sound.SetSoundClass(Root); });
    }
}

void UMCGameUserSettings::BeginDestroy()
{
    FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitializedHandle);
    FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
    if(PreferencesMix)
        if(FAudioDeviceManager* Manager=FAudioDevice::GetAudioDeviceManager())
            for(FAudioDevice* Device:Manager->GetAudioDevices())
                if(Device) Device->ClearSoundMixModifier(PreferencesMix);
    Super::BeginDestroy();
}
