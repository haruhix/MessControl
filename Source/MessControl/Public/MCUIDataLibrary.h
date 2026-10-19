#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCUIDataTypes.h"
#include "MCUIDataLibrary.generated.h"

/** Shared explicit-culture lookup for runtime UI and isolated Widget Designer previews. */
UCLASS()
class MESSCONTROL_API UMCUIDataLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Call during UI setup, retain the table on the widget; do not load tables from Tick/Paint. */
    UFUNCTION(BlueprintCallable, Category="MessControl|UI|Data")
    static UDataTable* GetDefaultLocalizationTable();

    UFUNCTION(BlueprintCallable, Category="MessControl|UI|Data")
    static UDataTable* GetDefaultBoosterTable();

    UFUNCTION(BlueprintCallable, Category="MessControl|UI|Data")
    static UDataTable* GetDefaultCardTable();

    /** Empty Culture uses the current language. Returns a whole FText pattern; never changes culture. */
    UFUNCTION(BlueprintPure, Category="MessControl|UI|Localization", meta=(AutoCreateRefTerm="Fallback,Culture"))
    static FText ResolveText(UDataTable* Table, FName Key, const FText& Fallback, const FString& Culture = TEXT(""));

    /** Pattern must already be selected for Culture. Uses local plural/ordinal rules without changing the editor language. */
    static FText FormatTextForCulture(const FText& Pattern, const FFormatNamedArguments& Arguments, const FString& Culture = TEXT(""));

    UFUNCTION(BlueprintPure, Category="MessControl|UI|Localization", meta=(AutoCreateRefTerm="Culture"))
    static FText NumberForCulture(int64 Value, const FString& Culture = TEXT(""));

    UFUNCTION(BlueprintPure, Category="MessControl|UI|Localization", meta=(AutoCreateRefTerm="Culture", ClampMin="0", ClampMax="6"))
    static FText DecimalForCulture(double Value, int32 FractionalDigits = 1, const FString& Culture = TEXT(""));

    UFUNCTION(BlueprintPure, Category="MessControl|UI|Data", meta=(AutoCreateRefTerm="Culture"))
    static FMCUIResolvedContent ResolveBooster(const FDataTableRowHandle& RowHandle, const FString& Culture = TEXT(""), UDataTable* LocalizationTable = nullptr);

    /** Duplicate GameplayPerkID mappings are ambiguous and return bFound=false. */
    UFUNCTION(BlueprintPure, Category="MessControl|UI|Data", meta=(AutoCreateRefTerm="Culture"))
    static FMCUIResolvedContent FindBoosterForGameplayID(UDataTable* Table, FName GameplayPerkID, const FString& Culture = TEXT(""), UDataTable* LocalizationTable = nullptr);

    UFUNCTION(BlueprintPure, Category="MessControl|UI|Data", meta=(AutoCreateRefTerm="Culture"))
    static FMCUIResolvedContent ResolveCard(const FDataTableRowHandle& RowHandle, const FString& Culture = TEXT(""), UDataTable* LocalizationTable = nullptr);

    UFUNCTION(BlueprintPure, Category="MessControl|UI|Data", meta=(AutoCreateRefTerm="Culture"))
    static FMCUIResolvedContent FindCardForGameplayID(UDataTable* Table, FName GameplayPerkID, const FString& Culture = TEXT(""), UDataTable* LocalizationTable = nullptr);

    /** Authoring check for supported row structs, empty source copy/culture variants and duplicate IDs. */
    UFUNCTION(BlueprintCallable, Category="MessControl|UI|Data")
    static bool ValidateContentTable(UDataTable* Table, TArray<FString>& OutProblems, UDataTable* LocalizationTable = nullptr);
};
