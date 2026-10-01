#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCTongueMotion.h"
#include "MCHazardWave.generated.h"
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class AMCTongue;

/** One expanding server-owned wave. A raised sole clears it; each victim is hit once. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCHazardWave : public AActor
{
    GENERATED_BODY()
public:
    AMCHazardWave();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> Ring;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite) float MaxRadius=250;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite) float Damage=12;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite) float WarningSeconds=.6f;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite) float TravelSeconds=1.1f;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite) float ClearHeight=45;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite) bool bSpicy=false;
    UPROPERTY(Replicated,BlueprintReadOnly) double StartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) int32 HitCount=0;
    UPROPERTY(Replicated) TObjectPtr<AActor> Source;
    float Radius() const;
    // Local ulcer pulses share the tongue's rendered/collision surface, while
    // this actor retains authority over timing, swept damage and jump clearance.
    bool SurfaceMotion(const AMCTongue* SurfaceTongue,float Time,FMCTongueMotionState& Out) const;
    static bool Crosses(float Before,float After,float RadiusBefore,float RadiusAfter,float HalfWidth);
private:
    double Now() const;
    void DrawRing();
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY(Replicated) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Replicated) FMCTongueMotionSettings TissueMotion;
    TSet<TWeakObjectPtr<AActor>> HitActors;
    TMap<TWeakObjectPtr<AActor>,float> PreviousDistances;
    float PreviousRadius=0,GeometryClock=0;
};
