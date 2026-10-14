#pragma once
#include "CoreMinimal.h"

/** Small persistent coverage mask. UV space is shared by the mesh and material. */
struct MESSCONTROL_API FMCCoffeeWipe
{
    static constexpr int32 Size=64;
    static constexpr int32 Count=Size*Size;
    static void Reset(TArray<uint8>& Mask) { Mask.Init(255,Count); }
    // An invalid or distant brush must not change any cells. No fluid simulation.
    static bool Stroke(TArray<uint8>& Mask,FVector2D From,FVector2D To,float Radius,float Seconds,TArray<float>* Fractional=nullptr,float Rate=1.f);
    static float Remaining(const TArray<uint8>& Mask);
    // Conservative wet interior, matching the puddle material's seeded outline.
    static bool WetAt(const TArray<uint8>& Mask,FVector2D UV,int32 Seed,bool bTile=false,uint8 Threshold=100);
    // Add just this capsule to persistent coverage. Existing cleaned cells outside it stay dry.
    static bool Deposit(TArray<uint8>& Mask,TArray<uint8>& Deposited,FVector2D From,FVector2D To,float Radius,TArray<float>* Fractional=nullptr,const TFunction<bool(FVector2D)>& CoverageAllowed={});
    static bool HasWetCells(const TArray<uint8>& Mask,uint8 Threshold=63);
};
