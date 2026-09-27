#include "MCCoffeeWipe.h"

bool FMCCoffeeWipe::Stroke(TArray<uint8>& Mask,FVector2D From,FVector2D To,float Radius,float Seconds)
{
    if (From.ContainsNaN() || To.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<=0 ||
        !FMath::IsFinite(Seconds) || Seconds<=0) return false;
    Radius=FMath::Clamp(Radius,.01f,.35f);
    if (FMath::Max(From.X,To.X)<-Radius || FMath::Min(From.X,To.X)>1+Radius ||
        FMath::Max(From.Y,To.Y)<-Radius || FMath::Min(From.Y,To.Y)>1+Radius) return false;
    if (Mask.Num()!=Count) Reset(Mask);
    const FVector2D Segment=To-From;
    const double Length=Segment.SizeSquared();
    const float Strength=1-FMath::Exp(-12*FMath::Min(Seconds,.1f));
    bool Changed=false;
    for (int32 Y=0;Y<Size;++Y) for (int32 X=0;X<Size;++X)
    {
        const FVector2D P((X+.5)/Size,(Y+.5)/Size);
        const double T=Length>1.e-8?FMath::Clamp(FVector2D::DotProduct(P-From,Segment)/Length,0.,1.):0;
        const float Distance=(P-(From+Segment*T)).Size()/Radius;
        if (Distance>=1) continue;
        const float Weight=1-FMath::SmoothStep(.45f,1.f,Distance);
        uint8& Value=Mask[Y*Size+X];
        const uint8 Next=FMath::FloorToInt(Value*(1-Strength*Weight));
        Changed|=Value!=Next; Value=Next;
    }
    return Changed;
}
float FMCCoffeeWipe::Remaining(const TArray<uint8>& Mask)
{
    if (Mask.Num()!=Count) return 1;
    int32 Sum=0; for (uint8 Value:Mask) Sum+=Value;
    return float(Sum)/(255*Count);
}
