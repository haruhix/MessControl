#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPlaytestBotTypes.h"
#include "MCPlaytestSession.generated.h"

class AController;
class APlayerController;
class AMCPlaytestBotController;
class AMCToothCharacter;
class AMCGameMode;

struct FMCPlaytestObserver
{
    TWeakObjectPtr<APlayerController> Controller;
    FName PreviousState;
    bool bWasSpectator=false;
    bool bWasOnlySpectator=false;
};

/** Server-only diagnostic run. Bots use normal pawns, care actions and reserve-tooth respawns. */
UCLASS(Transient, NotBlueprintable)
class MESSCONTROL_API AMCPlaytestSession : public AActor
{
    GENERATED_BODY()
public:
    AMCPlaytestSession();
    static AMCPlaytestSession* Find(UWorld* World);
    bool StartSession(int32 Count, EMCPlaytestBotSkill Skill, bool bObserve, int32 Seed, FString& Error);
    void StopSession();
    void PrintReport();
    bool IsActive() const { return bActive; }
    bool IsObserver(const AController* Controller) const;
    void RecordDeath(AMCToothCharacter* Hero);
    const FString& GetReportPath() const { return ReportPath; }
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    bool SpawnBot(AMCGameMode* Mode, int32 Index, FString& Error);
    void CleanupParticipants(bool bRestoreObservers);
    void WriteSnapshot(const FString& Outcome);
    void FlushReport();
    FString CurrentOutcome() const;
    TArray<TWeakObjectPtr<AMCPlaytestBotController>> Bots;
    TArray<int32> BotDeaths;
    TArray<FMCPlaytestObserver> Observers;
    TArray<FString> PendingRows;
    FString ReportPath;
    FString TerminalOutcome;
    EMCPlaytestBotSkill BotSkill=EMCPlaytestBotSkill::Regular;
    int32 DecisionSeed=0;
    int32 InitialHumans=0;
    int32 InitialTeamSize=0;
    double StartedAt=0;
    double NextSnapshotAt=0;
    double NextFlushAt=0;
    bool bActive=false;
    bool bObservation=false;
    bool bLoggedWriteFailure=false;
};
