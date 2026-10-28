#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MCRogueTypes.h"
#include "MCPerkTypes.generated.h"

class UMCPerkEffect;
class UTexture2D;

UENUM(BlueprintType)
enum class EMCPerkPolarity : uint8
{
    Positive,
    Negative,
    Risky
};

UENUM(BlueprintType)
enum class EMCToolUpgrade : uint8 { None, MeshaBrush, Chainsaw, Buffer, Watergun };
UENUM(BlueprintType)
enum class EMCPerkRarity : uint8 { Standard, Rare, Legendary };

UENUM(BlueprintType)
enum class EMCPerkKind : uint8 { Automatic, Passive, Active, Consumable, ToolUpgrade };

/** Row name is the stable perk ID. EffectClass is the extension point for custom effects. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCPerkDefinition : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk") bool bEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk") EMCPerkKind Kind=EMCPerkKind::Automatic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk") FName Category;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk|Localization") FName DisplayNameKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk|Localization") FName DescriptionKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk|Activation",meta=(ClampMin="1",ClampMax="10000")) int32 InitialCharges=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk|Activation",meta=(ClampMin="0")) float CooldownSeconds=0.f;
    /** Optional adapter to an already implemented legacy action; no description text is executed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk|Activation") FName ActivationAdapterID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk", meta=(MultiLine=true))
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk")
    EMCPerkPolarity Polarity = EMCPerkPolarity::Positive;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk", meta=(ClampMin="0"))
    float Weight = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk", meta=(ClampMin="1", ClampMax="10000"))
    int32 MaxStacks = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk")
    TSubclassOf<UMCPerkEffect> EffectClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perk")
    TSoftObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Perk") EMCToolUpgrade ToolUpgrade=EMCToolUpgrade::None;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Perk") EMCPerkRarity Rarity=EMCPerkRarity::Standard;
    /** Authored chest-only or placeholder rows may opt out of personal level cards. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Perk") bool bAvailableForLevelChoice=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike") bool bRoguePerk=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike") bool bEarlyChoice=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike") bool bRequiresTeam=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike") bool bConsumableBooster=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike") FName ChoiceFamily;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike") EMCToolUpgrade RequiredTool=EMCToolUpgrade::None;
    /** Any tag may match the active or still upcoming route. Empty means universal. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike") TArray<FName> RequiredEvents;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike") TArray<FMCRogueModifier> Modifiers;
    /** Event-effect tuning in the canonical catalog. Percentages use fractions (0.2 = 20%). */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelike|Parameters") TMap<FName,float> Parameters;
};

/** One owned active item. Empty IDs have no identity, charges or cooldown. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCActiveSkillSlot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName PerkID;
    UPROPERTY(BlueprintReadOnly) FGuid InstanceID;
    /** -1 for reusable actives; positive for remaining consumable charges. */
    UPROPERTY(BlueprintReadOnly) int32 Charges=0;
    UPROPERTY(BlueprintReadOnly) double CooldownEndsAt=0.;
};

/** Trusted server-prepared acquisition. Custom charge counts preserve old paid inventory. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCPerkGrant
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName PerkID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DestinationSlot=INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ExpectedReplacedID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid ExpectedInstanceID;
    /** -1 uses the authored initial charges. Positive overrides apply to consumables only. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Charges=-1;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCActivePerk
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Perk")
    FName PerkID;

    UPROPERTY(BlueprintReadOnly, Category="Perk")
    int32 Stacks = 0;
};
