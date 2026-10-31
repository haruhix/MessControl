#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCVFXComposerBuilderLibrary.generated.h"

class UMCVFXComposition;
class UNiagaraSystem;

/** Editor-only, strict curated-system compiler. Source presets and generated NS are distinct assets. */
UCLASS()
class MESSCONTROLEDITOR_API UMCVFXComposerBuilderLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer")
    static bool ValidateRecipe(const UMCVFXComposition* Recipe,FString& OutError);
    /** Returns a separately owned unsaved system. Compilation may still be pending. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer")
    static UNiagaraSystem* BuildTransient(const UMCVFXComposition* Recipe,FString& OutError);
    /** Explicit export creates a new package only; refuses overwrites and compilation errors. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer")
    static UNiagaraSystem* ExportNew(const UMCVFXComposition* Recipe,const FString& LongPackageName,FString& OutError);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer")
    static bool DiscoverControls(UMCVFXComposition* Recipe,int32 LayerIndex,FString& OutError);
    /** Required user-visible distinction between preview clock and native exported System timing. */
    UFUNCTION(BlueprintPure,Category="MessControl|VFX Composer")
    static FString GetExportNotes();
};
