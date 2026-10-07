#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/AnimInstance.h"
#include "MCBossProfile.generated.h"

class USkeletalMesh;
class UAnimSequence;

/** Explicit F3 presentation previews. These never run combat or navigation. */
UENUM(BlueprintType)
enum class EMCBossAnimationPreview : uint8
{
    None, Idle, Walk, PunchLeft, PunchRight, Kick, Hurt, Death, Roar, AreaAttack
};

UENUM(BlueprintType)
enum class EMCBossState : uint8
{
    Dormant, Searching, Chasing, Telegraph, Attacking, Recovering, Dead
};

/** An attack slot: native frontal damage by default, replace ExecuteAttack in a boss Blueprint for abilities. */
USTRUCT(BlueprintType)
struct FMCBossAttackDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack") FName AttackId=TEXT("Claw");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0")) int32 MinimumPhase=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0")) float SelectionWeight=1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0")) float Damage=20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="1")) float Range=250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="1")) float VerticalReach=160.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="1",ClampMax="180")) float HalfAngleDegrees=60.f;
    /** Projectile saliva replaces the immediate melee/radial hit for this slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mouth attack") bool bMouthClotAttack=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mouth attack", meta=(ClampMin="1",ClampMax="64")) int32 ClotCount=28;
    /** Per-clot damage; Damage above caps the total direct damage to each player per salvo. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mouth attack", meta=(ClampMin="0")) float ClotDamage=4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mouth attack", meta=(ClampMin="0")) float WindPushAcceleration=260.f;
    /** Idle pause before the strike clip begins; WindupSeconds remains the clip's authored impact time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0",Units="s")) float StartDelaySeconds=0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0.05")) float WindupSeconds=.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0.05")) float ActiveSeconds=.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0.05")) float RecoverySeconds=.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0")) float CooldownSeconds=2.f;
    /** Full windup/impact/recovery clip, authored in place on this boss's own skeleton. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TSoftObjectPtr<UAnimSequence> Animation;
    void Sanitize();
};

USTRUCT(BlueprintType)
struct FMCBossPhaseDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Phase", meta=(ClampMin="0",ClampMax="1")) float HealthFraction=.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Phase", meta=(ClampMin="0.1")) float MovementMultiplier=1.2f;
};

/** A profile is reusable across boss Blueprints; it does not spawn a boss or author an encounter. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCBossProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss", meta=(ClampMin="1")) float MaxHealth=600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss", meta=(ClampMin="1")) float WalkSpeed=260.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sensing", meta=(ClampMin="1")) float SightRadius=2500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sensing", meta=(ClampMin="1")) float LoseTargetRadius=3200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sensing", meta=(ClampMin="0")) float SightMemorySeconds=3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sensing", meta=(ClampMin="0.1")) float DecisionInterval=.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sensing", meta=(ClampMin="0.2")) float TargetScanInterval=.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigation", meta=(ClampMin="0.1")) float PathRetrySeconds=.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigation", meta=(ClampMin="1")) float ChaseAcceptanceRadius=130.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") TArray<FMCBossAttackDefinition> Attacks={FMCBossAttackDefinition()};
    /** Ordered by descending HealthFraction during initialization. Phase 0 is the initial phase. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") TArray<FMCBossPhaseDefinition> Phases;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TSoftClassPtr<UAnimInstance> AnimationClass;
    /** Full-body crossfade duration for native playback. Zero retains immediate sequence switches. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation", meta=(ClampMin="0",Units="s")) float AnimationBlendSeconds=0.f;
    /** Native sequence playback is used when AnimationClass is empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TSoftObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TSoftObjectPtr<UAnimSequence> WalkAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TSoftObjectPtr<UAnimSequence> HurtAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TSoftObjectPtr<UAnimSequence> DeathAnimation;
    /** Five-second in-place scream used by the encounter intro and the explicit F3 preview. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TSoftObjectPtr<UAnimSequence> RoarAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") FTransform MeshTransform=FTransform::Identity;
    /** Scale the torso hit volume about its lower tip, retaining the Blueprint's authored shape. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(ClampMin="0.1")) float BodyHitboxScale=1.f;
};
