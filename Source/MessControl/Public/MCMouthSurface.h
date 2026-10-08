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
    static AMCMouthSurface* SpawnDamageUlcer(UWorld* World,FVector Point,int32 Batch=0);
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
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Liquid") FVector2D LiquidSizeRange=FVector2D(28,220);
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_LiquidSize,BlueprintReadOnly,Category="Liquid",meta=(ClampMin="20",ClampMax="260")) float LiquidHalfSize=92;
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_LiquidMaterial,BlueprintReadOnly,Category="Liquid") TSoftObjectPtr<UMaterialInterface> LiquidMaterial;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Liquid") double LiquidBornAt=-100;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Liquid") void SetLiquidAppearance(UMaterialInterface* Preset);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCToothStatusComponent> Status;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bUlcer=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Ulcer") bool bTreatmentBlocked=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") bool bTutorialLesion=false;
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
    AMCTongue* GetTongue() const { return Tongue; }
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
    struct FBrushCenter { FVector Point=FVector::ZeroVector; double At=-100; };
    TMap<TWeakObjectPtr<AActor>,FBrushCenter> BrushCenters;
    TArray<float> WipeFractional;
    TWeakObjectPtr<AMCTongue> BoundTongue;
    float Finish=0,TextureElapsed=0,GeometryElapsed=0,BrushClock=0;
    bool bWipeDirty=true;
    UPROPERTY(Replicated) TObjectPtr<class AMCTongue> Tongue;
    UPROPERTY(Replicated) FVector TongueAnchor=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> UlcerMID;
    float ContactCooldown=0;
    float PulseClock=0;
    uint64 LastTreatmentFrame=MAX_uint64;
};
