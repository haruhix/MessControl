#pragma once
#include "CoreMinimal.h"
#include "MCHandlingState.generated.h"

/** Accepted load for one movement sample. No visual transforms or solver velocities. */
USTRUCT()
struct MESSCONTROL_API FMCHandlingMoveSample
{
    GENERATED_BODY()
    UPROPERTY() FGuid LoadEpoch;
    UPROPERTY() uint32 Revision=0;
    UPROPERTY() float LoadMass=0;
    UPROPERTY() float StackMass=0;
    UPROPERTY() float IncomingMass=0;
    UPROPERTY() float DragSpeedLimit=0;
    UPROPERTY() bool bHeavyGrip=false;
    UPROPERTY() bool bPlayerPull=false;
    bool IsWellFormed() const;
    bool Equals(const FMCHandlingMoveSample& Other) const;
    bool Serialize(FArchive& Ar);
};
