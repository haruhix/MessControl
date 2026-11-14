#pragma once

#include "CoreMinimal.h"

class FMCVFXActionSession;
class UMCVFXComposition;
enum class EMCPerkPreviewScenario : uint8;

/** Single public action -> event -> layers authoring workspace. */
namespace MCVFXActionWorkspace
{
    void Register();
    void Unregister();
    void Open();
    TSharedPtr<FMCVFXActionSession> GetActiveSession();
    FString GetSnapshotJson();
    /** Switches this view; shared documents retain unapplied drafts. */
    bool SelectActionForReview(FName ActionID,FString& OutError);
    /** Explicit cross-window navigation to an exact shared audio event/layer. */
    bool SelectAudioTargetForReview(FName ActionID,FName Event,FGuid Layer,FString& OutError);
    bool PlayForReview(EMCPerkPreviewScenario Scenario,bool bLoop,FString& OutError);
    void StopForReview();
    /** Resizes only the detached Composer window, never the main Editor. */
    bool ResizeForReview(FVector2D ClientSize);
    bool SetPreviewViewForReview(int32 Preset);
    bool ImportEventRecipe(FName ActionID,FName Stage,UMCVFXComposition* Source,FString& OutError);
}
