#pragma once
#include "CoreMinimal.h"
#include "GameFramework/RootMotionSource.h"
#include "MCHookPullPrediction.h"

enum class EMCAcceptedMotionPolicy : uint8 { ConstantVelocity,SteeredVelocity,BoundedGoal };

/** One authority-owned external override. Existing additive/dash sources remain independent. */
struct MESSCONTROL_API FMCAcceptedMotionSnapshot
{
    FGuid OperationID,Epoch;
    uint32 Revision=0;
    bool bActive=false,bFreeFlight=false;
    EMCAcceptedMotionPolicy Policy=EMCAcceptedMotionPolicy::ConstantVelocity;
    uint8 PreviousMode=0,PreviousCustomMode=0;
    FVector Velocity=FVector::ZeroVector,Goal=FVector::ZeroVector;
    float Speed=850,StopDistance=120,TurnRate=0,SteeringLockedUntil=0,Duration=-1,StartMoveTime=0,SourceTime=0;
    uint16 ServerSourceID=0;
    bool IsWellFormed() const;
    bool Serialize(FArchive& Ar);
    FMCHookPullReceipt AsHookReceipt() const;
};

namespace MCAcceptedExternalMotion
{
    MESSCONTROL_API FName SourceName(const FMCAcceptedMotionSnapshot& Snapshot);
    MESSCONTROL_API TSharedPtr<FRootMotionSource> MakeSource(const FMCAcceptedMotionSnapshot& Snapshot);
    /** Only the registered external family/name; never dash, wind or unrelated sources. */
    MESSCONTROL_API bool MatchesSource(const TSharedPtr<FRootMotionSource>& Source,const FMCAcceptedMotionSnapshot& Snapshot);
    MESSCONTROL_API bool IsExternalFamily(const TSharedPtr<FRootMotionSource>& Source);
    MESSCONTROL_API bool IsRunning(const TSharedPtr<FRootMotionSource>& Source);
    MESSCONTROL_API void ReplaceHistory(FRootMotionSourceGroup& Group,const FMCAcceptedMotionSnapshot& Snapshot,uint16 LocalID,bool bAdd);
    /** Remove send-time external state before inserting the certified ACK snapshot. */
    MESSCONTROL_API void RemoveUnstampedSources(FRootMotionSourceGroup& Group);
}
