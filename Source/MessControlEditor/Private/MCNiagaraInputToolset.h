#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "MCNiagaraInputToolset.generated.h"

class UNiagaraSystem;

/** Native MCP transport for the existing bounded owned-draft input writer. */
UCLASS()
class UMCNiagaraInputToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Forward one batch of at most 16 ordinary typed inputs. No save or compile. */
    UFUNCTION(meta=(AICallable))
    static FString SetNiagaraStackInputBatch(UNiagaraSystem* System,const FString& OperationsJson,
        FName OwnerMetadataKey,const FString& ExpectedOwnerValue);

    /** Bind a compiled CPU particle float to a Component Renderer template float's explicit BlueprintSetter. */
    UFUNCTION(meta=(AICallable))
    static FString SetNiagaraComponentFloatBinding(UNiagaraSystem* System,FName EmitterName,int32 RendererIndex,
        FName PropertyName,FName ParticleAttribute,FName OwnerMetadataKey,const FString& ExpectedOwnerValue);

    /** One generated owned NS, one data-only edit context for the entire ordered spec.
     * Synchronous local no-capture scope preserves previous Undo; this generated edit is
     * intentionally not undoable. Rejects artist-open/history-referenced targets.
     * Uses ordinary typed SDK and actual aliases/readbacks. No compile/save/Undo/reset.
     * Failure reports an applied prefix; never automatically replay an unknown request. */
    UFUNCTION(meta=(AICallable))
    static FString ExecuteNiagaraSystemSpec(UNiagaraSystem* System,const FString& OperationsJson,
        FName OwnerMetadataKey,const FString& ExpectedOwnerValue);

    /** Fresh asset + exact owner tag + full ordered spec, in the same generated-edit scope.
     * Existing disk/loaded targets are refused. Standard SDK creation may request its initial
     * compile; final compile/wait/save remain outside this synchronous graph-edit scope. */
    UFUNCTION(meta=(AICallable))
    static FString CreateNiagaraSystemFromSpec(const FString& AssetName,const FString& AssetPath,
        UNiagaraSystem* TemplateSystem,const FString& OperationsJson,FName OwnerMetadataKey,
        const FString& ExpectedOwnerValue);
    /** Actual SDK script compile state with NO System ViewModel, preview or stack expansion.
     * Poll this while compilation is pending. This does not certify stack diagnostics. */
    UFUNCTION(meta=(AICallable))
    static FString GetOwnedNiagaraCompileState(UNiagaraSystem* System,
        FName OwnerMetadataKey,const FString& ExpectedOwnerValue);

    /** Queue the ordinary compiler for this exact owned system. Poll GetOwnedNiagaraCompileState afterward. */
    UFUNCTION(meta=(AICallable))
    static FString RequestOwnedNiagaraCompile(UNiagaraSystem* System,
        FName OwnerMetadataKey,const FString& ExpectedOwnerValue);

    /** Save just this owned package after all system and emitter scripts have compiled without warnings. */
    UFUNCTION(meta=(AICallable))
    static FString SaveOwnedNiagaraSystem(UNiagaraSystem* System,
        FName OwnerMetadataKey,const FString& ExpectedOwnerValue);
};
