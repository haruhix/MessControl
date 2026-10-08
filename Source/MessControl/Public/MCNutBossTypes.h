#pragma once

#include "CoreMinimal.h"
#include "MCNutBossTypes.generated.h"

class UStaticMesh;

UENUM(BlueprintType)
enum class EMCNutBossRole : uint8 { Tank, Mage };

UENUM(BlueprintType)
enum class EMCNutBossState : uint8 { Falling, Idle, Telegraph, Executing, Recovery, Defeated };

UENUM(BlueprintType)
enum class EMCNutBossAttack : uint8 { None, Melee, Charge, Jump, Fireball, Summon, NutRain };

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
