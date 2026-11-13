#pragma once

#include "MCKatanaSwing.h"

namespace MCKatanaSwing
{
// The held gesture follows an immediate ordinary cut. Play the charged clip's
// authored preparation from its matching guard instead of jumping to release.
// Keep this runtime clock shared by the pose, contact queries, facing and VFX.
inline float PlaybackStart(EVariant Variant)
{
    return Variant==EVariant::Charged?0.f:StartOffset(Variant);
}

inline float PlaybackRate(EVariant Variant,float AttackRate)
{
    return FMath::Clamp(AttackRate,.2f,3.f)*(Variant==EVariant::Charged?1.4f:2.f);
}

inline TConstArrayView<FCutWindow> PlaybackCuts(EVariant Variant)
{
    // Zero-based authored frames from the broad-swing master at 30 fps.
    // Contact queries, trail windows and the end of facing assistance must
    // agree on this interval; preparation and recovery remain harmless.
    static constexpr FCutWindow Right[]={{6.f/30.f,13.f/30.f}};
    static constexpr FCutWindow Left[]={{7.f/30.f,14.f/30.f}};
    static constexpr FCutWindow Overhead[]={{8.f/30.f,16.f/30.f}};
    static constexpr FCutWindow Charged[]={{13.f/30.f,18.f/30.f},
        {20.f/30.f,25.f/30.f},{27.f/30.f,32.f/30.f}};
    switch(Variant) {
    case EVariant::DiagonalRight:
    case EVariant::Horizontal:return MakeArrayView(Right);
    case EVariant::DiagonalLeft:return MakeArrayView(Left);
    case EVariant::Overhead:return MakeArrayView(Overhead);
    case EVariant::Charged:return MakeArrayView(Charged);
    default:return {};
    }
}
}
