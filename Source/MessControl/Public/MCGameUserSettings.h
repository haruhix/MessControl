#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "MCVoiceChatTypes.h"
#include "MCGameUserSettings.generated.h"

class USoundClass;
class USoundMix;
class UWorld;
class FAudioDevice;
class UAudioComponent;

/** Local preferences only. Graphics use the engine's normal scalability settings. */
UCLASS(config=GameUserSettings, BlueprintType)
class MESSCONTROL_API UMCGameUserSettings : public UGameUserSettings
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Settings") static UMCGameUserSettings* Get();
    UFUNCTION(BlueprintPure, Category="Settings|Audio") float GetMasterVolume() const { return MasterVolume; }
    UFUNCTION(BlueprintPure, Category="Settings|Audio") float GetMusicVolume() const { return MusicVolume; }
    UFUNCTION(BlueprintPure, Category="Settings|Audio") float GetAmbienceVolume() const { return AmbienceVolume; }
    UFUNCTION(BlueprintPure, Category="Settings|Audio") float GetSFXVolume() const { return SFXVolume; }
    UFUNCTION(BlueprintCallable, Category="Settings|Audio") void SetMasterVolume(float Value);
    UFUNCTION(BlueprintCallable, Category="Settings|Audio") void SetMusicVolume(float Value);
    UFUNCTION(BlueprintCallable, Category="Settings|Audio") void SetAmbienceVolume(float Value);
    UFUNCTION(BlueprintCallable, Category="Settings|Audio") void SetSFXVolume(float Value);
    /** Absolute gains, never multiplied by previously applied gains. */
    UFUNCTION(BlueprintPure, Category="Settings|Audio") float GetEffectiveSFXVolume() const;
    UFUNCTION(BlueprintPure, Category="Settings|Audio") float GetEffectiveMusicVolume() const;
    UFUNCTION(BlueprintPure, Category="Settings|Audio") float GetEffectiveAmbienceVolume() const;
    /** Empty keeps the existing language until the player explicitly chooses one. */
    UFUNCTION(BlueprintPure, Category="Settings|Language") FString GetLanguageCode() const { return LanguageCode; }
    UFUNCTION(BlueprintCallable, Category="Settings|Language") bool SetLanguageCode(const FString& Value);
    UFUNCTION(BlueprintPure, Category="Settings|Language") static TArray<FString> GetAvailableLanguageCodes();
    UFUNCTION(BlueprintPure, Category="Settings|Controls") float GetMouseSensitivity() const { return MouseSensitivity; }
    UFUNCTION(BlueprintPure, Category="Settings|Controls") float GetGamepadSensitivity() const { return GamepadSensitivity; }
    UFUNCTION(BlueprintPure, Category="Settings|Controls") bool GetInvertY() const { return bInvertY; }
    UFUNCTION(BlueprintCallable, Category="Settings|Controls") void SetMouseSensitivity(float Value);
    UFUNCTION(BlueprintCallable, Category="Settings|Controls") void SetGamepadSensitivity(float Value);
    UFUNCTION(BlueprintCallable, Category="Settings|Controls") void SetInvertY(bool Value) { bInvertY=Value; }
    UFUNCTION(BlueprintPure,Category="Settings|Voice") bool GetVoiceEnabled() const { return bVoiceEnabled; }
    UFUNCTION(BlueprintCallable,Category="Settings|Voice") void SetVoiceEnabled(bool Value) { bVoiceEnabled=Value; }
    UFUNCTION(BlueprintPure,Category="Settings|Voice") EMCVoiceInputMode GetVoiceInputMode() const { return VoiceInputMode; }
    UFUNCTION(BlueprintCallable,Category="Settings|Voice") void SetVoiceInputMode(EMCVoiceInputMode Value);
    UFUNCTION(BlueprintPure,Category="Settings|Voice") FKey GetPushToTalkKey() const { return PushToTalkKey; }
    UFUNCTION(BlueprintCallable,Category="Settings|Voice") bool SetPushToTalkKey(FKey Value);
    UFUNCTION(BlueprintPure,Category="Settings|Voice") float GetMicrophoneGain() const { return MicrophoneGain; }
    UFUNCTION(BlueprintCallable,Category="Settings|Voice") void SetMicrophoneGain(float Value);
    UFUNCTION(BlueprintPure,Category="Settings|Voice") float GetVoiceActivationThreshold() const { return VoiceActivationThreshold; }
    UFUNCTION(BlueprintCallable,Category="Settings|Voice") void SetVoiceActivationThreshold(float Value);
    UFUNCTION(BlueprintPure,Category="Settings|Voice") float GetVoiceVolume() const { return VoiceVolume; }
    UFUNCTION(BlueprintCallable,Category="Settings|Voice") void SetVoiceVolume(float Value);
    UFUNCTION(BlueprintPure,Category="Settings|Voice") bool GetSelfMuted() const { return bSelfMuted; }
    UFUNCTION(BlueprintCallable,Category="Settings|Voice") void SetSelfMuted(bool Value) { bSelfMuted=Value; }
    UFUNCTION(BlueprintPure,Category="Settings|Voice") TArray<FString> GetMutedVoicePlayerIds() const { return MutedVoicePlayerIds; }
    UFUNCTION(BlueprintPure,Category="Settings|Voice") bool IsVoicePlayerMuted(const FString& Id) const;
    UFUNCTION(BlueprintCallable,Category="Settings|Voice") void SetVoicePlayerMuted(const FString& Id,bool bMuted);
    UFUNCTION(BlueprintPure,Category="Settings|Voice") float GetEffectiveVoiceVolume() const;
    /** Local camera deltas only; movement and replicated gameplay input are untouched. */
    FVector2D ScaleLookInput(FVector2D Delta, bool bGamepad) const;
    /** Custom preferences without changing resolution or saving. ApplySettings also saves. */
    UFUNCTION(BlueprintCallable, Category="Settings") void ApplyGameplaySettings();
    virtual void ApplyNonResolutionSettings() override;
    virtual void LoadSettings(bool bForceReload=false) override;
    virtual void ValidateSettings() override;
    virtual void SetToDefaults() override;
    /** Per-voice category routing; no authored sound or SoundClass is modified. */
    USoundClass* GetSceneSoundClass(UWorld* World, bool bMusic);
    /** Routes current and future playback to a voice root independent of SFX. */
    void RouteVoiceAudio(UAudioComponent* Component);
