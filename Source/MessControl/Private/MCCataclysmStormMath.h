#pragma once

#include "CoreMinimal.h"

/** The material author reads these exact constants; centimeters and shared WaterTime. */
namespace MCCataclysmStormMath
{
    constexpr float PrimaryLength = 640.f;
    constexpr float SecondaryLength = 370.f;
    constexpr float PrimaryAmplitude = 24.f;
    constexpr float SecondaryAmplitude = 10.f;
    constexpr float HarmonicAmplitude = 4.f;
    constexpr float PrimarySpeed = 1.65f;
    constexpr float SecondarySpeed = 2.2f;
    constexpr float SecondaryCos = .838670568f;
    constexpr float SecondarySin = .544639035f;

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
}
