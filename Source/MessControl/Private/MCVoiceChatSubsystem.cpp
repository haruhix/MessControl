#include "MCVoiceChatSubsystem.h"

#include "MCGameUserSettings.h"
#include "MCVoiceSoundClass.h"
#include "MCUIDataLibrary.h"
#include "MCSteamSessionSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/VoiceCapture.h"
#include "Interfaces/VoiceInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Slate/SGameLayerManager.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/UObjectGlobals.h"
#include "VoiceModule.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Microsoft/COMPointer.h"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

namespace MCVoicePrivate
{
    FText Copy(UDataTable* Table,FName Key,const FText& Fallback)
    { return UMCUIDataLibrary::ResolveText(Table,Key,Fallback); }
    FString PlayerId(const APlayerState* Player)
    { return Player && Player->GetUniqueId().IsValid()?Player->GetUniqueId()->ToString():FString(); }
    bool Foreground() { return FApp::HasFocus(); }
    float ReadCVar(const TCHAR* Name,float Fallback)
    { const auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name);return Variable?Variable->GetFloat():Fallback; }
    void WriteCVar(const TCHAR* Name,float Value)
    { if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name)) Variable->SetWithCurrentPriority(Value); }
    IOnlineSubsystem* OnlineFor(const UObject* Context)
    {
        const UWorld* World=Context?Context->GetWorld():nullptr;
        if(!World || !World->IsGameWorld() || World->WorldType==EWorldType::PIE || IsRunningCommandlet() || IsRunningDedicatedServer()) return nullptr;
        return Online::GetSubsystem(World);
    }
}

/** Observes Slate before UIOnly routing. Never consumes input or polls OS keys. */
class FMCVoiceInputProcessor final : public IInputProcessor
{
public:
    explicit FMCVoiceInputProcessor(UMCVoiceChatSubsystem* InOwner) : Owner(InOwner) {}
    virtual void Tick(float,FSlateApplication& SlateApp,TSharedRef<ICursor>) override
    {
        const auto Focus=SlateApp.GetKeyboardFocusedWidget();
        if(Owner.IsValid() && Owner->bPTTHeld && (Focus!=LastFocus.Pin() || !Owner->CanAcceptPushToTalkInput()))
            Owner->ReleasePushToTalk();
        LastFocus=Focus;
    }
    virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& Event) override
    { if(!Event.IsRepeat()) Press(Event.GetKey());return false; }
    virtual bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& Event) override
    { Release(Event.GetKey());return false; }
    virtual bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& Event) override
    { Press(Event.GetEffectingButton());return false; }
    virtual bool HandleMouseButtonUpEvent(FSlateApplication&,const FPointerEvent& Event) override
    { Release(Event.GetEffectingButton());return false; }
    virtual const TCHAR* GetDebugName() const override { return TEXT("MessControlVoicePTT"); }
private:
    void Press(FKey Key)
    {
        const auto* Settings=UMCGameUserSettings::Get();
        if(Owner.IsValid() && Settings && Key==Settings->GetPushToTalkKey())
        {
            LastFocus=FSlateApplication::Get().GetKeyboardFocusedWidget();
            Owner->SetPushToTalkHeld(true);
        }
    }
    void Release(FKey Key)
    {
        const auto* Settings=UMCGameUserSettings::Get();
        if(Owner.IsValid() && Settings && Key==Settings->GetPushToTalkKey()) Owner->ReleasePushToTalk();
    }
    TWeakObjectPtr<UMCVoiceChatSubsystem> Owner;
    TWeakPtr<SWidget> LastFocus;
};

void UMCVoiceTalker::OnTalkingBegin(UAudioComponent* AudioComponent)
{
    PlayingAudio=AudioComponent;bSpeaking=true;
    if(AudioComponent)
    {
        BasePlaybackVolume=AudioComponent->VolumeMultiplier;
        if(auto* Preferences=UMCGameUserSettings::Get()) Preferences->RouteVoiceAudio(AudioComponent);
        if(UGameInstance* Instance=GetWorld()?GetWorld()->GetGameInstance():nullptr)
            if(auto* Chat=Instance->GetSubsystem<UMCVoiceChatSubsystem>())
                if(Chat->IsPlayerMuted(Cast<APlayerState>(GetOwner()))) AudioComponent->SetVolumeMultiplier(0.f);
    }
    Super::OnTalkingBegin(AudioComponent);
}

