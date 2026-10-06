#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "MCOrbitSpringArmComponent.generated.h"

class UMeshComponent;
class UPrimitiveComponent;

/** Retract before blocking geometry; ease back out after the obstruction clears. */
UCLASS()
class MESSCONTROL_API UMCOrbitSpringArmComponent : public USpringArmComponent
{
    GENERATED_BODY()
public:
    void SetSurfaceProbeActive(bool bActive) { bSurfaceProbeActive=bActive; }
    void SetIgnoredViewActor(AActor* Actor) { IgnoredViewActor=Actor; }
    AActor* GetIgnoredViewActor() const { return IgnoredViewActor.Get(); }
    // The wall aperture may bypass a view-ray obstruction, but never the camera's own volume.
    void SetRevealedWall(UMeshComponent* Wall, bool bReveal);
protected:
    virtual FVector BlendLocations(const FVector& Desired, const FVector& Hit, bool bBlocked, float Dt) override;
private:
    float SafeArmLength=-1.f;
    FVector LastOrigin=FVector::ZeroVector;
    double NextSurfaceScan=0;
    bool bSurfaceProbeActive=false;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> DetailedSurfaces;
    TArray<TWeakObjectPtr<UMeshComponent>> RevealedWalls;
    TWeakObjectPtr<AActor> IgnoredViewActor;
    void RefreshDetailedSurfaces();
};
