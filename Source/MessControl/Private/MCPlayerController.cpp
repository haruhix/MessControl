#include "MCPlayerController.h"
#include "MCPrototypeWidget.h"
#include "MCGameUserSettings.h"
#include "MCDevPanelWidget.h"
#include "MCEmoteWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGameDirector.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/GameInstance.h"
#include "MCSteamSessionSubsystem.h"
#include "MCScoreboardWidget.h"
#include "MCPlayerState.h"
#include "MCToothStatusComponent.h"
#include "EngineUtils.h"
#include "MCRewardChest.h"
#include "MCPerkChoiceWidget.h"
#include "TimerManager.h"
#include "MCBossCharacter.h"
#include "MCBossIntro.h"
#include "MCBossHealthWidget.h"
#include "MCMainMenuWidget.h"
#include "MCTutorialWidget.h"
#include "MCTutorialDirector.h"
#include "MCGameplayHUD.h"
#include "MCSceneAudioComponent.h"
#include "MCCataclysmEvent.h"
#include "MCIceEvent.h"
#include "MCFogBrawlEvent.h"
#include "MCNutRainEvent.h"
#include "MCNutBoss.h"
#include "MCUIDataLibrary.h"
#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "MCGameplayMenuWidget.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MCColdCola.h"
#include "InputKeyEventArgs.h"
#include "MCHubGameState.h"
#include "MCHubWidget.h"
#include "MCLoadingWidget.h"
#include "MCVoiceChatSubsystem.h"
#include "Framework/Application/SlateApplication.h"

void AMCPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        RefreshLoadingPresentation();
        if(!BackgroundSoundPalette) BackgroundSoundPalette=LoadObject<UMCSoundPalette>(nullptr,TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds"));
        SceneAudio=NewObject<UMCSceneAudioComponent>(this,TEXT("BackgroundSceneAudio"));
        AddInstanceComponent(SceneAudio); SceneAudio->RegisterComponent();
        RefreshBackgroundAudio();
        if (auto* Steam=GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>()) Steam->RefreshAvailability();
        if(!IsHubWorld()) {
            PrototypeWidget = CreateWidget<UMCPrototypeWidget>(this,UMCPrototypeWidget::StaticClass());
            if(PrototypeWidget) PrototypeWidget->AddToViewport();
            BossHealthWidget=CreateWidget<UMCBossHealthWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("BossHealth"),UMCBossHealthWidget::StaticClass()));
            if (BossHealthWidget) BossHealthWidget->AddToViewport(40);
            GetWorldTimerManager().SetTimer(BossHUDTimer,this,&AMCPlayerController::RefreshBossHUD,.1f,true);
        }
        RefreshHubPresentation(0.f); UpdateInputMode();
    }
}
void AMCPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::Enter,IE_Pressed,this,&AMCPlayerController::HandleHubStartInput);
    InputComponent->BindKey(EKeys::G,IE_Pressed,this,&AMCPlayerController::ActivateRogueBooster);
    InputComponent->BindKey(EKeys::T,IE_Pressed,this,&AMCPlayerController::ToggleEmotes);
    InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&AMCPlayerController::TogglePauseMenu);
    InputComponent->BindKey(EKeys::Tab,IE_Pressed,this,&AMCPlayerController::ShowScoreboard);
    InputComponent->BindKey(EKeys::Tab,IE_Released,this,&AMCPlayerController::HideScoreboard);
    InputComponent->BindKey(EKeys::Right,IE_Pressed,this,&AMCPlayerController::NextSpectator);
    InputComponent->BindKey(EKeys::Left,IE_Pressed,this,&AMCPlayerController::PreviousSpectator);
    InputComponent->BindKey(EKeys::Gamepad_Special_Right,IE_Pressed,this,&AMCPlayerController::TogglePauseMenu);
    InputComponent->BindKey(EKeys::Gamepad_FaceButton_Right,IE_Pressed,this,&AMCPlayerController::ActivateRogueBooster);
    InputComponent->BindKey(EKeys::Gamepad_Special_Left,IE_Pressed,this,&AMCPlayerController::ShowScoreboard);
    InputComponent->BindKey(EKeys::Gamepad_Special_Left,IE_Released,this,&AMCPlayerController::HideScoreboard);
#if !UE_BUILD_SHIPPING
    InputComponent->BindKey(EKeys::F3,IE_Pressed,this,&AMCPlayerController::ToggleDevPanel);