void UMCVoiceTalker::OnTalkingEnd()
{
    bSpeaking=false;PlayingAudio.Reset();Super::OnTalkingEnd();
}

void UMCVoiceChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // Materialize the native object used by AudioSettings' soft object path.
    // The constructor assigns class defaults only; no device/capture is opened.
    GetDefault<UMCVoiceSoundClass>();
    if(FSlateApplication::IsInitialized())
    {
        InputProcessor=MakeShared<FMCVoiceInputProcessor>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
    }
    // Resolve once during setup; the retained table is never loaded from Tick.
    StatusTable=UMCUIDataLibrary::GetDefaultLocalizationTable();
    SetStatus(TEXT("UI.Voice.Status.NotInRoom"),TEXT("Голосовой чат подключается в комнате. Для передачи удерживайте назначенную кнопку."));
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::TickVoice),.05f);
    PreTravelHandle=FCoreUObjectDelegates::PreLoadMap.AddWeakLambda(this,[this](const FString&)
    { bTravelling=true;SuspendLocalInput(); });
    PostTravelHandle=FCoreUObjectDelegates::PostLoadMapWithWorld.AddWeakLambda(this,[this](UWorld* World)
    { if(World && World->GetGameInstance()==GetGameInstance()) { bTravelling=false;NextTalkerRefresh=0; } });
    SeamlessTravelHandle=FWorldDelegates::OnSeamlessTravelStart.AddWeakLambda(this,[this](UWorld* World,const FString&)
    {
        if(World && World->GetGameInstance()==GetGameInstance())
        {bSeamlessTravelPending=true;bTravelling=true;SuspendLocalInput();}
    });
}

void UMCVoiceChatSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    FCoreUObjectDelegates::PreLoadMap.Remove(PreTravelHandle);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostTravelHandle);
    FWorldDelegates::OnSeamlessTravelStart.Remove(SeamlessTravelHandle);
    if(InputProcessor && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
    InputProcessor.Reset();
    StopMicTest();
    if(Voice)
    {
        Voice->StopNetworkedVoice(0);
        if(bRegisteredLocal) Voice->UnregisterLocalTalker(0);
    }
    bTransmitting=false;bRegisteredLocal=false;
    MicrophonePatch.Reset();Voice.Reset();
    for(UMCVoiceTalker* Talker:Talkers) if(IsValid(Talker)) Talker->DestroyComponent();
    Talkers.Reset();RestoreCaptureCVars();Super::Deinitialize();
}

bool UMCVoiceChatSubsystem::HasRoom() const
{
    IOnlineSubsystem* OSS=MCVoicePrivate::OnlineFor(this);
    if(!OSS || OSS->GetSubsystemName()!=FName(TEXT("STEAM"))) return false;
    const auto Sessions=OSS->GetSessionInterface();
    const auto* Session=Sessions?Sessions->GetNamedSession(NAME_GameSession):nullptr;
    return Session && Session->SessionState!=EOnlineSessionState::NoSession && Session->SessionState!=EOnlineSessionState::Destroying;
}

bool UMCVoiceChatSubsystem::IsVoiceAvailable() const
{ return Voice.IsValid() && HasRoom() && bRegisteredLocal; }

void UMCVoiceChatSubsystem::SetStatus(FName Key,const TCHAR* Fallback)
{
    StatusKey=Key;StatusFallback=FText::AsCultureInvariant(Fallback);
}

FText UMCVoiceChatSubsystem::GetStatusText() const
{
    // Lookup uses the current language, including for an unchanged error/test
    // state when the player applies a different UI language.
    return MCVoicePrivate::Copy(StatusTable,StatusKey,StatusFallback);
}

bool UMCVoiceChatSubsystem::ResolveVoice()
{
    if(Voice) return true;
    IOnlineSubsystem* OSS=MCVoicePrivate::OnlineFor(this);
    const auto Identity=OSS?OSS->GetIdentityInterface():nullptr;
    if(!OSS || OSS->GetSubsystemName()!=FName(TEXT("STEAM")) || !Identity || Identity->GetLoginStatus(0)!=ELoginStatus::LoggedIn) return false;
    Voice=OSS->GetVoiceInterface();return Voice.IsValid();
}

