#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "MCVFXComposerToolset.generated.h"

class UNiagaraSystem;

/** Native MCP access to the same action workspace and isolated runner used by the editor UI. */
UCLASS()
class UMCVFXComposerToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Page the shared action catalogue. Empty Query lists all actions. Limit must be 1–50. */
    UFUNCTION(meta=(AICallable))
    static FString GetCatalogue(const FString& Query,int32 Offset,int32 Limit,bool bIncludeCanonical);

    /** Read the open Composer action, exact draft token, revision, events, groups and layers. */
    UFUNCTION(meta=(AICallable))
    static FString GetWorkspace();

    /** Read the real private preview's events, clock, resource and live Niagara evidence. */
    UFUNCTION(meta=(AICallable))
    static FString GetPreview();

    /** Read residency of up to 32 exact object paths in the Composer sandbox or active PIE world. Never loads assets. */
    UFUNCTION(meta=(AICallable))
    static FString GetVisualReadiness(const TArray<FString>& ObjectPaths,bool bUsePIE);

    /** Open the real Composer tab and select a registered action. Existing drafts and playback are protected. */
    UFUNCTION(meta=(AICallable))
    static FString SelectAction(FName ActionID);

    /** Run one supported scenario for the selected action. Returns a token for stopping this exact run. */
    UFUNCTION(meta=(AICallable))
    static FString PlayScenario(FName ExpectedAction,const FString& Scenario);

    /** Stop and reset a run started by PlayScenario. Requires its playback token and selected action. */
    UFUNCTION(meta=(AICallable))
    static FString StopPreview(FName ExpectedAction,const FString& PlaybackToken);

    /** Add a saved project Niagara system to the draft event. Empty GroupID adds an ungrouped layer. No saves. */
    UFUNCTION(meta=(AICallable))
    static FString AddSystemToGroup(FName ExpectedAction,const FString& DraftToken,int64 ExpectedRevision,
        FName Event,const FString& GroupID,UNiagaraSystem* System);

    /** Explicitly apply the reviewed runtime draft through Composer's canonical writer. ConfirmApply must be true. */
    UFUNCTION(meta=(AICallable))
    static FString ApplyDraftToGame(FName ExpectedAction,const FString& DraftToken,int64 ExpectedRevision,bool bConfirmApply);
};
