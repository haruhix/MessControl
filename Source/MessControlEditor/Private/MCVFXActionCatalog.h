#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** Shared editor catalogue: source identity, runtime data and preview capability
 * are separate. A catalogue row is not proof of a working scenario or binding. */
struct FMCVFXActionCatalogueEntry
{
    FName ID;
    FText Label;
    FText Description;
    TSoftObjectPtr<UTexture2D> Icon;
    FName Category;
    FString SourceOwner;
    TArray<FName> Stages;
    bool bEnabled=true;
    bool bSupportsPreview=false;
    bool bSupportsProfileOverride=false;
    FString UnsupportedReason;
    FString ProfileOverrideUnsupportedReason;
};

namespace MCVFXActionCatalog
{
    /** Reads the canonical DT, then merges shared source hooks and registered
     * preview providers. Does not modify/load a user's editor world. */
    TArray<FMCVFXActionCatalogueEntry> GetCatalogue();
    /** Extension point for source-verified non-perk events. Registering metadata
     * never invents a gameplay adapter or a runtime VFXProfile binding. */
    void RegisterSource(FMCVFXActionCatalogueEntry Entry);
}
