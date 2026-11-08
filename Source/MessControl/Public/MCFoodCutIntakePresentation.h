#pragma once

#include "CoreMinimal.h"

class AMCFoodActor;

namespace MCFoodCutIntakePresentation
{
    /** Authority-only accepted cosmetic boundary. No food credit, loads or particle replication. */
    MESSCONTROL_API void Play(AMCFoodActor* Food,const FTransform& Source,FVector Target,FVector Direction,float Radius);
}
