#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MCBossProfile.h"
#include "MCBossCharacter.generated.h"

class AMCToothCharacter;
class AMCBossAIController;
class UCapsuleComponent;
class UMCBossFaceComponent;

/** One replicated snapshot keeps phase and attack presentation coherent on clients. Times use server world time. */
USTRUCT(BlueprintType)
struct FMCBossRuntimeState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) EMCBossState State=EMCBossState::Dormant;
    UPROPERTY(BlueprintReadOnly) float Health=600.f;
    UPROPERTY(BlueprintReadOnly) float MaxHealth=600.f;
    UPROPERTY(BlueprintReadOnly) int32 Phase=0;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AMCToothCharacter> Target;
    UPROPERTY(BlueprintReadOnly) FName AttackId;
    UPROPERTY(BlueprintReadOnly) int32 AttackSerial=0;
    UPROPERTY(BlueprintReadOnly) double StateStartedAt=0;
    UPROPERTY(BlueprintReadOnly) double StateEndsAt=0;
    UPROPERTY(BlueprintReadOnly) FVector AttackForward=FVector::ForwardVector;
    /** Strike clip start; may be in the future during the configured pre-attack pause. */
    UPROPERTY(BlueprintReadOnly) double AttackStartedAt=0;
    UPROPERTY(BlueprintReadOnly) double HurtStartedAt=-1000;
    UPROPERTY(BlueprintReadOnly) EMCBossAnimationPreview AnimationPreview=EMCBossAnimationPreview::None;
    UPROPERTY(BlueprintReadOnly) int32 PreviewSerial=0;
    UPROPERTY(BlueprintReadOnly) double PreviewStartedAt=0;
};

UCLASS(Blueprintable)
class MESSCONTROL_API AMCBossCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AMCBossCharacter();
    /** Combat volume follows the torso; the narrower root capsule remains the navigation shape. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boss|Collision") TObjectPtr<UCapsuleComponent> BodyHitbox;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boss|Presentation") TObjectPtr<UMCBossFaceComponent> Face;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual float TakeDamage(float DamageAmount,const FDamageEvent& DamageEvent,AController* EventInstigator,AActor* DamageCauser) override;
    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Profile, BlueprintReadOnly, Category="Boss") TSoftObjectPtr<UMCBossProfile> Profile;
    /** Retained for old packages. Bosses always start dormant; activation is an explicit dev action. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss", meta=(DeprecatedProperty, DeprecationMessage="Use the explicit F3 AI test.")) bool bStartAwake=false;
    UPROPERTY(ReplicatedUsing=OnRep_Runtime, BlueprintReadOnly, Category="Boss") FMCBossRuntimeState Runtime;
    UFUNCTION(BlueprintPure, Category="Boss") bool IsBossAlive() const { return Runtime.Health>0.f && Runtime.State!=EMCBossState::Dead; }
    UFUNCTION(BlueprintPure, Category="Boss|Collision") bool CanReceiveWeaponHit() const { return IsBossAlive() && Runtime.AnimationPreview==EMCBossAnimationPreview::None; }
    /** Closest point on the actual torso capsule, rather than its enclosing axis-aligned box. */
    FVector GetMeleeTargetPoint(const FVector& Source) const;
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Boss") void ActivateBoss();
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Boss") void DeactivateBoss();
    /** Restart the encounter without activating it, including a previously killed boss. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Boss") void ResetForRun();
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Boss|Debug") bool PreviewAnimation(EMCBossAnimationPreview Preview);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Boss") float ReceiveBossDamage(float Damage,AActor* DamageCauser);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Boss") bool BeginAttack(FName AttackId,AMCToothCharacter* Target);
    UFUNCTION(BlueprintPure, Category="Boss") float GetStateAge() const;
    /** Executes only on authority and only once per attack. Overrides own authoritative damage, projectiles or hazards. */
    UFUNCTION(BlueprintNativeEvent, Category="Boss|Attacks") void ExecuteAttack(const FMCBossAttackDefinition& Attack,AMCToothCharacter* Target);
    virtual void ExecuteAttack_Implementation(const FMCBossAttackDefinition& Attack,AMCToothCharacter* Target);
    UFUNCTION(BlueprintImplementableEvent, Category="Boss|Presentation") void OnBossStateChanged(const FMCBossRuntimeState& State);
    UFUNCTION(BlueprintImplementableEvent, Category="Boss|Presentation") void OnBossHealthChanged(float Health,float MaxHealth);
    UFUNCTION(BlueprintImplementableEvent, Category="Boss|Presentation") void OnBossPhaseChanged(int32 Phase);
    /** Runs for authority and peers. Drive VFX/montages from StateStartedAt and StateEndsAt, never damage here. */
    UFUNCTION(BlueprintImplementableEvent, Category="Boss|Presentation") void OnBossTelegraph(FName AttackId,int32 AttackSerial,FVector Direction,double StartsAt,double EndsAt);
    UFUNCTION(BlueprintImplementableEvent, Category="Boss|Presentation") void OnBossAttackImpact(FName AttackId,int32 AttackSerial);
    UFUNCTION(BlueprintImplementableEvent, Category="Boss|Presentation") void OnBossDied();
    const UMCBossProfile* GetResolvedProfile() const { return ResolvedProfile; }
    const TArray<FMCBossAttackDefinition>& GetAttackDefinitions() const { return AttackDefinitions; }
    bool IsAttackReady(const FMCBossAttackDefinition& Attack) const;
    static bool IsLivingPlayer(const AMCToothCharacter* Target);
    bool CanSeePlayer(const AMCToothCharacter* Target) const;
    bool IsPlayerInAttack(const AMCToothCharacter* Target,const FMCBossAttackDefinition& Attack,const FVector& Forward) const;
    void SetBrainState(EMCBossState State,AMCToothCharacter* Target);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UFUNCTION() void OnRep_Profile();
    UFUNCTION() void OnRep_Runtime();
    UFUNCTION(NetMulticast, Reliable) void MulticastAttackImpact(FName AttackId,int32 AttackSerial);
    void ImpactAttack();
    void RecoverAttack();
    void FinishRecovery();
    void PublishRuntime();
    void ChangeState(EMCBossState State,float Duration=0.f);
    void UpdatePhase();
    void UpdateAnimationPresentation();
    UAnimSequence* PreviewSequence(EMCBossAnimationPreview Preview) const;
    double ServerNow() const;
    UPROPERTY(Transient) TObjectPtr<UMCBossProfile> ResolvedProfile;
    TArray<FMCBossAttackDefinition> AttackDefinitions;
    TArray<FMCBossPhaseDefinition> PhaseDefinitions;
    TMap<FName,double> NextAttackAt;
    FMCBossAttackDefinition PendingAttack;
    FMCBossRuntimeState LastPresented;
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> LoadedAnimations;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentAnimation;
    double CurrentAnimationStartedAt=-1000;
    FTimerHandle AttackTimer;
    float BaseWalkSpeed=260.f;
};