bool UMCVoiceChatSubsystem::ShouldTransmit(bool bEnabled,bool bMuted,EMCVoiceInputMode Mode,bool bHeld,bool bRoom,bool bFocused,bool bTesting,bool bSuppressed)
{ return bEnabled && !bMuted && bRoom && bFocused && !bTesting && !bSuppressed && (Mode==EMCVoiceInputMode::OpenMic || bHeld); }

void UMCVoiceChatSubsystem::SetPushToTalkHeld(bool bHeld)
{
    if(IsTemplate()) return;
    bPTTHeld=bHeld && CanAcceptPushToTalkInput();UpdateTransmission();
}

void UMCVoiceChatSubsystem::SetInputSuppressed(bool bSuppressed)
{
    if(IsTemplate()) return;
    bInputSuppressed=bSuppressed;
    if(bSuppressed) bPTTHeld=false;
    UpdateTransmission();
}

bool UMCVoiceChatSubsystem::CanAcceptPushToTalkInput() const
{
    if(bInputSuppressed || bTravelling || !MCVoicePrivate::Foreground() || !GetGameInstance() || !FSlateApplication::IsInitialized()) return false;
    const auto* Viewport=GetGameInstance()->GetGameViewportClient();
    const auto Focus=FSlateApplication::Get().GetKeyboardFocusedWidget();
    if(!Viewport || !Focus) return false;
    FWidgetPath Path;
    if(!FSlateApplication::Get().GeneratePathToWidgetUnchecked(Focus.ToSharedRef(),Path)) return false;
    // A shared editor window is insufficient to identify the active PIE. Require
    // this GameInstance's actual viewport or UI layer to be on the focus path.
    const auto ViewportWidget=Viewport->GetGameViewportWidget();
    const auto Layers=Viewport->GetGameLayerManager();
    bool bOwnsFocus=(ViewportWidget && Path.ContainsWidget(ViewportWidget.Get()))
        || (Layers && Path.ContainsWidget(&Layers->AsWidget().Get()));
    if(!bOwnsFocus && !GIsEditor)
    {
        const auto GameWindow=Viewport->GetWindow();
        const auto FocusWindow=FSlateApplication::Get().FindWidgetWindow(Focus.ToSharedRef());
        bOwnsFocus=GameWindow && FocusWindow && (GameWindow==FocusWindow || FocusWindow->IsDescendantOf(GameWindow));
    }
    if(!bOwnsFocus) return false;
    for(const FArrangedWidget& Arranged:Path.Widgets.GetInternalArray())
    {
        const FString Type=Arranged.Widget->GetTypeAsString();
        if(Type.Contains(TEXT("EditableText")) || Type.Contains(TEXT("InputKeySelector"))) return false;
    }
    return true;
}

void UMCVoiceChatSubsystem::SuspendLocalInput()
{
    bPTTHeld=false;StopMicTest();
    if(Voice && bTransmitting) Voice->StopNetworkedVoice(0);
    bTransmitting=false;InputLevel=0.f;MicrophonePatch.Reset();
}

void UMCVoiceChatSubsystem::ApplyCapturePreferences()
{
    const auto* Settings=UMCGameUserSettings::Get();if(!Settings) return;
    if(!bCapturedCVars)
    {
        PreviousMicGain=MCVoicePrivate::ReadCVar(TEXT("voice.MicInputGain"),1.f);
        PreviousThreshold=MCVoicePrivate::ReadCVar(TEXT("voice.SilenceDetectionThreshold"),.08f);
        PreviousNoiseGate=MCVoicePrivate::ReadCVar(TEXT("voice.MicNoiseGateThreshold"),.08f);
        bCapturedCVars=true;
    }
    const float Threshold=Settings->GetVoiceInputMode()==EMCVoiceInputMode::OpenMic?Settings->GetVoiceActivationThreshold():.001f;
    MCVoicePrivate::WriteCVar(TEXT("voice.MicInputGain"),Settings->GetMicrophoneGain());
    MCVoicePrivate::WriteCVar(TEXT("voice.SilenceDetectionThreshold"),Threshold);
    MCVoicePrivate::WriteCVar(TEXT("voice.MicNoiseGateThreshold"),Threshold);
}

void UMCVoiceChatSubsystem::RestoreCaptureCVars()
{
    if(!bCapturedCVars) return;
    MCVoicePrivate::WriteCVar(TEXT("voice.MicInputGain"),PreviousMicGain);
    MCVoicePrivate::WriteCVar(TEXT("voice.SilenceDetectionThreshold"),PreviousThreshold);
    MCVoicePrivate::WriteCVar(TEXT("voice.MicNoiseGateThreshold"),PreviousNoiseGate);
    bCapturedCVars=false;
}