#endif
}
bool AMCPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    // Ignore stick drift and release events when choosing the visible control hints.
    if (Params.Event==IE_Pressed || (Params.Event==IE_Axis && FMath::Abs(Params.AmountDepressed)>.25f))
        bUsingGamepad=Params.Key.IsGamepadKey();
    // GameAndUI retains the held TAB key and its release. Unhandled clicks or
    // sticks over empty parts of the table must not activate weapons/gameplay.
    // Releases still reach PlayerInput so keys released in the panel cannot stick.
    if(ScoreboardWidget && ScoreboardWidget->IsVisible() && Params.Key!=EKeys::Tab && Params.Key!=EKeys::Gamepad_Special_Left
        && Params.Event!=IE_Released) return true;
    return Super::InputKey(Params);
}
void AMCPlayerController::ShowScoreboard()
{
    if (!IsLocalController() || IsRewardInteractionActive() || bBossIntroPlaying || bLoadingInputBlocked
        || bLobbyUIOpen || bPauseMenuOpen || bConnectionMenuOpen || bTutorialMenuInput || bHubMenuInput
        || (DevPanel && DevPanel->IsVisible()) || (EmoteWidget && EmoteWidget->IsVisible())
        || (PrototypeWidget && PrototypeWidget->IsPanelOpen())) return;
    if (!ScoreboardWidget)
    {
        ScoreboardWidget=CreateWidget<UMCScoreboardWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("Scoreboard"),UMCScoreboardWidget::StaticClass()));
        if (!ScoreboardWidget) return;
        ScoreboardWidget->AddToViewport(12);
    }
    ScoreboardWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible); ScoreboardWidget->Refresh();
    UpdateInputMode();ScoreboardWidget->FocusVoiceControls();
}
void AMCPlayerController::HideScoreboard()
{
    if(!ScoreboardWidget || !ScoreboardWidget->IsVisible()) return;
    ScoreboardWidget->SetVisibility(ESlateVisibility::Collapsed);
    if(IsLocalController() && GetWorld() && !GetWorld()->bIsTearingDown) UpdateInputMode();
}
void AMCPlayerController::Tick(float DeltaSeconds)
{
    // Drive the cosmetic mouth stage before its skeletal components update.
    if(IsLocalController()) {RefreshLoadingPresentation();RefreshHubPresentation(DeltaSeconds);}
    Super::Tick(DeltaSeconds);
    if(HasAuthority()) RefreshPerkChoiceWorld();
    if (HasAuthority() && GetWorld()->GetRealTimeSeconds()>=NextPerkTeamCheck) {
        NextPerkTeamCheck=GetWorld()->GetRealTimeSeconds()+.1;RefreshPerkChoiceTeamState();
    }
    if (!IsLocalController()) return;
    if(GetWorld()->GetRealTimeSeconds()>=NextBackgroundAudioAt) {
        NextBackgroundAudioAt=GetWorld()->GetRealTimeSeconds()+.5;
        RefreshBackgroundAudio();
    }
    // Lobby/tutorial presentation must follow replicated state even while a perk
    // choice has paused gameplay time on this or another player's controller.
    if (GetWorld()->GetRealTimeSeconds()>=NextFrontEndCheck) { NextFrontEndCheck=GetWorld()->GetRealTimeSeconds()+.1; RefreshFrontEnd(); }
    // Choice maintenance must keep running while gameplay time and timers are paused.
    if (GetWorld()->GetRealTimeSeconds()>=NextLevelChoiceCheck) {
        NextLevelChoiceCheck=GetWorld()->GetRealTimeSeconds()+.1;
        if (ActiveRewardChest.IsValid() || RewardPawn.IsValid()) CheckRewardUI();
        RefreshLevelPerkChoices();
        RefreshPerkChoiceWaiting();
        UpdatePerkChoicePause();
    }
    if (bBossIntroPlaying) return;
    // UI input mode can swallow the release event when a menu opens while Tab is held.
    if (ScoreboardWidget && ScoreboardWidget->IsVisible()
        && ((!IsInputKeyDown(EKeys::Tab) && !IsInputKeyDown(EKeys::Gamepad_Special_Left))
            || (FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive()))) HideScoreboard();
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now>=NextSpectatorCheck) { NextSpectatorCheck=Now+.1; UpdateDeathSpectating(); }
    if(bDeathSpectating && SpectatorTarget.IsValid() && !bShowMouseCursor && !IsLookInputIgnored()) {
        float X=0,Y=0; GetInputMouseDelta(X,Y);
        const auto* Settings=UMCGameUserSettings::Get();
        const FVector2D MouseDelta(X,Y);
        ApplySpectatorOrbitInput(Settings?Settings->ScaleLookInput(MouseDelta,false):MouseDelta);
        const auto* Target=SpectatorTarget.Get();
        const float Scale=FMath::Clamp(Target->GamepadCameraTurnRate,30.f,360.f)*DeltaSeconds/FMath::Clamp(Target->CameraOrbitSensitivity,.02f,1.f);
        auto Stick=[](float Value){return FMath::Sign(Value)*FMath::Clamp((FMath::Abs(Value)-.2f)/.8f,0.f,1.f);};
        const FVector2D StickDelta=FVector2D(Stick(GetInputAnalogKeyState(EKeys::Gamepad_RightX)),Stick(GetInputAnalogKeyState(EKeys::Gamepad_RightY)))*Scale;
        ApplySpectatorOrbitInput(Settings?Settings->ScaleLookInput(StickDelta,true):StickDelta);
        ZoomSpectatorCamera(GetInputAnalogKeyState(EKeys::MouseWheelAxis));
    }
}
void AMCPlayerController::SetBackgroundAudioContext(FName Context)
{
    BackgroundAudioContextOverride=Context;
    RefreshBackgroundAudio();
}
void AMCPlayerController::RefreshBackgroundAudio()
{
    if(!IsLocalController() || !SceneAudio || !GetWorld()) return;
    FName Context=BackgroundAudioContextOverride;
    if(Context.IsNone()) {
        auto ChooseContext=[&]() -> FName {
            if(IsHubWorld()) return TEXT("Hub");
            for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
                if(It->IsBossAlive() && It->Runtime.State!=EMCBossState::Dormant
                    && It->Runtime.AnimationPreview==EMCBossAnimationPreview::None) return TEXT("FinalBoss");
            if(const auto* Event=AMCCataclysmEvent::FindActive(GetWorld())) {
                if(Event->Phase==EMCCataclysmPhase::Cleanup) return TEXT("Relax");
                return Event->Variant==EMCCataclysmVariant::Jellies?FName(TEXT("CataclysmJellies")):FName(TEXT("CataclysmRafts"));
            }
            for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It) if(It->IsActive()) return TEXT("IceEvent");
            for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It) if(It->IsActive()) return TEXT("FogBrawl");
            for(TActorIterator<AMCNutRainEvent> It(GetWorld());It;++It)
                if(It->Stage!=EMCNutRainStage::Idle && It->Stage!=EMCNutRainStage::Complete) return TEXT("NutEncounter");
            for(TActorIterator<AMCColdColaEvent> It(GetWorld());It;++It) if(It->bActive) return TEXT("ColdCola");
            const auto* Game=GetWorld()->GetGameState<AMCGameState>();
            if(!Game || Game->bLobbyWaiting || Game->Phase!=EMCShiftPhase::Working
                || (Game->DirectorState.bEnabled && Game->DirectorState.Pacing==EMCGameDirectorPacing::Rest)) return TEXT("Relax");
            return TEXT("Gameplay");
        };
        Context=ChooseContext();
    }
    SceneAudio->SetScene(BackgroundSoundPalette,Context);
}
void AMCPlayerController::CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult)
{
    if(IsLocalController() && IsHubWorld() && HubSceneCamera.IsValid()) {
        HubSceneCamera->CalcCamera(DeltaTime,OutResult); return;
    }
    if(IsLocalController() && bDeathSpectating && SpectatorTarget.IsValid()) {
        UMCPlayerCameraComponent::GetSpectatorView(SpectatorTarget.Get(),GetPawn(),SpectatorOrbit,
            SpectatorDistance,DeltaTime,SpectatorCameraView,OutResult);
        return;
    }
    if(GetPawn()) { GetPawn()->CalcCamera(DeltaTime,OutResult); return; }
    Super::CalcCamera(DeltaTime,OutResult);
}
void AMCPlayerController::NotifyLoadedWorld(FName WorldPackageName,bool bFinalDest)
{
    Super::NotifyLoadedWorld(WorldPackageName,bFinalDest);
    if(bFinalDest) ResetWorldPresentation();
}
void AMCPlayerController::PostSeamlessTravel()
{
    Super::PostSeamlessTravel();
    ResetWorldPresentation();
}
void AMCPlayerController::ResetWorldPresentation()
{
    ResetLoadingPresentation();
    ResetHubPresentation();
    RefreshPerkChoiceWorld();
    bGameplayLoaded=false; bGameplayLoadAckSent=false;
    AcknowledgedTutorial.Reset(); NextFrontEndCheck=0; NextLevelChoiceCheck=0;
    NextAlarmAt=0; NextColorAt=0;
    UMCPlayerCameraComponent::ClearSpectatorView(SpectatorCameraView);
    SpectatorTarget.Reset(); PreviousPawn.Reset(); bDeathSpectating=false;
    bSpectatorOrbitInitialized=false; bAutoManageActiveCameraTarget=true; NextSpectatorCheck=0;
    if(GameplayHUD) GameplayHUD->ResetGameplayAlerts();
    HealthBoss.Reset(); GuardedIntroBoss.Reset(); BossDiedAt=-1; NextBossScan=0;
    if(!IsLocalController()) return;
    // A retained controller needs fresh world-bound widgets and timers after seamless travel.
    if(IsValid(BossIntro)) BossIntro->CancelIntro();
    BossIntro=nullptr; bBossIntroPlaying=false;
    CloseRewardUI(false); HideScoreboard();
    if(FrontEndWidget) FrontEndWidget->RemoveFromParent(); FrontEndWidget=nullptr;
    CloseGameplayMenus(); PauseWidget=nullptr; ConnectionWidget=nullptr;
    if(TutorialWidget) TutorialWidget->RemoveFromParent(); TutorialWidget=nullptr;
    if(DevPanel) DevPanel->RemoveFromParent(); DevPanel=nullptr;
    if(EmoteWidget) EmoteWidget->RemoveFromParent(); EmoteWidget=nullptr;
    if(ScoreboardWidget) ScoreboardWidget->RemoveFromParent(); ScoreboardWidget=nullptr;
    if(PerkChoiceWidget) PerkChoiceWidget->RemoveFromParent(); PerkChoiceWidget=nullptr;
    bLobbyUIOpen=bPauseMenuOpen=bTutorialMenuInput=bFrontEndHidPrototype=false;
    if(PrototypeWidget) PrototypeWidget->RemoveFromParent();
    PrototypeWidget=nullptr; GameplayHUD=nullptr;
    if(BossHealthWidget) BossHealthWidget->RemoveFromParent();
    BossHealthWidget=nullptr;
    GetWorldTimerManager().ClearTimer(BossHUDTimer);
    if(!IsHubWorld()) {
        PrototypeWidget=CreateWidget<UMCPrototypeWidget>(this,UMCPrototypeWidget::StaticClass());
        if(PrototypeWidget) PrototypeWidget->AddToViewport();
        BossHealthWidget=CreateWidget<UMCBossHealthWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("BossHealth"),UMCBossHealthWidget::StaticClass()));
        if(BossHealthWidget) BossHealthWidget->AddToViewport(40);
        GetWorldTimerManager().SetTimer(BossHUDTimer,this,&AMCPlayerController::RefreshBossHUD,.1f,true);
    }
    RefreshLoadingPresentation();RefreshHubPresentation(0.f); UpdateInputMode();
    if(IsValid(GetPawn()) && GetPawn()->GetWorld()==GetWorld()) SetViewTarget(GetPawn());
}
void AMCPlayerController::ApplySpectatorOrbitInput(FVector2D Delta)
{
    const auto* Target=SpectatorTarget.Get();
    if(!IsLocalController() || !bDeathSpectating || !Target || bShowMouseCursor || IsLookInputIgnored()
        || Delta.ContainsNaN() || Delta.IsNearlyZero()) return;
    const float Sensitivity=FMath::Clamp(Target->CameraOrbitSensitivity,.02f,1.f);
    SpectatorOrbit.Yaw=FRotator::NormalizeAxis(SpectatorOrbit.Yaw+Delta.X*Sensitivity);
    SpectatorOrbit.Pitch=FMath::Clamp(SpectatorOrbit.Pitch+Delta.Y*Sensitivity,-85.,85.);
}
void AMCPlayerController::ZoomSpectatorCamera(float ScrollDelta)
{
    const auto* Target=SpectatorTarget.Get();
    if(!IsLocalController() || !bDeathSpectating || !Target || bShowMouseCursor || IsLookInputIgnored()
        || !FMath::IsFinite(ScrollDelta) || FMath::IsNearlyZero(ScrollDelta)) return;
    SpectatorDistance=FMath::Clamp(SpectatorDistance-ScrollDelta*FMath::Clamp(Target->CameraZoomStep,10.f,300.f),250.f,1600.f);
}
void AMCPlayerController::RegisterGameplayHUD(UMCGameplayHUD* HUD)
{ if(IsLocalController() && IsValid(HUD) && HUD->GetOwningPlayer()==this) GameplayHUD=HUD; }
void AMCPlayerController::UnregisterGameplayHUD(UMCGameplayHUD* HUD)
{ if(GameplayHUD==HUD) GameplayHUD=nullptr; }
void AMCPlayerController::QueueGameplayAlert(const FText& Message,float HoldSeconds,FName EventId)
{ if(IsLocalController() && GameplayHUD) GameplayHUD->QueueGameplayAlert(Message,HoldSeconds,EventId); }
void AMCPlayerController::ClientShowGameplayAlert_Implementation(const FText& Message,float HoldSeconds,FName EventId)
{ QueueGameplayAlert(Message,HoldSeconds,EventId); }
void AMCPlayerController::UpdateDeathSpectating()
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    const bool Dead=Hero && Hero->Status && !Hero->Status->IsAlive();
    if (!Dead)
    {
        if (bDeathSpectating || PreviousPawn!=GetPawn())
        {
            if (auto* Previous=Cast<AMCToothCharacter>(GetViewTarget())) Previous->ClearCameraWallReveal();
            bDeathSpectating=false; SpectatorTarget.Reset(); bAutoManageActiveCameraTarget=true;
            bSpectatorOrbitInitialized=false; UMCPlayerCameraComponent::ClearSpectatorView(SpectatorCameraView);
            if (GetPawn()) SetViewTargetWithBlend(GetPawn(),.2f);
        }
        PreviousPawn=GetPawn(); return;
    }
    PreviousPawn=Hero;
    if (!bDeathSpectating)
    {
        bDeathSpectating=true; bAutoManageActiveCameraTarget=false;
        if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
        UpdateInputMode();
    }
    auto* Target=SpectatorTarget.Get();
    if (!IsValid(Target) || !Target->Status->IsAlive() || Target==Hero) CycleSpectator(1);
}
void AMCPlayerController::CycleSpectator(int32 Direction)
{
    if (!IsLocalController() || !bDeathSpectating) return;
    TArray<AMCToothCharacter*> Living;
    for (TActorIterator<AMCToothCharacter> It(GetWorld()); It; ++It)
        if (It->GetPlayerState() && It->Status && It->Status->IsAlive() && *It!=GetPawn()) Living.Add(*It);
    Living.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B)
    { return (A.GetPlayerState()?A.GetPlayerState()->GetPlayerId():0)<(B.GetPlayerState()?B.GetPlayerState()->GetPlayerId():0); });
    if (Living.IsEmpty())
    {
        SpectatorTarget.Reset(); UMCPlayerCameraComponent::ClearSpectatorView(SpectatorCameraView);
        if (GetPawn() && GetViewTarget()!=GetPawn()) SetViewTargetWithBlend(GetPawn(),.2f);
        return;
    }
    const int32 Current=Living.IndexOfByKey(SpectatorTarget.Get());
    const int32 Next=Current==INDEX_NONE?0:(Current+Direction+Living.Num())%Living.Num();
    if (auto* Previous=Cast<AMCToothCharacter>(GetViewTarget())) Previous->ClearCameraWallReveal();
    SpectatorTarget=Living[Next];
    if(!bSpectatorOrbitInitialized) {
        const auto* Target=SpectatorTarget.Get();
        SpectatorOrbit=Target->Camera?Target->Camera->GetComponentRotation():FRotator(-30,0,0);
        SpectatorOrbit.Pitch=FMath::Clamp(SpectatorOrbit.Pitch,-85.,85.); SpectatorOrbit.Roll=0;
        SpectatorDistance=Target->Camera?FMath::Clamp(float(FVector::Dist(Target->Camera->GetComponentLocation(),
            Target->GetCameraFocusLocation())),250.f,1600.f):1000.f;
        bSpectatorOrbitInitialized=true;
    }
    UMCPlayerCameraComponent::ClearSpectatorView(SpectatorCameraView);
    // This controller computes a local view without changing the teammate's camera or control rotation.
    SetViewTargetWithBlend(this,.2f);
}
void AMCPlayerController::NextSpectator() { CycleSpectator(1); }
void AMCPlayerController::PreviousSpectator() { CycleSpectator(-1); }
float AMCPlayerController::GetRespawnSecondsRemaining() const
{
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn()); const auto* State=GetWorld()->GetGameState();
    return Hero && State && !Hero->Status->IsAlive()?FMath::Max(0.,Hero->RespawnAt-State->GetServerWorldTimeSeconds()):0.f;
}
void AMCPlayerController::ServerSendAlarm_Implementation(EMCPlayerAlarm Alarm)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn()); auto* State=GetPlayerState<AMCPlayerState>();
    auto* Game=GetWorld()->GetGameState();
    if (!Hero || !Hero->Status->IsAlive() || !State || !Game || Alarm==EMCPlayerAlarm::None || uint8(Alarm)>uint8(EMCPlayerAlarm::Help)) return;
    const double Now=Game->GetServerWorldTimeSeconds(); if (Now<NextAlarmAt) return;
    NextAlarmAt=Now+2.; State->Alarm=Alarm; State->AlarmUntil=Now+4.; State->ForceNetUpdate();
}
void AMCPlayerController::ServerSetPlayerColor_Implementation(FLinearColor Color)
{
    if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B) || !FMath::IsFinite(Color.A)) return;
    const double Now=GetWorld()->GetTimeSeconds(); if (Now<NextColorAt) return; NextColorAt=Now+.2;
    Color=Color.GetClamped(0,1); Color.A=1;
    if (auto* State=GetPlayerState<AMCPlayerState>()) { State->PlayerColor=Color; State->ForceNetUpdate(); }
    if (auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->ApplyPlayerColor(Color);
}
void AMCPlayerController::ToggleDevPanel()
{
#if !UE_BUILD_SHIPPING
    if (!IsLocalController() || IsRewardInteractionActive() || bBossIntroPlaying) return;
    if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (!DevPanel)
    {
        DevPanel=CreateWidget<UMCDevPanelWidget>(this,UMCDevPanelWidget::StaticClass());
        DevPanel->AddToViewport(20); DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    }
    const bool bOpen=!DevPanel->IsVisible();
    if (PrototypeWidget) PrototypeWidget->ClosePanels();
    DevPanel->SetVisibility(bOpen?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if (bOpen) DevPanel->RefreshActions();
    UpdateInputMode();
#endif
}
bool AMCPlayerController::CanUseDevPanel() const
{
    const auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    return Mode && Mode->CanUseDevPanel(this);
}
void AMCPlayerController::RequestDevAction(EMCDevAction Action,int32 StepIndex)
{
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>())
    {
        const FText Result=Mode->ExecuteDevAction(this,Action,StepIndex);
        if (DevPanel && Action==EMCDevAction::OneHitKill) DevPanel->RefreshActions();
        if (DevPanel) DevPanel->SetFeedback(Result);
    }
    else if (DevPanel) DevPanel->SetFeedback(FText::FromString(TEXT("События запускает хост. Его изменения появятся у всех игроков.")));
    UpdateInputMode();
}
void AMCPlayerController::ToggleEmotes()
{
    if (!IsLocalController() || IsRewardInteractionActive() || bBossIntroPlaying) return;
    if (!EmoteWidget)
    {
        EmoteWidget=CreateWidget<UMCEmoteWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("Emotes"),UMCEmoteWidget::StaticClass()));
        EmoteWidget->AddToViewport(15); EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    const bool Open=!EmoteWidget->IsVisible();
    if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (PrototypeWidget) PrototypeWidget->ClosePanels();
    EmoteWidget->SetVisibility(Open?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if (Open) EmoteWidget->Refresh(); UpdateInputMode();
}
void AMCPlayerController::ToggleTuning() { if (IsRewardInteractionActive() || bBossIntroPlaying) return; if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed); if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed); if (PrototypeWidget) { PrototypeWidget->ToggleTuning(); UpdateInputMode(); } }
void AMCPlayerController::ToggleConnection()
{
    if (!IsLocalController() || bLobbyUIOpen || IsRewardInteractionActive() || bBossIntroPlaying) return;
    if (bConnectionMenuOpen) { CloseConnectionMenu(); return; }
    CloseGameplayMenus();
    auto* Menu=EnsureGameplayMenu(true);
    if (!Menu) return;
    bConnectionMenuOpen=true;
    Menu->ShowConnection(); Menu->AddToViewport(60);
    if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (PrototypeWidget) PrototypeWidget->ClosePanels();
    HideScoreboard(); UpdateInputMode();
}

