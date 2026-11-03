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
    /** Uses the public workspace and refuses to discard any unapplied draft. */
    bool SelectActionForReview(FName ActionID,FString& OutError);
    bool PlayForReview(EMCPerkPreviewScenario Scenario,bool bLoop,FString& OutError);
    void StopForReview();
    /** Resizes only the detached Composer window, never the main Editor. */
    bool ResizeForReview(FVector2D ClientSize);
    bool ImportEventRecipe(FName ActionID,FName Stage,UMCVFXComposition* Source,FString& OutError);
}
