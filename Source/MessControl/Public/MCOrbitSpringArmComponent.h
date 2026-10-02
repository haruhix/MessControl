#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "MCOrbitSpringArmComponent.generated.h"

/** Enter blocking geometry immediately; ease back out after the obstruction clears. */
UCLASS()
class MESSCONTROL_API UMCOrbitSpringArmComponent : public USpringArmComponent
{
    GENERATED_BODY()
protected:
    virtual FVector BlendLocations(const FVector& Desired, const FVector& Hit, bool bBlocked, float Dt) override;
private:
    float SafeArmLength=-1.f;
    FVector LastOrigin=FVector::ZeroVector;
};
