#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MCDevCommands.h"
#include "MCPresentationTypes.h"
#include "MCPerkTypes.h"
#include "MCPlayerController.generated.h"
class UMCPrototypeWidget;
class UMCDevPanelWidget;
class UMCEmoteWidget;
class UMCScoreboardWidget;
class AMCToothCharacter;
class AMCRewardChest;
class UMCPerkChoiceWidget;

UCLASS()
class MESSCONTROL_API AMCPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void ToggleDevPanel();
    void ToggleEmotes();
    bool CanUseDevPanel() const;
    void RequestDevAction(EMCDevAction Action,int32 StepIndex=INDEX_NONE);
    void ToggleTuning();
    void ToggleConnection();
    void UpdateInputMode();
    void ShowScoreboard();
    void HideScoreboard();
    void NextSpectator();
    void PreviousSpectator();
    UFUNCTION(BlueprintPure, Category="Spectating") bool IsSpectating() const { return bDeathSpectating; }
    UFUNCTION(BlueprintPure, Category="Spectating") AMCToothCharacter* GetSpectatorTarget() const { return SpectatorTarget.Get(); }
    UFUNCTION(BlueprintPure, Category="Spectating") float GetRespawnSecondsRemaining() const;
    UFUNCTION(Server, Reliable) void ServerSendAlarm(EMCPlayerAlarm Alarm);
    UFUNCTION(Server, Reliable) void ServerSetPlayerColor(FLinearColor Color);
    UFUNCTION(BlueprintCallable, Category="Multiplayer") void HostGame();
    UFUNCTION(BlueprintCallable, Category="Multiplayer") void JoinGame(const FString& Address);
    UFUNCTION(BlueprintCallable, Category="Shift") void RequestRestart();
    UFUNCTION(Client, Reliable) void ClientShowRewardOpening(AMCRewardChest* Chest, double ServerEndsAt);
    UFUNCTION(Client, Reliable) void ClientShowPerkChoices(AMCRewardChest* Chest, const TArray<FName>& IDs, EMCPerkPolarity Polarity);
    UFUNCTION(Server, Reliable) void ServerChooseRewardPerk(AMCRewardChest* Chest, int32 ChoiceIndex);
    UFUNCTION(Client, Reliable) void ClientClosePerkChoices(AMCRewardChest* Chest);
    UFUNCTION(BlueprintCallable, Category="Rewards") void ChooseRewardPerk(int32 ChoiceIndex);
    UFUNCTION(BlueprintPure, Category="Rewards") bool IsRewardMenuOpen() const;
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UMCPrototypeWidget> PrototypeWidget;
    UPROPERTY() TObjectPtr<UMCDevPanelWidget> DevPanel;
    UPROPERTY() TObjectPtr<UMCEmoteWidget> EmoteWidget;
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UMCScoreboardWidget> ScoreboardWidget;
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UMCPerkChoiceWidget> PerkChoiceWidget;
private:
    bool PrepareRewardUI(AMCRewardChest* Chest);
    void CloseRewardUI(bool bRestoreInput=true);
    void CheckRewardUI();
    TWeakObjectPtr<AMCRewardChest> ActiveRewardChest;
    TWeakObjectPtr<APawn> RewardPawn;
    FTimerHandle RewardUITimer;
    double RewardShownAt=0;
    bool bRewardChoicePending=false;
    void UpdateDeathSpectating();
    void CycleSpectator(int32 Direction);
    TWeakObjectPtr<AMCToothCharacter> SpectatorTarget;
    TWeakObjectPtr<APawn> PreviousPawn;
    bool bDeathSpectating=false;
    double NextSpectatorCheck=0;
    double NextAlarmAt=0;
    double NextColorAt=0;
    UFUNCTION(Server, Reliable) void ServerRestartShift();
};
