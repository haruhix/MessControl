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

/** Vertex waves need vertical render bounds even though the authored grid is flat. */
UCLASS()
class UMCCataclysmStormSurfaceComponent : public UMCRiverSurfaceComponent
{
    GENERATED_BODY()
public:
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override
    {
        FBoxSphereBounds RenderBounds=Super::CalcBounds(LocalToWorld);
        RenderBounds.BoxExtent.Z+=800.f;RenderBounds.SphereRadius+=800.f;return RenderBounds;
    }
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
    /** Timed hazard only; the Cataclysm owner independently waits for cleanup. */
    void SetCataclysmDuration(float FillSeconds,float HoldSeconds,float DrainSeconds);
    /** Opt-in only for the deep raft branch; ordinary coffee/river behavior is preserved. */
    void EnableCataclysmStorm(int32 Seed);
    UFUNCTION(BlueprintPure,Category="Cataclysm|Storm") bool IsCataclysmStorm() const;
    UFUNCTION(BlueprintPure,Category="Cataclysm|Storm") FVector StormStreamDirection() const;
    UFUNCTION(BlueprintPure,Category="Cataclysm|Storm") float StormStrength() const;
    UFUNCTION(BlueprintPure,Category="Cataclysm|Storm") FVector StormCurrentAt(FVector Position) const;
    UFUNCTION(BlueprintPure,Category="Cataclysm|Storm") float StormWaveHeightAt(FVector Position) const;
    UFUNCTION(BlueprintPure,Category="Cataclysm|Storm") float StormWetWeightAt(FVector Position) const;
    /** Confirmed authority contact only; a bounded cosmetic burst reuses the spray pool. */
    void NotifyStormImpact(FVector Position,FVector Direction,float Strength=1.f);
    void Stop();
    int32 ResidueBatch=0;
    bool Contains(FVector Position) const;
    UFUNCTION(BlueprintPure,Category="Coffee|Water") float SurfaceHeightAt(FVector Position) const;
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
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Coffee|Water") FVector RiverOrigin=FVector::ZeroVector;
    UPROPERTY(Replicated) FVector RiverDirection=FVector::ForwardVector;
    UPROPERTY(Replicated) float RiverLength=2100;
    UPROPERTY(Replicated) float RiverWidth=1000;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Coffee|Water") float RiverSpeed=1050;
    UPROPERTY(Replicated) float RiverDepth=78;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bActive=false;
    UPROPERTY(Replicated, BlueprintReadOnly) float Level=-40;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Wave=0;
    UPROPERTY(Replicated) int32 Waves=1;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Coffee|Water") float Height=155;
    UPROPERTY(Replicated) float Flow=320;
    UPROPERTY(Replicated) float Paddle=400;
    UPROPERTY(Replicated) float Reach=160;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Coffee|Water") FVector HalfSize=FVector(1050,740,220);
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Coffee|Water") FVector ArenaCenter=FVector::ZeroVector;
    UPROPERTY(Replicated) float InletFloorZ=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Coffee|Water") double StartedAt=0;
    UPROPERTY(Replicated) float Seconds=6;
    UPROPERTY(ReplicatedUsing=OnRep_Profile) TObjectPtr<UMCCoffeeProfile> Profile;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Coffee|Water") FMCCoffeeWaterSettings WaterSettings;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Jet;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Crown;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> DrainRibbon;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCCoffeeDropComponent> Drops;
    UPROPERTY(ReplicatedUsing=OnRep_Profile,BlueprintReadOnly,Category="Cataclysm|Storm") bool bCataclysmStorm=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Storm") int32 StormSeed=0;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCRiverSurfaceComponent> StormSurface;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCCoffeeDropComponent> StormSpray;
private:
    UFUNCTION() void OnRep_Profile();
    float WaterTime() const;
    float BaseHeight(float Time) const;
    void UpdateSurface();
    void UpdatePour(float Time);
    void UpdateRiverSurface(float Time);
    void UpdateStormSurface(float Time);
    void UpdateStormSpray(float Time);
    UFUNCTION(NetMulticast,Unreliable) void MulticastStormImpact(FVector Position,FVector Direction,float Strength,int32 ImpactSeed);
    float StormStrengthAt(float Time) const;
    float StormWaveAt(FVector Position,float Time) const;
    float StormFrontAt(FVector Position,float Time) const;
    float StormWetAt(FVector Position,float Time) const;
    float StormHeightAt(FVector Position,float Time) const;
    AMCTongue* FindRiverTongue() const;
    float RiverFloorAt(FVector Position) const;
    float RiverHeightAt(FVector Position,float Time,float FloorZ) const;
    bool IsFlowBlocked(FVector Position,const AActor* Ignore) const;
    void UpdateRiverDebris();
    void InitializeResidue();
    void AccumulateResidue(float Time);
    bool HasResidueAt(FVector Position,bool bNewOnly=false) const;
    void DepositResidue();
    static constexpr int32 ResidueSize=48;
    struct FResidueCell { FVector Point=FVector::ZeroVector; float WetTop=-MAX_flt,EmittedTop=-MAX_flt; bool bSurface=false; };
    TArray<FResidueCell> ResidueCells;
    struct FResiduePatch { FVector Point=FVector::ZeroVector; float HalfSize=240; TWeakObjectPtr<class AMCMouthSurface> Surface; };
    TArray<FResiduePatch> ResiduePatches;
    float LastResidueTime=-100;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> JetMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CrownMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DrainMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StormSprayMaterial;
    TSet<TWeakObjectPtr<AMCToothCharacter>> HitThisWave;
    TSet<TWeakObjectPtr<AActor>> FoodHitThisWave;
    mutable TWeakObjectPtr<AMCTongue> RiverTongue;
    TArray<FVector> RiverVertices,RiverNormals;
    TArray<FVector2D> RiverUVs;
    TArray<FColor> RiverColors;
    TArray<FProcMeshTangent> RiverTangents;
    int32 RiverMeshVertexCount=0;
    FVector2D StormMeshHalfSize=FVector2D::ZeroVector;
    struct FStormDrop { FVector Origin=FVector::ZeroVector,Velocity=FVector::ZeroVector; float Born=-10.f,Size=0.f,LifeSeconds=.85f; };
    TArray<FStormDrop> StormDrops;
    int32 StormSprayBucket=INDEX_NONE;
    float LastStormImpactAt=-10.f;
    int32 StormImpactSerial=0,StormImpactVisualCursor=0;
};
