#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MCDataAssets.h"
#include "MCDevCommands.h"
#include "MCGameMode.generated.h"
class UMCDayEvent;
class UMCRunRules;
class AMCTaskActor;
class UMCArenaToothProfile;
class AMCToothCharacter;
class AMCFoodDisposal;
class AMCDayDirector;

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
    void ResolveTask(AMCTaskActor* Task);
    void PlayerDied(AMCToothCharacter* Hero);
    void UpdateObjectives();
    void ProcessRespawns();
    UFUNCTION(BlueprintCallable, Category="Shift") void RestartShift();
    bool CanUseDevPanel(const APlayerController* Requester) const;
    FText ExecuteDevAction(APlayerController* Requester,EMCDevAction Action,int32 StepIndex=INDEX_NONE);
    UPROPERTY(EditDefaultsOnly, Category="Shift") TArray<TObjectPtr<UMCDayEvent>> EventPool;
    UPROPERTY(EditDefaultsOnly, Category="Shift") TSubclassOf<AMCTaskActor> TaskClass;
    // Read only when starting/restarting a run; editing the DA does not change an active run.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shift") TSoftObjectPtr<UMCRunRules> RunRulesProfile;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shift") TSoftObjectPtr<UMCArenaToothProfile> ArenaToothProfile;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shift") TSoftObjectPtr<class UMCDayPlan> FirstDayPlan;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shift") bool bUseDayOnePlan=true;
    UPROPERTY() TObjectPtr<AMCDayDirector> DayDirector;
private:
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