void UMCVoiceChatSubsystem::UpdateTransmission()
{
    const auto* Settings=UMCGameUserSettings::Get();
    const bool bWanted=Settings && ShouldTransmit(Settings->GetVoiceEnabled(),Settings->GetSelfMuted(),Settings->GetVoiceInputMode(),
        bPTTHeld,HasRoom() && !bTravelling,MCVoicePrivate::Foreground(),IsMicTestActive(),bInputSuppressed);
    if(!Voice || bWanted==bTransmitting) return;
    if(bWanted && bRegisteredLocal)
    {
        ApplyCapturePreferences();Voice->StartNetworkedVoice(0);MicrophonePatch=Voice->GetMicrophoneOutput();bTransmitting=true;
    }
    else
    {
        Voice->StopNetworkedVoice(0);bTransmitting=false;MicrophonePatch.Reset();InputLevel=0.f;
        Voice->ClearVoicePackets();
    }
}

void UMCVoiceChatSubsystem::ApplyPreferences()
{
    if(IsTemplate() || IsRunningCommandlet()) return;
    if(bRegisteredLocal || TestCapture) ApplyCapturePreferences();
    UpdateTransmission();RefreshTalkers();
}

bool UMCVoiceChatSubsystem::TickVoice(float DeltaSeconds)
{
    if(!GetGameInstance() || IsRunningCommandlet() || IsRunningDedicatedServer()) return true;
    // Seamless travel retains the controller and skips LoadMap delegates.
    // Query the handler until final destination (or cancellation), then require
    // a new PTT press; no sticky held-key state crosses between worlds.
    if(bSeamlessTravelPending && GetWorld() && !GetWorld()->IsInSeamlessTravel() && !GetWorld()->bIsTearingDown)
    {bSeamlessTravelPending=false;bTravelling=false;bPTTHeld=false;NextTalkerRefresh=0;}
    if(!MCVoicePrivate::Foreground()) bPTTHeld=false;
    const bool bRoom=HasRoom();
    if(TestCapture && (bRoom || bTravelling)) StopMicTest();
    const auto* Preferences=UMCGameUserSettings::Get();
    const double Now=FPlatformTime::Seconds();
    if(bRoom && Preferences && Preferences->GetVoiceEnabled() && ResolveVoice() && !bRegisteredLocal && Now>=NextRegistrationAttempt)
    {
        NextRegistrationAttempt=Now+2.0;
        ApplyCapturePreferences();
        bRegisteredLocal=Voice->RegisterLocalTalker(0);
        // UE RegisterLocalTalker sets network-enabled before registration. Close
        // that gate immediately; this subsystem owns the selected PTT policy.
        Voice->StopNetworkedVoice(0);bTransmitting=false;
        if(!bRegisteredLocal) SetStatus(TEXT("UI.Voice.Status.NoMicrophone"),TEXT("Микрофон не удалось подключить. Проверьте разрешение доступа и устройство Windows."));
    }
    if((!bRoom || (Preferences && !Preferences->GetVoiceEnabled())) && bRegisteredLocal && Voice)
    {
        Voice->StopNetworkedVoice(0);Voice->ClearVoicePackets();Voice->UnregisterLocalTalker(0);
        bRegisteredLocal=false;bTransmitting=false;MicrophonePatch.Reset();InputLevel=0;
        RestoreCaptureCVars();
    }
    UpdateTransmission();PollMicrophone(DeltaSeconds);
    if(Now>=NextTalkerRefresh)
    {
        NextTalkerRefresh=Now+.25;RefreshTalkers();
        if(!TestCapture)
        {
            const auto* Settings=UMCGameUserSettings::Get();
            if(Settings && !Settings->GetVoiceEnabled()) SetStatus(TEXT("UI.Voice.Status.Disabled"),TEXT("Голосовой чат выключен."));
            else if(Settings && Settings->GetSelfMuted()) SetStatus(TEXT("UI.Voice.Status.SelfMuted"),TEXT("Ваш микрофон выключен."));
            else if(bRegisteredLocal)
            {
                if(bTransmitting) SetStatus(TEXT("UI.Voice.Status.Recording"),TEXT("Микрофон активен."));
                else SetStatus(TEXT("UI.Voice.Status.PTTReady"),TEXT("Для разговора удерживайте назначенную кнопку."));
            }
            else if(!bRoom) SetStatus(TEXT("UI.Voice.Status.NotInRoom"),TEXT("Голосовой чат подключается в комнате. Для передачи удерживайте назначенную кнопку."));
            else if(!Voice) SetStatus(TEXT("UI.Voice.Status.Unavailable"),TEXT("Голос Steam недоступен в этой сессии. Проверьте Steam и настройки микрофона."));
        }
    }
    return true;
}

