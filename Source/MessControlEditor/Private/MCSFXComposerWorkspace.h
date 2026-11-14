#pragma once

#include "CoreMinimal.h"

class FMCVFXActionSession;
enum class EMCPerkPreviewScenario : uint8;

/** Independent audio workspace over the same editor-lifetime action documents as VFX Composer. */
namespace MCSFXComposerWorkspace
{
    void Register();
    void Unregister();
    void Open();
    TSharedPtr<FMCVFXActionSession> GetActiveSession();
    FString GetSnapshotJson();
    bool SelectForReview(FName Action, FName Event, FGuid Layer, FString& Error);
    bool PlayForReview(EMCPerkPreviewScenario Scenario, bool bLoop, FString& Error);
    void StopForReview();
    /** Changes only a detached SFX window; a docked main Editor window is never resized. */
    bool ResizeForReview(FVector2D ClientSize);
}
