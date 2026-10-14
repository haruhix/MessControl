#pragma once

#include "CoreMinimal.h"

/** The material author reads these exact constants; centimeters and shared WaterTime. */
namespace MCCataclysmStormMath
{
    constexpr float PrimaryLength = 640.f;
    constexpr float SecondaryLength = 370.f;
    constexpr float PrimaryAmplitude = 32.f;
    constexpr float SecondaryAmplitude = 14.f;
    constexpr float HarmonicAmplitude = 6.f;
    constexpr float PrimarySpeed = 5.8f;
    constexpr float SecondarySpeed = 8.f;
    constexpr float SecondaryCos = .838670568f;
    constexpr float SecondarySin = .544639035f;
    constexpr float FrontWidth = 180.f;
    constexpr float FrontAmplitude = 90.f;
    constexpr float FrontCrestWidth = 210.f;
    constexpr float DrainRadius = 520.f;
    constexpr float DrainDepth = 130.f;
    constexpr float OpticalSpeed = 650.f;

    inline float SeedPhase(int32 Seed) { return float(uint32(Seed) & 4095u) * (2.f * PI / 4096.f); }

    inline float Height(FVector Position,FVector Center,FVector Direction,float Time,int32 Seed,float Strength)
    {
        const FVector Q=Position-Center;
        const FVector Secondary(Direction.X*SecondaryCos-Direction.Y*SecondarySin,
            Direction.X*SecondarySin+Direction.Y*SecondaryCos,0);
        const float Phase=SeedPhase(Seed);
        const float A=float(FVector::DotProduct(Q,Direction))*(2.f*PI/PrimaryLength)-Time*PrimarySpeed+Phase;
        const float B=float(FVector::DotProduct(Q,Secondary))*(2.f*PI/SecondaryLength)-Time*SecondarySpeed+Phase*1.37f+1.1f;
        return Strength*(PrimaryAmplitude*FMath::Sin(A)+SecondaryAmplitude*FMath::Sin(B)
            +HarmonicAmplitude*FMath::Sin(2.f*A+.4f));
    }

    inline float FrontJitter(float Across,float Time)
    {
        return FMath::Sin(Across*.0035f+Time*1.6f)*75.f+FMath::Sin(Across*.0081f-Time*2.2f)*32.f;
    }

    inline float ArrivalWeight(FVector P,FVector Inlet,FVector Direction,float Time,float Front,float Tail)
    {
        const FVector Side(-Direction.Y,Direction.X,0),Q=P-Inlet;
        const float Along=FVector::DotProduct(Q,Direction),Across=FVector::DotProduct(Q,Side);
        const float Edge=Front+FrontJitter(Across,Time);
        return FMath::SmoothStep(0.f,.2f,Time)*FMath::SmoothStep(-120.f,20.f,Along)
            *(1-FMath::SmoothStep(Edge-FrontWidth,Edge+FrontWidth,Along))
            *FMath::SmoothStep(Tail-FrontWidth,Tail+FrontWidth,Along);
    }

    inline float WetWeight(FVector P,FVector Center,FVector Inlet,FVector Direction,FVector HalfSize,
        float Time,float Front,float Tail)
    {
        // Flood the complete mouth footprint. The sink radius controls the
        // local depression/current, never the width or length of wet water.
        if(FMath::Abs(P.X-Center.X)>=HalfSize.X || FMath::Abs(P.Y-Center.Y)>=HalfSize.Y) return 0.f;
        return ArrivalWeight(P,Inlet,Direction,Time,Front,Tail);
    }

    inline float InflowOffset(FVector P,FVector Center,FVector Inlet,FVector Outlet,FVector Direction,
        float Time,int32 Seed,float Front)
    {
        const FVector Side(-Direction.Y,Direction.X,0),Q=P-Inlet;
        const float Along=FVector::DotProduct(Q,Direction),Across=FVector::DotProduct(Q,Side);
        const float Crest=FrontAmplitude*FMath::Exp(-FMath::Square((Along-Front-FrontJitter(Across,Time)+100.f)/FrontCrestWidth))
            *(1.f+.12f*FMath::Sin(Across*.009f-Time*5.f));
        const float Funnel=DrainDepth*FMath::Exp(-FMath::Square(float(FVector::Dist2D(P,Outlet))/DrainRadius));
        return Height(P,Center,Direction,Time,Seed,1.f)+Crest-Funnel;
    }
}
