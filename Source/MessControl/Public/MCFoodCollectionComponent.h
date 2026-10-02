#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCFoodCollectionComponent.generated.h"
class AMCFoodActor;
class AMCToothCharacter;

/** A hand supports the bottom rigid body; upper pieces balance through Chaos contact. */
UCLASS(ClassGroup=(Food),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCFoodCollectionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCFoodCollectionComponent();
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    bool CanCollect(const AMCFoodActor* Food) const;
    bool HasCandidate() const;
    void Toggle();
    void Stop(bool Throw=false);
    bool Collect(AMCFoodActor* Food);
    bool Contains(const AMCFoodActor* Food) const;
    bool IsSettlingRelease(const AMCFoodActor* Food) const;
    FVector HandPoint() const;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bCollecting=false;
    UPROPERTY(Replicated,BlueprintReadOnly) TArray<TObjectPtr<AMCFoodActor>> Pieces;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxPieces=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float CollectionReach=180;
    int32 FallenPieces=0;
private:
    TMap<TWeakObjectPtr<AMCFoodActor>,double> PlacedAt;
    TMap<TWeakObjectPtr<AMCFoodActor>,double> DroppedAt;
    FVector PreviousHand=FVector::ZeroVector;
    bool bHasHand=false;
    double NextCollectAt=0;
    void ReleaseFrom(int32 Index,bool Throw=false);
};
