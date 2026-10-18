#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MCDevCommands.h"
#include "MCPresentationTypes.h"
#include "MCPerkTypes.h"
#include "Components/SlateWrapperTypes.h"
#include "MCPlayerCameraComponent.h"
#include "MCPlayerController.generated.h"
class UMCPrototypeWidget;
class UMCDevPanelWidget;
class UMCEmoteWidget;
class UMCScoreboardWidget;
class AMCToothCharacter;
class AMCRewardChest;
class UMCPerkChoiceWidget;
class UMCBossHealthWidget;
class AMCBossCharacter;
class AMCBossIntro;
class UMCMainMenuWidget;
class UMCTutorialWidget;
class AMCTutorialDirector;
class UMCGameplayHUD;
class UMCSoundPalette;
class UMCSceneAudioComponent;
class UMCGameplayMenuWidget;

UCLASS()
class MESSCONTROL_API AMCPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio") TObjectPtr<UMCSoundPalette> BackgroundSoundPalette;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Audio") TObjectPtr<UMCSceneAudioComponent> SceneAudio;
    /** A custom context such as Hub; None restores automatic gameplay selection. */
    UFUNCTION(BlueprintCallable,Category="Audio") void SetBackgroundAudioContext(FName Context);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio") FName BackgroundAudioContextOverride=NAME_None;
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    bool IsUsingGamepad() const { return bUsingGamepad; }
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult) override;
    virtual void NotifyLoadedWorld(FName WorldPackageName,bool bFinalDest) override;
    virtual void PostSeamlessTravel() override;
    UFUNCTION(BlueprintCallable,Category="Debug") void ToggleDevPanel();
    void ToggleEmotes();
    bool CanUseDevPanel() const;
    UFUNCTION(BlueprintCallable, Category="Debug") void RequestDevAction(EMCDevAction Action,int32 StepIndex=-1);
    void ToggleTuning();
    void ToggleConnection();
    void UpdateInputMode();
    void TogglePauseMenu();
    UFUNCTION(BlueprintCallable, Category="Front End") void ReturnToMainMenu();
    UFUNCTION(Server, Reliable) void ServerStartLobby();
    UFUNCTION(Server, Reliable) void ServerSetTutorialReady(bool bReady);
    bool HasAcknowledgedGameplay() const { return bGameplayLoaded; }
    void ShowScoreboard();
    void HideScoreboard();
    void NextSpectator();
    void PreviousSpectator();
    UFUNCTION(BlueprintPure, Category="Spectating") bool IsSpectating() const { return bDeathSpectating; }
    UFUNCTION(BlueprintPure, Category="Spectating") AMCToothCharacter* GetSpectatorTarget() const { return SpectatorTarget.Get(); }
    UFUNCTION(BlueprintPure, Category="Spectating") float GetRespawnSecondsRemaining() const;
    UFUNCTION(BlueprintCallable, Category="Spectating") void ApplySpectatorOrbitInput(FVector2D Delta);
    UFUNCTION(BlueprintCallable, Category="Spectating") void ZoomSpectatorCamera(float ScrollDelta);
    /** Local presentation. Use ClientShowGameplayAlert from server event code. */
    UFUNCTION(BlueprintCallable, Category="HUD|Alerts") void QueueGameplayAlert(const FText& Message,float HoldSeconds=3.f,FName EventId=NAME_None);
    UFUNCTION(Client,Reliable) void ClientShowGameplayAlert(const FText& Message,float HoldSeconds,FName EventId);
    void RegisterGameplayHUD(UMCGameplayHUD* HUD);
    void UnregisterGameplayHUD(UMCGameplayHUD* HUD);
    UFUNCTION(Server, Reliable) void ServerSendAlarm(EMCPlayerAlarm Alarm);
    UFUNCTION(Server, Reliable) void ServerSetPlayerColor(FLinearColor Color);
    UFUNCTION(BlueprintCallable, Category="Multiplayer") void HostGame();
    UFUNCTION(BlueprintCallable, Category="Multiplayer") void JoinGame(const FString& Address);
    UFUNCTION(BlueprintCallable, Category="Shift") void RequestRestart();
    UFUNCTION(Client, Reliable) void ClientShowRewardOpening(AMCRewardChest* Chest, double ServerEndsAt);
    UFUNCTION(Client, Reliable) void ClientShowPerkChoices(AMCRewardChest* Chest, const TArray<FName>& IDs, EMCPerkPolarity Polarity);
    UFUNCTION(Client,Reliable) void ClientShowRogueChoices(AMCRewardChest* Chest,const FMCRogueChestOffer& Offer);
    UFUNCTION(Server,Reliable) void ServerConfirmRogueReward(AMCRewardChest* Chest,bool Accept);
    UFUNCTION(Server, Reliable) void ServerChooseRewardPerk(AMCRewardChest* Chest, int32 ChoiceIndex);
    UFUNCTION(Client, Reliable) void ClientClosePerkChoices(AMCRewardChest* Chest);
    UFUNCTION(BlueprintCallable, Category="Rewards") void ChooseRewardPerk(int32 ChoiceIndex);
    void RerollLevelPerks();
    void ActivateRogueBooster();
    UFUNCTION(Server,Reliable) void ServerActivateRogueBooster();
    void ConfirmRogueReward(bool Accept);
    UFUNCTION(Server,Reliable) void ServerRerollLevelPerks(FGuid OfferId);
    /** Reconstructs a durable owner-only level offer without requiring a chest or pawn. */
    void RefreshLevelPerkChoices();
    UFUNCTION(Server, Reliable) void ServerChooseLevelPerk(FGuid OfferId,int32 ChoiceIndex);
    UFUNCTION(Client, Reliable) void ClientLevelPerkChoiceResolved(FGuid OfferId,bool bAccepted);
    UFUNCTION(BlueprintPure, Category="Rewards") bool IsRewardMenuOpen() const;
    /** An open choice panel pauses the shared world until every chooser is done. */
    UFUNCTION(Server, Reliable) void ServerSetPerkChoicePause(bool bOpen);
    /** Server snapshot contains counts only; personal offer cards stay owner-only. */
    UFUNCTION(Client, Reliable) void ClientPerkChoiceTeamState(int32 Ready,int32 Total,bool bActive,bool bNeedsChoice);
    void RefreshPerkChoiceTeamState();
    UFUNCTION(BlueprintPure,Category="Rewards") int32 GetPerkChoiceReadyPlayers() const { return PerkChoiceReadyPlayers; }
    UFUNCTION(BlueprintPure,Category="Rewards") int32 GetPerkChoiceTotalPlayers() const { return PerkChoiceTotalPlayers; }
    UFUNCTION(BlueprintPure,Category="Rewards") bool IsWaitingForPerkTeam() const;
    bool IsRewardInteractionActive() const;
    UFUNCTION(Client, Reliable) void ClientPlayBossIntro(AMCBossCharacter* Boss);
    UFUNCTION(BlueprintPure, Category="Boss") bool IsBossIntroPlaying() const;
    void SetBossIntroGuard(AMCBossCharacter* Boss);
    void BeginBossIntroPresentation();
    void FinishBossIntroPresentation();
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UMCPrototypeWidget> PrototypeWidget;
    UPROPERTY() TObjectPtr<UMCDevPanelWidget> DevPanel;
    UPROPERTY() TObjectPtr<UMCEmoteWidget> EmoteWidget;
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UMCScoreboardWidget> ScoreboardWidget;
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UMCPerkChoiceWidget> PerkChoiceWidget;
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UMCBossHealthWidget> BossHealthWidget;
private:
    bool bUsingGamepad=false;
    void ResetWorldPresentation();
    void RefreshFrontEnd();
    UFUNCTION(Server, Reliable) void ServerGameplayLoaded();
    UFUNCTION(Server, Reliable) void ServerTutorialLoaded();
    UPROPERTY(Transient) TObjectPtr<UMCMainMenuWidget> FrontEndWidget;
    UPROPERTY(Transient) TObjectPtr<UMCGameplayMenuWidget> PauseWidget;
    UPROPERTY(Transient) TObjectPtr<UMCGameplayMenuWidget> ConnectionWidget;
    bool bConnectionMenuOpen=false;
    UMCGameplayMenuWidget* EnsureGameplayMenu(bool bConnection);
    void CloseGameplayMenus();
    UFUNCTION() void ResumeGameplayMenu();
    UFUNCTION() void OpenMenuEmotes();
    UFUNCTION() void CloseConnectionMenu();
    UFUNCTION() void ExitFromGameplayMenu();
    UFUNCTION() void JoinFromGameplayMenu(const FString& Address);
    UPROPERTY(Transient) TObjectPtr<UMCTutorialWidget> TutorialWidget;
    TWeakObjectPtr<AMCTutorialDirector> AcknowledgedTutorial;
    double NextFrontEndCheck=0;
    bool bGameplayLoaded=false;
    bool bGameplayLoadAckSent=false;
    bool bPauseMenuOpen=false;
    bool bLobbyUIOpen=false;
    bool bTutorialMenuInput=false;
    bool bFrontEndHidPrototype=false;
    void RefreshBossHUD();
    UPROPERTY(Transient) TObjectPtr<AMCBossIntro> BossIntro;
    TWeakObjectPtr<AMCBossCharacter> HealthBoss;
    TWeakObjectPtr<AMCBossCharacter> GuardedIntroBoss;
    int32 GuardedIntroSerial=0;
    FTimerHandle BossHUDTimer;
    double NextBossScan=0;
    double BossDiedAt=-1;
    bool bBossIntroPlaying=false;
    ESlateVisibility PreviousPrototypeVisibility=ESlateVisibility::Visible;
    bool PrepareRewardUI(AMCRewardChest* Chest);
    bool PrepareRewardInteraction(AMCRewardChest* Chest);
    void CloseRewardUI(bool bRestoreInput=true);
    void CheckRewardUI();
    void UpdatePerkChoicePause();
    bool HasAuthoritativePerkChoice() const;
    bool IsPerkChoiceParticipant() const;
    void RefreshPerkChoiceWorld();
    void RefreshPerkChoiceWaiting();
    bool CanUnpausePerkChoice();
    bool bPerkChoicePauseSent=false;
    bool bPerkChoicePauseRequested=false;
    bool bPerkChoiceEnding=false;
    bool bPerkChoiceTeamActive=false,bPerkChoiceNeedsChoice=false;
    int32 PerkChoiceReadyPlayers=0,PerkChoiceTotalPlayers=0;
    int32 LastSentPerkReady=-1,LastSentPerkTotal=-1;
    bool bLastSentPerkActive=false,bLastSentPerkNeedsChoice=false;
    double NextPerkTeamCheck=0;
    /** Pause delegates belong to a GameMode/world, unlike a retained controller. */
    UPROPERTY(Transient) TWeakObjectPtr<UWorld> PerkChoiceWorld;
    TWeakObjectPtr<AMCRewardChest> ActiveRewardChest;
    TWeakObjectPtr<APawn> RewardPawn;
    FTimerHandle RewardUITimer;
    double RewardShownAt=0;
    bool bRewardChoicePending=false;
    FGuid ActiveLevelOfferId;
    FGuid AcceptedLevelOfferId;
    double NextLevelChoiceCheck=0;
    void UpdateDeathSpectating();
    void CycleSpectator(int32 Direction);
    TWeakObjectPtr<AMCToothCharacter> SpectatorTarget;
    TWeakObjectPtr<APawn> PreviousPawn;
    bool bDeathSpectating=false;
    FRotator SpectatorOrbit=FRotator(-30,0,0);
    float SpectatorDistance=1000.f;
    bool bSpectatorOrbitInitialized=false;
    FMCSpectatorCameraViewState SpectatorCameraView;
    UPROPERTY(Transient) TObjectPtr<UMCGameplayHUD> GameplayHUD;
    double NextSpectatorCheck=0;
    double NextAlarmAt=0;
    double NextColorAt=0;
    double NextBackgroundAudioAt=0;
    void RefreshBackgroundAudio();
    UFUNCTION(Server, Reliable) void ServerRestartShift();
};
