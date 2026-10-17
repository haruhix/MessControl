#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
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
    UFUNCTION(BlueprintPure,Category="MessControl|UI|Factory")
    static UMCGameplayUITheme* GetMasterTheme();
};
