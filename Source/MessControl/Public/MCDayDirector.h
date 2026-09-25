#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCDayPlan.h"
#include "MCDayDirector.generated.h"
class AMCCoffeeFlood;
class AMCFoodActor;
class AMCGameState;
UCLASS()
class MESSCONTROL_API AMCDayDirector : public AActor
{
    GENERATED_BODY()
public:
    AMCDayDirector();
    void Start(UMCDayPlan* Plan);
    virtual void Tick(float Dt) override;
    void Next(bool bFailed=false);
    int32 CountDirt() const;
    int32 CountFood(int32 Batch) const;
    void DropBrushes();
    UPROPERTY() TObjectPtr<UMCDayPlan> Settings;
    UPROPERTY() TObjectPtr<AMCCoffeeFlood> Flood;
    int32 RainSpawned=0;
private:
    void EnterStep();
    void DirtyMouth(bool bCoffee);
    AMCFoodActor* SpawnMenuFood(FVector Position,int32 Batch);
    FRandomStream Random;
    double StepStartedAt=0;
};
