#pragma once

#include "CoreMinimal.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "MCNiagaraInputBatchTypes.generated.h"

/** Reuses the installed toolset's typed variant conversion, not a second wire format. */
USTRUCT(BlueprintInternalUseOnly)
struct FMCNiagaraInputBatchEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) FNiagaraExt_StackItemReference InputItemRef;
    UPROPERTY(EditAnywhere) FNiagaraExt_StackInputValue InputData;
};
