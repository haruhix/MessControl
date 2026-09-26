#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "MCTongueProfile.h"
#include "MCTongue.generated.h"

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
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tongue") FMCTongueMotionState Motion;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tongue|Weight") FMCTonguePressureSettings PressureSettings;
    UPROPERTY(ReplicatedUsing=OnRep_PressureFrame) FMCTonguePressureFrame PressureFrame;
    UFUNCTION(BlueprintPure,Category="Tongue|Weight") float IndentationAt(FVector WorldPoint) const;
    const TArray<FMCTongueLoad>& PressureLoads() const { return HasAuthority()?CurrentLoads:PressureFrame.Sources; }
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tongue|Weight") void ResetPressure();
    // Returns false while another motion/rest interval is active. Call on the server.
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tongue") bool PlayMotion(UMCTongueMotionProfile* MotionProfile,FVector WorldOrigin,FVector WorldDirection,float Strength=1);
    UFUNCTION(BlueprintPure,Category="Tongue") bool IsMotionActive() const;
    UFUNCTION(CallInEditor,Category="Tongue") void RebuildSurface();
    bool TriggerPain(FVector WorldPoint);
    bool TriggerJolt();
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tongue") void ResetPain();
    bool SurfacePoint(FVector WorldPoint,FHitResult& Hit) const;
    float ServerTime() const;
    // Used by validation to compare the rendered triangle with collision.
    const TArray<FVector>& CurrentVertices() const { return Positions; }
    const TArray<int32>& TriangleIndices() const { return Indices; }
    int32 PlayerPushes=0,FoodPushes=0;
private:
    void GatherPressure(float Dt);
    void UpdatePressureField(float Dt);
    bool PressureSupport(AActor* Actor,FVector Center,float Bottom,FHitResult& Hit) const;
    UFUNCTION() void OnRep_PressureFrame();
    UPROPERTY() TArray<FMCTongueLoad> CurrentLoads;
    TArray<float> IndentDepth;
    TArray<FVector> IndentGradient;
    struct FLoadHistory
    {
        float DownSpeed=0,Impact=0;
        double LastSeen=0,LastContact=-100,LandingAt=-100;
        bool bSupported=false;
    };
    TMap<TWeakObjectPtr<AActor>,FLoadHistory> LoadHistory;
    float PressureSendElapsed=0;
    int32 PressureEpoch=0;
    bool StartMotion(FMCTongueMotionSettings Event,FVector Origin,FVector Direction,float Strength);
    float JoltWeight(FVector Local) const;
    float SurfaceWeight(FVector Local) const;
    float MotionWeight(FVector Local) const;
    float MotionDistance(FVector Local) const;
    void PushMotion(float Age);
    void ScheduleJolt();
    double NextJoltAt=0;
    float Offset(FVector Local,float Time,float& Red) const;
    void Deform(float Time);
    TArray<FVector> Rest,RestNormals,Positions,Normals;
    TArray<float> AnchorWeights;
    TArray<FVector> AnchorGradients;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FProcMeshTangent> RestTangents,Tangents;
    TArray<FColor> Colors;
    FBox RestBounds=FBox(ForceInit);
    TSet<TWeakObjectPtr<AActor>> HitActors;
    float PreviousMotionAge=-1;
};
