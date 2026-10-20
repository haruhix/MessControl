#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCFrostNovaAssetLibrary.generated.h"

/** Creates the isolated Frost Nova Niagara burst after its material authoring step. */
UCLASS()
class MESSCONTROL_API UMCFrostNovaAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Existing assets are preserved unless an explicitly owned system is rebuilt. */
    UFUNCTION(BlueprintCallable, Category="MessControl|Editor")
    static bool AuthorAssets(bool bRebuildOwned=false);

    UFUNCTION(BlueprintCallable, Category="MessControl|Editor")
    static bool WaitForMaterialShaders();
};
