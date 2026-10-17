#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MCUIDataTypes.generated.h"

class UTexture2D;

/** Shared UI strings. Row names are stable semantic keys; the native copy is Russian. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCUILocalizationRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization", meta=(MultiLine=true))
    FText SourceText;

    /** UE culture names, for example en or pt-BR. Empty entries fall back to SourceText. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization")
    TMap<FString, FText> Localizations;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization", meta=(MultiLine=true))
    FString Context;
};

/** A translated name and description, authored in the same row as the UI image. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCUILocalizedCopy
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization", meta=(MultiLine=true))
    FText Description;
};

/** Presentation only. Adding a row never creates or changes a gameplay booster effect. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCUIBoosterRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Existing DT_Perks row ID. This can differ from this presentation table's row name. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster")
    FName GameplayPerkID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster")
    TSoftObjectPtr<UTexture2D> Image;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Booster", meta=(MultiLine=true))
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization")
    TMap<FString, FMCUILocalizedCopy> Localizations;
};

/** Reward/perk card presentation; gameplay definitions remain in the existing DT_Perks. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCUICardRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card")
    FName GameplayPerkID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card")
    TSoftObjectPtr<UTexture2D> Image;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card", meta=(MultiLine=true))
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization")
    TMap<FString, FMCUILocalizedCopy> Localizations;
};

/** Safe value copy for a widget; a soft image can be displayed without an ID-specific art switch. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCUIResolvedContent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="UI")
    TSoftObjectPtr<UTexture2D> Image;

    UPROPERTY(BlueprintReadOnly, Category="UI")
    FText DisplayName;

    UPROPERTY(BlueprintReadOnly, Category="UI")
    FText Description;

    UPROPERTY(BlueprintReadOnly, Category="UI")
    bool bFound = false;

    /** A requested translation/parent variant or missing row required fallback. */
    UPROPERTY(BlueprintReadOnly, Category="UI")
    bool bUsedFallback = false;
};
