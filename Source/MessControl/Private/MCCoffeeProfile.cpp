#include "MCCoffeeProfile.h"

void FMCCoffeeWaterSettings::Sanitize()
{
    auto Safe=[](float V,float Default,float Min,float Max){return FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Default;};
    RippleHeight=Safe(RippleHeight,6,0,15); RippleLength=Safe(RippleLength,260,100,600); RippleSpeed=Safe(RippleSpeed,1.4f,0,4);
    DryHeight=Safe(DryHeight,-18,-1000,100);
    FloatDepth=Safe(FloatDepth,20,0,55); BuoyancyStiffness=Safe(BuoyancyStiffness,18,5,35);
    VerticalDamping=Safe(VerticalDamping,5.2f,1,12); WaterDrag=Safe(WaterDrag,1.8f,.5f,6);
    FillSeconds=Safe(FillSeconds,4,1,15); DrainSeconds=Safe(DrainSeconds,2,.5f,10); Cycles=FMath::Clamp(Cycles,1,4);
    if (Inlet.ContainsNaN()) Inlet=FVector(420,-100,1100);
    Inlet=Inlet.BoundToBox(FVector(-2500,-1500,300),FVector(2500,1500,1800));
    if (DrainPoint.ContainsNaN()) DrainPoint=FVector(920,0,0);
    DrainPoint=DrainPoint.BoundToBox(FVector(-3000,-2000,-500),FVector(3000,2000,500));
    JetRadius=Safe(JetRadius,85,30,180); FrontSpeed=Safe(FrontSpeed,950,200,2000);
    FrontWidth=Safe(FrontWidth,110,40,250); FrontHeight=Safe(FrontHeight,26,0,60);
    ImpactImpulse=Safe(ImpactImpulse,520,0,900); DrainAcceleration=Safe(DrainAcceleration,720,0,1500);
    DrainRadius=Safe(DrainRadius,320,120,650); DrainDepth=Safe(DrainDepth,65,0,120);
}
float FMCCoffeeWaterSettings::CycleTime(float Time) const { return FMath::Fmod(FMath::Max(0.f,Time),CycleSeconds()); }
EMCCoffeePhase FMCCoffeeWaterSettings::Phase(float Time) const
{
    if (Time<0 || Time>=CycleSeconds()*Cycles) return EMCCoffeePhase::Inactive;
    return CycleTime(Time)<FillSeconds?EMCCoffeePhase::Filling:EMCCoffeePhase::Draining;
}
float FMCCoffeeWaterSettings::FillAmount(float Time) const
{
    const auto State=Phase(Time); const float T=CycleTime(Time);
    if (State==EMCCoffeePhase::Inactive) return 0;
    if (State==EMCCoffeePhase::Draining) return 1-FMath::SmoothStep(0.f,1.f,(T-FillSeconds)/DrainSeconds);
    return FMath::SmoothStep(0.f,1.f,T/FillSeconds);
}
float FMCCoffeeWaterSettings::DrainAmount(float Time) const
{
    return Phase(Time)==EMCCoffeePhase::Draining?FMath::SmoothStep(0.f,.25f,CycleTime(Time)-FillSeconds):0;
}
float FMCCoffeeWaterSettings::JetAmount(float Time) const
{
    if (Phase(Time)!=EMCCoffeePhase::Filling) return 0;
    const float T=CycleTime(Time);
    return FMath::SmoothStep(0.f,.2f,T)*(1-FMath::SmoothStep(FillSeconds-.2f,FillSeconds,T));
}
float FMCCoffeeWaterSettings::FrontRadius(float Time) const { return FMath::Max(0.f,CycleTime(Time)-.25f)*FrontSpeed; }
float FMCCoffeeWaterSettings::SurfaceOffset(FVector P,float Time) const
{
    const float D=FVector::Dist2D(P,Inlet), R=FVector::Dist2D(P,DrainPoint)/DrainRadius;
    const float Band=(D-FrontRadius(Time))/FrontWidth;
    const float Ridge=Phase(Time)==EMCCoffeePhase::Filling?FrontHeight*FMath::Exp(-Band*Band)*FMath::Exp(-D/1600):0;
    return Ripple(P,Time)*FMath::Clamp(FillAmount(Time)*4,0.f,1.f)+Ridge
        +JetAmount(Time)*18*FMath::Exp(-FMath::Square(D/140))
        -DrainAmount(Time)*DrainDepth*FMath::Exp(-R*R);
}
FVector FMCCoffeeWaterSettings::FlowAt(FVector P,float Time,float FillAcceleration) const
{
    if (Phase(Time)==EMCCoffeePhase::Inactive) return FVector::ZeroVector;
    if (Phase(Time)==EMCCoffeePhase::Draining)
    {
        const FVector To=(DrainPoint-P).GetSafeNormal2D();
        const float Near=FMath::Exp(-FMath::Square(FVector::Dist2D(P,DrainPoint)/DrainRadius));
        return To*DrainAcceleration*(.65f+.35f*Near)*DrainAmount(Time);
    }
    const FVector Out=(P-Inlet).GetSafeNormal2D();
    return Out*FillAcceleration*(.35f+.65f*FMath::Exp(-FVector::Dist2D(P,Inlet)/550))*JetAmount(Time);
}
float FMCCoffeeWaterSettings::Ripple(FVector P,float Time) const
{
    const float K=2*PI/RippleLength, T=Time*RippleSpeed;
    // Matches the height expression in create_coffee_pour.py. Normals use its analytic derivative.
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
