#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCNutBossTypes.h"
#include "MCNutCombatEffect.generated.h"

class AMCTongue;
class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EMCNutCombatCue : uint8
{
    ChargeTell, JumpTell, SlamImpact, FireCast, FireImpact, NutRain, SummonTell, EntranceImpact, RollTell,
    CastCharge, CastRelease, RitualCast, ShieldHit, Transform, DeathBurst,
    MeleeTell, MeleeSlash, TeleportTell, TeleportBurst
};

/** One server-clock cue. Rendering peers reconstruct bounded cosmetic geometry from these values. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCNutCombatCueState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) EMCNutBossRole Role=EMCNutBossRole::Tank;
    UPROPERTY(BlueprintReadOnly) EMCNutCombatCue Type=EMCNutCombatCue::ChargeTell;
    UPROPERTY(BlueprintReadOnly) FVector Origin=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector Target=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) double StartedAt=-1;
    UPROPERTY(BlueprintReadOnly) float WindupSeconds=1;
    UPROPERTY(BlueprintReadOnly) float ActiveSeconds=0;
    UPROPERTY(BlueprintReadOnly) float Radius=100;
    UPROPERTY(BlueprintReadOnly) float DetailRadius=90;
    UPROPERTY(BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(BlueprintReadOnly) int32 DropCount=8;
    UPROPERTY(BlueprintReadOnly) float DropCadence=.5f;
    double EndsAt() const { return StartedAt+WindupSeconds+ActiveSeconds; }
};

/** Presentation only: this actor has no collision, damage or objective contribution. */
UCLASS()
class MESSCONTROL_API AMCNutCombatEffect : public AActor
{
    GENERATED_BODY()
public:
    AMCNutCombatEffect();
    static AMCNutCombatEffect* Spawn(AActor* SourceActor,AMCTongue* Tongue,EMCNutCombatCue Type,
        FVector Origin,FVector Target,float Radius,float WindupSeconds,float ActiveSeconds,int32 Seed=0,
        float DetailRadius=90,int32 DropCount=8,float DropCadence=.5f);
    static FVector GetNutRainDropPoint(FVector Center,float Radius,int32 Seed,int32 Index);
    static constexpr int32 MaxRainDrops=24;
    static constexpr float DropFlightSeconds=.8f;
    UFUNCTION(BlueprintCallable,Category="Nut Combat") void Cancel();
    /** Editor-only asset creation; existing Niagara artist edits are preserved. */
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") static bool AuthorNiagaraAssets();
    /** Cosmetic preview for transient editor actors; creates no gameplay actors or damage. */
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") void PreviewAtAge(float Age);
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated,BlueprintReadOnly) FMCNutCombatCueState Cue;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AActor> SourceActor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Warning;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> DropWarnings;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> DropShadows;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Flames;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> RainNuts;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Debris;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Magic;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Storm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> SustainedParticles;
    /** Reflected defaults keep these owned assets reachable by cooking. */
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> TelegraphMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> ShadowMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> FlameMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> MagicMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> StormMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> DustImpactSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> FireImpactSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> ArcaneBurstSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> CastChargeSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> MotionDustSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> StormMotesSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> ShieldHitSystem;
private:
    double Now() const;
    void InitializeMaterials();
    void Present(float Age);
    void Burst(FVector Point,bool bFire,float Scale=1);
    void PlayBurst(UNiagaraSystem* System,FVector Point,FVector Direction,float Scale=1);
    void PresentMagic(float Age,FVector Floor,FVector Normal,FVector Right,FVector Side,float Fade);
    UFUNCTION() void FinishedBurst(UNiagaraComponent* Component);
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WarningMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SmallWarningMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShadowMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FireMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MagicMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> StormMID;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Bursts;
    uint32 ImpactMask=0;
    int32 BuiltDropCount=0;
    bool bBurstPresented=false;
    bool bSustainedStarted=false;
    bool bReleasePresented=false;
};
