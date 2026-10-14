#pragma once

#include "CoreMinimal.h"

/** Shared stroke cycle for brush/character motion and authoritative paddle thrust. */
namespace MCRowingStroke
{
    constexpr double CycleSeconds = 1.2;

    struct FSample
    {
        float Reach;    // +1 catch/reach, -1 completed pull.
        float Lift;     // Blade clears the water during recovery.
        float Power;    // Loaded stroke effort; recovery produces no thrust.
        float Recovery;
    };

    inline FSample Blend(const FSample& A, const FSample& B, float T)
    {
        const float Ease = FMath::SmoothStep(0.f, 1.f, T);
        return {FMath::Lerp(A.Reach,B.Reach,Ease),FMath::Lerp(A.Lift,B.Lift,Ease),
            FMath::Lerp(A.Power,B.Power,Ease),FMath::Lerp(A.Recovery,B.Recovery,Ease)};
    }

    inline FSample Sample(float PhaseRadians)
    {
        const float T = FMath::Fmod(FMath::Fmod(PhaseRadians / UE_TWO_PI,1.f)+1.f,1.f);
        const FSample Ready{1.f,.85f,0.f,1.f};
        const FSample Catch{1.f,0.f,.65f,0.f};
        const FSample Pull{-1.f,0.f,1.f,0.f};
        const FSample Exit{-1.f,1.f,0.f,1.f};
        if(T<.16f) return Blend(Ready,Catch,T/.16f);
        if(T<.58f) return Blend(Catch,Pull,(T-.16f)/.42f);
        if(T<.72f) return Blend(Pull,Exit,(T-.58f)/.14f);
        return Blend(Exit,Ready,(T-.72f)/.28f);
    }
}
