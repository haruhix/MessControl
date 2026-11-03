#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCUIPresentationTypes.h"
#include "MCGameplayUIFactory.generated.h"

class UMCGameplayUITheme;

/** Resolves presentation classes without changing assets or running gameplay initialization. */
UCLASS()
class MESSCONTROL_API UMCGameplayUIFactory : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Validates the authored class against the native role; an absent/invalid mapping uses the native class. */
    UFUNCTION(BlueprintPure,Category="MessControl|UI|Factory")
    static UClass* ResolveClass(FName Role,UClass* NativeFallback);
    static UClass* ResolveClassForTheme(FName Role,UClass* NativeFallback,const UMCGameplayUITheme* Theme);
    UFUNCTION(BlueprintPure,Category="MessControl|UI|Factory")
    static UMCGameplayUITheme* GetMasterTheme();
    /** Explicit version is also used by Designer previews; it never changes player preferences. */
    UFUNCTION(BlueprintPure,Category="MessControl|UI|Factory")
    static UMCGameplayUITheme* GetThemeForVersion(EMCUIVersion Version);
    UFUNCTION(BlueprintPure,Category="MessControl|UI|Presentation")
    static EMCUIVersion GetPresentationVersion();
    UFUNCTION(BlueprintCallable,Category="MessControl|UI|Presentation")
    static bool SetPresentationVersion(EMCUIVersion Version,bool bSavePreference=true);
    static FOnMCUIPresentationChanged& OnPresentationChanged();
    static bool IsPresentationTheme(const UMCGameplayUITheme* Theme);
    /** Editor authoring can replace the transient fallback with a newly saved profile. */
    UFUNCTION(BlueprintCallable,Category="MessControl|UI|Editor")
    static void InvalidateNewTheme();
};
