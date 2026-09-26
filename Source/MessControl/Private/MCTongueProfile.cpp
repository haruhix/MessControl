#include "MCTongueProfile.h"
void FMCTongueSettings::Sanitize()
{
    auto Clamp=[](float V,float Default,float Min,float Max) { return FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Default; };
    IdleHeight=Clamp(IdleHeight,3,0,8); IdlePeriod=Clamp(IdlePeriod,5,2,30);
    JoltRestMin=Clamp(JoltRestMin,22,8,180); JoltRestMax=Clamp(JoltRestMax,32,JoltRestMin,240);
    JoltHeight=Clamp(JoltHeight,180,0,220); JoltAnticipation=Clamp(JoltAnticipation,.7f,.4f,2);
    JoltRise=Clamp(JoltRise,.35f,.25f,1); JoltReturn=Clamp(JoltReturn,1.4f,.7f,3);
    JoltLift=Clamp(JoltLift,1000,400,1200); JoltPush=Clamp(JoltPush,260,0,500);
    WaveHeight=Clamp(WaveHeight,28,0,50); WaveSpeed=Clamp(WaveSpeed,850,100,2000);
    WaveWidth=Clamp(WaveWidth,260,100,600); WaveRadius=Clamp(WaveRadius,2600,200,4000);
    Cooldown=FMath::Max(Clamp(Cooldown,4,1,60),Duration()+.2f);
    PushSpeed=Clamp(PushSpeed,470,0,1000); LiftSpeed=Clamp(LiftSpeed,210,0,500); AffectHeight=Clamp(AffectHeight,170,20,400);
}
float FMCTongueSettings::JoltShape(float Age) const
{
    if (Age<0 || Age>=JoltDuration()) return 0;
    if (Age<JoltAnticipation) return -.12f*FMath::SmoothStep(0.f,JoltAnticipation,Age);
    Age-=JoltAnticipation;
    if (Age<JoltRise) return FMath::Lerp(-.12f,1.f,FMath::SmoothStep(0.f,JoltRise,Age));
    Age-=JoltRise;
    if (Age<.15f) return 1;
    Age-=.15f;
    if (Age<JoltReturn) return 1-FMath::SmoothStep(0.f,JoltReturn,Age);
    Age-=JoltReturn;
    // Small follow-through after the main bend; both ends have zero velocity.
    return -.035f*FMath::Square(FMath::Sin(PI*Age/.5f));
}
float FMCTongueSettings::Band(float Distance,float Age) const
{
    if (Age<0 || Age>Duration() || Distance>WaveRadius) return 0;
    const float X=FMath::Abs(Distance-WaveSpeed*Age)/WaveWidth;
    const float Fade=1-FMath::SmoothStep(WaveRadius-WaveWidth,WaveRadius,Distance);
    return X<1?.5f*(1+FMath::Cos(PI*X))*Fade:0;
}
bool FMCTongueSettings::Crossed(float Distance,float PreviousAge,float Age) const
{
    return Age>=0 && Distance<=WaveRadius && Distance<=Age*WaveSpeed+WaveWidth*.5f
        && Distance>=FMath::Max(0.f,PreviousAge*WaveSpeed-WaveWidth*.5f);
}