bool UMCVoiceChatSubsystem::IsLocalPlayer(const APlayerState* Player) const
{
    if(!Player || !GetGameInstance()) return false;
    for(const ULocalPlayer* Local:GetGameInstance()->GetLocalPlayers())
        if(Local && Local->PlayerController && Local->PlayerController->PlayerState==Player) return true;
    return false;
}

UMCVoiceTalker* UMCVoiceChatSubsystem::FindTalker(APlayerState* Player) const
{
    for(UMCVoiceTalker* Talker:Talkers) if(IsValid(Talker) && Talker->GetOwner()==Player) return Talker;
    return nullptr;
}

void UMCVoiceChatSubsystem::RefreshTalkers()
{
    Talkers.RemoveAll([](const TObjectPtr<UMCVoiceTalker>& Talker) { return !IsValid(Talker.Get()) || !IsValid(Talker->GetOwner()); });
    const auto* Settings=UMCGameUserSettings::Get();
    AGameStateBase* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    if(!State || !Voice || !HasRoom()) return;
    for(APlayerState* Player:State->PlayerArray)
    {
        if(!IsValid(Player) || IsLocalPlayer(Player) || !Player->GetUniqueId().IsValid()) continue;
        auto* Talker=FindTalker(Player);
        if(!Talker && !UVOIPStatics::GetVOIPTalkerForPlayer(Player->GetUniqueId()))
        {
            Talker=NewObject<UMCVoiceTalker>(Player);
            Player->AddInstanceComponent(Talker);Talker->RegisterComponent();Talker->RegisterWithPlayerState(Player);Talkers.Add(Talker);
            Voice->RegisterRemoteTalker(*Player->GetUniqueId());
        }
        const bool bMuted=Settings && (!Settings->GetVoiceEnabled() || Settings->IsVoicePlayerMuted(MCVoicePrivate::PlayerId(Player)));
        if(Voice->IsMuted(0,*Player->GetUniqueId())!=bMuted)
        {
            if(bMuted) Voice->MuteRemoteTalker(0,*Player->GetUniqueId(),false);
            else Voice->UnmuteRemoteTalker(0,*Player->GetUniqueId(),false);
        }
        if(Talker && Talker->PlayingAudio.IsValid()) Talker->PlayingAudio->SetVolumeMultiplier(bMuted?0.f:Talker->BasePlaybackVolume);
    }
}

bool UMCVoiceChatSubsystem::IsPlayerMuted(APlayerState* Player) const
{
    const auto* Settings=UMCGameUserSettings::Get();
    return Settings && (!Settings->GetVoiceEnabled() || (IsLocalPlayer(Player)?Settings->GetSelfMuted():Settings->IsVoicePlayerMuted(MCVoicePrivate::PlayerId(Player))));
}

void UMCVoiceChatSubsystem::SetPlayerMuted(APlayerState* Player,bool bMuted)
{
    if(IsTemplate()) return;
    auto* Settings=UMCGameUserSettings::Get();if(!Settings || !Player) return;
    if(IsLocalPlayer(Player)) Settings->SetSelfMuted(bMuted);else Settings->SetVoicePlayerMuted(MCVoicePrivate::PlayerId(Player),bMuted);
    Settings->SaveSettings();ApplyPreferences();
}

float UMCVoiceChatSubsystem::GetPlayerVoiceLevel(APlayerState* Player) const
{
    if(!Player || IsPlayerMuted(Player)) return 0.f;
    if(IsLocalPlayer(Player)) return bTransmitting?InputLevel:0.f;
    if(Voice && Player->GetUniqueId().IsValid())
    {
        const float Amplitude=Voice->GetAmplitudeOfRemoteTalker(*Player->GetUniqueId());
        if(Amplitude>=0.f) return FMath::Clamp(Amplitude,0.f,1.f);
    }
    if(auto* Talker=FindTalker(Player)) return FMath::Clamp(Talker->GetVoiceLevel(),0.f,1.f);
    return 0.f;
}

