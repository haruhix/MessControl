#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MCGameMode.generated.h"
class UMCDayEvent;
class AMCTaskActor;

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
    UFUNCTION(BlueprintCallable, Category="Shift") void RestartShift();
    UPROPERTY(EditDefaultsOnly, Category="Shift") TArray<TObjectPtr<UMCDayEvent>> EventPool;
    UPROPERTY(EditDefaultsOnly, Category="Shift") TSubclassOf<AMCTaskActor> TaskClass;
    UPROPERTY(EditDefaultsOnly, Category="Shift", meta=(ClampMin="1")) int32 DaysToSurvive = 7;
private:
    void StartDay();
    void FinishDay(bool bTimedOut);
    void ClearTasks();
    FRandomStream Random;
    int32 PreviousEvent = INDEX_NONE;
    UPROPERTY() TArray<TObjectPtr<AMCTaskActor>> ActiveTasks;
};
