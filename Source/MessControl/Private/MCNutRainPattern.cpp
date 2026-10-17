#include "MCNutRainPattern.h"

void MCNutRainPattern::BuildSchedule(TConstArrayView<FVector2D> PatternA,TConstArrayView<FVector2D> PatternB,
    int32 FirstPattern,float DropSeconds,float SeriesGapSeconds,TArray<FMCNutRainPatternStep>& Out)
{
    Out.Reset();
    const float Interval=FMath::IsFinite(DropSeconds)?FMath::Clamp(DropSeconds,.1f,3.f):.5f;
    const float Gap=FMath::IsFinite(SeriesGapSeconds)?FMath::Clamp(SeriesGapSeconds,.1f,8.f):.9f;
    float Time=0;
    for(int32 Series=0;Series<2;++Series) {
        const int32 Pattern=((FirstPattern==1?1:0)+Series)%2;
        const TConstArrayView<FVector2D> Zones=Pattern==0?PatternA:PatternB;
        const int32 Count=FMath::Min(Zones.Num(),32);
        for(int32 Zone=0;Zone<Count;++Zone) {
            if(!Out.IsEmpty()) Time+=Zone==0?Gap:Interval;
            Out.Add({Zones[Zone],Pattern,Zone,Time});
        }
    }
}

double MCNutRainPattern::NextLaunchAt(double ActualLaunchAt,float PreviousPlanned,float NextPlanned)
{
    return ActualLaunchAt+FMath::Max(.1f,NextPlanned-PreviousPlanned);
}
