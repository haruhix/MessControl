#pragma once
#include "CoreMinimal.h"
#include "MCGameDirectorTypes.generated.h"

UENUM(BlueprintType)
enum class EMCGameDirectorEvent : uint8
{
    Food, Coffee, CoffeeFlood, ColdCola, Yawn, Pepper, StuckFood, LooseTooth, Reward, Boss
};

UENUM(BlueprintType)
enum class EMCGameDirectorPacing : uint8 { Build, Drain, Rest, FinalCleanup, Intermission };

/** Admission score, including reasons for a zero probability. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCGameDirectorCandidate
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) EMCGameDirectorEvent Kind=EMCGameDirectorEvent::Food;
    UPROPERTY(BlueprintReadOnly) float BaseWeight=0;
    UPROPERTY(BlueprintReadOnly) float EffectiveWeight=0;
    UPROPERTY(BlueprintReadOnly) float Probability=0;
    UPROPERTY(BlueprintReadOnly) float ForecastPressure=0;
    UPROPERTY(BlueprintReadOnly) FString BlockReason;
};

/** Server observation and decisions, replicated together for HUD and diagnostics. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCGameDirectorState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bEnabled=false;
    UPROPERTY(BlueprintReadOnly) EMCGameDirectorPacing Pacing=EMCGameDirectorPacing::Intermission;
    UPROPERTY(BlueprintReadOnly) float Pressure=0;
    UPROPERTY(BlueprintReadOnly) float PressureLimit=0;
    UPROPERTY(BlueprintReadOnly) float WorkSeconds=0;
    UPROPERTY(BlueprintReadOnly) float TargetPressure=0;
    UPROPERTY(BlueprintReadOnly) float Difficulty=0;
    UPROPERTY(BlueprintReadOnly) float Throughput=1;
    UPROPERTY(BlueprintReadOnly) float TeamHealth=1;
    UPROPERTY(BlueprintReadOnly) float TeamStamina=1;
    UPROPERTY(BlueprintReadOnly) float Stress=0;
    UPROPERTY(BlueprintReadOnly) float DayProgress=0;
    UPROPERTY(BlueprintReadOnly) float CompletionProgress=0;
    UPROPERTY(BlueprintReadOnly) float WaitWeight=0;
    UPROPERTY(BlueprintReadOnly) float WaitProbability=0;
    UPROPERTY(BlueprintReadOnly) bool bNextReserved=false;
    UPROPERTY(BlueprintReadOnly) TArray<FMCGameDirectorCandidate> Candidates;
    UPROPERTY(BlueprintReadOnly) int32 QueuedEvents=0;
    UPROPERTY(BlueprintReadOnly) int32 PlannedFood=0;
    UPROPERTY(BlueprintReadOnly) int32 SpawnedFood=0;
    UPROPERTY(BlueprintReadOnly) int32 FinishedFood=0;
    UPROPERTY(BlueprintReadOnly) int32 WholeFood=0;
    UPROPERTY(BlueprintReadOnly) int32 Fragments=0;
    UPROPERTY(BlueprintReadOnly) int32 CarriedFood=0;
    UPROPERTY(BlueprintReadOnly) int32 ThroatQueued=0;
    UPROPERTY(BlueprintReadOnly) int32 CleaningTasks=0;
    UPROPERTY(BlueprintReadOnly) int32 Fires=0;
    UPROPERTY(BlueprintReadOnly) int32 Ulcers=0;
    UPROPERTY(BlueprintReadOnly) int32 Ice=0;
    UPROPERTY(BlueprintReadOnly) int32 LivingPlayers=0;
    UPROPERTY(BlueprintReadOnly) int32 AvailablePlayers=0;
    UPROPERTY(BlueprintReadOnly) bool bGlobalMovement=false;
    UPROPERTY(BlueprintReadOnly) bool bUrgent=false;
    UPROPERTY(BlueprintReadOnly) double DayEndAt=0;
    UPROPERTY(BlueprintReadOnly) FString CurrentTitle;
    UPROPERTY(BlueprintReadOnly) FString Instruction;
    UPROPERTY(BlueprintReadOnly) FString NextTitle;
    UPROPERTY(BlueprintReadOnly) FString DecisionReason;
    UPROPERTY(BlueprintReadOnly) FString LastDecision;
    UPROPERTY(BlueprintReadOnly) TArray<FString> DecisionLog;
};