UMCGameplayMenuWidget* AMCPlayerController::EnsureGameplayMenu(bool bConnection)
{
    auto& Widget=bConnection?ConnectionWidget:PauseWidget;
    if (!Widget)
    {
        Widget=CreateWidget<UMCGameplayMenuWidget>(this,UMCGameplayUIFactory::ResolveClass(
            bConnection?TEXT("ConnectionMenu"):TEXT("PauseMenu"),UMCGameplayMenuWidget::StaticClass()));
        if (!Widget) return nullptr;
        Widget->OnResumeRequested.AddUniqueDynamic(this,&AMCPlayerController::ResumeGameplayMenu);
        Widget->OnEmotesRequested.AddUniqueDynamic(this,&AMCPlayerController::OpenMenuEmotes);
        Widget->OnConnectionCloseRequested.AddUniqueDynamic(this,&AMCPlayerController::CloseConnectionMenu);
        Widget->OnLeaveRequested.AddUniqueDynamic(this,&AMCPlayerController::ReturnToMainMenu);
        Widget->OnExitRequested.AddUniqueDynamic(this,&AMCPlayerController::ExitFromGameplayMenu);
        Widget->OnHostRequested.AddUniqueDynamic(this,&AMCPlayerController::HostGame);
        Widget->OnJoinAddressRequested.AddUniqueDynamic(this,&AMCPlayerController::JoinFromGameplayMenu);
    }
    return Widget;
}
void AMCPlayerController::CloseGameplayMenus()
{
    if (PauseWidget) PauseWidget->RemoveFromParent();
    if (ConnectionWidget) ConnectionWidget->RemoveFromParent();
    bPauseMenuOpen=bConnectionMenuOpen=false;
}
void AMCPlayerController::ResumeGameplayMenu() {CloseGameplayMenus();UpdateInputMode();}
void AMCPlayerController::CloseConnectionMenu() {CloseGameplayMenus();UpdateInputMode();}
void AMCPlayerController::OpenMenuEmotes() {CloseGameplayMenus();ToggleEmotes();UpdateInputMode();}
void AMCPlayerController::ExitFromGameplayMenu() {UKismetSystemLibrary::QuitGame(this,this,EQuitPreference::Quit,false);}
void AMCPlayerController::JoinFromGameplayMenu(const FString& Address)
{
    FString Normalized;
    if (UMCGameplayMenuWidget::NormalizeJoinAddress(Address,Normalized)) ClientTravel(Normalized,TRAVEL_Absolute);
}
void AMCPlayerController::UpdateInputMode()
{
    UpdatePerkChoicePause();
    // A modal owns focus over the held overlay; hiding directly here avoids
    // recursively re-entering this input-mode resolver during a transition.
    if(ScoreboardWidget && ScoreboardWidget->IsVisible() && (bLoadingInputBlocked || bLobbyUIOpen || bPauseMenuOpen
        || bConnectionMenuOpen || bTutorialMenuInput || bHubMenuInput || bBossIntroPlaying || IsRewardInteractionActive()
        || (DevPanel && DevPanel->IsVisible()) || (EmoteWidget && EmoteWidget->IsVisible())
        || (PrototypeWidget && PrototypeWidget->IsPanelOpen()))) ScoreboardWidget->SetVisibility(ESlateVisibility::Collapsed);
    if(bLoadingInputBlocked) {ApplyLoadingInputMode();return;}
    if (bLobbyUIOpen || bPauseMenuOpen || bConnectionMenuOpen || bTutorialMenuInput)
    {
        bShowMouseCursor=true;
        ResetIgnoreMoveInput(); SetIgnoreMoveInput(true);
        ResetIgnoreLookInput(); SetIgnoreLookInput(true);
        if (auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
        FInputModeUIOnly Mode;
        if (bLobbyUIOpen && FrontEndWidget) Mode.SetWidgetToFocus(FrontEndWidget->TakeWidget());
        else if (bPauseMenuOpen && PauseWidget) Mode.SetWidgetToFocus(PauseWidget->TakeWidget());
        else if (bConnectionMenuOpen && ConnectionWidget) Mode.SetWidgetToFocus(ConnectionWidget->TakeWidget());
        else if (TutorialWidget) Mode.SetWidgetToFocus(TutorialWidget->TakeWidget());
        SetInputMode(Mode);
        if (bLobbyUIOpen && FrontEndWidget) FrontEndWidget->FocusMenu();
        else if (bPauseMenuOpen && PauseWidget) PauseWidget->FocusMenu();
        else if (bConnectionMenuOpen && ConnectionWidget) ConnectionWidget->FocusMenu();
        return;
    }
    if(HubWidget && bHubMenuInput)
    {
        bShowMouseCursor=true;
        ResetIgnoreMoveInput(); SetIgnoreMoveInput(true);
        ResetIgnoreLookInput(); SetIgnoreLookInput(true);
        if(auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
        FInputModeUIOnly Mode; Mode.SetWidgetToFocus(HubWidget->TakeWidget());
        SetInputMode(Mode); HubWidget->SetUserFocus(this);
        return;
    }
    if (bBossIntroPlaying)
    {
        bShowMouseCursor=false;
        ResetIgnoreMoveInput(); SetIgnoreMoveInput(true);
        ResetIgnoreLookInput(); SetIgnoreLookInput(true);
        SetInputMode(FInputModeGameOnly());
        return;
    }
    if(ScoreboardWidget && ScoreboardWidget->IsVisible())
    {
        bShowMouseCursor=true;
        ResetIgnoreMoveInput();SetIgnoreMoveInput(true);
        ResetIgnoreLookInput();SetIgnoreLookInput(true);
        if(auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
        FInputModeGameAndUI Mode;Mode.SetWidgetToFocus(ScoreboardWidget->TakeWidget());
        Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);return;
    }
    const bool bReward=IsRewardMenuOpen();
    const bool bOpening=IsRewardInteractionActive() && !bReward;
    const bool bDev=DevPanel && DevPanel->IsVisible();
    const bool bEmote=EmoteWidget && EmoteWidget->IsVisible();
    const bool bPanel = bReward || bEmote || bDev || (PrototypeWidget && PrototypeWidget->IsPanelOpen());
    bShowMouseCursor = bPanel;
    if (auto* Tooth = Cast<AMCToothCharacter>(GetPawn())) Tooth->bPreviewAnimation = PrototypeWidget && PrototypeWidget->IsTuningOpen();
    ResetIgnoreMoveInput(); SetIgnoreMoveInput(bPanel || bOpening);
    ResetIgnoreLookInput(); SetIgnoreLookInput(bPanel);
    if (bReward || bDev || bEmote)
    {
        if (auto* Tooth=Cast<AMCToothCharacter>(GetPawn())) Tooth->CancelGameplayInput();
        FInputModeUIOnly Mode; Mode.SetWidgetToFocus(bReward?PerkChoiceWidget->TakeWidget():(bEmote?EmoteWidget->TakeWidget():DevPanel->TakeWidget())); SetInputMode(Mode);
    }
    else if (bPanel) {
        if(auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
        FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(PrototypeWidget->TakeWidget()); Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode);
    }
    else SetInputMode(FInputModeGameOnly());
}

bool AMCPlayerController::IsRewardMenuOpen() const
{
    return PerkChoiceWidget && PerkChoiceWidget->IsInViewport() && PerkChoiceWidget->IsVisible();
}

void AMCPlayerController::UpdatePerkChoicePause()
{
    if (!IsLocalController()) return;
    const bool bOpen=IsRewardMenuOpen() && PerkChoiceWidget->IsShowingChoices();
    if (bOpen==bPerkChoicePauseSent) return;
    bPerkChoicePauseSent=bOpen;
    ServerSetPerkChoicePause(bOpen);
}

bool AMCPlayerController::HasAuthoritativePerkChoice() const
{
    if(!IsPerkChoiceParticipant()) return false;
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    const auto* State=GetPlayerState<AMCPlayerState>();
    // Incapacitated workers keep earned cards queued until they can choose. A
    // world pause must not freeze the very recovery that would unlock their UI.
    if(IsBossIntroPlaying() || Hero->Inventory->bChainsawRunning || (State->Perks && State->Perks->IsFuryActive())
        || Hero->HasFrozenLegs() || Hero->IsMimicCaptured() || !Hero->ToothPhysics->CanAct()) return false;
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    if(Game && (Game->bLobbyWaiting || Game->bTutorialActive || Game->Phase==EMCShiftPhase::Won || Game->Phase==EMCShiftPhase::Lost)) return false;
    const auto* Chest=Hero->RewardInteraction.Get();
    // An already approved chest must finish its lid/lockpicking before a pause;
    // its open cards do not need fresh admission from the event director.
    if(IsValid(Chest) && Chest->OpeningPlayer==Hero) return Chest->Stage==EMCRewardChestStage::Open;
    if(!bPerkChoicePauseRequested) if(const auto* Director=AMCGameDirector::Find(GetWorld());Director && Director->IsManagingEvents()
        && !Director->CanStartRewardInteraction()) return false;
    return false; // Only an open, server-owned chest can pause for a choice.
}

bool AMCPlayerController::IsPerkChoiceParticipant() const
{
    const auto* State=GetPlayerState<AMCPlayerState>();
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    return !bPerkChoiceEnding && !IsActorBeingDestroyed() && State && !State->IsSpectator() && !State->IsOnlyASpectator()
        && IsValid(Hero) && !Hero->IsActorBeingDestroyed() && Hero->Status && Hero->Status->IsAlive();
}

void AMCPlayerController::RefreshPerkChoiceWorld()
{
    if(PerkChoiceWorld.Get()==GetWorld()) return;
    // Seamless travel retains this controller, but the old GameMode's pause
    // delegates and real-time clock do not transfer to the destination world.
    PerkChoiceWorld=GetWorld();
    bPerkChoicePauseRequested=bPerkChoicePauseSent=false;
    bPerkChoiceTeamActive=bPerkChoiceNeedsChoice=false;
    PerkChoiceReadyPlayers=PerkChoiceTotalPlayers=0;
    LastSentPerkReady=LastSentPerkTotal=-1;
    bLastSentPerkActive=bLastSentPerkNeedsChoice=false;
    NextPerkTeamCheck=0;
}

void AMCPlayerController::RefreshPerkChoiceTeamState()
{
    if(!HasAuthority() || !GetWorld()) return;
    RefreshPerkChoiceWorld();
    // A listen host's client callback can re-enter through UpdateInputMode.
    static bool bRefreshing=false;
    if(bRefreshing) return;
    TGuardValue<bool> Guard(bRefreshing,true);
    auto* Mode=GetWorld()->GetAuthGameMode(); if(!Mode) return;
    struct FMember {AMCPlayerController* Controller;bool Participant;bool NeedsChoice;};
    TArray<FMember> Controllers;
    int32 Total=0,Pending=0;bool Changed=false,Released=false;
    if(bPerkChoiceEnding && bPerkChoicePauseRequested) {bPerkChoicePauseRequested=false;Changed=Released=true;}
    for(TActorIterator<AMCPlayerController> It(GetWorld());It;++It) {
        It->RefreshPerkChoiceWorld();
        if(It->bPerkChoiceEnding || It->IsActorBeingDestroyed()) continue;
        const bool Participant=It->IsPerkChoiceParticipant(),NeedsChoice=It->HasAuthoritativePerkChoice();
        Controllers.Add({*It,Participant,NeedsChoice});
        Total+=Participant;Pending+=NeedsChoice;
    }
    // Acquire every authoritative choice before releasing any old lease. A fast
    // first selection cannot resume a teammate whose widget/RPC arrived later.
    for(const auto& Member:Controllers) if(auto* PC=Member.Controller;Member.NeedsChoice && !PC->bPerkChoicePauseRequested) {
        PC->bPerkChoicePauseRequested=Mode->SetPause(PC,FCanUnpause::CreateUObject(PC,&AMCPlayerController::CanUnpausePerkChoice));
        Changed|=PC->bPerkChoicePauseRequested;
    }
    for(const auto& Member:Controllers) if(auto* PC=Member.Controller;!Member.NeedsChoice && PC->bPerkChoicePauseRequested) {PC->bPerkChoicePauseRequested=false;Changed=Released=true;}
    // Only release our completed leases. Polling must not clear unrelated pauses.
    if(Released) Mode->ClearPause();
    for(const auto& Member:Controllers) {
        auto* PC=Member.Controller;
        const bool Active=Pending>0 && Member.Participant,NeedsChoice=Member.NeedsChoice;
        if(PC->LastSentPerkReady!=Total-Pending || PC->LastSentPerkTotal!=Total
            || PC->bLastSentPerkActive!=Active || PC->bLastSentPerkNeedsChoice!=NeedsChoice) {
            PC->LastSentPerkReady=Total-Pending;PC->LastSentPerkTotal=Total;
            PC->bLastSentPerkActive=Active;PC->bLastSentPerkNeedsChoice=NeedsChoice;
            PC->ClientPerkChoiceTeamState(Total-Pending,Total,Active,NeedsChoice);
            Changed=true;
        }
    }
    if(Changed) GetWorldSettings()->ForceNetUpdate();
}

void AMCPlayerController::ClientPerkChoiceTeamState_Implementation(int32 Ready,int32 Total,bool bActive,bool bNeedsChoice)
{
    RefreshPerkChoiceWorld();
    PerkChoiceTotalPlayers=FMath::Max(0,Total);PerkChoiceReadyPlayers=FMath::Clamp(Ready,0,PerkChoiceTotalPlayers);
    bPerkChoiceTeamActive=bActive;bPerkChoiceNeedsChoice=bNeedsChoice;
    if(!IsLocalController()) return;
    RefreshPerkChoiceWaiting();
    RefreshLevelPerkChoices();
}

bool AMCPlayerController::IsWaitingForPerkTeam() const
{
    if(!bPerkChoiceTeamActive || bPerkChoiceNeedsChoice || !IsPerkChoiceParticipant()) return false;
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    const auto* State=GetPlayerState<AMCPlayerState>();
    // Other players can pause the shared clock without cancelling an ongoing
    // saw run or covering an intro/recovery with a modal that calls CancelInput.
    return !IsBossIntroPlaying() && !Hero->Inventory->bChainsawRunning
        && (!State->Perks || !State->Perks->IsFuryActive()) && !Hero->HasFrozenLegs()
        && !Hero->IsMimicCaptured() && Hero->ToothPhysics->CanAct();
}

void AMCPlayerController::RefreshPerkChoiceWaiting()
{
    if(!IsLocalController()) return;
    if(!IsWaitingForPerkTeam()) {
        if(PerkChoiceWidget && PerkChoiceWidget->IsWaitingForTeam() && PerkChoiceWidget->IsInViewport()) {
            PerkChoiceWidget->RemoveFromParent();UpdateInputMode();
        }
        return;
    }
    const bool AlreadyWaiting=IsRewardMenuOpen() && PerkChoiceWidget->IsWaitingForTeam();
    ActiveLevelOfferId.Invalidate();bRewardChoicePending=false;
    if(!PerkChoiceWidget) PerkChoiceWidget=CreateWidget<UMCPerkChoiceWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("PerkChoice"),UMCPerkChoiceWidget::StaticClass()));
    if(!PerkChoiceWidget) return;
    if(!PerkChoiceWidget->IsInViewport()) PerkChoiceWidget->AddToViewport(30);
    PerkChoiceWidget->SetVisibility(ESlateVisibility::Visible);
    PerkChoiceWidget->ShowTeamWaiting(PerkChoiceReadyPlayers,PerkChoiceTotalPlayers);
    if(!AlreadyWaiting) {UpdateInputMode();PerkChoiceWidget->SetUserFocus(this);}
}

