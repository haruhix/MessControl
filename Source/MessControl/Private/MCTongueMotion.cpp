#include "MCTongueMotion.h"
void FMCTongueMotionSettings::Sanitize()
{
    auto C=[](float V,float D,float Min,float Max){return FMath::Clamp(FMath::IsFinite(V)?V:D,Min,Max);};
    if (uint8(Shape)>uint8(EMCTongueShape::DirectionalWave)) Shape=EMCTongueShape::LocalLift;
    Height=C(Height,60,-220,220); Radius=C(Radius,550,100,4000); Width=C(Width,260,100,600); Speed=C(Speed,850,100,2000);
    Anticipation=C(Anticipation,.5f,0,3); Rise=C(Rise,.35f,.25f,3); Hold=C(Hold,.15f,0,3); Return=C(Return,1.4f,.5f,5);
    RestAfter=C(RestAfter,1,0,30); Compression=C(Compression,.12f,0,.25f); FollowThrough=C(FollowThrough,.035f,0,.1f);
    Redness=C(Redness,0,0,1); Lift=C(Lift,450,0,1200); Push=C(Push,200,0,1000); AffectHeight=C(AffectHeight,170,20,400);
}
float FMCTongueMotionSettings::Duration() const
{
    return Anticipation+(IsWave()?(Radius+Width)/Speed:Rise+Hold+Return+.5f);
}
float FMCTongueMotionSettings::Envelope(float Age) const
{
    if (Age<0 || Age>=Duration()) return 0;
    if (Age<Anticipation) return -Compression*FMath::SmoothStep(0.f,Anticipation,Age);
    Age-=Anticipation;
    if (Age<Rise) return FMath::Lerp(-Compression,1.f,FMath::SmoothStep(0.f,Rise,Age));
    Age-=Rise; if (Age<Hold) return 1;
    Age-=Hold; if (Age<Return) return 1-FMath::SmoothStep(0.f,Return,Age);
    return -FollowThrough*FMath::Square(FMath::Sin(PI*(Age-Return)/.5f));
}
float FMCTongueMotionSettings::Band(float Distance,float Age) const
{
    Age-=Anticipation; if (Age<0 || Age>(Radius+Width)/Speed || Distance<0 || Distance>Radius) return 0;
    const float X=FMath::Abs(Distance-Speed*Age)/Width;
    return X<1?.5f*(1+FMath::Cos(PI*X))*(1-FMath::SmoothStep(FMath::Max(0.f,Radius-Width),Radius,Distance)):0;
}
bool FMCTongueMotionSettings::Crossed(float Distance,float Before,float Age) const
{
    Age-=Anticipation; Before-=Anticipation;
    return Age>=0 && Distance>=0 && Distance<=Radius && Distance<=Age*Speed+Width*.5f
        && Distance>=FMath::Max(0.f,Before*Speed-Width*.5f);
}
