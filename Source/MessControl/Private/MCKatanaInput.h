#pragma once

#include "CoreMinimal.h"
#include "MCKatanaSwing.h"

class AMCToothCharacter;

namespace MCKatanaInput
{
constexpr int32 MaxPending=2;
void Press(const AMCToothCharacter* Hero,double Now);
double PressedAt(const AMCToothCharacter* Hero);
void Release(const AMCToothCharacter* Hero);
bool Queue(const AMCToothCharacter* Hero,bool bCharged);
bool Consume(const AMCToothCharacter* Hero,bool& bCharged);
bool QueueAutoCharge(const AMCToothCharacter* Hero,double Now);
bool IsPreparingCharge(const AMCToothCharacter* Hero);
MCKatanaSwing::EVariant ChooseNormal(const AMCToothCharacter* Hero);
void ArmFacing(const AMCToothCharacter* Hero,const FVector& Target);
bool WantsFacing(const AMCToothCharacter* Hero,FVector& Direction);
void Reset(const AMCToothCharacter* Hero);
}