bool AMCPlayerController::CanUnpausePerkChoice()
{
    return !bPerkChoicePauseRequested || !HasAuthoritativePerkChoice();
}

void AMCPlayerController::ServerSetPerkChoicePause_Implementation(bool bOpen)
{
    if(bOpen && !HasAuthoritativePerkChoice()) return;
    // Closing a local panel is not proof that its server-owned offer was consumed.
    RefreshPerkChoiceTeamState();
}

bool AMCPlayerController::IsRewardInteractionActive() const
{
    return ActiveRewardChest.IsValid() && RewardPawn.Get()==GetPawn();
}

bool AMCPlayerController::PrepareRewardInteraction(AMCRewardChest* Chest)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if (!IsLocalController() || ActiveLevelOfferId.IsValid() || !IsValid(Chest) || !Hero || !Hero->Status || !Hero->Status->IsAlive()) return false;
    CloseGameplayMenus();
    ActiveRewardChest=Chest;
    RewardPawn=Hero;
    RewardShownAt=GetWorld()->GetTimeSeconds();
    bRewardChoicePending=false;
    if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (PrototypeWidget) PrototypeWidget->ClosePanels();
    HideScoreboard();
    Hero->CancelGameplayInput();
    GetWorldTimerManager().SetTimer(RewardUITimer,this,&AMCPlayerController::CheckRewardUI,.1f,true);
    return true;
}

