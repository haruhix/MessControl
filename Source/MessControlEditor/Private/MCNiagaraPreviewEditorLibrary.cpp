#include "MCNiagaraPreviewEditorLibrary.h"

#include "Editor.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectIterator.h"

bool UMCNiagaraPreviewEditorLibrary::ConfigureNiagaraPreview(
    UNiagaraSystem* System, FVector ViewLocation, FRotator ViewRotation)
{
    if (!IsInGameThread() || !IsValid(System) || !GEditor
        || ViewLocation.ContainsNaN() || ViewRotation.ContainsNaN())
    {
        return false;
    }

    UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditors)
    {
        return false;
    }

    // An open Niagara viewport writes its camera back when closed; close it before changing the saved settings.
    AssetEditors->CloseAllEditorsForAsset(System);
    if (AssetEditors->FindEditorForAsset(System, false))
    {
        return false;
    }

    UNiagaraSystemEditorData* EditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
    if (!EditorData)
    {
        return false;
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("MessControlEditor", "ConfigureNiagaraPreview", "Configure Niagara Preview Camera"));
    System->Modify();
    EditorData->Modify();
    FNiagaraPerAssetViewportSettings Settings = EditorData->GetAssetViewportSettings();
    Settings.ViewLocation = ViewLocation;
    Settings.ViewRotation = ViewRotation;
    Settings.bUseOrbitMode = false;
    EditorData->SetAssetViewportSettings(Settings);
    System->MarkPackageDirty();
    return true;
}

FString UMCNiagaraPreviewEditorLibrary::InspectNiagaraPreview(UNiagaraSystem* System)
{
    if (!IsInGameThread() || !IsValid(System))
    {
        return TEXT("Invalid system or non-game-thread call");
    }

    const UNiagaraSystemEditorData* EditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
    if (!EditorData)
    {
        return TEXT("System has no Niagara system editor data");
    }

    const FNiagaraPerAssetViewportSettings& Settings = EditorData->GetAssetViewportSettings();
    // These are the persisted camera fields, not a claim about an open viewport's unsaved camera.
    FString Result = FString::Printf(TEXT("Asset=%s; StoredViewLocation=(%s); StoredViewRotation=(%s); StoredUseOrbitMode=%s"),
        *System->GetPathName(), *Settings.ViewLocation.ToString(), *Settings.ViewRotation.ToString(),
        Settings.bUseOrbitMode ? TEXT("true") : TEXT("false"));

    int32 PreviewCount = 0;
    for (TObjectIterator<UNiagaraComponent> It; It; ++It)
    {
        const UNiagaraComponent* Component = *It;
        if (!IsValid(Component))
        {
            continue;
        }
        const UWorld* World = Component->GetWorld();
        if (Component->GetAsset() != System || !World || World->WorldType != EWorldType::EditorPreview)
        {
            continue;
        }

        ++PreviewCount;
        Result += FString::Printf(TEXT("; PreviewComponent=%s Registered=%d Visible=%d RenderingEnabled=%d Active=%d Complete=%d BoundsOrigin=(%s) BoundsExtent=(%s)"),
            *Component->GetPathName(), Component->IsRegistered(), Component->IsVisible(), Component->GetRenderingEnabled(),
            Component->IsActive(), Component->IsComplete(), *Component->Bounds.Origin.ToString(), *Component->Bounds.BoxExtent.ToString());
    }
    Result += FString::Printf(TEXT("; EditorPreviewComponentCount=%d"), PreviewCount);
    return Result;
}
