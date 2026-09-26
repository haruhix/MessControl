#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MCToothMovementComponent.generated.h"
class AMCCoffeeFlood;

/** Surface swimming through CharacterMovement's existing prediction and replication. */
UCLASS()
class MESSCONTROL_API UMCToothMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UMCToothMovementComponent();
    AMCCoffeeFlood* DeepWaterAt(FVector Position,bool Continuing=false) const;
    virtual void UpdateCharacterStateBeforeMovement(float Dt) override;
    virtual void PhysSwimming(float Dt,int32 Iterations) override;
};
