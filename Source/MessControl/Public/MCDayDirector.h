#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCDayPlan.h"
#include "MCDayDirector.generated.h"
class AMCCoffeeFlood;
class AMCColdColaEvent;
class AMCFoodActor;
class AMCGameState;
UCLASS()
class MESSCONTROL_API AMCDayDirector : public AActor
{
    GENERATED_BODY()
public:
    AMCDayDirector();
    void Start(UMCDayPlan* Plan,int32 InitialStep=0,bool bManual=false);
    virtual void Tick(float Dt) override;
    void Next(bool bFailed=false);
    int32 CountDirt() const;
    int32 CountFood(int32 Batch) const;
    void DropBrushes(); // Legacy developer action; permanent inventory tools are never spawned.
    AMCFoodActor* SpawnMenuFood(FVector Position,int32 Batch);
    // Random event drops use the shared tongue zones; explicit placed food keeps its position.
    AMCFoodActor* SpawnMenuFoodDrop(float Height,int32 Batch,bool bHeightFromSurface=false);
    AMCFoodActor* SpawnMenuFoodEntry(int32 Batch);
    void DirtyMouth(bool bCoffee);
    UPROPERTY() TObjectPtr<UMCDayPlan> Settings;
    UPROPERTY() TObjectPtr<AMCCoffeeFlood> Flood;
    UPROPERTY() TObjectPtr<AMCColdColaEvent> ColdCola;
    int32 RainSpawned=0;
private:
    void EnterStep();
    AMCFoodActor* SpawnMenuFoodInternal(FVector Position,int32 Batch,bool bRandomDrop,bool bHeightFromSurface=false,bool bMouthEntry=false);
    FRandomStream Random;
    double StepStartedAt=0;
};
