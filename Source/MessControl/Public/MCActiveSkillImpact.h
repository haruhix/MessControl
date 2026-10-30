#pragma once

#include "CoreMinimal.h"

class AActor;
class AMCPlayerState;
class AMCActiveSkillAction;
struct FHitResult;

/** Prototype balance comes from the action's immutable Parameters snapshot. */
struct MESSCONTROL_API FMCActiveSkillBlastSettings
{
    float Radius=300.f;
    float FoodDamage=100.f;
    float EnemyDamage=60.f;
    float PlayerDamage=25.f;
    float PushSpeed=500.f;
    float ArenaDamage=0.f;
    bool bDamageArenaTeeth=false;
};

struct MESSCONTROL_API FMCActiveSkillBlastResult
{
    int32 Candidates=0;
    int32 Occluded=0;
    int32 FoodHits=0;
    int32 EnemyHits=0;
    int32 PlayerHits=0;
    int32 ArenaToothHits=0;
};

namespace MCActiveSkillImpact
{
    /** One call per sealed detonation. Snapshot targets AND cover before any destructive mutation. */
    MESSCONTROL_API FMCActiveSkillBlastResult ApplyRadial(AActor* Causer,AMCPlayerState* SourcePS,
        FVector Origin,const FMCActiveSkillBlastSettings& Settings);
    /** Camera aim, but the projectile starts at a swept pawn-side muzzle, never beyond a nearby wall. */
    MESSCONTROL_API bool ResolveLaunch(const AMCActiveSkillAction* Action,FVector PawnOffset,float Radius,float Range,
        FVector& Origin,FVector& Direction,FHitResult* BlockingHit=nullptr);
}
