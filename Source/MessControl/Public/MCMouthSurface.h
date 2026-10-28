#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "MCLocomotionSurface.h"
#include "MCResultPassives.h"
#include "MCMouthSurface.generated.h"
class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMCToothStatusComponent;
class UMaterialInstanceDynamic;
class AMCTongue;

/** A replaceable stain/ulcer on walkable mouth tissue, independent of the arena mesh. */
UCLASS()
class MESSCONTROL_API AMCMouthSurface : public AActor
{
    GENERATED_BODY()
public:
    static AMCMouthSurface* SpawnDamageUlcer(UWorld* World,FVector Point,int32 Batch=0);
    static AMCMouthSurface* SpawnExperimentalHugeStain(UWorld* World,FVector Near);
    AMCMouthSurface();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Area;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Ulcer") TObjectPtr<class UDecalComponent> UlcerDecal;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Ulcer") TObjectPtr<class UWidgetComponent> TreatmentIndicator;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> Liquid;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Liquid") bool bShowCareLabel=false;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Liquid") EMCGroundSurface GroundResponse=EMCGroundSurface::Slippery;
    bool AffectsFooting(FVector Sole) const;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Liquid") bool bRandomizeLiquidSize=true;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Liquid") FVector2D LiquidSizeRange=FVector2D(180,440);
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_LiquidSize,BlueprintReadOnly,Category="Liquid",meta=(ClampMin="20",ClampMax="1600")) float LiquidHalfSize=240;
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_LiquidMaterial,BlueprintReadOnly,Category="Liquid") TSoftObjectPtr<UMaterialInterface> LiquidMaterial;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Liquid") double LiquidBornAt=-100;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Liquid") void SetLiquidAppearance(UMaterialInterface* Preset);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCToothStatusComponent> Status;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bUlcer=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Ulcer") bool bTreatmentBlocked=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") bool bTutorialLesion=false;
    /** Toothpick wounds react only to a fresh player step, never timed pulses or food contact. */
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Ulcer") bool bContactPainOnly=false;
    UPROPERTY(Replicated, BlueprintReadOnly) float Healing=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Ulcer") double NumbUntil=0;
    UFUNCTION(BlueprintPure,Category="Ulcer") bool IsNumb() const;
    bool ApplyAnesthetic(float Seconds);
    bool Treat(class AMCToothCharacter* Worker,float Seconds,float Power=1.f);
    bool IsBurning() const;
    bool IsHealed() const { return bUlcer && Healing>=1.f; }
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Ulcer") float HealSeconds=7;
    UPROPERTY(Replicated) float DamagePerSecond=.35f;
    UPROPERTY(Replicated) float DisturbDamage=1;
    UPROPERTY(Replicated) bool bDisturbed=false;
    UPROPERTY(Replicated) int32 Batch=0;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ulcer") float PulseInterval=3;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ulcer") float PulseRadius=260;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ulcer") float PulseDamage=12;
    bool IsClean() const;
    /** Event paint still cleans/counts normally; it does not send the currency-like success burst. */
    bool ShouldShowCleaningCompletionFeedback() const { return !ActorHasTag(TEXT("MCCataclysmDirt")); }
    AMCTongue* GetTongue() const { return Tongue; }
    void Disturb();
    void ResetLiquid();
    void ClearLiquidForBooster();
    // Initial wet coverage, clipped to the path of the liquid that deposited it.
    void SetLiquidDepositMask(const TArray<uint8>& Mask);
    /** Grow a source's footprint without restoring its already-cleaned coverage. */
    bool AppendLiquidDepositMask(const TArray<uint8>& Mask);
    /** Bounded shared paint canvas; its shape is the mask, rather than a seeded puddle outline. */
    UPROPERTY(ReplicatedUsing=OnRep_LiquidSize,BlueprintReadOnly,Category="Liquid") bool bPaintTile=false;
    void InitializePaintTile(AMCTongue* InTongue);
    bool DepositPaintStroke(FVector From,FVector To,float Radius,const TFunction<bool(FVector)>& CoverageAllowed={});
    void FlushPaintDeposit();
    UFUNCTION(BlueprintPure,Category="Liquid") bool IsPaintWetAt(FVector Point) const;
    UFUNCTION(BlueprintPure,Category="Liquid") bool GetNearestPaintPoint(FVector Near,FVector& Point,FVector& Normal) const;
    // Called only after the character's authoritative reach/tool/occlusion checks.
    bool BrushLiquid(class AMCToothCharacter* Worker,float Seconds);
    /** Spatial cleaning after the authoritative spray/residue reach and occlusion checks. */
    bool CleanLiquidAt(class AMCToothCharacter* Worker,FVector Point,float Radius,float Seconds,float Rate,
        EMCStainCleanReason Reason=EMCStainCleanReason::Spray);
    bool CleanLiquidFromPassive(class AMCToothCharacter* Worker);
    bool FindDirtyContact(class AMCToothCharacter* Worker,FVector& Point,FVector& Normal) const;
    float RemainingLiquid() const;
    UPROPERTY(ReplicatedUsing=OnRep_Wipe,BlueprintReadOnly,Category="Liquid") TArray<uint8> WipeMask;
    UPROPERTY(ReplicatedUsing=OnRep_Wipe) TArray<uint8> LiquidDepositMask;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Liquid") int32 LiquidSeed=0;
