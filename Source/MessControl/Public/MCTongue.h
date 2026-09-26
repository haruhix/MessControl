#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "MCTongueProfile.h"
#include "MCTongue.generated.h"

USTRUCT()
struct FMCTonguePulse
{
    GENERATED_BODY()
    UPROPERTY() FVector Origin=FVector::ZeroVector;
    UPROPERTY() double StartedAt=-100;
    UPROPERTY() int32 Serial=0;
};

/** One vertex buffer drives both the visible tongue and its Chaos triangle surface.
 * Fixed topology: UpdateMeshSection refits collision, never cooks a new mesh per tick.
 */
UCLASS()
class MESSCONTROL_API AMCTongue : public AActor
{
    GENERATED_BODY()
public:
    AMCTongue();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> Surface;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Tongue") TObjectPtr<UStaticMesh> SourceMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Tongue") TObjectPtr<UMaterialInterface> SurfaceMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Tongue") TObjectPtr<UMCTongueProfile> Profile;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tongue") FMCTongueSettings Settings;
    UPROPERTY(Replicated) FMCTonguePulse Pulse;
    UFUNCTION(CallInEditor,Category="Tongue") void RebuildSurface();
    bool TriggerPain(FVector WorldPoint);
    void ResetPain();
    bool SurfacePoint(FVector WorldPoint,FHitResult& Hit) const;
    float ServerTime() const;
    // Used by validation to compare the rendered triangle with collision.
    const TArray<FVector>& CurrentVertices() const { return Positions; }
    const TArray<int32>& TriangleIndices() const { return Indices; }
    int32 PlayerPushes=0,FoodPushes=0;
private:
    float Offset(FVector Local,float Time,float& Red) const;
    void Deform(float Time);
    void PushWave(float Age);
    TArray<FVector> Rest,RestNormals,Positions,Normals;
    TArray<float> AnchorWeights;
    TArray<FVector> AnchorGradients;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FProcMeshTangent> RestTangents,Tangents;
    TArray<FColor> Colors;
    FBox RestBounds=FBox(ForceInit);
    TSet<TWeakObjectPtr<AActor>> HitActors;
    float PreviousWaveAge=-1;
    int32 PreviousSerial=0;
};
