#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCNiagaraPreviewEditorLibrary.generated.h"

class UNiagaraSystem;

/** Editor-only access to Niagara's per-asset preview settings. */
UCLASS()
class MESSCONTROLEDITOR_API UMCNiagaraPreviewEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Closes only this system's editors, stores a non-orbit camera and marks the asset dirty. Does not save or reopen it. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Niagara Preview")
    static bool ConfigureNiagaraPreview(UNiagaraSystem* System, FVector ViewLocation, FRotator ViewRotation);

    /** Reads stored camera fields and the current rendering state of loaded EditorPreview components. Does not change the asset. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Niagara Preview")
    static FString InspectNiagaraPreview(UNiagaraSystem* System);
};
