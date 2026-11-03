#pragma once
#include "CoreMinimal.h"
#include "MCFreezeDeathSnapshot.h"

/** Exact original presentation RNG producer; invoke ONCE after accepted pose bounds.
 * Both native fallback and prepared NS consume this retained immutable episode. */
class FMCFreezeNativeMotion final
{
public:
    bool IssueOnce(const FBox& ActualBounds,FString& Error);
    TConstArrayView<MCFreezeDeathSnapshot::FNativeShard> GetShards() const
    {return bIssued?TConstArrayView<MCFreezeDeathSnapshot::FNativeShard>(Shards,36):TConstArrayView<MCFreezeDeathSnapshot::FNativeShard>();}
    float GetHalfSize() const {check(bIssued);return HalfSize;}
    FVector GetCenter() const {check(bIssued);return CubeCenter;}
private:
    MCFreezeDeathSnapshot::FNativeShard Shards[36];
    FVector CubeCenter=FVector::ZeroVector;
    float HalfSize=0;
    bool bIssued=false;
};
