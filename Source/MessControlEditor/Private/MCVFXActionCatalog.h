#pragma once

#include "CoreMinimal.h"
#include "MCPerkTypes.h"

class UTexture2D;

/** Navigation metadata, independent of source identity and preview capability. */
enum class EMCVFXActionSection : uint8
{
    All, Boosters, Bosses, WorldEvents, General, InterfaceHub, Other
};

/** Shared editor catalogue: source identity, runtime data and preview capability
 * are separate. A catalogue row is not proof of a working scenario or binding. */
struct FMCVFXActionCatalogueEntry
{
    FName ID;
    FText Label;
    FText Description;
    TSoftObjectPtr<UTexture2D> Icon;
    FName Category;
    /** Editor navigation identity, distinct from the canonical source owner/path. */
    FName OwnerID;
    FText OwnerLabel;
    /** Optional short action name beneath an explicit owner node. */
    FText ActionLabel;
    EMCVFXActionSection Section=EMCVFXActionSection::Other;
    /** Set only by an actual canonical DT row, never inferred from an ID/name. */
    TOptional<EMCPerkKind> PerkKind;
    TArray<FString> SearchNames;
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
