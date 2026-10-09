#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCIceVFXAssetLibrary.generated.h"

/** Editor authoring for the cold event's isolated, lightweight Niagara assets. */
UCLASS()
class MESSCONTROL_API UMCIceVFXAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Creates missing assets; saved artist adjustments survive subsequent runs. */
    UFUNCTION(BlueprintCallable, Category="MessControl|Editor")
    static bool AuthorAssets();

    /** Waits for the six owned materials and rejects incomplete or failed shaders. */
    UFUNCTION(BlueprintCallable, Category="MessControl|Editor")
    static bool WaitForMaterialShaders();
};
