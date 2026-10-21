#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Net/VoiceConfig.h"
#include "MCVoiceChatTypes.h"
#include "MCVoiceChatSubsystem.generated.h"

class APlayerState;
class APlayerController;
class IVoiceCapture;
class IOnlineVoice;
class UAudioComponent;
class USoundWaveProcedural;
class UDataTable;
class IInputProcessor;
class FMCVoiceInputProcessor;
namespace Audio { struct FPatchOutput; }

/** Receives actual OSS playback events/envelopes, never a replicated fake talking flag. */
UCLASS()
class MESSCONTROL_API UMCVoiceTalker : public UVOIPTalker
{
    GENERATED_BODY()
public:
    virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
    virtual void OnTalkingEnd() override;
    TWeakObjectPtr<UAudioComponent> PlayingAudio;
    bool bSpeaking=false;
    float BasePlaybackVolume=1.f;
};

/** Built-in OSS voice transport and codec. Local state survives Hub/arena travel. */
UCLASS()
class MESSCONTROL_API UMCVoiceChatSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure,Category="Voice") FText GetStatusText() const;
    UFUNCTION(BlueprintPure,Category="Voice") FName GetStatusKey() const { return StatusKey; }
    UFUNCTION(BlueprintPure,Category="Voice") bool IsVoiceAvailable() const;
    UFUNCTION(BlueprintPure,Category="Voice") bool CanSelectInputDevice() const { return false; }
    UFUNCTION(BlueprintCallable,Category="Voice") TArray<FMCVoiceInputDevice> RefreshInputDevices();
    UFUNCTION(BlueprintPure,Category="Voice") TArray<FMCVoiceInputDevice> GetInputDevices() const { return InputDevices; }
    UFUNCTION(BlueprintCallable,Category="Voice") void OpenSystemInputSettings();
    UFUNCTION(BlueprintPure,Category="Voice") float GetInputLevel() const { return InputLevel; }
    UFUNCTION(BlueprintPure,Category="Voice") bool IsTalking(APlayerState* Player) const;
    UFUNCTION(BlueprintPure,Category="Voice") float GetPlayerVoiceLevel(APlayerState* Player) const;
    UFUNCTION(BlueprintCallable,Category="Voice") void SetPlayerMuted(APlayerState* Player,bool bMuted);
    UFUNCTION(BlueprintPure,Category="Voice") bool IsPlayerMuted(APlayerState* Player) const;
    UFUNCTION(BlueprintCallable,Category="Voice") void SetPushToTalkHeld(bool bHeld);
    UFUNCTION(BlueprintCallable,Category="Voice") void ReleasePushToTalk() { SetPushToTalkHeld(false); }
    /** Rebinding/text capture can suppress PTT without changing saved preference. */
    UFUNCTION(BlueprintCallable,Category="Voice") void SetInputSuppressed(bool bSuppressed);
    UFUNCTION(BlueprintCallable,Category="Voice") void ApplyPreferences();
    /** Exclusive offline test. Never sends the test audio into a room. */
    UFUNCTION(BlueprintCallable,Category="Voice") bool StartMicTest();
    UFUNCTION(BlueprintCallable,Category="Voice") void StopMicTest();
    UFUNCTION(BlueprintPure,Category="Voice") bool IsMicTestActive() const { return TestCapture.IsValid(); }
    /** Root controller calls on travel/EndPlay or application focus loss. */
    UFUNCTION(BlueprintCallable,Category="Voice") void SuspendLocalInput();
    static bool ShouldTransmit(bool bEnabled,bool bSelfMuted,EMCVoiceInputMode Mode,bool bPTTHeld,bool bRoom,bool bFocused,bool bTesting,bool bSuppressed=false);
private:
    friend class FMCVoiceInputProcessor;
    bool CanAcceptPushToTalkInput() const;
    bool TickVoice(float DeltaSeconds);
    bool ResolveVoice();
    bool HasRoom() const;
    bool IsLocalPlayer(const APlayerState* Player) const;
    void RefreshTalkers();
    void UpdateTransmission();
    void PollMicrophone(float DeltaSeconds);
    void RestoreCaptureCVars();
    void ApplyCapturePreferences();
    void SetStatus(FName Key,const TCHAR* Fallback);
    UMCVoiceTalker* FindTalker(APlayerState* Player) const;
    TSharedPtr<IOnlineVoice,ESPMode::ThreadSafe> Voice;
    TSharedPtr<IVoiceCapture> TestCapture;
    TSharedPtr<Audio::FPatchOutput,ESPMode::ThreadSafe> MicrophonePatch;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCVoiceTalker>> Talkers;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> TestPlayback;
    UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> TestWave;
    UPROPERTY(Transient) TArray<FMCVoiceInputDevice> InputDevices;
    UPROPERTY(Transient) TObjectPtr<UDataTable> StatusTable;
    UPROPERTY(Transient) FName StatusKey;
    UPROPERTY(Transient) FText StatusFallback;
    FTSTicker::FDelegateHandle TickHandle;
    FDelegateHandle PreTravelHandle;
    FDelegateHandle PostTravelHandle;
    FDelegateHandle SeamlessTravelHandle;
    TSharedPtr<IInputProcessor> InputProcessor;
    bool bPTTHeld=false;
    bool bTransmitting=false;
    bool bRegisteredLocal=false;
    bool bCapturedCVars=false;
    bool bTravelling=false;
    bool bSeamlessTravelPending=false;
    bool bInputSuppressed=false;
    float PreviousMicGain=1.f,PreviousThreshold=.08f,PreviousNoiseGate=.08f;
    float InputLevel=0.f;
    double NextTalkerRefresh=0;
    double LastInputAudioAt=0;
    double NextRegistrationAttempt=0;
    TArray<uint8> CaptureBuffer;
    TArray<float> PatchBuffer;
};