bool UMCVoiceChatSubsystem::IsTalking(APlayerState* Player) const
{
    if(!Player || IsPlayerMuted(Player)) return false;
    if(IsLocalPlayer(Player)) return bTransmitting && Voice && Voice->IsLocalPlayerTalking(0);
    return Voice && Player->GetUniqueId().IsValid() && Voice->IsRemotePlayerTalking(*Player->GetUniqueId());
}

void UMCVoiceChatSubsystem::PollMicrophone(float DeltaSeconds)
{
    const double Now=FPlatformTime::Seconds();
    if(TestCapture)
    {
        uint32 Available=0;const auto State=TestCapture->GetCaptureState(Available);
        InputLevel=FMath::Clamp(TestCapture->GetCurrentAmplitude(),0.f,1.f);
        if(State==EVoiceCaptureState::Ok && Available>0)
        {
            // Consume a full capture buffer after a frame hitch. Supplying a
            // truncated buffer would leave GetVoiceData stuck on BufferTooSmall.
            if(Available>2*1024*1024)
            { StopMicTest();SetStatus(TEXT("UI.Voice.Status.CaptureOverflow"),TEXT("Буфер микрофона переполнен. Запустите проверку заново."));return; }
            CaptureBuffer.SetNumUninitialized(static_cast<int32>(Available));uint32 Written=0;
            if(TestCapture->GetVoiceData(CaptureBuffer.GetData(),Available,Written)==EVoiceCaptureState::Ok && Written && TestWave)
            {
                if(TestWave->GetAvailableAudioByteCount()>UVOIPStatics::GetVoiceSampleRate()*UVOIPStatics::GetVoiceNumChannels()) TestWave->ResetAudio();
                TestWave->QueueAudio(CaptureBuffer.GetData(),static_cast<int32>(Written));
            }
        }
        else if(State==EVoiceCaptureState::Error || State==EVoiceCaptureState::UnInitialized)
        { StopMicTest();SetStatus(TEXT("UI.Voice.Status.CaptureError"),TEXT("Ошибка микрофона. Проверьте разрешение доступа в Windows.")); }
        return;
    }
    if(bTransmitting && MicrophonePatch)
    {
        PatchBuffer.SetNumUninitialized(4096);
        const int32 Samples=MicrophonePatch->PopAudio(PatchBuffer.GetData(),PatchBuffer.Num(),true);
        if(Samples>0)
        {
            double Sum=0;for(int32 Index=0;Index<Samples;++Index) Sum+=FMath::Square(PatchBuffer[Index]);
            InputLevel=FMath::Clamp(float(FMath::Sqrt(Sum/Samples)),0.f,1.f);LastInputAudioAt=Now;
        }
        else if(Samples<0) MicrophonePatch.Reset();
    }
    if(!bTransmitting || Now-LastInputAudioAt>.15) InputLevel=FMath::FInterpTo(InputLevel,0.f,DeltaSeconds,18.f);
}

