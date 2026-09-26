#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCDayOneScenario.generated.h"
class AMCToothCharacter;
class AMCFoodActor;
class AMCCoffeeFlood;
class AMCMouthSurface;
class ACameraActor;
/** Opt-in integration check, never spawned by an ordinary play session. */
UCLASS()
class AMCDayOneScenario : public AActor
{
    GENERATED_BODY()
public:
    AMCDayOneScenario();
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) int32 Stage=0;
    UPROPERTY(Replicated) double StageAt=0;
    UPROPERTY(Replicated) TArray<TObjectPtr<AMCToothCharacter>> Heroes;
    UPROPERTY(Replicated) TArray<TObjectPtr<AMCMouthSurface>> Patches;
    UPROPERTY(Replicated) TObjectPtr<AMCFoodActor> Food;
    UPROPERTY(Replicated) TObjectPtr<AMCCoffeeFlood> Flood;
    UPROPERTY(Replicated) bool bFailed=false;
private:
    void Next();
    void Move(int32 Slot,FVector P,FRotator R=FRotator::ZeroRotator);
    float Age=0,LogAt=0,NextSwing=0;
    int32 LocalStage=-1;
    bool bObservedUlcer=false;
    double RecoveryReadyAt=-1;
    uint32 Seen=0;
    double NextCapture=0,LastCapture=-1;
    int32 CaptureFrame=0;
    FString CaptureTiming;
    UPROPERTY() TObjectPtr<ACameraActor> Camera;
};
