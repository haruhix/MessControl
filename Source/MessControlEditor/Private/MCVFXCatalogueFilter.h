#pragma once

#include "MCVFXActionCatalog.h"

/** Pure view predicate: filtering cannot switch/stop a source or mutate its draft. */
struct FMCVFXCatalogueFilter
{
    EMCVFXActionSection Section=EMCVFXActionSection::All;
    TOptional<EMCPerkKind> BoosterKind;
    FString Query;

    bool Matches(const FMCVFXActionCatalogueEntry& Entry)const;
};

namespace MCVFXCatalogueNavigation
{
    FText Label(EMCVFXActionSection Section);
    FText BoosterLabel(TOptional<EMCPerkKind> Kind);
    /** Compatibility mapping of declared source Category metadata, not skill IDs. */
    EMCVFXActionSection FromSourceCategory(FName Category);
}
