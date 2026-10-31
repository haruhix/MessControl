#pragma once

#include "CoreMinimal.h"

class FMCVFXActionSession;
class UMCVFXComposition;

/** Single public action -> event -> layers authoring workspace. */
namespace MCVFXActionWorkspace
{
    void Register();
    void Unregister();
    void Open();
    TSharedPtr<FMCVFXActionSession> GetActiveSession();
    FString GetSnapshotJson();
    bool ImportEventRecipe(FName ActionID,FName Stage,UMCVFXComposition* Source,FString& OutError);
}
