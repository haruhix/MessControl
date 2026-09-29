#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "MCLocomotionSurface.h"
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
    AMCMouthSurface();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Area;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Ulcer") TObjectPtr<class UDecalComponent> UlcerDecal;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> Liquid;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Liquid") bool bShowCareLabel=false;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Liquid") EMCGroundSurface GroundResponse=EMCGroundSurface::Slippery;
    bool AffectsFooting(FVector Sole) const;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Liquid") bool bRandomizeLiquidSize=true;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Liquid") FVector2D LiquidSizeRange=FVector2D(28,220);
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_LiquidSize,BlueprintReadOnly,Category="Liquid",meta=(ClampMin="20",ClampMax="260")) float LiquidHalfSize=92;
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_LiquidMaterial,BlueprintReadOnly,Category="Liquid") TSoftObjectPtr<UMaterialInterface> LiquidMaterial;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Liquid") void SetLiquidAppearance(UMaterialInterface* Preset);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCToothStatusComponent> Status;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bUlcer=false;
    UPROPERTY(Replicated, BlueprintReadOnly) float Healing=0;
    UPROPERTY(Replicated) float HealSeconds=15;
    UPROPERTY(Replicated) float DamagePerSecond=.35f;
    UPROPERTY(Replicated) float DisturbDamage=1;
    UPROPERTY(Replicated) bool bDisturbed=false;
    UPROPERTY(Replicated) int32 Batch=0;
    bool IsClean() const;
    void Disturb();
    void ResetLiquid();
    // Called only after the character's authoritative reach/tool/occlusion checks.
    bool BrushLiquid(class AMCToothCharacter* Worker,float Seconds);
    bool FindDirtyContact(class AMCToothCharacter* Worker,FVector& Point,FVector& Normal) const;
    float RemainingLiquid() const;
    UPROPERTY(ReplicatedUsing=OnRep_Wipe,BlueprintReadOnly,Category="Liquid") TArray<uint8> WipeMask;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Liquid") int32 LiquidSeed=0;
private:
    UFUNCTION() void OnRep_Wipe();
    UFUNCTION() void OnRep_LiquidSize();
    UFUNCTION() void OnRep_LiquidMaterial();
    void BuildLiquid();
    void UpdateLiquid(float Dt);
    void UploadWipe();
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
    TMap<TWeakObjectPtr<AActor>,FVector2D> PreviousBrush;
    TWeakObjectPtr<AMCTongue> BoundTongue;
    float Finish=0,TextureElapsed=0,GeometryElapsed=0,BrushClock=0;
    bool bWipeDirty=true;
    UPROPERTY(Replicated) TObjectPtr<class AMCTongue> Tongue;
    UPROPERTY(Replicated) FVector TongueAnchor=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> UlcerMID;
    float ContactCooldown=0;
};
