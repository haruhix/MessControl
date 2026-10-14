#include "MCCoffeeWipe.h"

bool FMCCoffeeWipe::WetAt(const TArray<uint8>& Mask,FVector2D UV,int32 Seed,bool bTile,uint8 Threshold)
{
    if (UV.ContainsNaN() || UV.X<0 || UV.Y<0 || UV.X>1 || UV.Y>1) return false;
    if(bTile) {
        if(Mask.Num()!=Count) return false;
        // Match the tile material's bilinear, clamped mask sampling.
        const FVector2D Texel=UV*Size-FVector2D(.5);
        const int32 X=FMath::FloorToInt(Texel.X),Y=FMath::FloorToInt(Texel.Y);
        const auto Sample=[&](int32 SX,int32 SY) {return float(Mask[FMath::Clamp(SY,0,Size-1)*Size+FMath::Clamp(SX,0,Size-1)]);};
        return FMath::Lerp(FMath::Lerp(Sample(X,Y),Sample(X+1,Y),float(Texel.X-X)),
            FMath::Lerp(Sample(X,Y+1),Sample(X+1,Y+1),float(Texel.X-X)),float(Texel.Y-Y))>Threshold;
    }
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
    return Mask.Num()!=Count || Mask[Y*Size+X]>Threshold;
}

bool FMCCoffeeWipe::HasWetCells(const TArray<uint8>& Mask,uint8 Threshold)
{
    for(uint8 Value:Mask) if(Value>Threshold) return true;
    return false;
}

bool FMCCoffeeWipe::Deposit(TArray<uint8>& Mask,TArray<uint8>& Deposited,FVector2D From,FVector2D To,float Radius,TArray<float>* Fractional,const TFunction<bool(FVector2D)>& CoverageAllowed)
{
    if(From.ContainsNaN() || To.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<=0
        || FMath::Max(From.X,To.X)<-Radius || FMath::Min(From.X,To.X)>1+Radius
        || FMath::Max(From.Y,To.Y)<-Radius || FMath::Min(From.Y,To.Y)>1+Radius) return false;
    if(Mask.Num()!=Count) Mask.Init(0,Count);
    if(Deposited.Num()!=Count) Deposited.Init(0,Count);
    const FVector2D Segment=To-From;
    const double Length=Segment.SizeSquared();
    const int32 MinX=FMath::Clamp(FMath::FloorToInt((FMath::Min(From.X,To.X)-Radius)*Size),0,Size-1);
    const int32 MaxX=FMath::Clamp(FMath::CeilToInt((FMath::Max(From.X,To.X)+Radius)*Size),0,Size-1);
    const int32 MinY=FMath::Clamp(FMath::FloorToInt((FMath::Min(From.Y,To.Y)-Radius)*Size),0,Size-1);
    const int32 MaxY=FMath::Clamp(FMath::CeilToInt((FMath::Max(From.Y,To.Y)+Radius)*Size),0,Size-1);
    bool Changed=false;
    for(int32 Y=MinY;Y<=MaxY;++Y) for(int32 X=MinX;X<=MaxX;++X) {
        const FVector2D P((X+.5)/Size,(Y+.5)/Size);
        const double T=Length>1.e-8?FMath::Clamp(FVector2D::DotProduct(P-From,Segment)/Length,0.,1.):0;
        const float Distance=(P-(From+Segment*T)).Size()/Radius;
        if(Distance>=1) continue;
        if(CoverageAllowed && !CoverageAllowed(P)) continue;
        const uint8 Value=FMath::Clamp(FMath::RoundToInt(255*(1-FMath::SmoothStep(.72f,1.f,Distance))),0,255);
        const int32 I=Y*Size+X;
        Deposited[I]=FMath::Max(Deposited[I],Value);
        if(Value>Mask[I]) {
            Mask[I]=Value;Changed=true;
            if(Fractional && Fractional->Num()==Count) (*Fractional)[I]=0;
        }
    }
    return Changed;
}

bool FMCCoffeeWipe::Stroke(TArray<uint8>& Mask,FVector2D From,FVector2D To,float Radius,float Seconds,TArray<float>* Fractional,float Rate)
{
    if (From.ContainsNaN() || To.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<=0 ||
        !FMath::IsFinite(Seconds) || Seconds<=0 || !FMath::IsFinite(Rate)) return false;
    Radius=FMath::Clamp(Radius,.01f,Rate>1.f?.5f:.35f);
    if (FMath::Max(From.X,To.X)<-Radius || FMath::Min(From.X,To.X)>1+Radius ||
        FMath::Max(From.Y,To.Y)<-Radius || FMath::Min(From.Y,To.Y)>1+Radius) return false;
    if (Mask.Num()!=Count) Reset(Mask);
    if(Fractional && Fractional->Num()!=Count) Fractional->Init(0,Count);
    const FVector2D Segment=To-From;
    const double Length=Segment.SizeSquared();
    const float Exposure=FMath::Min(Seconds,.1f)*FMath::Clamp(Rate,1.f,2.f);
    const float Strength=1-FMath::Exp(-12*Exposure);
    bool Changed=false;
    for (int32 Y=0;Y<Size;++Y) for (int32 X=0;X<Size;++X)
    {
        const FVector2D P((X+.5)/Size,(Y+.5)/Size);
        const double T=Length>1.e-8?FMath::Clamp(FVector2D::DotProduct(P-From,Segment)/Length,0.,1.):0;
        const float Distance=(P-(From+Segment*T)).Size()/Radius;
        if (Distance>=1) continue;
        const float Weight=1-FMath::SmoothStep(.45f,1.f,Distance);
        const int32 Index=Y*Size+X;
        uint8& Value=Mask[Index];
        // Keep sub-byte coverage on the server. Rounding every rendered frame
        // otherwise erases the same contact faster at a higher packaged FPS.
        const float Coverage=Value+(Fractional?(*Fractional)[Index]:0.f);
        const float Reduced=Fractional?Coverage*FMath::Exp(-12*Exposure*Weight):Coverage*(1-Strength*Weight);
        const uint8 Next=FMath::Clamp(FMath::FloorToInt(Reduced),0,255);
        if(Fractional) (*Fractional)[Index]=Reduced-Next;
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
