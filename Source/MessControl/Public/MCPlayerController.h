#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MCDevCommands.h"
#include "MCPresentationTypes.h"
#include "MCPerkTypes.h"
#include "MCActiveSkillAction.h"
#include "Components/SlateWrapperTypes.h"
#include "MCPlayerCameraComponent.h"
#include "MCCosmeticCatalog.h"
#include "MCSessionTypes.h"
#include "MCHubEconomyTypes.h"
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
class UMCHubWidget;
class UMCLoadingWidget;
class UMCFishingRewardWidget;
class UMCTaskCoinPresentationComponent;
class UMCHubWardrobeWidget;
class AMCHubWardrobeStation;
class UMCHubShopWidget;
class AMCHubShopStation;
class AMCWardrobePreviewActor;
class AMCGameState;
class AMCHubGameState;

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
    virtual void FlushPressedKeys() override;
    bool IsUsingGamepad() const { return bUsingGamepad; }
    /** Local UMG/input needs an attached LocalPlayer that owns this controller in this world. */
    bool HasLocalPresentationOwner() const;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult) override;
    virtual void NotifyLoadedWorld(FName WorldPackageName,bool bFinalDest) override;
    virtual void PostSeamlessTravel() override;
    UFUNCTION(BlueprintCallable,Category="Debug") void ToggleDevPanel();
    void ToggleEmotes();
    UFUNCTION(BlueprintCallable,Category="Emotes") void BeginEmoteSelection();
    UFUNCTION(BlueprintCallable,Category="Emotes") void EndEmoteSelection();
    UFUNCTION(BlueprintCallable,Category="Emotes") void CloseEmotes(bool bConfirm=false);
    UFUNCTION(BlueprintPure,Category="Emotes") bool IsHoldingEmoteSelection() const { return bEmoteSelectionHeld; }
    bool CanUseDevPanel() const;
    UFUNCTION(BlueprintCallable, Category="Debug") void RequestDevAction(EMCDevAction Action,int32 StepIndex=-1);
    void ToggleTuning();
    void ToggleConnection();
    void UpdateInputMode();
    void TogglePauseMenu();
    UFUNCTION(BlueprintCallable, Category="Front End") void ReturnToMainMenu();
    UFUNCTION(Server, Reliable) void ServerStartLobby();
    UFUNCTION(BlueprintCallable,Category="Hub") void RequestHubOpenSelection();
    UFUNCTION(BlueprintCallable,Category="Hub") void RequestHubSelectDay(int32 Day);
    UFUNCTION(BlueprintCallable,Category="Hub") void RequestHubLaunch();
    UFUNCTION(BlueprintCallable,Category="Hub") void RequestHubReturn();
    UFUNCTION(BlueprintPure,Category="Hub") bool IsHubWorld() const;
    UFUNCTION(Server,Reliable) void ServerHubOpenSelection();
    UFUNCTION(Server,Reliable) void ServerHubSelectDay(int32 Day);
    UFUNCTION(Server,Reliable) void ServerHubLaunch();
    UFUNCTION(Server,Reliable) void ServerHubReturn();
    UFUNCTION(Server,Reliable) void ServerHubSceneLoaded(AMCHubGameState* LoadedHub,int32 Generation);
    UFUNCTION(Server, Reliable) void ServerSetTutorialReady(bool bReady);
    bool HasAcknowledgedGameplay() const;
    /** Server accepts only the current location's preparation receipt after possession. */
    bool AcknowledgeGameplayLoaded(AMCGameState* LoadedState,FGuid LoadId);
    /** Local asset/render preparation; remote server controllers use their client's ACK. */
    UFUNCTION(BlueprintPure,Category="Loading") bool IsWorldPresentationReady() const;
    void ShowScoreboard();
    void HideScoreboard();
    /** Hub uses a toggled party panel; arena scoreboards retain held-Tab behavior. */
    UFUNCTION(BlueprintPure,Category="Hub|Party") bool IsHubPartyPanelOpen() const;
    UFUNCTION(BlueprintCallable,Category="Hub|Party") void RequestHubRoomPrivacy(EMCRoomPrivacy Privacy);
    UFUNCTION(Server,Reliable) void ServerSetHubRoomPrivacy(EMCRoomPrivacy Privacy);
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
    /** A hub spawn/recovery supplies yaw once; later camera input remains entirely local. */
    UFUNCTION(Client, Reliable) void ClientSetHubArrivalView(float Yaw);
    UFUNCTION(BlueprintCallable,Category="Hub|Wardrobe") bool TryOpenWardrobe();
    UFUNCTION(BlueprintPure,Category="Hub|Wardrobe") bool IsWardrobeOpen() const;
    UFUNCTION(BlueprintPure,Category="Hub|Wardrobe") bool IsWardrobeAvailable() const;
    /** Local display and its UI were constructed before releasing the Hub loading barrier. */
    UFUNCTION(BlueprintPure,Category="Hub|Wardrobe") bool HasPreparedWardrobePresentation() const;
    UFUNCTION(BlueprintCallable,Category="Hub|Shop") bool TryOpenHubShop();
    UFUNCTION(BlueprintPure,Category="Hub|Shop") bool IsHubShopOpen() const;
    UFUNCTION(BlueprintPure,Category="Hub|Shop") bool IsHubShopAvailable() const;
    UFUNCTION(BlueprintCallable,Category="Hub|Grill") bool TryInteractHubGrill();
    UFUNCTION(Server,Reliable) void ServerInteractHubGrill(class AMCHubGrillStation* Station);
    UFUNCTION() void PurchaseHubItem(FName ItemID);
    UFUNCTION() void PurchaseHubItemInSlot(FName ItemID,int32 DestinationSlot,FName ExpectedReplacedID,FGuid ExpectedInstanceID);
    UFUNCTION(Server,Reliable) void ServerPurchaseHubItemInSlot(FName ItemID,int32 DestinationSlot,FName ExpectedReplacedID,FGuid ExpectedInstanceID,FGuid ExpectedShopRotationID);
    UFUNCTION(Server,Reliable) void ServerPurchaseHubItem(FName ItemID);
    UFUNCTION(Client,Reliable) void ClientHubTransactionFeedback(const FText& Message);
    /** Authority-confirmed catch receipt; this local presentation never grants currency. */
    UFUNCTION(Client,Reliable) void ClientFishingCatch(EMCHubItemRarity Rarity,int32 FishCoins,int32 WalletAfter);
    /** Cosmetic receipt only. The authority has already credited the personal shop wallet. */
    UFUNCTION(Client,Reliable) void ClientObjectiveCoins(int32 Amount,int32 WalletAfter);
    UFUNCTION() void PreviewWardrobeCosmetics(FMCCharacterCosmeticLoadout Loadout);
    UFUNCTION(Server,Reliable) void ServerEquipHubCosmetics(FMCCharacterCosmeticLoadout Loadout);
    UFUNCTION(BlueprintCallable,Category="Consumables") void UseCalciumGrenade();
    UFUNCTION(Server,Reliable) void ServerUseCalciumGrenade();
    void RefreshHubEconomy();
    double NextHubProfileSaveAt=0;
    UFUNCTION(BlueprintCallable, Category="Multiplayer") void HostGame();
    UFUNCTION(BlueprintCallable, Category="Multiplayer") void JoinGame(const FString& Address);
    UFUNCTION(BlueprintCallable, Category="Shift") void RequestRestart();
    UFUNCTION(Client, Reliable) void ClientShowRewardOpening(AMCRewardChest* Chest, double ServerEndsAt);
    UFUNCTION(Client, Reliable) void ClientShowPerkChoices(AMCRewardChest* Chest, const TArray<FName>& IDs, EMCPerkPolarity Polarity,bool bTeamBooster=false,double StageChoiceEndsAt=0,FGuid OfferID=FGuid());
    UFUNCTION(Client,Reliable) void ClientShowRogueChoices(AMCRewardChest* Chest,const FMCRogueChestOffer& Offer);
    UFUNCTION(Server,Reliable) void ServerConfirmRogueReward(AMCRewardChest* Chest,bool Accept,FGuid OfferID);
    UFUNCTION(Server, Reliable) void ServerChooseRewardPerk(AMCRewardChest* Chest, int32 ChoiceIndex);
    UFUNCTION(Server, Reliable) void ServerStageChoicePresented(AMCRewardChest* Chest,AMCToothCharacter* ChoicePawn);
    UFUNCTION(Client, Reliable) void ClientClosePerkChoices(AMCRewardChest* Chest);
    UFUNCTION(BlueprintCallable, Category="Rewards") void ChooseRewardPerk(int32 ChoiceIndex);
    UFUNCTION(BlueprintCallable,Category="Rewards") void ChooseRewardSlot(int32 SlotIndex);
    UFUNCTION(BlueprintCallable,Category="Rewards") void CancelRewardReplacement();
    UFUNCTION(BlueprintCallable,Category="Rewards") void SkipRewardPerk();
    UFUNCTION(BlueprintCallable,Category="Skills") void ActivateSkillSlot(int32 SlotIndex);
    UFUNCTION(BlueprintCallable,Category="Skills") void BeginSkillInput(int32 SlotIndex);
    UFUNCTION(BlueprintCallable,Category="Skills") void EndSkillInput(int32 SlotIndex,bool bCanceled=false);
    UFUNCTION(BlueprintCallable,Category="Skills") void CancelSkillInputs();
    UFUNCTION(Server,Reliable) void ServerBeginSkillInput(FMCActiveSkillInputHandle Handle);
    UFUNCTION(Server,Reliable) void ServerEndSkillInput(FMCActiveSkillInputHandle Handle,bool bCanceled);
    UFUNCTION(Server,Reliable) void ServerActivateSkillSlot(int32 SlotIndex,FGuid ExpectedInstanceID);
    UFUNCTION(Server,Reliable) void ServerChooseRewardPerkInSlot(AMCRewardChest* Chest,int32 ChoiceIndex,FName ExpectedPerkID,FGuid OfferID,int32 DestinationSlot,FName ExpectedReplacedID,FGuid ExpectedInstanceID);
    UFUNCTION(Server,Reliable) void ServerSkipRewardPerk(AMCRewardChest* Chest,FGuid OfferID);
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
    void EnsureLocalPresentation();
    TWeakObjectPtr<UWorld> LocalPresentationWorld;
    bool bResettingWorldPresentation=false;
    void PrepareWardrobePreview();
    void RefreshWardrobe();
    void FlushPlayerColor();
    TOptional<FLinearColor> PendingPlayerColor;
    void CloseWardrobe(bool bRestoreColor=true);
    UFUNCTION() void PreviewWardrobeColor(FLinearColor Color);
    UFUNCTION() void ApplyWardrobeColor(FLinearColor Color);
    UFUNCTION() void CancelWardrobe();
    UPROPERTY(Transient) TObjectPtr<UMCHubWardrobeWidget> WardrobeWidget;
    UPROPERTY(Transient) TObjectPtr<AMCWardrobePreviewActor> WardrobePreviewActor;
    TSharedPtr<SWidget> WardrobeWarmSlate;
    TSharedPtr<SWidget> HubShopWarmSlate;
    UPROPERTY(Transient) TObjectPtr<UMCHubShopWidget> HubShopWidget;
    TWeakObjectPtr<AMCHubShopStation> NearbyShop;
    UFUNCTION() void CloseHubShop();
    TWeakObjectPtr<AMCHubWardrobeStation> NearbyWardrobe;
    TWeakObjectPtr<AMCToothCharacter> WardrobePawn;
    TWeakObjectPtr<APawn> SavedAppearancePawn;
    TWeakObjectPtr<APawn> HubArrivalViewPawn;
    TOptional<float> HubArrivalYaw;
    double NextWardrobeCheck=0,ApplySavedAppearanceAt=0;
    bool bSavedAppearanceApplied=false;
    float WardrobePreviousYaw=0,WardrobePreviousPitch=-30,WardrobePreviousDistance=1000;
    void RefreshLoadingPresentation();
    void ResetLoadingPresentation();
    void ApplyLoadingInputMode();
    bool NeedsWorldPreparation() const;
    bool IsWaitingForWorldStart() const;
    UPROPERTY(Transient) TObjectPtr<UMCLoadingWidget> LoadingWidget;
    TWeakObjectPtr<UWorld> LoadingPresentationWorld;
    TWeakObjectPtr<APawn> LoadingCancelledPawn;
    bool bLoadingInputBlocked=false;
    bool bLoadingPresentationReleased=false;
    bool bLoadingFailureFocused=false;
    void RefreshHubPresentation(float DeltaSeconds);
    void ResetHubPresentation();
    void RestoreHubGameplayView();
    void HandleHubStartInput();
    UPROPERTY(Transient) TObjectPtr<UMCHubWidget> HubWidget;
    void PrepareFishingRewardPresentation();
    void ResetFishingRewardPresentation();
    UPROPERTY(Transient) TObjectPtr<UMCFishingRewardWidget> FishingRewardWidget;
    void PrepareTaskCoinPresentation();
    void ResetTaskCoinPresentation();
    UPROPERTY(Transient) TObjectPtr<UMCTaskCoinPresentationComponent> TaskCoinPresentation;
    TWeakObjectPtr<AActor> HubSceneCamera;
    TWeakObjectPtr<AActor> HubGameplayViewTarget;
    TWeakObjectPtr<APawn> HubGameplayViewPawn;
    FRotator HubGameplayControlRotation=FRotator::ZeroRotator;
    bool bHubGameplayAutoManageCamera=true;
    bool bHubSceneViewActive=false;
    bool bHubMenuInput=false;
    bool bHubStagePrerequisitesAdded=false;
    double NextHubLoadAckAt=0;
    int32 LastHubLoadAckGeneration=INDEX_NONE;
    bool bUsingGamepad=false;
    bool CanOpenEmotes() const;
    void OpenEmotes(bool bHeld);
    void MaintainEmoteSelection();
    bool bEmoteSelectionHeld=false;
    TWeakObjectPtr<APawn> EmoteSelectionPawn;
    void HandleScoreboardPressed();
    void HandleScoreboardReleased();
    void PrepareHubPartyPresentation();
    void ResetWorldPresentation();
    void RefreshFrontEnd();
    UFUNCTION(Server, Reliable) void ServerGameplayLoaded(AMCGameState* LoadedState,FGuid LoadId);
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
    UPROPERTY(Replicated) FGuid AcknowledgedGameplayLoadId;
    double NextGameplayLoadAckAt=0;
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
    void RepublishRewardOffer(AMCRewardChest* Chest);
    FGuid ActiveRewardOfferID;
    TArray<FName> ActiveRewardIDs;
    TArray<FMCActiveSkillSlot> RewardReplacementSlots;
    TMap<int32,FMCActiveSkillInputHandle> HeldSkillInputs;
    uint32 NextSkillPressSequence=0;
    uint32 LastServerSkillPressSequence=0;
    int32 PendingRewardChoice=INDEX_NONE;
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
