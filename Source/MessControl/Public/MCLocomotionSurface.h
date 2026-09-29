#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "MCLocomotionSurface.generated.h"
class UBoxComponent;

UENUM(BlueprintType)
enum class EMCGroundSurface : uint8 { Normal, Sticky, Slippery };

/** Assign to a mesh's physical material to share its ground response with locomotion. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCLocomotionMaterial : public UPhysicalMaterial
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion") EMCGroundSurface GroundSurface=EMCGroundSurface::Normal;
};

/** Optional local override for a patch of floor. Tests the sole, never blocks movement. */
UCLASS()
class MESSCONTROL_API AMCLocomotionSurface : public AActor
{
    GENERATED_BODY()
public:
    AMCLocomotionSurface();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBoxComponent> Bounds;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Surface") EMCGroundSurface Surface=EMCGroundSurface::Sticky;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Surface") int32 Priority=0;
    UPROPERTY(EditAnywhere,ReplicatedUsing=RefreshBounds,BlueprintReadWrite,Category="Surface",meta=(ClampMin="1")) FVector HalfExtent=FVector(200,200,40);
    bool ContainsSole(FVector WorldPoint) const;
    UFUNCTION() void RefreshBounds();
};
