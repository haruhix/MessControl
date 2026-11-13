#pragma once

#include "Math/UnrealMathUtility.h"

namespace MCDeathRespawnPresentation
{
constexpr double WhiteLead=.36;
constexpr double MergeLead=.18;
constexpr double SpitLead=.075;

inline float WhiteAlpha(double Now,double ReleaseAt)
{
    return float(FMath::SmoothStep(ReleaseAt-WhiteLead,ReleaseAt-MergeLead,Now));
}

inline float CollapseAlpha(double Now,double ReleaseAt)
{
    return float(FMath::SmoothStep(ReleaseAt-MergeLead,ReleaseAt-SpitLead,Now));
}
}
