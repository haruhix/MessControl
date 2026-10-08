#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCNutBossTypes.h"
#include "MCNutCombatEffect.generated.h"

class AMCTongue;
class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UNiagaraComponent;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EMCNutCombatCue : uint8 { ChargeTell, JumpTell, SlamImpact, FireCast, FireImpact, NutRain, SummonTell, EntranceImpact };

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
private:
    double Now() const;
    void Present(float Age);
    void Burst(FVector Point,bool bFire,float Scale=1);
    UFUNCTION() void FinishedBurst(UNiagaraComponent* Component);
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WarningMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SmallWarningMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShadowMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FireMID;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Bursts;
    uint32 ImpactMask=0;
    int32 BuiltDropCount=0;
    bool bBurstPresented=false;
};
