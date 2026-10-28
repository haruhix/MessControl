#pragma once
#include "CoreMinimal.h"

/** Calibration for the existing 45-degree manual-food fallback to a level support plane.
 * Aimable nearby targets still use SuggestProjectileVelocity and keep their endpoint.
 * Obstacles and a differently elevated landing surface are not a flat-range promise.
 */
struct FMCFoodPerkMath
{
    static double RangeAt45(double Speed,double Height,double Gravity)
    {
        if(!FMath::IsFinite(Speed) || Speed<=0 || !FMath::IsFinite(Gravity) || Gravity<=SMALL_NUMBER) return 0;
        Height=FMath::IsFinite(Height)?FMath::Max(0.,Height):0.;
        const double Axis=Speed/FMath::Sqrt(2.);
        return Axis*(Axis+FMath::Sqrt(Axis*Axis+2.*Gravity*Height))/Gravity;
    }

    static float SpeedForRangeScale(float Speed,float Scale,float Height,float Gravity)
    {
        if(!FMath::IsFinite(Speed) || Speed<=0) return 0;
        if(!FMath::IsFinite(Scale) || Scale<=0) Scale=1;
        if(FMath::IsNearlyEqual(Scale,1.f) || !FMath::IsFinite(Gravity) || Gravity<=SMALL_NUMBER) return Speed;
        const double H=FMath::IsFinite(Height)?FMath::Max(0.,double(Height)):0.;
        const double Range=RangeAt45(Speed,H,Gravity)*Scale;
        // For a 45-degree release, y=-Height at horizontal distance Range:
        // v^2 = g*Range^2 / (Height+Range). Same-height scaling is sqrt(Scale).
        const double Squared=double(Gravity)*Range*Range/FMath::Max(double(SMALL_NUMBER),H+Range);
        return FMath::IsFinite(Squared)?float(FMath::Sqrt(FMath::Max(0.,Squared))):Speed;
    }
};
