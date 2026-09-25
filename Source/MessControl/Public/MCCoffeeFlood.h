#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCCoffeeFlood.generated.h"
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AMCToothCharacter;
class UMCDayPlan;
UCLASS()
class MESSCONTROL_API AMCCoffeeFlood : public AActor
{
    GENERATED_BODY()
public:
    AMCCoffeeFlood();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Start(const UMCDayPlan* Plan,float Duration);
    void Stop();
    bool Contains(FVector Position) const;
    bool IsActive() const { return bActive; }
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Surface;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bActive=false;
    UPROPERTY(Replicated, BlueprintReadOnly) float Level=-40;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Wave=0;
    UPROPERTY(Replicated) int32 Waves=4;
    UPROPERTY(Replicated) float Height=155;
    UPROPERTY(Replicated) float Flow=320;
    UPROPERTY(Replicated) float Paddle=400;
    UPROPERTY(Replicated) float Reach=160;
    UPROPERTY(Replicated) FVector HalfSize=FVector(1050,740,220);
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) float Seconds=10;
private:
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    TSet<TWeakObjectPtr<AMCToothCharacter>> HitThisWave;
};
