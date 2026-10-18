#pragma once

#include "CoreMinimal.h"

class AMCTongue;
struct FHitResult;

/** Shared floor-dirt envelope. Tooth coating and ulcers retain their own rules. */
namespace MCDirtPlacement
{
    /** Existing gameplay X bands, central 72% of each tongue half-width, inset by Margin. */
    MESSCONTROL_API FBox WorkBounds(const AMCTongue* Tongue,float Margin=0.f);
    MESSCONTROL_API bool IsInWorkArea(const AMCTongue* Tongue,FVector Point,float Margin=0.f);
    /** Whole footprint plus 65 cm brush approach must have real tissue and free static space. */
    MESSCONTROL_API bool FindSurface(const AMCTongue* Tongue,FVector Point,float Radius,FHitResult& Hit);
}