bool AMCPlayerController::PrepareRewardUI(AMCRewardChest* Chest)
{
    if (!PrepareRewardInteraction(Chest)) return false;
    if (!PerkChoiceWidget)
        PerkChoiceWidget=CreateWidget<UMCPerkChoiceWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("PerkChoice"),UMCPerkChoiceWidget::StaticClass()));
    if (!PerkChoiceWidget) { CloseRewardUI(); return false; }
    if (!PerkChoiceWidget->IsInViewport()) PerkChoiceWidget->AddToViewport(30);
    PerkChoiceWidget->SetVisibility(ESlateVisibility::Visible);
    return true;
}

void AMCPlayerController::ClientShowRewardOpening_Implementation(AMCRewardChest* Chest,double ServerEndsAt)
{
    if (!FMath::IsFinite(ServerEndsAt) || !PrepareRewardInteraction(Chest)) return;
    // Lockpicking is presented by the character animation in the world. The modal
    // is created only after the server has opened the lid and sends three cards.
    if (PerkChoiceWidget) PerkChoiceWidget->RemoveFromParent();
    UpdateInputMode();
    CheckRewardUI();
}

void AMCPlayerController::ClientShowPerkChoices_Implementation(AMCRewardChest* Chest,const TArray<FName>& IDs,EMCPerkPolarity Polarity,bool bTeamBooster)
{
    if (IDs.Num()!=3 || IDs.Contains(NAME_None) || IDs[0]==IDs[1] || IDs[1]==IDs[2] || IDs[0]==IDs[2]
        || uint8(Polarity)>uint8(EMCPerkPolarity::Negative) || !PrepareRewardUI(Chest)) return;
    // Render the reliable payload directly; actor replication may still be one frame behind this RPC.
    PerkChoiceWidget->ShowChoices(IDs,Polarity);
    if(bTeamBooster) PerkChoiceWidget->SetTeamBoosterHeader();
    UpdateInputMode();
    PerkChoiceWidget->SetUserFocus(this);
}

