#pragma once

#include "CoreMinimal.h"

class AMCToothCharacter;
class AActor;
class UMCToothMovementComponent;
struct FHitResult;

/** Owns one named CMC root-motion source. Never clears another source or edits walking tuning. */
struct MESSCONTROL_API FMCActiveSkillMotionLease
{
    bool Begin(AMCToothCharacter* Pawn,const FGuid& ActionID,FVector Velocity,bool bFreeFlight=false,float Duration=-1.f);
    bool Update(FVector Velocity);
    bool IsOwned() const;
    void End();
private:
    TWeakObjectPtr<AMCToothCharacter> Character;
    TWeakObjectPtr<UMCToothMovementComponent> Movement;
    FName SourceName;
    uint16 SourceID=0;
    uint8 PreviousMode=0,PreviousCustomMode=0;
    bool bOwnsFlying=false;
};

namespace MCActiveSkillMotion
{
    /** Ragdoll, traversal, carried food and existing motion leases are not implicit handoffs. */
    MESSCONTROL_API bool IsEligible(const AMCToothCharacter* Pawn,bool bRequireGround);
    MESSCONTROL_API bool CanAcquire(const AMCToothCharacter* Pawn);
    MESSCONTROL_API FVector FootPoint(const AMCToothCharacter* Pawn);
    MESSCONTROL_API bool HasGround(const AMCToothCharacter* Pawn,FVector& Point,FVector& Normal);
    MESSCONTROL_API FTransform RidePose(const AMCToothCharacter* Pawn,FVector Forward,float Height=0.f);
    MESSCONTROL_API bool ClearRideFootprint(const AMCToothCharacter* Pawn,const AActor* Action,
        FVector Forward,FVector HalfSize,float LookAheadSeconds,float Speed);
    MESSCONTROL_API bool ClearPullStep(const AMCToothCharacter* Pawn,const AActor* Action,
        const AActor* Goal,FVector Velocity,float Seconds,FHitResult* BlockingHit=nullptr);
    MESSCONTROL_API bool ContinuousPath(FVector From,FVector To,float Seconds,float MaxSpeed);
    /** Native mask mutations/receipts only; no repair, global clear or synthetic completion. */
    MESSCONTROL_API bool CleanSweep(AMCToothCharacter* Worker,const AActor* Action,FVector From,
        FVector To,float Seconds,float Radius,float Rate,float MaxSpeed);
    MESSCONTROL_API FVector Steering(const AMCToothCharacter* Pawn,FVector Previous,float DegreesPerSecond,float Seconds);
}
