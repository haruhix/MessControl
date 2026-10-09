#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCIceEvent.generated.h"

class AMCToothCharacter;
class AMCTongue;
class AMCLocomotionSurface;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UProceduralMeshComponent;
class UWidgetComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialParameterCollection;

UENUM(BlueprintType)
enum class EMCIceEventStage : uint8 { Idle, Arrival, Active, Complete };

USTRUCT(BlueprintType)
struct FMCPlayerFreeze
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY(BlueprintReadOnly) float Amount=0;
    UPROPERTY(BlueprintReadOnly) bool bSafe=false;
};

/** The strike stays at this tongue anchor after the warning starts. */
USTRUCT(BlueprintType)
struct FMCIcicleStrike
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FVector Anchor=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) double ImpactAt=0;
    UPROPERTY(BlueprintReadOnly) int32 Id=0;
    UPROPERTY(BlueprintReadOnly) bool bImpacted=false;
};

/** Winter prototype: break the mint, warm up in moving circles, dodge marked ice. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCIceEvent : public AActor
{
    GENERATED_BODY()
public:
    AMCIceEvent();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void Start();
    void Stop();
    bool IsComplete() const { return Stage==EMCIceEventStage::Complete && !bFailed; }
    bool IsActive() const { return Stage==EMCIceEventStage::Arrival || Stage==EMCIceEventStage::Active; }
    bool HitWithPickaxe(AMCToothCharacter* Hero,float Damage);
    float FrostAmount() const;
    float SafeRadius() const;
    bool IsSafePoint(FVector WorldPoint) const;
    float FreezeAmount(const AMCToothCharacter* Hero) const;
    FVector CandyContactPoint(FVector From) const;

    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> CandyFace;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> FloorGuides;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> FrostSurface;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Ice|Appearance") TObjectPtr<UMaterialInterface> PlayerIceMaterial;
    UPROPERTY(ReplicatedUsing=OnRep_State,BlueprintReadOnly) EMCIceEventStage Stage=EMCIceEventStage::Idle;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bFailed=false;
    UPROPERTY(Replicated,BlueprintReadOnly) float CandyHealth=960;
    UPROPERTY(Replicated,BlueprintReadOnly) float MaxCandyHealth=960;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ice",meta=(ClampMin="1")) float CandyHealthPerPlayer=960;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice",meta=(ClampMin="1",Units="s")) float ArrivalSeconds=3;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice",meta=(ClampMin="4",Units="s")) float FreezeSeconds=12;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice",meta=(ClampMin="1",Units="s")) float ThawSeconds=4;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="6",Units="s")) float CircleSeconds=14;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="1",Units="s")) float CircleOverlapSeconds=4;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="100")) float CircleRadius=330;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="80")) float MinCircleRadius=200;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin="1",Units="s")) float IcicleWarningSeconds=2.2f;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin="3",Units="s")) float IcicleIntervalSeconds=5.5f;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin="50")) float IcicleRadius=155;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin="0")) float IcicleDamage=35;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector CandyAnchor=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector SafeAnchor=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector NextSafeAnchor=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bNextCircle=false;
    UPROPERTY(Replicated,BlueprintReadOnly) double StartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) double CircleStartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) int32 CircleIndex=0;
    UPROPERTY(Replicated,BlueprintReadOnly) TArray<FMCPlayerFreeze> Players;
    UPROPERTY(Replicated,BlueprintReadOnly) TArray<FMCIcicleStrike> Strikes;
private:
    UFUNCTION() void OnRep_State();
    double Now() const;
    bool AnchorFloor(FVector Anchor,FHitResult& Hit) const;
    bool ChooseCircle(FVector& Anchor,bool bFirst=false);
    void UpdateFreeze(float DeltaSeconds);
    void QueueIcicle();
    void ResolveIcicle(FMCIcicleStrike& Strike);
    void Finish(bool bFailure=false);
    void ClearGameplay();
    void ClearPresentation();
    void RefreshPresentation(float DeltaSeconds);
    void RefreshFloorGuides();
    void RefreshFrostSurface();
    void RefreshFreezeBars();
    void RefreshIceCoatings();
    void PublishManualHUD();
    void BuildCandyFace();
    void SetClimate();
    UPROPERTY() TObjectPtr<AMCLocomotionSurface> SlipperyFloor;
    UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> GuideMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> IcicleMaterial;
    UPROPERTY() TObjectPtr<UMaterialParameterCollection> Climate;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> IcicleMeshes;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidgetComponent>> FreezeBars;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> IceCoatings;
    TArray<TWeakObjectPtr<AMCToothCharacter>> BarHeroes;
    FRandomStream Random;
    double NextIcicleAt=0;
    int32 StrikeSerial=0;
    float GuideElapsed=0;
    float FreezeSendElapsed=0;
};
