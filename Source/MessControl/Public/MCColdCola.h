#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "Components/StaticMeshComponent.h"
#include "MCColdCola.generated.h"
class AMCCoffeeFlood;
class AMCLocomotionSurface;
class UStaticMeshComponent;
class AMCToothCharacter;
class UMCCoffeeProfile;
class UNiagaraComponent;
class UStaticMesh;
class UMaterialInterface;

UCLASS()
class UMCIceSurfaceComponent : public UStaticMeshComponent
{
    GENERATED_BODY()
protected:
    virtual bool UsePSOPrecacheRenderProxyDelay() const override { return false; }
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCColdColaProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UMCCoffeeProfile> Drink;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="12")) int32 IceCount=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="300")) float IceHealth=80;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="20",ClampMax="180")) float IceSize=95;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="10",ClampMax="120")) float ColdSeconds=35;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="10")) float FrostRiseSeconds=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="10")) float ThawSeconds=5;
};

/** A hard obstacle with server-owned health; visual shards never block players. */
UCLASS()
class MESSCONTROL_API AMCIceBlock : public AActor
{
    GENERATED_BODY()
public:
    AMCIceBlock();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(ReplicatedUsing=RefreshAppearance,BlueprintReadOnly) float Health=80;
    UPROPERTY(ReplicatedUsing=RefreshAppearance,BlueprintReadOnly) float MaxHealth=80;
    UPROPERTY(ReplicatedUsing=RefreshAppearance) bool bBroken=false;
    UPROPERTY(ReplicatedUsing=RefreshAppearance) int32 Shape=0;
    UPROPERTY(ReplicatedUsing=RefreshAppearance) FVector Size=FVector(95);
    bool HitWithPickaxe(AMCToothCharacter* Hero,float Damage);
    UFUNCTION() void RefreshAppearance();
    UFUNCTION(NetMulticast,Reliable) void Shatter(FVector Position,float Diameter);
private:
    // Hard default references retain Engine shapes in cooked games.
    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> ShapeMeshes;
    UPROPERTY() TObjectPtr<UMaterialInterface> IceMaterial;
    UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> IceMID;
};

/** Shared timeline event: top-down cola, growing frost, slippery ground, falling ice. */
UCLASS()
class MESSCONTROL_API AMCColdColaEvent : public AActor
{
    GENERATED_BODY()
public:
    AMCColdColaEvent();
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Start(const class UMCDayPlan* Plan,int32 IceCountOverride=0);
    void Stop();
    int32 IceLeft() const;
    float FrostAmount() const;
    bool IsComplete() const;
    UPROPERTY(ReplicatedUsing=OnRep_Timeline,BlueprintReadOnly) bool bActive=false;
    UPROPERTY(ReplicatedUsing=OnRep_Timeline) double StartedAt=0;
    UPROPERTY(ReplicatedUsing=OnRep_Timeline) double ThawStartedAt=0;
    UPROPERTY(ReplicatedUsing=OnRep_Timeline) TObjectPtr<UMCColdColaProfile> Profile;
    UPROPERTY() TObjectPtr<AMCCoffeeFlood> Drink;
    UPROPERTY() TObjectPtr<AMCLocomotionSurface> SlipperyFloor;
private:
    UFUNCTION() void OnRep_Timeline();
    int32 Spawned=0,IceTarget=5;
    double NextIceAttempt=0;
    FVector Center=FVector::ZeroVector,Extent=FVector(1050,740,220);
    TArray<TWeakObjectPtr<AMCIceBlock>> Blocks;
    TArray<FVector> SpawnPositions;
    void SetFrost(float Amount);
    double Now() const;
};
