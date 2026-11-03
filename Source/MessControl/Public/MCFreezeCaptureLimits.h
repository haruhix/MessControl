#pragma once
#include "CoreMinimal.h"

/** Finite capture budgets. Readiness allocates actual data sizes, not these ceilings. */
namespace MCFreezeCaptureLimits
{
    inline constexpr int32 MaxVertices=131072;
    inline constexpr int32 MaxTriangles=262144;
    inline constexpr int32 MaxSlots=8;
    // The current 66,319-vertex appearance has 46 morphs. Even a dense delta
    // for every vertex in each target fits this finite cache; sparse data stays sparse.
    inline constexpr int32 MaxMorphDeltas=4194304;
}
