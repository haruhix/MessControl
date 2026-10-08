#pragma once

#include "CoreMinimal.h"
#include "MCNutBossTypes.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UAnimSequence;

UENUM(BlueprintType)
enum class EMCNutBossRole : uint8 { Tank, Mage };

UENUM(BlueprintType)
enum class EMCNutBossState : uint8 { Falling, Idle, Telegraph, Executing, Recovery, Defeated };

UENUM(BlueprintType)
enum class EMCNutBossAttack : uint8 { None, Melee, Charge, Jump, Fireball, Summon, NutRain, Roll };

/** Encounter tuning shared by the event and the two authoritative boss actors. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCNutBossSettings
{
    GENERATED_BODY()
    FMCNutBossSettings();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Health") float TankHealth=1400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Health") float MageHealth=1100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Health") float ExtraPlayerHealth=.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") TSoftObjectPtr<UStaticMesh> WholeMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") TSoftObjectPtr<USkeletalMesh> TankSkeletalMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") float TankModelYaw=-90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") TSoftObjectPtr<UStaticMesh> TankBallMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") float TankBallHeight=200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankIdleAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankWalkAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankWalkLeftAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankWalkRightAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankMeleeAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankJumpAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankTransformAnimation;
    /** Optional dedicated charge phases. Missing clips retain the legacy accelerated walk. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankChargeTellAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankChargeLoopAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> TankChargeRecoveryAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.1",ClampMax="4")) float TankChargeLoopPlayRate=2.f;
    /** Clip contact poses map to the existing server resolve/landing times. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.05",ClampMax="0.95")) float TankMeleeImpactFraction=.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.05",ClampMax="0.8")) float TankJumpTakeoffFraction=.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.1",ClampMax="0.95")) float TankJumpImpactFraction=.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") TSoftObjectPtr<USkeletalMesh> MageSkeletalMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") float MageModelYaw=-90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageIdleAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageWalkAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageCastAnimation;
    /** Optional close-range shove; spell clips remain the fallback for older profiles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageMeleeAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.05",ClampMax="0.95")) float MageMeleeImpactFraction=.55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageHeavyCastAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageSummonAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageRainAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageHitAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TSoftObjectPtr<UAnimSequence> MageDeathAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") float MageCastReleaseFraction=.55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") float AnimationBlendSeconds=.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") float DeathSeconds=2.4f;
    /** Gameplay emitter in world centimetres relative to the boss body, rotated toward the locked target. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FVector MageCastOffset=FVector(120,-22,65);
    /** Optional visual socket; authoritative projectile origins never depend on evaluated cosmetic bones. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName MageCastSocket=TEXT("cast_palm");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName TorsoBone=TEXT("root_x");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName HeadBone=TEXT("head_x");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName LeftArmBone=TEXT("arm_stretch_l");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName RightArmBone=TEXT("arm_stretch_r");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName LeftForearmBone=TEXT("forearm_stretch_l");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName RightForearmBone=TEXT("forearm_stretch_r");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName LeftHandBone=TEXT("hand_l");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FName RightHandBone=TEXT("hand_r");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") TSoftObjectPtr<UStaticMesh> ShellMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") TSoftObjectPtr<UStaticMesh> KernelMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") float TankHeight=220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") float MageHeight=200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") float CreepHeight=70;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entrance") float EntranceSeconds=1.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entrance") float EntranceHeight=800;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement") float TankMoveSpeed=160;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement") float MageMoveSpeed=120;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shield") float ShieldFrontDamageScale=.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shield") float ShieldHalfAngle=55;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee") float MeleeDamage=12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee") float MeleeRange=190;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee") float MeleeWindup=.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee") float MeleeCooldown=2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charge") float ChargeDamage=22;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charge") float ChargeWindup=1.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charge") float ChargeCooldown=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charge") float ChargeSpeed=650;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charge") float ChargeDistance=1100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roll") float RollDamage=16;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roll") float RollWindup=1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roll") float RollCooldown=16;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roll") float RollDuration=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roll") float RollSpeed=520;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roll") float RollTurnDegreesPerSecond=50;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roll") float RollHitGap=1.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roll") float RollPush=260;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Jump") float JumpDamage=25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Jump") float JumpWindup=1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Jump") float JumpFlightSeconds=.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Jump") float JumpRadius=230;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Jump") float JumpCooldown=13;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fireball") float FireballDamage=18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fireball") float FireballWindup=.9f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fireball") float FireballCooldown=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fireball") float FireballSpeed=550;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fireball") float FireballRadius=28;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Summon") float SummonCooldown=17;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Summon") int32 SummonCount=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Summon") int32 MaxLiveCreeps=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Summon") float CreepHealth=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Rain") float RainRadius=300;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Rain") float RainImpactRadius=90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Rain") float RainWindup=1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Rain") float RainActiveSeconds=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Rain") int32 RainDrops=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Rain") float RainDamage=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Rain") float RainHitGap=.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Rain") float RainCooldown=20;
    void Sanitize();
    float HealthForPlayers(EMCNutBossRole Role,int32 Players) const;
};
