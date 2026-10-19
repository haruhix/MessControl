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

    /** Editable common languages. RU is the native copy and empty translations use it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional)) FText RU;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional)) FText EN;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional)) FText DE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional)) FText FR;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional)) FText ES;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional, DisplayName="PT-BR")) FText PT_BR;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional, DisplayName="Chinese (Simplified)")) FText ZH_HANS;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional, DisplayName="Chinese (Traditional)")) FText ZH_HANT;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional, DisplayName="Japanese")) FText JA;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional, DisplayName="Korean")) FText KO;

    /** Exact additional cultures, including regional variants such as en-GB and es-419. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(DataTableImportOptional)) TMap<FString, FText> AdditionalLanguages;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(MultiLine=true, DataTableImportOptional)) FString Context;

    // Kept serialized for existing packages/Blueprints and old import tools. SourceText is an
    // immutable legacy-string alias after migration; changing RU must never change that identity.
    UPROPERTY(BlueprintReadWrite, Category="Localization|Legacy", meta=(HideFromDataTableEditorColumn, DataTableImportOptional)) FText SourceText;
    UPROPERTY(BlueprintReadWrite, Category="Localization|Legacy", meta=(HideFromDataTableEditorColumn, DataTableImportOptional)) TMap<FString, FText> Localizations;

    const FText& GetNativeText() const { return RU.IsEmptyOrWhitespace() ? SourceText : RU; }

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

    UPROPERTY(BlueprintReadWrite, Category="Localization|Legacy", meta=(HideFromDataTableEditorColumn, DataTableImportOptional))
    FText DisplayName;

    UPROPERTY(BlueprintReadWrite, Category="Localization|Legacy", meta=(HideFromDataTableEditorColumn, DataTableImportOptional))
    FText Description;

    /** Stable row names in the shared translation catalogue. Image and gameplay identity stay here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(DataTableImportOptional)) FName DisplayNameKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(DataTableImportOptional)) FName DescriptionKey;
    UPROPERTY(BlueprintReadWrite, Category="Localization|Legacy", meta=(HideFromDataTableEditorColumn, DataTableImportOptional))
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

    UPROPERTY(BlueprintReadWrite, Category="Localization|Legacy", meta=(HideFromDataTableEditorColumn, DataTableImportOptional))
    FText DisplayName;

    UPROPERTY(BlueprintReadWrite, Category="Localization|Legacy", meta=(HideFromDataTableEditorColumn, DataTableImportOptional))
    FText Description;

    /** Stable row names in the shared translation catalogue. Image and gameplay identity stay here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(DataTableImportOptional)) FName DisplayNameKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Translations", meta=(DataTableImportOptional)) FName DescriptionKey;
    UPROPERTY(BlueprintReadWrite, Category="Localization|Legacy", meta=(HideFromDataTableEditorColumn, DataTableImportOptional))
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
