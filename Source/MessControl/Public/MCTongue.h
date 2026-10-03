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
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tongue|Weight") TObjectPtr<UMCTonguePressurePreset> ActivePressurePreset;
    // nullptr explicitly restores the inline settings/material, preserving all event settings.
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tongue|Weight") void ApplyPressurePreset(UMCTonguePressurePreset* Preset);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,CallInEditor,Category="Tongue|Weight") void ReloadPressureProfile();
    UFUNCTION(BlueprintPure,Category="Tongue|Weight") float IndentationAt(FVector WorldPoint) const;
    const TArray<FMCTongueLoad>& PressureLoads() const { return HasAuthority()?CurrentLoads:PressureFrame.Sources; }
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tongue|Weight") void ResetPressure();
    // Returns false while another motion/rest interval is active. Call on the server.
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tongue") bool PlayMotion(UMCTongueMotionProfile* MotionProfile,FVector WorldOrigin,FVector WorldDirection,float Strength=1);
    UFUNCTION(BlueprintPure,Category="Tongue") bool IsMotionActive() const;
    UFUNCTION(CallInEditor,Category="Tongue") void RebuildSurface();
    bool TriggerPain(FVector WorldPoint);
    bool TriggerJolt();
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tongue|Yawn") bool StartYawn(float Seconds=4);
    UFUNCTION(BlueprintPure,Category="Tongue|Yawn") bool IsYawnActive() const;
    void ResetYawn();
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tongue|Yawn") double YawnStartedAt=-100;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tongue|Yawn") float YawnDuration=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tongue|Yawn") bool bAutomaticYawns=true;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tongue") void ResetPain();
    bool SurfacePoint(FVector WorldPoint,FHitResult& Hit) const;
    // Stable support for permanent coatings: breathing, pressure and pain waves
    // must not give server and clients different grime layouts.
    bool RestSurfacePoint(FVector WorldPoint,FVector& Point) const;
    float ServerTime() const;
    // Used by validation to compare the rendered triangle with collision.
    const TArray<FVector>& CurrentVertices() const { return Positions; }
    const TArray<int32>& TriangleIndices() const { return Indices; }
    int32 PlayerPushes=0,FoodPushes=0;
private:
    struct FFoodSupport
    {
        FVector Point=FVector::ZeroVector;
        FVector Normal=FVector::UpVector;
    };
    TMap<TWeakObjectPtr<class AMCFoodActor>,FFoodSupport> FoodSupports;
    void GatherPressure(float Dt);
    void UpdatePressureField(float Dt);
    bool PressureSupport(AActor* Actor,FVector Center,float Bottom,FHitResult& Hit) const;
    UFUNCTION() void OnRep_PressureFrame();
    UPROPERTY() TArray<FMCTongueLoad> CurrentLoads;
    TArray<float> IndentDepth;
    TArray<FVector> IndentGradient;
    TArray<float> PressureHold,TargetDepth;
    TArray<FVector> TargetGradient,PressureWorldVertices;
    TMap<FIntPoint,TArray<int32>> PressureCells;
    FTransform PressureGridTransform;
    void BuildPressureGrid();
    UPROPERTY(ReplicatedUsing=RefreshPressureMaterial) TObjectPtr<UMaterialInterface> ActivePressureMaterial;
    UFUNCTION() void RefreshPressureMaterial();
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
    double NextYawnAt=65;
    float Offset(FVector Local,float Time,float& Red,TConstArrayView<FMCTongueMotionState> Pulses) const;
    // The vertex and its six normal samples never change in local space.
    // Cache their masks/distances without changing the collision topology.
    struct FDeformationSample
    {
        FVector Point;
        float SurfaceMask=0,PulseWeight=0,JoltMask=0,IdlePhase=0;
        float MotionMask=0,Distance=0;
    };
    TArray<FDeformationSample> DeformationSamples;
    FTransform DeformationTransform;
    int32 DeformationMotionSerial=INDEX_NONE;
    FMCTongueMotionState DeformationMotion;
    void BuildDeformationSamples();
    void RefreshDeformationMotion();
    float SampleOffset(const FDeformationSample& Sample,float Time,float IdleAngle,float Envelope,float YawnHeight,float& Red,TConstArrayView<FMCTongueMotionState> Pulses) const;
    friend class FMCTongueCachedDeformationTest;
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
