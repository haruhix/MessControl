#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCFoodCollectionComponent.generated.h"
class AMCFoodActor;
struct FMCFoodStackSettings;
class AMCToothCharacter;

/** A held stack sways as one load and returns to Chaos when released or hit. */
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
    void Spill(FVector Impulse=FVector::ZeroVector);
    void HandleCarrierCollision(AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit);
    void HandleStackCollision(AMCFoodActor* Food,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit);
    bool Collect(AMCFoodActor* Food);
    /** Atomically hands off a Swallowing piece, preserving the rest of the load. */
    bool DetachForDelivery(AMCFoodActor* Food);
    bool Contains(const AMCFoodActor* Food) const;
    bool IsSettlingRelease(const AMCFoodActor* Food) const;
    FVector HandPoint() const;
    FTransform StackPose(float SlotHeight) const;
    FTransform StackPose(const AMCFoodActor* Food) const;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bCollecting=false;
    UPROPERTY(Replicated,BlueprintReadOnly) TArray<TObjectPtr<AMCFoodActor>> Pieces;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxPieces=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float CollectionReach=180;
    /** Minimum normal contact impulse (kg cm/s). A resting overlap has no impact strength. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Collection|Collision",meta=(ClampMin="1")) float SpillContactImpulse=600;
    int32 FallenPieces=0;
private:
    struct FPieceMotion { FVector Linear=FVector::ZeroVector,Angular=FVector::ZeroVector; bool bLanded=false; };
    TMap<TWeakObjectPtr<AMCFoodActor>,FPieceMotion> PieceMotion;
    TMap<TWeakObjectPtr<AMCFoodActor>,double> DroppedAt;
    FVector PreviousHand=FVector::ZeroVector;
    FVector PreviousHandVelocity=FVector::ZeroVector,PreviousCarrierVelocity=FVector::ZeroVector;
    FVector2D SwayAngle=FVector2D::ZeroVector,SwayVelocity=FVector2D::ZeroVector;
    bool bHasHand=false;
    double NextCollectAt=0;
    const FMCFoodStackSettings* LayoutSettings() const;
    void RebuildStackLayout();
    FQuat StackRotation() const;
    float ContactThreshold() const;
    bool IsLoosePileFood(const AActor* Actor) const;
    float ContactStrength(float HeldMass,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,FVector Normal) const;
    void CheckIncomingContacts(AMCFoodActor* Food);
    void ReleaseFrom(int32 Index,bool Throw=false,FVector Impulse=FVector::ZeroVector);
};
