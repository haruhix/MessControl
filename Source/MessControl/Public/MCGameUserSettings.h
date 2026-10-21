#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "MCGameUserSettings.generated.h"

class USoundClass;
class USoundMix;
class UWorld;
class FAudioDevice;

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
    UPROPERTY(Transient) TObjectPtr<USoundMix> PreferencesMix;
    UPROPERTY(Transient) TObjectPtr<USoundClass> MusicClass;
    UPROPERTY(Transient) TObjectPtr<USoundClass> AmbienceClass;
    FDelegateHandle WorldInitializedHandle;
    FDelegateHandle WorldCleanupHandle;
    bool bHasLoadedSettings=false;
    void NormalizePreferences();
    void InitializeForWorld(UWorld* World);
    void CleanupWorldAudio(UWorld* World);
    void EnsureAudioRouting();
    void ApplyAudioToDevice(FAudioDevice* Device);
};
