#pragma once

#include "Math/UnrealMathUtility.h"

namespace MCNutGravity
{
inline float FlightSeconds(float Height,float GravityZ,float DownwardSpeed)
{
    if(Height<=0.f || GravityZ>0.f || DownwardSpeed<=0.f) return 0.f;
    const float FinalSpeed=FMath::Sqrt(FMath::Square(DownwardSpeed)-2.f*GravityZ*Height);
    return 2.f*Height/(FinalSpeed+DownwardSpeed);
}
}
