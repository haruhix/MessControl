#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "MCFrostNovaVFXComponent.generated.h"

class AMCHazardWave;
class AMCTongue;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMesh;

/** A gameplay warning must remain visible while an uncached PSO is prepared. */
UCLASS()
class UMCFrostNovaFrontComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()
protected:
    virtual bool UsePSOPrecacheRenderProxyDelay() const override { return false; }
};

/** Local cosmetics only. The caller supplies the authoritative ground-wave clock and radius. */
UCLASS(ClassGroup=(VFX), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCFrostNovaVFXComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCFrostNovaVFXComponent();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    /** False keeps the caller's existing warning visible when optional art is unavailable. */
    bool UpdateWave(const AMCHazardWave& Wave);
    /** Prepare this exact mesh/material/instance and Niagara controller before the pulse. */
    bool PrepareForWave() { return PrepareVisuals(); }
    bool PresentWave(AMCTongue* Tongue, FVector Origin, float Radius, float MaxRadius,
        double ImpactAt, float TravelSeconds, float WarningProgress);
    void Stop();
    bool HasRequiredAssets() const;

    UPROPERTY(EditDefaultsOnly, Category="Frost Nova") TSoftObjectPtr<UMaterialInterface> FrontMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Frost Nova") TSoftObjectPtr<UMaterialInterface> CrystalMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Frost Nova") TSoftObjectPtr<UStaticMesh> CrystalMesh;
    UPROPERTY(EditDefaultsOnly, Category="Frost Nova") TSoftObjectPtr<UNiagaraSystem> BurstSystem;
    UPROPERTY(EditDefaultsOnly, Category="Frost Nova", meta=(ClampMin="8", ClampMax="32")) int32 CrystalCount=24;

private:
    struct FCrystalPose
    {
        int32 Pulse=INDEX_NONE;
        bool bOnFloor=false;
        FVector Point=FVector::ZeroVector;
        FVector Normal=FVector::UpVector;
        FQuat Rotation=FQuat::Identity;
        FVector Size=FVector::OneVector;
    };
    bool PrepareVisuals();
    void DrawFront(AMCTongue* Tongue,FVector Origin,float Radius,float MaxRadius,float Age,bool bWarning,float WarningProgress);
    void UpdateCrystals(AMCTongue* Tongue,FVector Origin,float MaxRadius,float Age,float TravelSeconds);
    double ServerNow() const;

    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> LoadedAssets;
    UPROPERTY(Transient) TObjectPtr<UMCFrostNovaFrontComponent> Front;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Crystals;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FrontMID;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Burst;
    TWeakObjectPtr<AMCTongue> CachedTongue;
    TArray<FCrystalPose> CrystalPoses;
    double WaveImpactAt=-1;
    double NextGeometryAt=-1;
    int32 WaveSeed=0;
    bool bTriedAssets=false;
    bool bBurstPlayed=false;
};
