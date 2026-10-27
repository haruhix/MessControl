#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "MCOrbitSpringArmComponent.generated.h"

class UMeshComponent;

/** Retract before blocking geometry; optional outward easing follows local camera preferences. */
UCLASS()
class MESSCONTROL_API UMCOrbitSpringArmComponent : public USpringArmComponent
{
    GENERATED_BODY()
public:
    void SetWallProbeActive(bool bActive) { bWallProbeActive=bActive; }
    void SetIgnoredViewActor(AActor* Actor) { IgnoredViewActor=Actor; }
    AActor* GetIgnoredViewActor() const { return IgnoredViewActor.Get(); }
    void SetRecoverySmoothingEnabled(bool bEnabled) { bRecoverySmoothingEnabled=bEnabled; }
    // The wall aperture may bypass a view-ray obstruction, but never the camera's own volume.
    void SetRevealedWall(UMeshComponent* Wall, bool bReveal);
protected:
    virtual FVector BlendLocations(const FVector& Desired, const FVector& Hit, bool bBlocked, float Dt) override;
private:
    float SafeArmLength=-1.f;
    FVector LastOrigin=FVector::ZeroVector;
    bool bWallProbeActive=false;
    bool bRecoverySmoothingEnabled=false;
    TArray<TWeakObjectPtr<UMeshComponent>> RevealedWalls;
    TWeakObjectPtr<AActor> IgnoredViewActor;
};
