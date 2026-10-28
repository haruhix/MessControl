#pragma once

#include "CoreMinimal.h"
#include "MCNutEnemy.h"
#include "MCNutBossTypes.h"
#include "MCNutBoss.generated.h"

class AMCNutRainEvent;
class AMCArenaTooth;
class AMCNutCombatEffect;
class AMCNutSpellProjectile;
class USceneComponent;
class USkeletalMeshComponent;
class AMCVFXLabBossStation;
struct FMCNutBossAnimationSnapshot;

/** The encounter knight transforms into the wizard; standalone roles retain their own schedules. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCNutBoss : public AMCNutEnemy
{
    GENERATED_BODY()
public:
    AMCNutBoss();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void PostNetReceiveLocationAndRotation() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void ConfigureEncounter(AMCTongue* OnTongue,AMCNutRainEvent* OwnerEvent,EMCNutBossRole InRole,
                            const FMCNutBossSettings& InSettings,int32 Players,int32 Seed);
    virtual bool IsEncounterAlive() const override { return (State==EMCNutBossState::Transforming && !bDefeated && !IsActorBeingDestroyed()) || Super::IsEncounterAlive(); }
    virtual bool CanReceiveToolHit() const override { return IsEncounterAlive() && State!=EMCNutBossState::Falling && State!=EMCNutBossState::Transforming; }
    static constexpr float MagePhaseSeconds=2.4f;
    static constexpr float HeadSplitAt=.42f;
    static constexpr float MageRevealAt=.9f;
    virtual float ReceiveToolDamage(float Damage,AMCToothCharacter* Source) override;
    /** Intact knight shields protect HP from every direction and combat pose. */
    bool IsShieldProtectingFrom(FVector SourcePoint) const;
    bool IsPlayerInThreat(const AMCToothCharacter* Hero,FVector& EscapeDirection) const;
    /** Counter resistance prevents hit reactions, not incoming damage or the guaranteed shield-break window. */
    bool IsCounterShoving() const;
    int32 GetTankPressureHits() const { return TankPressureHits; }
    UFUNCTION(BlueprintPure,Category="Nut Combat") float GetAttackCooldownRemaining(EMCNutBossAttack Ability) const;
    FVector GetCastOrigin() const;
    /** Cosmetic focus follows the authored palms; projectile origin remains server-defined. */
    FVector GetSpellFocusOrigin() const;
    void BuildAnimationSnapshot(const USkeletalMeshComponent* Model,FMCNutBossAnimationSnapshot& Snapshot) const;
    /** Client smoothing moves only this collision-free visual subtree. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USceneComponent> PresentationRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Shield;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Kernel;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> OpenShell;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> TankModel;
    /** Floor-level turning axis underneath the nut, independent of weapon/shield bounds. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USceneComponent> TankTurnPivot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> MageModel;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> TankBall;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AMCNutRainEvent> EncounterOwner;
    UPROPERTY(ReplicatedUsing=RefreshBossPresentation, BlueprintReadOnly) FMCNutBossSettings BossSettings;
    UPROPERTY(ReplicatedUsing=RefreshBossPresentation, BlueprintReadOnly) EMCNutBossRole BossRole=EMCNutBossRole::Tank;
    /** Only the event's opening knight has a second phase; preview/standalone bosses do not. */
    UPROPERTY(Replicated, BlueprintReadOnly) bool bHasMagePhase=false;
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
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Animation") bool bLastToolHitBlocked=false;
    /** 0/1: one authored swing, 2: three contacts ending in a ground slam. */
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Animation") uint8 TankMeleeVariant=0;
    /** Initial acquisition and reselecting the same player are not switches. */
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Animation") int32 TargetSwitchCount=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Animation") uint8 HeadTurnVariant=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Shield") double ShieldBrokenAt=-100;
    UPROPERTY(ReplicatedUsing=RefreshBossPresentation, BlueprintReadOnly,Category="Shield") float ShieldHealth=500;
    UPROPERTY(Replicated, BlueprintReadOnly) FVector VisualHitDirection=FVector::ZeroVector;
    // Absolute server-clock readiness is shared with clients and the diagnostic UI.
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextMeleeAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextChargeAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextJumpAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextRollAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextFireballAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextSummonAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextRainAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextTeleportAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly,Category="Cooldown") double NextSpecialAt=0;
protected:
    virtual void RefreshPresentation() override;
    virtual void ReportDamageFeedback(AMCToothCharacter* Source,float Applied) override;
    virtual void Defeat() override;
