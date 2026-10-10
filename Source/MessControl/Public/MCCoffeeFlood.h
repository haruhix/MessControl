#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "MCCoffeeProfile.h"
#include "ProceduralMeshComponent.h"
#include "MCCoffeeFlood.generated.h"
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AMCToothCharacter;
class UMCDayPlan;
class AMCTongue;

// Gameplay-critical surface: an uncached graphics pipeline may wait on first
// draw, but must never hide the water while its physical volume is active.
UCLASS()
class UMCCoffeeSurfaceComponent : public UStaticMeshComponent
{
    GENERATED_BODY()
protected:
    virtual bool UsePSOPrecacheRenderProxyDelay() const override { return false; }
};

UCLASS()
class UMCCoffeeDropComponent : public UInstancedStaticMeshComponent
{
    GENERATED_BODY()
protected:
    virtual bool UsePSOPrecacheRenderProxyDelay() const override { return false; }
};

UCLASS()
class UMCRiverSurfaceComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()
protected:
    virtual bool UsePSOPrecacheRenderProxyDelay() const override { return false; }
};

UCLASS()
class MESSCONTROL_API AMCCoffeeFlood : public AActor
{
    GENERATED_BODY()
public:
    AMCCoffeeFlood();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Start(const UMCDayPlan* Plan,float SwimTestSeconds=0,bool bUseLegacyFlood=false);
    void Stop();
    int32 ResidueBatch=0;
    bool Contains(FVector Position) const;
    float SurfaceHeightAt(FVector Position) const;
    float SurfaceVerticalSpeedAt(FVector Position) const;
    bool IsActive() const { return bActive; }
    UFUNCTION(BlueprintPure) EMCCoffeePhase GetPhase() const;
    float PhaseTime() const { return WaterSettings.CycleTime(WaterTime()); }
    FVector FlowAtPosition(FVector Position,const AActor* Ignore=nullptr) const;
    /** Compact, already loose rigid bodies; scenery and oversized food stay in place. */
    static bool IsRiverDebris(const UPrimitiveComponent* Body);
    FVector RiverDebrisAcceleration(const UPrimitiveComponent* Body) const;
    float RiverFrontDistance() const;
    float RiverFrontAt(FVector Position,float Time) const;
    float RiverWeightAt(FVector Position) const;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Surface;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCRiverSurfaceComponent> RiverSurface;
    UPROPERTY(ReplicatedUsing=OnRep_Profile, BlueprintReadOnly) bool bRiverFlood=false;
    UPROPERTY(Replicated) FVector RiverOrigin=FVector::ZeroVector;
    UPROPERTY(Replicated) FVector RiverDirection=FVector::ForwardVector;
    UPROPERTY(Replicated) float RiverLength=2100;
    UPROPERTY(Replicated) float RiverWidth=1000;
    UPROPERTY(Replicated) float RiverSpeed=1050;
    UPROPERTY(Replicated) float RiverDepth=78;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bActive=false;
    UPROPERTY(Replicated, BlueprintReadOnly) float Level=-40;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Wave=0;
    UPROPERTY(Replicated) int32 Waves=1;
    UPROPERTY(Replicated) float Height=155;
    UPROPERTY(Replicated) float Flow=320;
    UPROPERTY(Replicated) float Paddle=400;
    UPROPERTY(Replicated) float Reach=160;
    UPROPERTY(Replicated) FVector HalfSize=FVector(1050,740,220);
    UPROPERTY(Replicated) FVector ArenaCenter=FVector::ZeroVector;
    UPROPERTY(Replicated) float InletFloorZ=0;
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) float Seconds=6;
    UPROPERTY(ReplicatedUsing=OnRep_Profile) TObjectPtr<UMCCoffeeProfile> Profile;
    UPROPERTY(Replicated) FMCCoffeeWaterSettings WaterSettings;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Jet;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Crown;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> DrainRibbon;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCCoffeeDropComponent> Drops;
private:
    UFUNCTION() void OnRep_Profile();
    float WaterTime() const;
    float BaseHeight(float Time) const;
    void UpdateSurface();
    void UpdatePour(float Time);
    void UpdateRiverSurface(float Time);
    AMCTongue* FindRiverTongue() const;
    float RiverFloorAt(FVector Position) const;
    float RiverHeightAt(FVector Position,float Time,float FloorZ) const;
    bool IsFlowBlocked(FVector Position,const AActor* Ignore) const;
    void UpdateRiverDebris();
    void InitializeResidue();
    void AccumulateResidue(float Time);
    bool HasResidueAt(FVector Position) const;
    void DepositResidue();
    static constexpr int32 ResidueSize=48;
    struct FResidueCell { FVector Point=FVector::ZeroVector; float WetTop=-MAX_flt; bool bSurface=false; };
    TArray<FResidueCell> ResidueCells;
    float LastResidueTime=-100;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> JetMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CrownMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DrainMaterial;
    TSet<TWeakObjectPtr<AMCToothCharacter>> HitThisWave;
    TSet<TWeakObjectPtr<AActor>> FoodHitThisWave;
    mutable TWeakObjectPtr<AMCTongue> RiverTongue;
    TArray<FVector> RiverVertices,RiverNormals;
    TArray<FVector2D> RiverUVs;
    TArray<FColor> RiverColors;
    TArray<FProcMeshTangent> RiverTangents;
    int32 RiverMeshVertexCount=0;
};
