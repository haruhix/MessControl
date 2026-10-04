#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/AnimInstance.h"
#include "MCBossProfile.generated.h"

class USkeletalMesh;

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0.05")) float WindupSeconds=.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0.05")) float ActiveSeconds=.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0.05")) float RecoverySeconds=.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0")) float CooldownSeconds=2.f;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") FTransform MeshTransform=FTransform::Identity;
};