void AMCPlayerController::ChooseRewardPerk(int32 ChoiceIndex)
{
    if (!IsLocalController() || !IsRewardMenuOpen() || !PerkChoiceWidget->IsShowingChoices()
        || bRewardChoicePending || ChoiceIndex<0 || ChoiceIndex>=3) return;
    if (ActiveLevelOfferId.IsValid())
    {
        bRewardChoicePending=true; PerkChoiceWidget->SetSelectionPending(true);
        ServerChooseLevelPerk(ActiveLevelOfferId,ChoiceIndex); return;
    }
    auto* Chest=ActiveRewardChest.Get();
    if (!IsValid(Chest)) { CloseRewardUI(); return; }
    bRewardChoicePending=true;
    PerkChoiceWidget->SetSelectionPending(true);
    ServerChooseRewardPerk(Chest,ChoiceIndex);
}

void AMCPlayerController::RerollLevelPerks()
{
    if(!IsLocalController() || !ActiveLevelOfferId.IsValid() || bRewardChoicePending) return;
    bRewardChoicePending=true;if(PerkChoiceWidget) PerkChoiceWidget->SetSelectionPending(true);
    ServerRerollLevelPerks(ActiveLevelOfferId);
}
void AMCPlayerController::ActivateRogueBooster() {if(IsLocalController() && !IsRewardMenuOpen()) ServerActivateRogueBooster();}
void AMCPlayerController::ServerActivateRogueBooster_Implementation()
{if(auto* PS=GetPlayerState<AMCPlayerState>();PS && PS->Perks) PS->Perks->ActivateBooster();}
void AMCPlayerController::ServerRerollLevelPerks_Implementation(FGuid OfferId)
{
    auto* PS=GetPlayerState<AMCPlayerState>();
    if(!PS || !PS->TryRerollLevelPerks(OfferId)) ClientLevelPerkChoiceResolved(OfferId,false);
}
void AMCPlayerController::ConfirmRogueReward(bool Accept)
{
    if(!IsLocalController() || !ActiveRewardChest.IsValid() || bRewardChoicePending) return;
    bRewardChoicePending=true;if(PerkChoiceWidget) PerkChoiceWidget->SetSelectionPending(true);
    ServerConfirmRogueReward(ActiveRewardChest.Get(),Accept);
}
void AMCPlayerController::ServerConfirmRogueReward_Implementation(AMCRewardChest* Chest,bool Accept)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!IsValid(Chest) || !Hero) return;
    const bool Done=Accept?Chest->ConfirmRogueContract(Hero):Chest->DeclineRogueReward(Hero);
    if(!Done && Chest->Stage==EMCRewardChestStage::Open && Chest->OpeningPlayer==Hero) Chest->PublishRogueOffer();
    RefreshPerkChoiceTeamState();
}
void AMCPlayerController::ClientShowRogueChoices_Implementation(AMCRewardChest* Chest,const FMCRogueChestOffer& Offer)
{
    if(!IsValid(Chest) || !PrepareRewardUI(Chest) || Offer.Kind==EMCRogueRewardKind::Ordinary
        || (Offer.Step<2 && Offer.IDs.Num()!=3)) return;
    bRewardChoicePending=false;PerkChoiceWidget->ShowRogueChoices(Offer);
    UpdateInputMode();PerkChoiceWidget->SetUserFocus(this);
}

void AMCPlayerController::ServerChooseRewardPerk_Implementation(AMCRewardChest* Chest,int32 ChoiceIndex)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if (!IsValid(Chest) || ChoiceIndex<0 || ChoiceIndex>=3 || !Hero || !Chest->TryChooseCard(Hero,ChoiceIndex))
    {
        if (IsValid(Chest) && Hero && Chest->Stage==EMCRewardChestStage::Open && Chest->OpeningPlayer==Hero)
            if(Chest->RogueKind!=EMCRogueRewardKind::Ordinary) Chest->PublishRogueOffer();
            else ClientShowPerkChoices(Chest,Chest->LootIDs,Chest->Polarity,Chest->bTeamStageBooster);
        else ClientClosePerkChoices(Chest);
    }
    RefreshPerkChoiceTeamState();
}

void AMCPlayerController::ClientClosePerkChoices_Implementation(AMCRewardChest* Chest)
{
    if (ActiveLevelOfferId.IsValid()) return;
    if (!Chest || ActiveRewardChest.Get()==Chest) CloseRewardUI();
}

void AMCPlayerController::RefreshLevelPerkChoices()
{
    // Old replicated/saved offers cannot open a free perk modal. Do not close
    // the independent chest UI when its PlayerState updates.
    if (IsLocalController() && ActiveLevelOfferId.IsValid()) CloseRewardUI();
}

