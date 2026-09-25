#include "MCCoffeeProfile.h"

void FMCCoffeeWaterSettings::Sanitize()
{
    auto Safe=[](float V,float Default,float Min,float Max){return FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Default;};
    RippleHeight=Safe(RippleHeight,6,0,15); RippleLength=Safe(RippleLength,260,100,600); RippleSpeed=Safe(RippleSpeed,1.4f,0,4);
    FloatDepth=Safe(FloatDepth,20,0,55); BuoyancyStiffness=Safe(BuoyancyStiffness,18,5,35);
    VerticalDamping=Safe(VerticalDamping,5.2f,1,12); WaterDrag=Safe(WaterDrag,1.8f,.5f,6);
}
float FMCCoffeeWaterSettings::Ripple(FVector P,float Time) const
{
    const float K=2*PI/RippleLength, T=Time*RippleSpeed;
    // Matches the height expression in create_coffee_water.py. Normals use its analytic derivative.
    return RippleHeight*(.6f*FMath::Sin(K*(P.X+.28f*P.Y)-T)
        +.28f*FMath::Sin(K*(-.45f*P.X+.9f*P.Y)*1.73f+T*1.21f)
        +.12f*FMath::Sin(K*(.2f*P.X+P.Y)*2.7f-T*.73f));
}
FVector FMCCoffeeWaterSettings::FloatAcceleration(float SurfaceHeight,FVector P,FVector V,FVector Drive,float Gravity,float Draft) const
{
    FVector A=Drive-FVector(V.X,V.Y,0)*WaterDrag;
    A.Z=FMath::Clamp(Gravity+(SurfaceHeight-P.Z-Draft)*BuoyancyStiffness-V.Z*VerticalDamping,-200.f,2200.f);
    return A.GetClampedToMaxSize(2400.f);
}
