#pragma once

#include "CoreMinimal.h"

class AActor;
class AMCToothCharacter;
class UPrimitiveComponent;

namespace MCKatanaEnemyContact
{
/** Combat collision used by the katana trace. Cosmetic meshes and accessory shapes are excluded. */
UPrimitiveComponent* BodyFor(const AActor& Target);

/** Apply an accepted authoritative blade contact through the target's ordinary damage path. */
bool Apply(AMCToothCharacter& Hero,AActor& Target,const FVector& ImpactPoint,float Damage);
}
