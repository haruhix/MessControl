#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCCoffeeProfile.generated.h"

USTRUCT(BlueprintType)
struct FMCCoffeeWaterSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="0",ClampMax="15")) float RippleHeight=6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="100",ClampMax="600")) float RippleLength=260;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="0",ClampMax="4")) float RippleSpeed=1.4f;
    // Depth of the ragdoll's main bone below the visible surface, in cm.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swimming") float FloatDepth=20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swimming") float BuoyancyStiffness=18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swimming") float VerticalDamping=5.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swimming") float WaterDrag=1.8f;
    void Sanitize();
    float Ripple(FVector Position,float Time) const;
    FVector FloatAcceleration(float SurfaceHeight,FVector Position,FVector Velocity,FVector Drive,float Gravity,float Draft) const;
};

/** Shared surface shape and buoyancy; appearance colors are editable in the material instance. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCCoffeeProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FMCCoffeeWaterSettings Settings;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> SurfaceMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> SurfaceMaterial;
};
