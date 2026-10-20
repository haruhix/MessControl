#pragma once
#include "CoreMinimal.h"
class AMCToothCharacter;

/** AI-only geometry checks. Never place a pawn or alter gameplay collision. */
namespace MCPlaytestPhysicalApproach
{
    constexpr float MaxDistance=200.f;
    bool FindStandingPose(const AMCToothCharacter* Hero,FVector Candidate,FVector& PawnCenter);
    bool CanWalkSegment(const AMCToothCharacter* Hero,FVector From,FVector To);
    bool SubmitWalkInput(AMCToothCharacter* Hero,FVector To);
}
