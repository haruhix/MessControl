#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCCoffeeProfile.generated.h"

UENUM(BlueprintType)
enum class EMCCoffeePhase : uint8 { Inactive, Filling, Draining };

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour", meta=(ClampMin="1",ClampMax="15")) float FillSeconds=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour", meta=(ClampMin="0.5",ClampMax="10")) float DrainSeconds=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour", meta=(ClampMin="1",ClampMax="4")) int32 Cycles=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour") FVector Inlet=FVector(420,-100,1100);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour") FVector DrainPoint=FVector(920,0,0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour") float JetRadius=85;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour") float FrontSpeed=950;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour") float FrontWidth=110;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour") float FrontHeight=26;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pour") float ImpactImpulse=520;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drain") float DrainAcceleration=720;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drain") float DrainRadius=320;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drain") float DrainDepth=65;
    // Authoring point is a fallback; a placed food disposal marks the actual throat.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drain") bool bUseThroatActor=true;
    void Sanitize();
    float CycleSeconds() const { return FillSeconds+DrainSeconds; }
    float CycleTime(float Time) const;
    EMCCoffeePhase Phase(float Time) const;
    float FillAmount(float Time) const;
    float DrainAmount(float Time) const;
    float JetAmount(float Time) const;
    float FrontRadius(float Time) const;
    float SurfaceOffset(FVector Position,float Time) const;
    FVector FlowAt(FVector Position,float Time,float FillAcceleration) const;
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
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pour Visuals") TSoftObjectPtr<UStaticMesh> JetMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pour Visuals") TSoftObjectPtr<UStaticMesh> CrownMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pour Visuals") TSoftObjectPtr<UStaticMesh> DropMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pour Visuals") TSoftObjectPtr<UStaticMesh> DrainMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pour Visuals") TSoftObjectPtr<UMaterialInterface> PourMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pour Visuals") TSoftObjectPtr<UMaterialInterface> DropMaterial;
};
