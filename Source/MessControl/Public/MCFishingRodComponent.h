#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "MCFishingRodComponent.generated.h"

class AMCToothCharacter;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class APhysicsVolume;
struct FReferenceSkeleton;

/** Hub rod: the artist's three meshes, two hand grips and a light Verlet line. */
UCLASS(ClassGroup=(Hub),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCFishingRodComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCFishingRodComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;

    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Fishing") TObjectPtr<UStaticMesh> RodMesh;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Fishing") TObjectPtr<UStaticMesh> ReelMesh;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Fishing") TObjectPtr<UStaticMesh> LureMesh;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Fishing") TObjectPtr<UMaterialInterface> LineMaterial;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Fishing|Catch") TObjectPtr<UStaticMesh> FishMesh;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Fishing|Catch") TObjectPtr<UMaterialInterface> FishMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fishing|Catch",meta=(ClampMin="0.1",ClampMax="60")) float MinBiteDelay=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fishing|Catch",meta=(ClampMin="0.1",ClampMax="60")) float MaxBiteDelay=9;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Fishing|Catch") bool bFishHooked=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Fishing|Catch") float LastBiteDelay=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fishing|Pose") FTransform CarryPose;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fishing|Pose") FTransform FishingPoseTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fishing|Pose") FVector RightGrip=FVector(-12,0,0);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fishing|Pose") FVector LeftGrip=FVector(50,12,44);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fishing|Line",meta=(ClampMin="20")) float HangingLength=90;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fishing|Line",meta=(ClampMin="100")) float MaxLineLength=2200;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Fishing") bool bEquipped=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Fishing") bool bFishingPose=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Fishing") bool bReeling=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Fishing") bool bLineOut=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Fishing") float FreeLineLength=90;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Fishing") FVector LurePoint=FVector::ZeroVector;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Fishing") FVector TipBend=FVector::ZeroVector;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Fishing") TObjectPtr<USceneComponent> RodFrame;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Fishing") TObjectPtr<UProceduralMeshComponent> RodVisual;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Fishing") TObjectPtr<UStaticMeshComponent> ReelVisual;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Fishing") TObjectPtr<UStaticMeshComponent> LureVisual;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Fishing") TObjectPtr<UProceduralMeshComponent> LineVisual;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Fishing|Catch") TObjectPtr<UStaticMeshComponent> FishVisual;

    UFUNCTION(BlueprintPure,Category="Fishing") bool IsPresented() const;
    UFUNCTION(BlueprintPure,Category="Fishing") bool IsInsideFishingZone() const { return bInsideFishingZone; }
    /** Recheck the real pier footprint on the server; clients cannot grant themselves access. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Fishing") void RefreshFishingZone();
    UFUNCTION(BlueprintPure,Category="Fishing") bool CanCastLine() const;
    UFUNCTION(BlueprintCallable,Category="Fishing") void ToggleEquipped();
    UFUNCTION(Server,Reliable,BlueprintCallable,Category="Fishing") void ServerSetEquipped(bool Equipped);
    UFUNCTION(Server,Reliable,BlueprintCallable,Category="Fishing") void ServerSetFishingPose(bool Raised);
    UFUNCTION(Server,Reliable,BlueprintCallable,Category="Fishing") void ServerSetReeling(bool Reeling);
    UFUNCTION(Server,Reliable,BlueprintCallable,Category="Fishing") void CastLine();
    void BuildHandPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt);

private:
    bool IsHub() const;
    void CreateVisuals();
    void ResetLine(FVector Tip);
    void SimulateLine(float Dt,FVector Tip);
    void DrawLine();
    FVector BentPoint(FVector Point) const;
    bool PondSurfaceAt(const APhysicsVolume* Pond,FVector Point,FVector& Surface) const;
    bool FindPondCast(FVector& Origin,FVector& Target,FVector& Velocity,float& Duration,APhysicsVolume*& Pond) const;
    void CancelFishing();
    void UpdateFishingCatch();
    bool IsWithinFishingZone() const;
    void SetEquippedState(bool Equipped);
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY(Replicated) bool bInsideFishingZone=false;
    UPROPERTY(Replicated) int32 CastSerial=0;
    UPROPERTY(Replicated) FVector CastOrigin=FVector::ZeroVector;
    UPROPERTY(Replicated) FVector CastVelocity=FVector::ZeroVector;
    UPROPERTY(Replicated) FVector CastTarget=FVector::ZeroVector;
    UPROPERTY(Replicated) float CastFlightDuration=1;
    UPROPERTY(Replicated) TObjectPtr<APhysicsVolume> CastPond;
    TArray<FVector> RestVertices,RestNormals,Vertices,Normals,LineVertices,LineNormals;
    TArray<FVector2D> UVs,LineUVs;
    TArray<int32> Triangles,LineTriangles;
    TArray<FProcMeshTangent> RestTangents,Tangents,LineTangents;
    TArray<FVector> Points,PreviousPoints;
    FVector LastAnchor=FVector::ZeroVector,LastFrameVelocity=FVector::ZeroVector,BendVelocity=FVector::ZeroVector;
    float PoseAlpha=0,HandsAlpha=0,Accumulator=0,ReelAngle=0;
    int32 LastCastSerial=0;
    bool bWasVisible=false,bWasLineOut=false,bCastFlight=false,bLureInWater=false;
    float CastAge=0;
    FVector WaterAnchor=FVector::ZeroVector;
    double BiteAt=-1;
    double NextFishingZoneCheckAt=0;
    bool bPendingFishingZoneEquip=false;
};
