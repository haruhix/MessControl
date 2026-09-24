#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MCDataAssets.h"
#include "MCGameState.generated.h"

UCLASS()
class MESSCONTROL_API AMCGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") int32 Day = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") EMCShiftPhase Phase = EMCShiftPhase::Intermission;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") float MouthHealth = 100.f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") int32 TasksLeft = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") int32 TasksTotal = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") double PhaseEndsAt = 0.;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") TObjectPtr<UMCDayEvent> CurrentEvent;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Shift") int32 RunSeed = 0;
    UFUNCTION(BlueprintPure, Category="Shift") float SecondsLeft() const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
