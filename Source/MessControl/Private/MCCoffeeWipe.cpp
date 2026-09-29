#include "MCCoffeeWipe.h"

bool FMCCoffeeWipe::WetAt(const TArray<uint8>& Mask,FVector2D UV,int32 Seed)
{
    if (UV.ContainsNaN() || UV.X<0 || UV.Y<0 || UV.X>1 || UV.Y>1) return false;
    const float Angle=Seed*2.39996f; const FVector2D U=(UV-FVector2D(.5))*2;
    FVector2D P(U.X*FMath::Cos(Angle)-U.Y*FMath::Sin(Angle),U.X*FMath::Sin(Angle)+U.Y*FMath::Cos(Angle));
    P.Y*=1.06+.20*FMath::Sin(Seed*1.731);
    auto Join=[](double A,double B) { const double H=FMath::Max(.065-FMath::Abs(A-B),0.)/.065; return FMath::Max(A,B)+H*H*.01625; };
    double D=.48-P.Size();
    D=Join(D,.30+.045*FMath::Sin(Seed*2.17)-(P-FVector2D(.32,.08)).Size());
    D=Join(D,.27+.045*FMath::Cos(Seed*3.11)-(P-FVector2D(-.35,-.09)).Size());
    D=Join(D,.25-(P-FVector2D(-.12,.34)).Size()); D=Join(D,.24-(P-FVector2D(.20,-.30)).Size());
    D+=.018*FMath::Sin(P.X*24+Seed)*FMath::Sin(P.Y*19-Seed);
    // Tiny decorative droplets have no movement penalty. Cleaned mask cells are dry immediately.
    if (D<.025) return false;
    const int32 X=FMath::Clamp(int32(UV.X*Size),0,Size-1),Y=FMath::Clamp(int32(UV.Y*Size),0,Size-1);
    return Mask.Num()!=Count || Mask[Y*Size+X]>100;
}

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
