#pragma once

#include "CoreMinimal.h"

class UMCMainMenuWidget;

/** Editor-only composition preview. Never creates a play world or saves packages. */
namespace MCMainMenuPreview
{
    void Register();
    void Unregister();
    void Open();
    void Close();
    void Refresh();
    void SetState(FName State, FString Culture);
    UMCMainMenuWidget* GetPreviewWidget();
    bool IsActive();
}
