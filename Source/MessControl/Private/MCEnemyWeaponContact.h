#pragma once

#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "Engine/World.h"

namespace MCEnemyWeaponContact
{
// Shared by target selection and the target's final damage guard. Cleaning's
// shorter care reach must not silently shorten a visible weapon strike.
inline bool CanHit(const AMCToothCharacter* Hero,const AActor* Enemy,const FVector& Point,float FacingThreshold=.25f)
{
    if(!IsValid(Hero) || !IsValid(Enemy) || Hero->GetWorld()!=Enemy->GetWorld()
        || !Hero->CanWork() || !Hero->Inventory || Point.ContainsNaN()) return false;
    const FVector Origin=Hero->GetActorLocation(),Forward=Hero->GetActorForwardVector();
    const FVector Offset=Point-Origin;
    if(Offset.SizeSquared()>FMath::Square(Hero->Inventory->MeleeReach()) || FMath::Abs(Offset.Z)>120.f) return false;
    // A closest point inside a hit volume is the worker's own position. Facing
    // still comes from the enemy, so overlap does not allow hits behind us.
    const FVector Aim=(Enemy->GetActorLocation()-Origin).GetSafeNormal2D(UE_SMALL_NUMBER,Forward);
    if(FVector::DotProduct(Aim,Forward)<FacingThreshold) return false;
    FHitResult Block;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCEnemyWeaponHit),false,Hero);
    Query.AddIgnoredActor(Enemy);
    return !Hero->GetWorld()->LineTraceSingleByChannel(Block,Origin,Point,ECC_Visibility,Query);
}
}
