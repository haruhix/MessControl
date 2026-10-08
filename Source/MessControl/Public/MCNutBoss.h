#pragma once

#include "CoreMinimal.h"
#include "MCNutEnemy.h"
#include "MCNutBossTypes.h"
#include "MCNutBoss.generated.h"

class AMCNutRainEvent;
class AMCNutCombatEffect;
class AMCNutSpellProjectile;
class USkeletalMeshComponent;
struct FMCNutBossAnimationSnapshot;

/** Two complementary bosses share tool contact, but own distinct server attack schedules. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCNutBoss : public AMCNutEnemy
{
    GENERATED_BODY()
public:
    AMCNutBoss();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void ConfigureEncounter(AMCTongue* OnTongue,AMCNutRainEvent* OwnerEvent,EMCNutBossRole InRole,
                            const FMCNutBossSettings& InSettings,int32 Players,int32 Seed);
    virtual bool CanReceiveToolHit() const override { return IsEncounterAlive() && State!=EMCNutBossState::Falling; }
    virtual float ReceiveToolDamage(float Damage,AMCToothCharacter* Source) override;
    bool IsShieldProtectingFrom(FVector SourcePoint) const;
    bool IsPlayerInThreat(const AMCToothCharacter* Hero,FVector& EscapeDirection) const;
    FVector GetCastOrigin() const;
    void BuildAnimationSnapshot(const USkeletalMeshComponent* Model,FMCNutBossAnimationSnapshot& Snapshot) const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Shield;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Kernel;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> OpenShell;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> TankModel;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> MageModel;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> TankBall;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AMCNutRainEvent> EncounterOwner;
    UPROPERTY(ReplicatedUsing=RefreshBossPresentation, BlueprintReadOnly) FMCNutBossSettings BossSettings;
    UPROPERTY(ReplicatedUsing=RefreshBossPresentation, BlueprintReadOnly) EMCNutBossRole BossRole=EMCNutBossRole::Tank;
    UPROPERTY(ReplicatedUsing=RefreshBossPresentation, BlueprintReadOnly) EMCNutBossState State=EMCNutBossState::Falling;
    UPROPERTY(ReplicatedUsing=RefreshBossPresentation, BlueprintReadOnly) EMCNutBossAttack Attack=EMCNutBossAttack::None;
    UPROPERTY(Replicated, BlueprintReadOnly) double StateStartedAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly) double ResolveAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly) double AttackEndAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly) FVector LockedStart=FVector::ZeroVector;
    UPROPERTY(Replicated, BlueprintReadOnly) FVector LockedTarget=FVector::ZeroVector;
    UPROPERTY(Replicated, BlueprintReadOnly) FVector AttackForward=FVector::ForwardVector;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 AttackSeed=0;
    UPROPERTY(Replicated, BlueprintReadOnly) double VisualHitAt=-100;
    UPROPERTY(Replicated, BlueprintReadOnly) double ShieldHitAt=-100;
    UPROPERTY(Replicated, BlueprintReadOnly) FVector VisualHitDirection=FVector::ZeroVector;
protected:
    virtual void RefreshPresentation() override;
    virtual void Defeat() override;
private:
    UFUNCTION() void RefreshBossPresentation();
    double Now() const;
    void Enter(EMCNutBossState Next,float Seconds);
    void SelectTarget(bool bRandom);
    bool LockSurfacePoint(FVector Candidate,float Margin,FVector& Result) const;
    void BeginAttack(EMCNutBossAttack Next);
    void ExecuteAttack(float Dt);
    void ExecuteRoll(float Dt);
    void PresentTankModel(double Time);
    void CacheBossAnimations();
    void DamageRollSegment(FVector Start,FVector End);
    void Recover(float Seconds);
    void DamageArea(FVector Center,float Radius,float Damage,float Push,TSet<TWeakObjectPtr<AMCToothCharacter>>* HitSet=nullptr);
    void DamageChargeSegment(FVector Start,FVector End);
    void SummonCreeps();
    void TrackAttackActor(AActor* Actor);
    void CancelAttacks();
    bool HasLineOfSight(const AMCToothCharacter* Hero,FVector From) const;
    int32 PartyPlayers=1;
    FRandomStream Random;
    FVector EntranceLanding=FVector::ZeroVector;
    double NextTargetAt=0,NextMeleeAt=0,NextChargeAt=0,NextJumpAt=0,NextRollAt=0;
    double NextFireballAt=0,NextSummonAt=0,NextRainAt=0;
    bool bResolved=false;
    int32 RainDropsResolved=0;
    TSet<TWeakObjectPtr<AMCToothCharacter>> AttackHits;
    TMap<TWeakObjectPtr<AMCToothCharacter>,double> RainHitAt;
    TMap<TWeakObjectPtr<AMCToothCharacter>,double> RollHitAt;
    FVector TankModelScale=FVector::OneVector;
    FVector TankModelCenter=FVector::ZeroVector;
    FVector TankBallScale=FVector::OneVector,TankBallCenter=FVector::ZeroVector;
    FVector MageModelScale=FVector::OneVector,MageModelCenter=FVector::ZeroVector;
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> BossAnimations;
    UPROPERTY(Transient) TObjectPtr<USkeletalMesh> FallbackTankMesh;
    bool bFallbackTankMeshLoaded=false;
    TArray<FSoftObjectPath> CachedBossAnimationPaths;
    EMCNutBossRole CachedAnimationRole=EMCNutBossRole::Tank;
    FVector LastTankPresentationLocation=FVector::ZeroVector,TankPresentationMotion=FVector::ZeroVector;
    double LastTankPresentationAt=-1;
    bool bTankWasBall=false;
    TArray<TWeakObjectPtr<AActor>> ActiveAttacks;
    TArray<FVector> SummonPositions;
};
