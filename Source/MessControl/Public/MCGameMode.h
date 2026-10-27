#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MCDataAssets.h"
#include "MCDevCommands.h"
#include "MCPlayerState.h"
#include "MCGameMode.generated.h"
class UMCDayEvent;
class UMCRunRules;
class AMCTaskActor;
class UMCArenaToothProfile;
class AMCToothCharacter;
class AMCFoodDisposal;
class AMCDayDirector;
class AMCTutorialDirector;
class AMCGameDirector;
class UMCGameDirectorProfile;
class UMCSingleDayProfile;

USTRUCT()
struct FMCEventObjective
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<AActor> Target;
    UPROPERTY() EMCTaskKind Kind=EMCTaskKind::Coffee;
    UPROPERTY() bool bCompleted=false;
};

UCLASS()
class MESSCONTROL_API AMCGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMCGameMode();
    void FinalizeRunLoadout();
    virtual void BeginPlay() override;
    virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer,const FTransform& SpawnTransform) override;
    virtual void FinishRestartPlayer(AController* NewPlayer,const FRotator& StartRotation) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    void ResolveTask(AMCTaskActor* Task, AMCToothCharacter* Worker=nullptr);
    void AwardTask(AMCToothCharacter* Worker, EMCScoreTask Kind);
    void AwardTaskToPlayerState(AMCPlayerState* Worker, EMCScoreTask Kind);
    /** One shared reward for a completed objective, independent of per-item score awards. */
    void NotifyObjectiveCompleted(FName CompletionId);
    /** Accept a shared completion once per run, crediting each current human participant's personal wallet.
     * Returns true when the ID is newly accepted, even if wallets are full or rewards are disabled. */
    bool AwardObjectiveCoinsOnce(FName CompletionId);
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Economy",meta=(ClampMin="0",ClampMax="1000")) int32 ObjectiveCoinReward=10;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Score") FMCScoreRewards ScoreRewards;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Respawn", meta=(ClampMin="0.1")) float RespawnDelay=5.f;
    /** Initial arrival only: clearance below the capsule. Low ceilings shorten the drop. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arrival", meta=(ClampMin="30",ClampMax="800",Units="cm")) float ArenaArrivalHeight=420.f;
    void PlayerDied(AMCToothCharacter* Hero);
    void UpdateObjectives();
    void ProcessRespawns();
    UFUNCTION(BlueprintCallable, Category="Shift") void RestartShift();
    const FGuid& GetHubBoosterRunId() const { return HubBoosterRunId; }
    /** Start a fresh diagnostic run; single-day bot tests skip the human tutorial once. */
    void RestartShiftForPlaytest(int32 Seed);
    /** Release the first personal perk only after the diagnostic team has been spawned. */
    void BeginPlaytestSequence();
    /** Human players and allied test controllers count alike; spectators do not. */
    int32 GetGameplayParticipantCount() const;
    static bool IsGameplayParticipant(const AController* Controller);
    void StartLobby(APlayerController* Requester);
    void FinishTutorial();
    bool CanUseDevPanel(const APlayerController* Requester) const;
    FText ExecuteDevAction(APlayerController* Requester,EMCDevAction Action,int32 StepIndex=INDEX_NONE);
    UPROPERTY(EditDefaultsOnly, Category="Shift") TArray<TObjectPtr<UMCDayEvent>> EventPool;
    UPROPERTY(EditDefaultsOnly, Category="Shift") TSubclassOf<AMCTaskActor> TaskClass;
    // Read only when starting/restarting a run; editing the DA does not change an active run.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shift") TSoftObjectPtr<UMCRunRules> RunRulesProfile;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shift") TSoftObjectPtr<UMCArenaToothProfile> ArenaToothProfile;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shift") TSoftObjectPtr<class UMCDayPlan> FirstDayPlan;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shift") bool bUseDayOnePlan=true;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Director") bool bUseAdaptiveDirector=true;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Director") TSoftObjectPtr<UMCGameDirectorProfile> DirectorProfile;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Director") TObjectPtr<AMCGameDirector> GameDirector;
    /** Saved game mode enables the new run; existing diagnostic fixtures can opt in explicitly. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sequence") bool bUseSingleDayLoop=false;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sequence") TSoftObjectPtr<UMCSingleDayProfile> SingleDayProfile;
    UPROPERTY() TObjectPtr<AMCDayDirector> DayDirector;
    UPROPERTY() TObjectPtr<AMCTutorialDirector> TutorialDirector;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Roguelike") TObjectPtr<class AMCRoguelikeDirector> RoguelikeDirector;
private:
    FGuid HubBoosterRunId;
    FGuid CompletedLoadoutRunId;
    TSet<FName> CoinRewardedObjectives;
    bool bTutorialRequested=false;
    bool bAutoStartHubRun=false;
    bool bLobbyRequested=false;
    bool bRestartingForPlaytest=false;
    TOptional<int32> NextPlaytestSeed;
    void BeginSingleDaySequence();
    void StartDay();
    void FinishDay(bool bTimedOut);
    void ClearTasks();
    void ReleaseReadyArenaArrivals();
    bool HasLivingPlayers() const;
    FRandomStream Random;
    int32 PreviousEvent = INDEX_NONE;
    UPROPERTY() TArray<TObjectPtr<AMCTaskActor>> ActiveTasks;
    UPROPERTY() TArray<FMCEventObjective> Objectives;
    UPROPERTY() TArray<TObjectPtr<AMCToothCharacter>> PendingRespawns;
    TArray<TWeakObjectPtr<AMCToothCharacter>> PendingArenaArrivals;
    /** Failed initial spawns retry only behind the existing startup barrier. */
    double NextInitialSpawnRetryAt=0;
    TMap<TWeakObjectPtr<AController>,FTransform> FailedInitialSpawnTransforms;
    UPROPERTY() TObjectPtr<AMCFoodDisposal> Throat;
};
