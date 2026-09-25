#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MCDataAssets.h"
#include "MCRunRules.h"
#include "MCDayPlan.h"
#include "MCGameState.generated.h"
class AMCArenaTooth;

UCLASS()
class MESSCONTROL_API AMCGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    // Snapshot for this run. Clients display the server's settings, not their local asset.
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") FMCRunSettings RunSettings;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") int32 Day = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") EMCShiftPhase Phase = EMCShiftPhase::Intermission;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") float MouthHealth = 100.f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") int32 TasksLeft = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") int32 TasksTotal = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") double PhaseEndsAt = 0.;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") TObjectPtr<UMCDayEvent> CurrentEvent;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") int32 RunSeed = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Day Plan") TObjectPtr<UMCDayPlan> DayPlan;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Day Plan") int32 StepIndex=INDEX_NONE;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Day Plan") bool bPhysicalBrushes=false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Day Plan") bool bDayOneComplete=false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Day Plan") double DayStartedAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Day Plan") int32 FailedEvents=0;
    // Dev sandbox suppresses deadlines/transitions, but physics and damage keep running.
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Development") bool bDevManualEvents=false;
    // Concrete teeth, not a separately decremented lives counter. Starting players are excluded.
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Arena") TArray<TObjectPtr<AMCArenaTooth>> ArenaTeeth;
    UFUNCTION(BlueprintPure, Category="Arena") int32 AvailableArenaTeeth() const;
    UFUNCTION(BlueprintPure, Category="Shift") float SecondsLeft() const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