void AMCPlayerController::ServerChooseLevelPerk_Implementation(FGuid OfferId,int32 ChoiceIndex)
{
    auto* ProgressPlayer=GetPlayerState<AMCPlayerState>();
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    const bool Accepted=ProgressPlayer && Hero && Hero->Status->IsAlive() && Game && !Game->bLobbyWaiting && !Game->bTutorialActive
        && ChoiceIndex>=0 && ChoiceIndex<3 && ProgressPlayer->TryChooseLevelUpPerk(OfferId,ChoiceIndex);
    RefreshPerkChoiceTeamState();
    ClientLevelPerkChoiceResolved(OfferId,Accepted);
}

void AMCPlayerController::ClientLevelPerkChoiceResolved_Implementation(FGuid OfferId,bool bAccepted)
{
    // Team state can arrive first and replace the cards with waiting UI. Still
    // remember the accepted token so a delayed offer replica cannot reopen it.
    if (bAccepted) {AcceptedLevelOfferId=OfferId;if(ActiveLevelOfferId==OfferId) CloseRewardUI();}
    else if(ActiveLevelOfferId==OfferId) { bRewardChoicePending=false; if (PerkChoiceWidget) PerkChoiceWidget->SetSelectionPending(false); }
    RefreshLevelPerkChoices();
    RefreshPerkChoiceWaiting();
}

void AMCPlayerController::CheckRewardUI()
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    auto* Chest=ActiveRewardChest.Get();
    if (!IsRewardInteractionActive() || !IsValid(Chest) || !Hero || Hero!=RewardPawn.Get() || !Hero->Status || !Hero->Status->IsAlive()
        || Chest->Stage==EMCRewardChestStage::Exhausted)
    { CloseRewardUI(); return; }
    // The explicit close RPC handles restart; this also covers loss of actor relevance or a cancelled interaction.
    if (GetWorld()->GetTimeSeconds()-RewardShownAt>2. && Chest->Stage!=EMCRewardChestStage::Lockpicking
        && Chest->Stage!=EMCRewardChestStage::Opening && Chest->Stage!=EMCRewardChestStage::Open)
    { CloseRewardUI(); return; }
}

void AMCPlayerController::CloseRewardUI(bool bRestoreInput)
{
    GetWorldTimerManager().ClearTimer(RewardUITimer);
    ActiveRewardChest.Reset();
    ActiveLevelOfferId.Invalidate();
    RewardPawn.Reset();
    bRewardChoicePending=false;
    if (PerkChoiceWidget) PerkChoiceWidget->RemoveFromParent();
    UpdatePerkChoicePause();
    if (HasAuthority()) ServerSetPerkChoicePause_Implementation(false);
    if (bRestoreInput && IsLocalController()) UpdateInputMode();
    if(bRestoreInput) RefreshPerkChoiceWaiting();
}

void AMCPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if(IsLocalController() && GetGameInstance())
        if(auto* Voice=GetGameInstance()->GetSubsystem<UMCVoiceChatSubsystem>()) Voice->SuspendLocalInput();
    ResetLoadingPresentation();
    ResetHubPresentation();
    bPerkChoiceEnding=true;
    if(HasAuthority()) RefreshPerkChoiceTeamState();
    UMCPlayerCameraComponent::ClearSpectatorView(SpectatorCameraView);
    if (FrontEndWidget) FrontEndWidget->RemoveFromParent();
    CloseGameplayMenus();
    if (TutorialWidget) TutorialWidget->RemoveFromParent();
    GetWorldTimerManager().ClearTimer(BossHUDTimer);
    if (IsValid(BossIntro)) BossIntro->CancelIntro();
    BossIntro=nullptr;
    if (BossHealthWidget) BossHealthWidget->RemoveFromParent();
    CloseRewardUI(false);
    Super::EndPlay(EndPlayReason);
}
void AMCPlayerController::ServerGameplayLoaded_Implementation()
{
    if (GetPawn() && GetPlayerState<AMCPlayerState>()) bGameplayLoaded=true;
}
void AMCPlayerController::ServerTutorialLoaded_Implementation()
{
    if (!GetPawn()) return;
    if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->SetLoaded(GetPlayerState<AMCPlayerState>());
}
void AMCPlayerController::ServerSetTutorialReady_Implementation(bool bReady)
{
    if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->SetReady(GetPlayerState<AMCPlayerState>(),bReady);
}
void AMCPlayerController::ServerStartLobby_Implementation()
{
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->StartLobby(this);
}
void AMCPlayerController::ReturnToMainMenu()
{
    if (!IsLocalController()) return;
    if (GetNetMode()==NM_Standalone) UGameplayStatics::SetGamePaused(this,false);
    if (auto* Steam=GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>()) Steam->LeaveRoom();
    else UGameplayStatics::OpenLevel(this,TEXT("/Game/Maps/L_MainMenu"));
}
void AMCPlayerController::TogglePauseMenu()
{
    if (!IsLocalController() || bLobbyUIOpen || IsRewardInteractionActive() || bBossIntroPlaying) return;
    bPauseMenuOpen=!bPauseMenuOpen;
    if (bPauseMenuOpen)
    {
        if (ConnectionWidget) ConnectionWidget->RemoveFromParent();
        bConnectionMenuOpen=false;
        if (auto* Menu=EnsureGameplayMenu(false)) { Menu->ShowPause(); if (!Menu->IsInViewport()) Menu->AddToViewport(60); }
        else bPauseMenuOpen=false;
        if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
        if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
        if (PrototypeWidget) PrototypeWidget->ClosePanels();
        HideScoreboard();
    }
    else if (PauseWidget) PauseWidget->RemoveFromParent();
    UpdateInputMode();
}
void AMCPlayerController::RefreshFrontEnd()
{
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if (!State || !GetPlayerState<AMCPlayerState>()) return;
    // Observe-mode bots remove the human pawn. Still clear the previous lesson
    // overlay and restore the HUD instead of leaving that player's UI frozen.
    if (GetPawn() && GetPawn()->GetWorld()==GetWorld() && IsWorldPresentationReady() && !bGameplayLoadAckSent) { ServerGameplayLoaded(); bGameplayLoadAckSent=true; }
    if (State->bLobbyWaiting && !bLobbyUIOpen)
    {
        CloseGameplayMenus();
        bLobbyUIOpen=true;
        if (!FrontEndWidget) FrontEndWidget=CreateWidget<UMCMainMenuWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("MainMenu"),UMCMainMenuWidget::StaticClass()));
        if (FrontEndWidget) { FrontEndWidget->ShowLobby(); FrontEndWidget->AddToViewport(60); }
        UpdateInputMode();
    }
    else if (!State->bLobbyWaiting && bLobbyUIOpen)
    {
        bLobbyUIOpen=false;
        if (FrontEndWidget) FrontEndWidget->RemoveFromParent();
        UpdateInputMode();
    }
    // The tutorial teaches the equipped tools, so their HUD stays visible.
    // The gameplay HUD hides its ordinary day overview while training is active.
    const bool HideGameplay=State->bLobbyWaiting;
    if (PrototypeWidget && HideGameplay) { PrototypeWidget->SetVisibility(ESlateVisibility::Collapsed); bFrontEndHidPrototype=true; }
    else if (PrototypeWidget && bFrontEndHidPrototype) { PrototypeWidget->SetVisibility(ESlateVisibility::Visible); bFrontEndHidPrototype=false; }
    if (State->bTutorialActive)
    {
        if (!TutorialWidget) { TutorialWidget=CreateWidget<UMCTutorialWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("Tutorial"),UMCTutorialWidget::StaticClass())); if (TutorialWidget) TutorialWidget->AddToViewport(20); }
        if (TutorialWidget) TutorialWidget->RefreshState();
        auto* Director=AMCTutorialDirector::Find(GetWorld());
        // The server rejects a load ACK without a pawn; retry after possession instead of caching that request.
        if (GetPawn() && GetPawn()->GetWorld()==GetWorld() && IsWorldPresentationReady() && Director && AcknowledgedTutorial.Get()!=Director) { ServerTutorialLoaded(); AcknowledgedTutorial=Director; }
        const bool NeedsInput=TutorialWidget && TutorialWidget->RequiresMenuInput();
        if (NeedsInput!=bTutorialMenuInput) { bTutorialMenuInput=NeedsInput; UpdateInputMode(); if (NeedsInput && TutorialWidget->GetReadyFocusTarget()) TutorialWidget->GetReadyFocusTarget()->SetUserFocus(this); }
    }
    else
    {
        if (TutorialWidget) { TutorialWidget->RemoveFromParent(); TutorialWidget=nullptr; }
        AcknowledgedTutorial.Reset();
        if (bTutorialMenuInput) { bTutorialMenuInput=false; UpdateInputMode(); }
    }
}

