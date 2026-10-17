#pragma once

#include "CoreMinimal.h"

/** One authored zone, visited exactly once; only the order of the two complete groups changes. */
struct FMCNutRainPatternStep
{
    FVector2D Zone=FVector2D::ZeroVector;
    int32 PatternIndex=0;
    int32 ZoneIndex=0;
    float LaunchSeconds=0;
};

namespace MCNutRainPattern
{
    MESSCONTROL_API void BuildSchedule(TConstArrayView<FVector2D> PatternA,TConstArrayView<FVector2D> PatternB,
        int32 FirstPattern,float DropSeconds,float SeriesGapSeconds,TArray<FMCNutRainPatternStep>& Out);
    /** A hitch stretches the sequence: never catch up by launching several nuts on one frame. */
    MESSCONTROL_API double NextLaunchAt(double ActualLaunchAt,float PreviousPlanned,float NextPlanned);
}
