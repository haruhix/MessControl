#pragma once

#include "CoreMinimal.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "MCNiagaraSystemSpecTypes.generated.h"

/** Ordered ordinary SDK operations; the installed converters retain exact Niagara types. */
USTRUCT(BlueprintInternalUseOnly)
struct FMCNiagaraSystemSpecOperation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere) FString Id;
    UPROPERTY(EditAnywhere) FName Operation;
    /** Alias produced by an earlier op. Explicit nonempty reference fields override it. */
    UPROPERTY(EditAnywhere) FName ReferenceAlias;
    UPROPERTY(EditAnywhere) FNiagaraExt_StackItemReference Reference;
    /** Optional new alias, published only after the SDK result was read back. */
    UPROPERTY(EditAnywhere) FName ResultAlias;

    UPROPERTY(EditAnywhere) TObjectPtr<UNiagaraEmitter> EmitterTemplate;
    UPROPERTY(EditAnywhere) FName EmitterName;
    UPROPERTY(EditAnywhere) TObjectPtr<UNiagaraScript> ModuleAsset;
    /** BindModule resolves exactly one existing module; no guessed UUIDs or first-match. */
    UPROPERTY(EditAnywhere) FName ModuleNamePrefix;
    UPROPERTY(EditAnywhere) TSubclassOf<UNiagaraRendererProperties> RendererClass;
    UPROPERTY(EditAnywhere) TArray<FNiagaraExt_SetParameterEntry> Parameters;
    UPROPERTY(EditAnywhere) FNiagaraExt_StackInputValue InputData;
    UPROPERTY(EditAnywhere) FNiagaraExt_UserVariable UserVariable;
    UPROPERTY(EditAnywhere) FString PropertyValues;
    UPROPERTY(EditAnywhere) bool Enabled = true;

    /** Reuses the existing finite gameplay-array declaration policy, unchanged. */
    UPROPERTY(EditAnywhere) FName UserName;
    UPROPERTY(EditAnywhere) FName NativeArrayElementType;
    /** Mesh renderer binding uses the existing typed helper, not property text indices. */
    UPROPERTY(EditAnywhere) int32 MeshIndex = 0;
    UPROPERTY(EditAnywhere) int32 MaterialIndex = 0;
    /** Exact display label resolved from the live input schema in this same VM. */
    UPROPERTY(EditAnywhere) FString StaticChoice;
};