private:
    bool ApplyLiquidCleaning(class AMCToothCharacter* Worker,FVector2D From,FVector2D To,FVector Point,
        float RadiusUV,float Seconds,float Rate,EMCStainCleanReason Reason);
    void SyncLiquidCleaningLayers(class AMCToothCharacter* Worker,EMCStainCleanReason Reason);
    UFUNCTION() void OnRep_Wipe();
    UFUNCTION() void OnRep_LiquidSize();
    UFUNCTION() void OnRep_LiquidMaterial();
    UFUNCTION() void OnRep_SurfaceAnchor();
    void BuildLiquid();
    void UpdateLiquid(float Dt);
    void UploadWipe();
    // Relocate only proven unsupported ordinary stains; never award cleanup.
    void RecoverUnsupportedFloorStain(float Dt);
    float FloorAccessElapsed=0,UnsupportedFloorSeconds=0;
    uint32 FloorRecoveryAttempts=0;
    double NextFloorRecoveryWarningAt=0;
    // The paint mesh is clipped to upward tissue triangles. Its gameplay mask
    // must have the same support, including tiles crossing a tongue edge.
    bool EnsurePaintSupport();
    TArray<uint8> PaintSupportMask;
    TWeakObjectPtr<class UStaticMesh> PaintSupportSource;
    FTransform PaintSupportTileTransform=FTransform::Identity,PaintSupportTongueTransform=FTransform::Identity;
    int32 PaintSupportVertexCount=0,PaintSupportIndexCount=0;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LiquidMID;
    UPROPERTY() TObjectPtr<class UTexture2D> WipeTexture;
    UPROPERTY(Replicated) FVector2D BrushUV=FVector2D(.5,.5);
    UPROPERTY(Replicated) FVector2D BrushDirection=FVector2D(1,0);
    UPROPERTY(Replicated) float BrushAt=-100;
    struct FSurfaceBinding { int32 Face=INDEX_NONE; FVector Bary=FVector::ZeroVector; FVector Fallback=FVector::ZeroVector; };
    TArray<FSurfaceBinding> SurfaceBindings;
    TArray<FVector> LiquidVertices,LiquidNormals;
    TArray<FVector2D> LiquidUV;
    TArray<FProcMeshTangent> LiquidTangents;
    struct FBrushStroke { FVector2D UV=FVector2D::ZeroVector; FVector WorldPoint=FVector::ZeroVector; double At=-100,ApproachStartedAt=-100; };
    TMap<TWeakObjectPtr<AActor>,FBrushStroke> PreviousBrush;
    struct FBrushCenter { FVector Point=FVector::ZeroVector; double At=-100; };
    TMap<TWeakObjectPtr<AActor>,FBrushCenter> BrushCenters;
    TArray<float> WipeFractional;
    TWeakObjectPtr<AMCTongue> BoundTongue;
    TWeakObjectPtr<class UStaticMesh> BoundTongueSource;
    int32 BoundTongueVertexCount=0,BoundTongueIndexCount=0;
    uint64 PaintSurfaceRevision=MAX_uint64;
    FTransform PaintGeometryTransform=FTransform::Identity,PaintTongueTransform=FTransform::Identity;
    float Finish=0,TextureElapsed=0,GeometryElapsed=0,BrushClock=0;
    bool bWipeDirty=true;
    mutable bool bRemainingLiquidDirty=true;
    mutable float CachedRemainingLiquid=0;
    bool bPaintDepositPending=false;
    UPROPERTY(ReplicatedUsing=OnRep_SurfaceAnchor) TObjectPtr<class AMCTongue> Tongue;
    UPROPERTY(ReplicatedUsing=OnRep_SurfaceAnchor) FVector TongueAnchor=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> UlcerMID;
    float ContactCooldown=0;
    float PulseClock=0;
    uint64 LastTreatmentFrame=MAX_uint64;
    TSet<TWeakObjectPtr<class AMCToothCharacter>> FootContacts;
};
