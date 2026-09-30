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
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") static UNiagaraSystem* CreateIceShatter();
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") static UNiagaraSystem* CreateSprayMist();
    /** Repairs the builder's empty mesh slot without replacing saved artist settings. Returns -1 on failure. */
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") static int32 RepairMeshRendererSlots(UNiagaraSystem* System);
};