void AMCPlayerController::ClientPlayBossIntro_Implementation(AMCBossCharacter* Boss)
{
    if (!IsLocalController() || IsRewardInteractionActive() || !IsValid(Boss)) return;
    if (IsValid(BossIntro)) BossIntro->CancelIntro();
    FActorSpawnParameters Spawn; Spawn.Owner=this;
    BossIntro=GetWorld()->SpawnActor<AMCBossIntro>(AMCBossIntro::StaticClass(),FTransform::Identity,Spawn);
    if (BossIntro && !BossIntro->PlayIntro(this,Boss)) { BossIntro->Destroy(); BossIntro=nullptr; }
}

void AMCPlayerController::SetBossIntroGuard(AMCBossCharacter* Boss)
{
    if (!HasAuthority()) return;
    GuardedIntroBoss=Boss;
    GuardedIntroSerial=IsValid(Boss)?Boss->Runtime.PreviewSerial:0;
}

bool AMCPlayerController::IsBossIntroPlaying() const
{
    const auto* Boss=GuardedIntroBoss.Get();
    return bBossIntroPlaying || (HasAuthority() && IsValid(Boss) && Boss->IsBossAlive()
        && Boss->Runtime.AnimationPreview==EMCBossAnimationPreview::Roar
        && Boss->Runtime.PreviewSerial==GuardedIntroSerial);
}

void AMCPlayerController::BeginBossIntroPresentation()
{
    if (bBossIntroPlaying) return;
    CloseGameplayMenus();
    bBossIntroPlaying=true;
    if (PrototypeWidget)
    {
        PreviousPrototypeVisibility=PrototypeWidget->GetVisibility();
        PrototypeWidget->ClosePanels(); PrototypeWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    HideScoreboard();
    if (auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
    if (BossHealthWidget) BossHealthWidget->SetLetterbox(true);
    UpdateInputMode();
}

void AMCPlayerController::FinishBossIntroPresentation()
{
    if (!bBossIntroPlaying) return;
    if (auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
    bBossIntroPlaying=false;
    if (PrototypeWidget) PrototypeWidget->SetVisibility(PreviousPrototypeVisibility);
    if (BossHealthWidget) BossHealthWidget->SetLetterbox(false);
    UpdateInputMode();
    RefreshBossHUD();
}

void AMCPlayerController::RefreshBossHUD()
{
    if (!BossHealthWidget || !IsLocalController()) return;
    if(const auto* State=GetWorld()->GetGameState<AMCGameState>(); State && State->bSingleDayLoop
        && (State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost)) {
        BossHealthWidget->HideHealth();
        if (GameplayHUD) GameplayHUD->SetBossStackCount(0);
        return;
    }
    const double Now=GetWorld()->GetTimeSeconds();
    auto* Boss=HealthBoss.Get();
    const auto Relevant=[](const AMCBossCharacter* Candidate)
    {
        return IsValid(Candidate) && !Candidate->IsActorBeingDestroyed()
            && Candidate->Runtime.AnimationPreview==EMCBossAnimationPreview::None
            && Candidate->Runtime.State!=EMCBossState::Dormant;
    };
    if (!Relevant(Boss))
    {
        HealthBoss.Reset(); Boss=nullptr; BossDiedAt=-1;
        if (Now>=NextBossScan)
        {
            NextBossScan=Now+.5;
            double BestDistance=FMath::Square(6500.f);
            const FVector Eye=GetPawn()?GetPawn()->GetActorLocation():GetFocalLocation();
            for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            {
                if (!Relevant(*It) || It->Runtime.State==EMCBossState::Dead) continue;
                const double Distance=FVector::DistSquared(Eye,It->GetActorLocation());
                if (Distance<BestDistance) { BestDistance=Distance; Boss=*It; }
            }
            HealthBoss=Boss;
        }
    }
    if (Boss && Boss->Runtime.State==EMCBossState::Dead)
    {
        if (BossDiedAt<0) BossDiedAt=Now;
        if (Now-BossDiedAt>2.) Boss=nullptr;
    }
    else BossDiedAt=-1;
    TArray<FMCBossHealthPresentation> Snapshots;
    auto Copy=[](FName Key,const FText& Fallback)
    {
        auto* Theme=UMCGameplayUIFactory::GetMasterTheme();
        auto* Table=Theme->LocalizationTable.LoadSynchronous();
        return UMCUIDataLibrary::ResolveText(Table?Table:UMCUIDataLibrary::GetDefaultLocalizationTable(),Key,Fallback);
    };
    if (Boss)
    {
        auto& Entry=Snapshots.AddDefaulted_GetRef();
        Entry.BossID=Boss->GetFName();
        Entry.Name=Copy(TEXT("UI.Boss.Zombie.Name"),NSLOCTEXT("MCBossHealth","Caries","Гнилой страж"));
        Entry.Portrait=UMCGameplayUIFactory::GetMasterTheme()->BossPortraits.FindRef(TEXT("RottenGuardian"));
        Entry.Health=Boss->Runtime.Health; Entry.MaxHealth=Boss->Runtime.MaxHealth;
    }
    TArray<AMCNutBoss*> Nuts;
    const FVector Eye=GetPawn()?GetPawn()->GetActorLocation():GetFocalLocation();
    for (TActorIterator<AMCNutBoss> It(GetWorld());It;++It)
        if (It->IsEncounterAlive() && FVector::DistSquared(Eye,It->GetActorLocation())<FMath::Square(6500.f)) Nuts.Add(*It);
    // Actor identity keeps row order stable while players and bosses move.
    Nuts.Sort([](const AMCNutBoss& A,const AMCNutBoss& B){return A.GetFName().LexicalLess(B.GetFName());});
    for (auto* Nut:Nuts)
    {
        if (Snapshots.Num()==3) break;
        auto& Entry=Snapshots.AddDefaulted_GetRef();
        Entry.BossID=Nut->GetFName();
        const bool bKnight=Nut->BossRole==EMCNutBossRole::Tank;
        Entry.Name=bKnight?Copy(TEXT("UI.Boss.NutTank.Name"),NSLOCTEXT("MCBossHealth","Knight","Ореховый рыцарь")):
            Copy(TEXT("UI.Boss.NutMage.Name"),NSLOCTEXT("MCBossHealth","Mage","Ореховый колдун"));
        Entry.Portrait=UMCGameplayUIFactory::GetMasterTheme()->BossPortraits.FindRef(bKnight?TEXT("NutKnight"):TEXT("NutWizard"));
        Entry.Health=Nut->Health; Entry.MaxHealth=Nut->Settings.MaxHealth;
        Entry.bHasShield=bKnight; Entry.Shield=Nut->ShieldHealth; Entry.MaxShield=Nut->BossSettings.ShieldMaxHealth;
    }
    BossHealthWidget->ShowBosses(Snapshots,.1f);
    if (GameplayHUD) GameplayHUD->SetBossStackCount(BossHealthWidget->GetDisplayedBossCount(),BossHealthWidget->GetDisplayedStackHeight());
}
void AMCPlayerController::HostGame()
{
    if (auto* Steam=GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>(); Steam && Steam->CanUseSteam())
        Steam->HostRoom();
    else UGameplayStatics::OpenLevel(this,TEXT("/Game/Maps/Hub"),true,TEXT("listen?game=/Script/MessControl.MCHubGameMode"));
}
void AMCPlayerController::JoinGame(const FString& Address)
{
    FString Clean = Address.TrimStartAndEnd();
    // Only hostname/IP and optional port; no URL options, commands or file paths.
    if (Clean.IsEmpty() || Clean.Len()>128) return;
    for (TCHAR C : Clean) if (!FChar::IsAlnum(C) && C != '.' && C != ':' && C != '-') return;
    ClientTravel(Clean,TRAVEL_Absolute);
}
void AMCPlayerController::RequestRestart() { ServerRestartShift(); }
void AMCPlayerController::ServerRestartShift_Implementation()
{
    // Only the listen host may restart, and only after a finished run.
    AMCGameState* State = GetWorld()->GetGameState<AMCGameState>();
    if (!IsLocalController() || !State || (!State->bDayOneComplete && State->Phase != EMCShiftPhase::Won && State->Phase != EMCShiftPhase::Lost)) return;
    if (AMCGameMode* Mode = GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->RestartShift();
}