bool UMCVoiceChatSubsystem::StartMicTest()
{
    if(TestCapture) return true;
    if(IsTemplate() || IsRunningCommandlet() || IsRunningDedicatedServer() || !GetWorld() || !GetWorld()->IsGameWorld()) return false;
    const auto* Steam=GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>();
    IOnlineSubsystem* OSS=MCVoicePrivate::OnlineFor(this);
    const auto Sessions=OSS?OSS->GetSessionInterface():nullptr;
    if((Sessions && Sessions->GetNumSessions()>0) || (Steam && Steam->IsBusy()) || bTravelling)
    { SetStatus(TEXT("UI.Voice.Status.TestOfflineOnly"),TEXT("Проверка микрофона доступна до входа в комнату, чтобы тест не слышали другие игроки."));return false; }
    bPTTHeld=false;InputLevel=0;MicrophonePatch.Reset();
    // Unregister alone retains the backend capture object. With no room/users,
    // reset the public voice engine and immediately reinitialize it, releasing
    // its device before opening exactly one offline capture of the same type.
    if(ResolveVoice())
    {
        Voice->StopNetworkedVoice(0);Voice->UnregisterLocalTalker(0);
        Voice->Shutdown();if(!Voice->Init()) Voice.Reset();
    }
    bTransmitting=false;bRegisteredLocal=false;
    ApplyCapturePreferences();
    TestCapture=FVoiceModule::Get().CreateVoiceCapture(TEXT(""),UVOIPStatics::GetVoiceSampleRate(),UVOIPStatics::GetVoiceNumChannels());
    if(!TestCapture || !TestCapture->Start())
    { StopMicTest();SetStatus(TEXT("UI.Voice.Status.TestNoMic"),TEXT("Не удалось открыть микрофон. Проверьте устройство и разрешение Windows."));return false; }
    TestWave=NewObject<USoundWaveProcedural>(this);
    TestWave->SetSampleRate(UVOIPStatics::GetVoiceSampleRate());TestWave->NumChannels=UVOIPStatics::GetVoiceNumChannels();
    TestWave->Duration=INDEFINITELY_LOOPING_DURATION;TestWave->VirtualizationMode=EVirtualizationMode::PlayWhenSilent;
    TestPlayback=UGameplayStatics::CreateSound2D(this,TestWave,1.f,1.f,0,nullptr,false,false);
    if(TestPlayback)
    {
        if(auto* Settings=UMCGameUserSettings::Get()) Settings->RouteVoiceAudio(TestPlayback);
        TestPlayback->Play();
    }
    SetStatus(TEXT("UI.Voice.Status.TestLocal"),TEXT("Проверка: слышите только вы. Используйте наушники, чтобы избежать обратной связи."));return true;
}

void UMCVoiceChatSubsystem::StopMicTest()
{
    if(TestPlayback) { TestPlayback->Stop();TestPlayback->DestroyComponent();TestPlayback=nullptr; }
    TestWave=nullptr;
    if(TestCapture) { TestCapture->Stop();TestCapture->Shutdown();TestCapture.Reset(); }
    CaptureBuffer.Reset();InputLevel=0.f;
    if(!bRegisteredLocal) RestoreCaptureCVars();
}

void UMCVoiceChatSubsystem::OpenSystemInputSettings()
{
    if(IsTemplate()) return;
#if PLATFORM_WINDOWS
    FPlatformProcess::LaunchURL(TEXT("ms-settings:sound"),nullptr,nullptr);
#endif
}

TArray<FMCVoiceInputDevice> UMCVoiceChatSubsystem::RefreshInputDevices()
{
    if(IsTemplate()) return {};
    InputDevices.Reset();
#if PLATFORM_WINDOWS
    // Enumerating endpoints never opens/starts a second capture stream.
    const HRESULT InitResult=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(SUCCEEDED(InitResult) || InitResult==RPC_E_CHANGED_MODE)
    {
        TComPtr<IMMDeviceEnumerator> Enumerator;
        if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&Enumerator))))
        {
            TComPtr<IMMDevice> DefaultDevice;FString DefaultId;
            if(SUCCEEDED(Enumerator->GetDefaultAudioEndpoint(eCapture,eConsole,&DefaultDevice)))
            {
                LPWSTR Id=nullptr;if(SUCCEEDED(DefaultDevice->GetId(&Id))) { DefaultId=Id;CoTaskMemFree(Id); }
            }
            TComPtr<IMMDeviceCollection> Devices;
            if(SUCCEEDED(Enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&Devices)))
            {
                UINT Count=0;Devices->GetCount(&Count);
                for(UINT Index=0;Index<Count;++Index)
                {
                    TComPtr<IMMDevice> Device;if(FAILED(Devices->Item(Index,&Device))) continue;
                    LPWSTR Id=nullptr;if(FAILED(Device->GetId(&Id))) continue;
                    FMCVoiceInputDevice Info;Info.Id=Id;Info.bDefault=Info.Id==DefaultId;CoTaskMemFree(Id);
                    TComPtr<IPropertyStore> Properties;
                    if(SUCCEEDED(Device->OpenPropertyStore(STGM_READ,&Properties)))
                    {
                        PROPVARIANT Name;PropVariantInit(&Name);
                        if(SUCCEEDED(Properties->GetValue(PKEY_Device_FriendlyName,&Name)) && Name.vt==VT_LPWSTR) Info.Name=Name.pwszVal;
                        PropVariantClear(&Name);
                    }
                    if(Info.Name.IsEmpty()) Info.Name=Info.Id;
                    InputDevices.Add(MoveTemp(Info));
                }
            }
        }
    }
    if(SUCCEEDED(InitResult)) CoUninitialize();
#endif
    return InputDevices;
}
