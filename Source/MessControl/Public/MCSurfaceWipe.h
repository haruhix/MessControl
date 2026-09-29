#pragma once
#include "CoreMinimal.h"

/** Small object-space volume: brushing one face cannot erase the opposite face or a UV seam. */
struct FMCSurfaceWipe
{
    static constexpr int32 Size=16, Atlas=64, Count=Size*Size*Size;
    static int32 Index(int32 X,int32 Y,int32 Z) { return X+(Z%4)*Size+(Y+(Z/4)*Size)*Atlas; }
    static void Reset(TArray<uint8>& Mask) { Mask.Init(255,Count); }
    static float Sample(const TArray<uint8>& Mask,FVector UV)
    {
        if(Mask.Num()!=Count) return 1;
        const FVector P=UV.ComponentMax(FVector::ZeroVector).ComponentMin(FVector::OneVector)*(Size-1);
        const int32 X=FMath::FloorToInt(P.X),Y=FMath::FloorToInt(P.Y),Z=FMath::FloorToInt(P.Z);
        float V=0;
        for(int32 K=0;K<2;++K) for(int32 J=0;J<2;++J) for(int32 I=0;I<2;++I)
            V+=Mask[Index(FMath::Min(X+I,Size-1),FMath::Min(Y+J,Size-1),FMath::Min(Z+K,Size-1))]/255.f*
                (I?P.X-X:1-P.X+X)*(J?P.Y-Y:1-P.Y+Y)*(K?P.Z-Z:1-P.Z+Z);
        return V;
    }
    static bool Stroke(TArray<uint8>& Mask,FVector From,FVector To,FVector Dimensions,float Radius,float Seconds)
    {
        if (Mask.Num()!=Count || From.ContainsNaN() || To.ContainsNaN() || Dimensions.ContainsNaN() ||
            Dimensions.GetMin()<=0 || !FMath::IsFinite(Radius) || Radius<=0 || !FMath::IsFinite(Seconds) || Seconds<=0) return false;
        bool Changed=false;
        for (int32 Z=0;Z<Size;++Z) for (int32 Y=0;Y<Size;++Y) for (int32 X=0;X<Size;++X)
        {
            const FVector P=FVector(X,Y,Z)/double(Size-1)*Dimensions;
            const float Distance=FMath::PointDistToSegment(P,From*Dimensions,To*Dimensions);
            const float Weight=1-FMath::SmoothStep(Radius*.45f,Radius,Distance);
            const int32 I=Index(X,Y,Z);
            const uint8 Next=FMath::Max(0,int32(Mask[I])-FMath::RoundToInt(Weight*FMath::Min(Seconds,.1f)*950));
            Changed|=Next!=Mask[I]; Mask[I]=Next;
        }
        return Changed;
    }
};
