#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCThroatVortex.generated.h"

class AMCFoodActor;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UAudioComponent;
class USoundBase;
struct FMCThroatVortexGeometry;
struct FMCThroatVortexGeometryDeleter
{
    void operator()(FMCThroatVortexGeometry* Geometry) const;
};

/** One shared, collisionless intake effect per meal. Cosmetic cost is independent of meal size. */
UCLASS()
class MESSCONTROL_API AMCThroatVortex : public AActor
{
    GENERATED_BODY()
public:
    AMCThroatVortex();
    virtual ~AMCThroatVortex() override;
    static AMCThroatVortex* Spawn(UWorld* World, FVector Inlet, FVector InwardDirection,
        float Radius, float FlowLength, float Duration, const TArray<AMCFoodActor*>& FoodSources,
        float HalfWidth=0.f, float Height=0.f);
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Airflow;
    /** Local room cue on every peer; no distance culling or server audio voice. */
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAudioComponent> SuctionAudio;
    UPROPERTY(EditDefaultsOnly, Category="Throat|VFX") TSoftObjectPtr<UMaterialInterface> RibbonMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Throat|VFX") TSoftObjectPtr<UMaterialInterface> MistMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Throat|VFX") TSoftObjectPtr<UMaterialInterface> GlowMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Throat|Audio") TSoftObjectPtr<USoundBase> IntakeSound;
    UPROPERTY(EditDefaultsOnly, Category="Throat|Audio") TSoftObjectPtr<USoundBase> AirflowSound;
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) float Duration=2;
    UPROPERTY(Replicated) float Radius=260;
    UPROPERTY(Replicated) float FlowLength=600;
    /** Mouth-wide field dimensions; Radius remains the near-inlet curl size. */
    UPROPERTY(Replicated) float FlowHalfWidth=600;
    UPROPERTY(Replicated) float FlowHeight=260;
    UPROPERTY(Replicated) FVector InwardDirection=FVector::ForwardVector;
    /** Sampled on the server once; at most 24 food wakes, even with hundreds of fragments. */
    UPROPERTY(Replicated) TArray<TObjectPtr<AMCFoodActor>> FoodSources;

private:
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    TUniquePtr<FMCThroatVortexGeometry,FMCThroatVortexGeometryDeleter> Geometry;
    float NextAirflowCueAt=0;
    double ServerNow() const;
};
