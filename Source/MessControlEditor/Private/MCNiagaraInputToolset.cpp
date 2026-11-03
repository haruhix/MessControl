#include "MCNiagaraInputToolset.h"
#include "MCPerkPreviewEditorLibrary.h"

FString UMCNiagaraInputToolset::SetNiagaraStackInputBatch(UNiagaraSystem* System,
    const FString& OperationsJson,FName OwnerMetadataKey,const FString& ExpectedOwnerValue)
{
    return UMCPerkPreviewEditorLibrary::SetNiagaraStackInputBatch(
        System,OperationsJson,OwnerMetadataKey,ExpectedOwnerValue);
}