private:
    // A placed laboratory station schedules real attacks without the encounter AI.
    friend class AMCVFXLabBossStation;
    friend class FMCEnamelContactLatchTest;
    TWeakObjectPtr<AMCVFXLabBossStation> LabStation;
    TWeakObjectPtr<AMCToothCharacter> LabTarget;
    int32 LabTelegraphs=0,LabExecutions=0,LabRecoveries=0;
    UFUNCTION() void RefreshBossPresentation();
    double Now() const;
    void Enter(EMCNutBossState Next,float Seconds);
    void SelectTarget(bool bRandom);
    void RecordTargetSwitch(const AMCToothCharacter* PreviousTarget);
    void TurnTowards(FVector Direction,float Dt);
    bool LockSurfacePoint(FVector Candidate,float Margin,FVector& Result) const;
    void BeginAttack(EMCNutBossAttack Next);
    void RegisterTankPressureHit();
    void ResetTankPressure();
    bool TryBeginTankCounter();
    void ExecuteAttack(float Dt);
    void ExecuteRoll(float Dt);
    /** Solid to ordinary character movement; attack sweeps resolve player hits separately. */
    void UpdatePlayerBlocking();
    void MoveAttackBody(FVector Goal,FHitResult& Hit);
    void PresentTankModel(double Time);
    void TickClientPresentation(float Dt);
    void ResetClientPresentation();
    void BeginMageTransformation();
    void RevealMagePhase();
    void CacheBossAnimations();
    void DamageRollSegment(FVector Start,FVector End);
    void PushPlayer(AMCToothCharacter* Hero,FVector Direction,float Speed);
    bool IsMageFocused() const;
    bool FindTeleportPoint(FVector& Result);
    bool IsTeleportPointFree(FVector Point) const;
    void BeginTeleport();
    void HandleRollToothImpact(const FHitResult& Hit);
    void Recover(float Seconds);
    float RecoverySeconds(float Requested) const;
    float MageAttackAnimationSeconds() const;
    float TankMeleeSeconds() const;
    float TankMeleeContactFraction(int32 Contact) const;
    float TankMeleeContactOffset(int32 Contact) const;
    float TankMeleeActiveSeconds() const;
    float DeathSeconds() const;
    void StartAttackCooldown(double RecoveryEndsAt);
    void DamageArea(FVector Center,float Radius,float Damage,float Push,TSet<TWeakObjectPtr<AMCToothCharacter>>* HitSet=nullptr);
    void PushNutsAlongSegment(FVector Start,FVector End,float Seconds);
    void PushNutsInArea(FVector Center,float Radius,float Speed,bool bForwardOnly=false);
    TMap<TWeakObjectPtr<AMCFoodActor>,double> NutContactAt;
    void DamageChargeSegment(FVector Start,FVector End);
    void SummonCreeps();
    void TrackAttackActor(AActor* Actor);
    void CancelAttacks();
    bool HasLineOfSight(const AMCToothCharacter* Hero,FVector From) const;
    int32 PartyPlayers=1;
    FRandomStream Random;
    FVector EntranceLanding=FVector::ZeroVector;
    double NextTargetAt=0,NextRollPainAt=0;
    double FocusWindowStartedAt=-100;
    int32 FocusHits=0;
    int32 TankPressureHits=0,TankCounterThreshold=0;
    double LastTankPressureHitAt=-100,NextTankCounterAt=0;
    bool bTankCounterQueued=false;
    bool bMageHitQueued=false,bMageTeleportQueued=false;
    bool bMeleeBetweenAbilities=true;
    bool bResolved=false;
    int32 RainDropsResolved=0;
    int32 MeleeHitsResolved=0;
    uint32 TankMeleeSequence=0;
    TArray<uint8> PendingHeadTurns;
    float PlannedRollSeconds=12;
    TSet<TWeakObjectPtr<AMCToothCharacter>> AttackHits;
    TMap<TWeakObjectPtr<AMCToothCharacter>,double> RainHitAt;
    TMap<TWeakObjectPtr<AMCToothCharacter>,double> RollHitAt;
    TMap<TWeakObjectPtr<AMCArenaTooth>,double> RollToothHitAt;
    FVector TankModelScale=FVector::OneVector;
    FVector TankModelGroundAnchor=FVector::ZeroVector;
    FVector TankBallScale=FVector::OneVector,TankBallCenter=FVector::ZeroVector;
    FVector MageModelScale=FVector::OneVector,MageModelCenter=FVector::ZeroVector;
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> BossAnimations;
    UPROPERTY(Transient) TObjectPtr<USkeletalMesh> FallbackTankMesh;
    UPROPERTY(Transient) TObjectPtr<USkeletalMesh> UnshieldedTankMesh;
    FSoftObjectPath CachedUnshieldedTankMeshPath;
    bool bFallbackTankMeshLoaded=false;
    TArray<FSoftObjectPath> CachedBossAnimationPaths;
    EMCNutBossRole CachedAnimationRole=EMCNutBossRole::Tank;
    FVector LastTankPresentationLocation=FVector::ZeroVector,TankPresentationMotion=FVector::ZeroVector;
    double LastTankPresentationAt=-1;
    double LastClientMovementAt=-1;
    FVector LastTankRollLocation=FVector::ZeroVector;
    FQuat TankRollWorldRotation=FQuat::Identity;
    bool bTankRollRotationInitialized=false;
    bool bTankWasBall=false;
    TArray<TWeakObjectPtr<AActor>> ActiveAttacks;
    TArray<FVector> SummonPositions;
};