protected:
    virtual void BeginDestroy() override;
private:
    UPROPERTY(Config) float MasterVolume=1.f;
    UPROPERTY(Config) float MusicVolume=1.f;
    UPROPERTY(Config) float AmbienceVolume=1.f;
    UPROPERTY(Config) float SFXVolume=1.f;
    UPROPERTY(Config) FString LanguageCode;
    UPROPERTY(Config) float MouseSensitivity=1.f;
    UPROPERTY(Config) float GamepadSensitivity=1.f;
    UPROPERTY(Config) bool bInvertY=false;
    UPROPERTY(Config) bool bVoiceEnabled=true;
    UPROPERTY(Config) EMCVoiceInputMode VoiceInputMode=EMCVoiceInputMode::PushToTalk;
    UPROPERTY(Config) FKey PushToTalkKey=EKeys::V;
    UPROPERTY(Config) float MicrophoneGain=1.f;
    UPROPERTY(Config) float VoiceActivationThreshold=.08f;
    UPROPERTY(Config) float VoiceVolume=1.f;
    UPROPERTY(Config) bool bSelfMuted=false;
    UPROPERTY(Config) TArray<FString> MutedVoicePlayerIds;
    UPROPERTY(Transient) TObjectPtr<USoundMix> PreferencesMix;
    UPROPERTY(Transient) TObjectPtr<USoundClass> MusicClass;
    UPROPERTY(Transient) TObjectPtr<USoundClass> AmbienceClass;
    UPROPERTY(Transient) TObjectPtr<USoundClass> VoiceClass;
    FDelegateHandle WorldInitializedHandle;
    FDelegateHandle WorldCleanupHandle;
    bool bHasLoadedSettings=false;
    void NormalizePreferences();
    void InitializeForWorld(UWorld* World);
    void CleanupWorldAudio(UWorld* World);
    void EnsureAudioRouting();
    void ApplyAudioToDevice(FAudioDevice* Device);
};
