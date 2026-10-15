#pragma once

#include "CoreMinimal.h"

class AMCToothCharacter;

struct FMCMeleeComboPose
{
    bool bActive=false;
    FVector HandOffset=FVector::ZeroVector;
    FRotator Wrist=FRotator(-12,0,0);
    FQuat ToolRotation=FQuat::Identity;
    float ToolRotationAlpha=0;
    float Lean=0,Twist=0,Side=0,Support=0;
};

/** Authority queues actual press edges; the existing reliable swing event carries the pose step. */
namespace MCMeleeCombo
{
    bool Supports(const AMCToothCharacter* Hero);
    void Reset(const AMCToothCharacter* Hero);
    void QueuePress(AMCToothCharacter* Hero);
    bool ConsumeBufferedPress(AMCToothCharacter* Hero);
    uint8 BeginSwing(AMCToothCharacter* Hero,bool bCombo);
    uint8 BeginFoodChop(AMCToothCharacter* Hero);
    bool IsFoodChop(const AMCToothCharacter* Hero);
    void PresentSwing(const AMCToothCharacter* Hero,uint8 ToolSlot,uint8 Step);
    float Duration(const AMCToothCharacter* Hero);
    float ContactTime(const AMCToothCharacter* Hero);
    FMCMeleeComboPose Sample(const AMCToothCharacter* Hero,float Elapsed);
}
