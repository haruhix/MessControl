#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCVFXAssetBuilder.generated.h"
class UNiagaraSystem;

UCLASS()
class MESSCONTROL_API UMCVFXAssetBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Creates the editable foam asset once; re-running preserves artist edits. */
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") static UNiagaraSystem* CreateBrushFoam();
};
