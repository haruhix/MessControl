#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
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
class AMCIceEventVFX;

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

/** A pickaxe-breakable crystal disables only the circle which owns it. */
USTRUCT(BlueprintType)
struct FMCIceZoneCrystal
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FVector Anchor=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) int32 ZoneIndex=0;
    UPROPERTY(BlueprintReadOnly) float Health=80;
};

/** Break the central crystal, transfer between warm zones and dodge cold spells. */
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
    bool HitObstructionWithPickaxe(AMCToothCharacter* Hero,float Damage,FVector& Point);
    float FrostAmount() const;
    float CircleDuration(int32 Index) const;
    float SafeRadius() const;
    float NextSafeRadius() const;
    bool IsCircleBlocked(int32 Index) const;
    bool IsSafePoint(FVector WorldPoint) const;
    bool IsInsideNova(FVector WorldPoint) const;
    float FreezeAmount(const AMCToothCharacter* Hero) const;
    FVector CandyContactPoint(FVector From) const;

    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> CandyFace;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> FloorGuides;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> FrostSurface;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> NovaWind;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Ice|Appearance") TObjectPtr<UMaterialInterface> PlayerIceMaterial;
    UPROPERTY(ReplicatedUsing=OnRep_State,BlueprintReadOnly) EMCIceEventStage Stage=EMCIceEventStage::Idle;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bFailed=false;
    UPROPERTY(Replicated,BlueprintReadOnly) float CandyHealth=960;
    UPROPERTY(Replicated,BlueprintReadOnly) float MaxCandyHealth=960;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ice",meta=(ClampMin="1")) float CandyHealthPerPlayer=960;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice",meta=(ClampMin="1",Units="s")) float ArrivalSeconds=3;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice",meta=(ClampMin="4",Units="s")) float FreezeSeconds=12;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice",meta=(ClampMin="1",Units="s")) float ThawSeconds=4;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="20",Units="s")) float CircleSeconds=45;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="0",Units="s")) float CircleStepSeconds=5;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="20",Units="s")) float MinCircleSeconds=20;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="1",Units="s")) float CircleOverlapSeconds=10;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="100")) float CircleRadius=330;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Circles",meta=(ClampMin="80")) float MinCircleRadius=200;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin="1",Units="s")) float IcicleWarningSeconds=2.2f;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin="3",Units="s")) float IcicleIntervalSeconds=12;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin=".5",Units="s")) float IcicleSeriesSpacingSeconds=1.2f;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin="50")) float IcicleRadius=155;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Icicles",meta=(ClampMin="0")) float IcicleDamage=35;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Crystals",meta=(ClampMin="4",Units="s")) float ZoneCrystalIntervalSeconds=16;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Crystals",meta=(ClampMin="1")) float ZoneCrystalHealth=80;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Nova",meta=(ClampMin="4",Units="s")) float NovaIntervalSeconds=18;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Nova",meta=(ClampMin="1",Units="s")) float NovaWarningSeconds=2.5f;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Nova",meta=(ClampMin="100")) float NovaRange=2600;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Nova",meta=(ClampMin="10",ClampMax="70")) float NovaHalfAngleDegrees=35;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="Ice|Nova",meta=(ClampMin="1")) float NovaFootHealth=80;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector CandyAnchor=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector SafeAnchor=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector NextSafeAnchor=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bNextCircle=false;
    UPROPERTY(Replicated,BlueprintReadOnly) double StartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) double CircleStartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) double NextCircleStartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) int32 CircleIndex=0;
    UPROPERTY(Replicated,BlueprintReadOnly) TArray<FMCPlayerFreeze> Players;
    UPROPERTY(Replicated,BlueprintReadOnly) TArray<FMCIcicleStrike> Strikes;
    UPROPERTY(Replicated,BlueprintReadOnly) TArray<FMCIceZoneCrystal> ZoneCrystals;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bNovaWarning=false;
    UPROPERTY(Replicated,BlueprintReadOnly) double NovaImpactAt=0;
    /** Direction in tongue space: the warning follows its surface, never the target. */
    UPROPERTY(Replicated,BlueprintReadOnly) FVector NovaDirection=FVector::ForwardVector;
private:
    UFUNCTION() void OnRep_State();
    UFUNCTION(NetMulticast,Reliable) void Multicast_FinishVFX(FVector_NetQuantize CoreFloorPoint);
    UFUNCTION(NetMulticast,Reliable) void Multicast_CancelVFX();
    void EnsurePresentationVFX();
    double Now() const;
    bool AnchorFloor(FVector Anchor,FHitResult& Hit) const;
    bool ChooseCircle(FVector& Anchor,bool bFirst=false);
    void UpdateFreeze(float DeltaSeconds);
    AMCToothCharacter* ChooseTarget();
    void UpdateCircles(double Time);
    void QueueZoneCrystal();
    void QueueIcicle(AMCToothCharacter* Target);
    void UpdateIcicleSeries(double Time);
    void ResolveIcicle(FMCIcicleStrike& Strike);
    void BeginNova();
    void ResolveNova();
    void RefreshZoneCrystals();
    void RefreshNovaWind();
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
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ZoneCrystalMeshes;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidgetComponent>> FreezeBars;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> IceCoatings;
    UPROPERTY(Transient) TObjectPtr<AMCIceEventVFX> EventVFX;
    bool bCompletionVFXSignaled=false;
    TArray<TWeakObjectPtr<AMCToothCharacter>> BarHeroes;
    TArray<TWeakObjectPtr<AMCToothCharacter>> FrozenLegHeroes;
    FRandomStream Random;
    double NextIcicleAt=0;
    double NextSeriesShotAt=0;
    double NextZoneCrystalAt=0;
    double NextNovaAt=0;
    TWeakObjectPtr<AMCToothCharacter> SeriesTarget;
    int32 SeriesShotsLeft=0;
    int32 StrikeSerial=0;
    float GuideElapsed=0;
    float FreezeSendElapsed=0;
};
