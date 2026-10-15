#pragma once

#include "CoreMinimal.h"

class AMCToothCharacter;
class AActor;

/** Short cosmetic contact holds. Never changes actor movement, damage clocks or world time. */
namespace MCWeaponImpact
{
    void BeginSwing(const AMCToothCharacter* Hero);
    void ConfirmSwing(AMCToothCharacter* Hero,uint8 ToolSlot);
    void HoldTarget(AActor* Target,const AMCToothCharacter* Source);
    bool IsPoseHeld(const AActor* Actor);
    float VisualSwingElapsed(const AMCToothCharacter* Hero);
    float Recoil(const AMCToothCharacter* Hero);
}
