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
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    void ResolveTask(AMCTaskActor* Task, AMCToothCharacter* Worker=nullptr);
    void AwardTask(AMCToothCharacter* Worker, EMCScoreTask Kind);
    void AwardTaskToPlayerState(AMCPlayerState* Worker, EMCScoreTask Kind);
    /** One shared reward for a completed objective, independent of per-item score awards. */
    void NotifyObjectiveCompleted(FName CompletionId);
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Score") FMCScoreRewards ScoreRewards;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Respawn", meta=(ClampMin="0.1")) float RespawnDelay=5.f;
    void PlayerDied(AMCToothCharacter* Hero);
    void UpdateObjectives();
    void ProcessRespawns();
    UFUNCTION(BlueprintCallable, Category="Shift") void RestartShift();
    /** Start a fresh diagnostic run with the same event seed used by the bot decisions. */
    void RestartShiftForPlaytest(int32 Seed);
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
    UPROPERTY() TObjectPtr<AMCDayDirector> DayDirector;
    UPROPERTY() TObjectPtr<AMCTutorialDirector> TutorialDirector;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Roguelike") TObjectPtr<class AMCRoguelikeDirector> RoguelikeDirector;
private:
    bool bTutorialRequested=false;
    bool bLobbyRequested=false;
    TOptional<int32> NextPlaytestSeed;
    void StartDay();
    void FinishDay(bool bTimedOut);
    void ClearTasks();
    bool HasLivingPlayers() const;
    FRandomStream Random;
    int32 PreviousEvent = INDEX_NONE;
    UPROPERTY() TArray<TObjectPtr<AMCTaskActor>> ActiveTasks;
    UPROPERTY() TArray<FMCEventObjective> Objectives;
    UPROPERTY() TArray<TObjectPtr<AMCToothCharacter>> PendingRespawns;
    UPROPERTY() TObjectPtr<AMCFoodDisposal> Throat;
};
