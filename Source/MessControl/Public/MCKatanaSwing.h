#pragma once

#include "CoreMinimal.h"

namespace MCKatanaSwing
{
// The existing melee event reserves the high nibble for the accepted pose.
// Values 1–4 still belong to the knife and pickaxe combo.
enum class EVariant : uint8
{
    None=0,
    DiagonalRight=5,
    DiagonalLeft=6,
    Horizontal=7,
    Charged=8
};

constexpr float ChargeHoldTime=11.f/30.f;
constexpr float ChargeThreshold=.38f;

inline EVariant Decode(uint8 Value)
{
    return Value>=uint8(EVariant::DiagonalRight) && Value<=uint8(EVariant::Charged) ? EVariant(Value) : EVariant::None;
}

inline float ClipDuration(EVariant Variant)
{
    switch(Variant) {
    case EVariant::DiagonalRight:return 25.f/30.f;
    case EVariant::DiagonalLeft:return 26.f/30.f;
    case EVariant::Horizontal:return 24.f/30.f;
    case EVariant::Charged:return 37.f/30.f;
    default:return 0.f;
    }
}

struct FCutWindow
{
    float Start;
    float End;
};

inline TConstArrayView<FCutWindow> Cuts(EVariant Variant)
{
    // The markers are one-based in Blender and zero-based in Unreal.
    static constexpr FCutWindow Right[]={{8.f/30.f,10.f/30.f}};
    static constexpr FCutWindow Left[]={{9.f/30.f,11.f/30.f}};
    static constexpr FCutWindow Charged[]={{15.f/30.f,17.f/30.f},{19.f/30.f,21.f/30.f},{24.f/30.f,26.f/30.f}};
    switch(Variant) {
    case EVariant::DiagonalRight:return MakeArrayView(Right);
    case EVariant::DiagonalLeft:return MakeArrayView(Left);
    case EVariant::Horizontal:return MakeArrayView(Right);
    case EVariant::Charged:return MakeArrayView(Charged);
    default:return {};
    }
}

inline float StartOffset(EVariant Variant) { return Variant==EVariant::Charged?ChargeHoldTime:0.f; }
}
