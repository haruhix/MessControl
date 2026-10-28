#pragma once
#include "CoreMinimal.h"
#include "MCRogueTypes.generated.h"

UENUM(BlueprintType)
enum class EMCRogueStat : uint8
{
    NormalSpeed, MaxStamina, StaminaRecovery, JumpHeight, DashCooldown, IceBraking,
    WaveResistance, PickaxeDamage, PickaxeReach, AttackRate, ChainsawDamage,
    BrushWidth, BrushPower, DirtyDuration, WarmDuration, WarmRadius, CarryPenalty,
    CarryCapacity, ThrowImpulse, SpoilRate, FoodScatter, DeliveryXP, MaxHealth,
    FallingNutDamage, FreezeRate, LegIceHealth, ChainsawCrashDamage, BraceDamage,
    RagdollDuration, RescueRate, WeaponDamage, IncomingDamage, ShieldDamage,
    // Append only: saved modifier enum values must retain their meaning.
    StaminaCost, FoodPickupRange, FoodThrowRange, SprayRange, SprayWidth
};
UENUM(BlueprintType)
enum class EMCRogueActivation : uint8
{
    Always, CarryingFood, HighFreeze, Night, RecentAllyHit, NoRecentAllyHit,
    CounterReady, TeamStrikeReady, WatergunFiring, NearbyAlly, PickaxeSelected, WatergunSelected
};
UENUM(BlueprintType)
enum class EMCRogueDamageKind : uint8 { Ordinary, FallingNut, Brace, Chainsaw, System };
UENUM(BlueprintType)
enum class EMCRogueRewardKind : uint8 { Ordinary, CursedContract, RiskyAltar };
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCRogueModifier
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EMCRogueStat Stat=EMCRogueStat::NormalSpeed;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Value=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bFlat=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EMCRogueActivation Condition=EMCRogueActivation::Always;
};
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCRogueRecentOffer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FName> IDs;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCRogueChestOffer
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid OfferID;
    UPROPERTY(BlueprintReadOnly) EMCRogueRewardKind Kind=EMCRogueRewardKind::Ordinary;
    UPROPERTY(BlueprintReadOnly) TArray<FName> IDs;
    UPROPERTY(BlueprintReadOnly) TArray<FName> RewardIDs;
    UPROPERTY(BlueprintReadOnly) FName Curse;
    UPROPERTY(BlueprintReadOnly) FName Reward;
    /** 0: curse, 1: reward, 2: final confirmation. */
    UPROPERTY(BlueprintReadOnly) uint8 Step=0;
};

/** Server-only personal receipt. Presentation is sent only to that recipient. */
USTRUCT()
struct MESSCONTROL_API FMCPersonalRewardOffer
{
    GENERATED_BODY()
    UPROPERTY() FGuid OfferID;
    UPROPERTY() TArray<FName> IDs;
};
