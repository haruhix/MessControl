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

/** Row name is the stable perk ID. EffectClass is the extension point for custom effects. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCPerkDefinition : public FTableRowBase
{
    GENERATED_BODY()

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
