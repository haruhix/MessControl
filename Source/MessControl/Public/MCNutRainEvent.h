#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCDayPlan.h"
#include "MCNutRainProfile.h"
#include "MCNutRainEvent.generated.h"

class AMCFoodActor;
class AMCNutEnemy;
class AMCTongue;

UENUM(BlueprintType)
enum class EMCNutRainStage : uint8 { Idle, Rainfall, Settling, Enemies, Complete };

/** Rain is timed; the hostile aftermath ends only when every awakened nut is defeated. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCNutRainEvent : public AActor
{
    GENERATED_BODY()
public:
    AMCNutRainEvent();
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Start(UMCDayPlan* Plan,UMCNutRainProfile* Profile=nullptr);
    bool IsComplete() const { return Stage==EMCNutRainStage::Complete && !bFailed; }
    /** Explicit cancellation/reset cleans this event's nuts and all their fragments. */
    void Stop();
    UPROPERTY(Replicated, BlueprintReadOnly) EMCNutRainStage Stage=EMCNutRainStage::Idle;
    UPROPERTY(Replicated, BlueprintReadOnly) double StageStartedAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 NutsSpawned=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 EnemiesLeft=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 RainTargetCount=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 EnemyTargetCount=0;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bFailed=false;
    UPROPERTY(Replicated, BlueprintReadOnly) FMCNutRainSettings Settings;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<TObjectPtr<AMCFoodActor>> Nuts;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<TObjectPtr<AMCNutEnemy>> Enemies;
private:
    void Enter(EMCNutRainStage Next);
    void SpawnNut();
    void AwakenLastNuts();
    UPROPERTY() TObjectPtr<UMCNutRainProfile> ActiveProfile;
    UPROPERTY() TObjectPtr<AMCTongue> Tongue;
    UPROPERTY() FMCFoodRow NutRow;
    FName NutRowName=TEXT("Walnut");
    FRandomStream Random;
    int32 DropsAttempted=0,FoodBatch=0;
};
