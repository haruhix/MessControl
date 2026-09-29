#pragma once
#include "CoreMinimal.h"

/** Continuous heel-to-toe stance and a smooth swing, with a longer planted phase on sticky ground. */
struct FMCLocomotionCycle
{
    float Sweep=0,Lift=0;
    bool bStance=true;
    static FMCLocomotionCycle Sample(float Phase,float Stance)
    {
        Phase=FMath::Frac(Phase); if (Phase<0) Phase+=1;
        Stance=FMath::Clamp(Stance,.35f,.8f);
        FMCLocomotionCycle Result;
        Result.bStance=Phase<Stance;
        if (Result.bStance) Result.Sweep=1-2*Phase/Stance;
        else
        {
            const float U=(Phase-Stance)/(1-Stance);
            Result.Sweep=-FMath::Cos(U*PI);
            Result.Lift=FMath::Square(FMath::Sin(U*PI));
        }
        return Result;
    }
};
