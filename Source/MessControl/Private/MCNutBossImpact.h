#pragma once

#include "CoreMinimal.h"

class AMCFoodActor;
class UMCVFXPresentationComponent;

namespace MCNutBossImpact
{
void Fracture(AMCFoodActor& Nut,UMCVFXPresentationComponent* Presentation,FName Event,
    FVector Point,FVector Normal,float WholeScatterSpeed=0.f);
}
