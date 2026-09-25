#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCCoreScenario.generated.h"
class AMCArenaTooth;
class AMCFoodActor;
class AMCToothCharacter;
class ACameraActor;

/** Opt-in -MCCore scenario: real input RPCs, server Chaos and replicated observations. */
UCLASS(NotBlueprintable)
class AMCCoreScenario : public AActor
{
    GENERATED_BODY()
public:
    AMCCoreScenario();
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) int32 Stage=0;
    UPROPERTY(Replicated) double StageAt=0;
    UPROPERTY(Replicated) TObjectPtr<AMCArenaTooth> Target;
    UPROPERTY(Replicated) TObjectPtr<AMCFoodActor> Food;
    UPROPERTY(Replicated) TArray<TObjectPtr<AMCToothCharacter>> Heroes;
    UPROPERTY(Replicated) bool bFailed=false;
    FString Caption() const;
private:
    void NextStage();
    void MoveHero(int32 Index,FVector Position,FRotator Rotation);
    float Age=0, NextLog=0;
    int32 LocalStage=-1;
    uint32 Observed=0;
    TWeakObjectPtr<AMCToothCharacter> LocalHero;
    UPROPERTY() TObjectPtr<ACameraActor> CaptureCamera;
    int32 CaptureFrame=0;
    double NextCapture=0, LastCapture=-1;
    FString CaptureTiming;
};
