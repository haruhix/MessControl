#include "MCActiveSkillImpact.h"
#include "MCActiveSkillAction.h"
#include "MCArenaTooth.h"
#include "MCBossCharacter.h"
#include "MCFoodActor.h"
#include "MCNutBoss.h"
#include "MCNutEnemy.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

namespace
{
    struct FBlastTarget
    {
        TWeakObjectPtr<AActor> Actor;
        FVector Point=FVector::ZeroVector;
        bool bVisible=false;
    };

    bool ContactPoint(AActor* Actor,FVector Origin,bool bArena,FVector& Point)
    {
        if(auto* Food=Cast<AMCFoodActor>(Actor)) {
            if(Food->IsDisposed() || Food->IsInActiveContainer() || Food->bBrushTool || !Food->Body) return false;
            Point=Food->Body->Bounds.GetBox().GetClosestPointTo(Origin);return true;
        }
        if(auto* Enemy=Cast<AMCNutEnemy>(Actor)) {
            if(!Enemy->CanReceiveToolHit()) return false;
            Point=Enemy->GetToolTargetPoint(Origin);return true;
        }
        if(auto* Boss=Cast<AMCBossCharacter>(Actor)) {
            if(!Boss->CanReceiveWeaponHit()) return false;
            Point=Boss->GetMeleeTargetPoint(Origin);return true;
        }
        if(auto* Hero=Cast<AMCToothCharacter>(Actor)) {
            if(!Hero->Status || !Hero->Status->IsAlive()) return false;
            if(Hero->ToothPhysics && Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll && Hero->GetMesh())
                Point=Hero->GetMesh()->Bounds.GetBox().GetClosestPointTo(Origin);
            else Point=Hero->GetCapsuleComponent()->Bounds.GetBox().GetClosestPointTo(Origin);
            return true;
        }
        if(auto* Tooth=Cast<AMCArenaTooth>(Actor)) {
            if(!bArena || !Tooth->IsAvailable() || !Tooth->Body) return false;
            Point=Tooth->Body->Bounds.GetBox().GetClosestPointTo(Origin);return true;
        }
        return false;
    }

    float Safe(float Value,float Fallback,float Min,float Max)
    {return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Fallback;}
}

FMCActiveSkillBlastResult MCActiveSkillImpact::ApplyRadial(AActor* Causer,AMCPlayerState* SourcePS,
    FVector Origin,const FMCActiveSkillBlastSettings& In)
{
    FMCActiveSkillBlastResult Result;
    if(!IsValid(Causer) || !Causer->HasAuthority() || !Causer->GetWorld() || Origin.ContainsNaN()
        || (IsValid(SourcePS) && SourcePS->GetWorld()!=Causer->GetWorld())) return Result;
    UWorld* World=Causer->GetWorld();
    const float Radius=Safe(In.Radius,300.f,20.f,2000.f);
    const float FoodDamage=Safe(In.FoodDamage,100.f,0.f,10000.f);
    const float EnemyDamage=Safe(In.EnemyDamage,60.f,0.f,10000.f);
    const float PlayerDamage=Safe(In.PlayerDamage,25.f,0.f,1000.f);
    const float Push=Safe(In.PushSpeed,500.f,0.f,3000.f);
    const float ArenaDamage=In.bDamageArenaTeeth?Safe(In.ArenaDamage,0.f,0.f,10000.f):0.f;
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCActiveBlastSnapshot),false,Causer);
    World->OverlapMultiByObjectType(Overlaps,Origin,FQuat::Identity,FCollisionObjectQueryParams::AllObjects,
        FCollisionShape::MakeSphere(Radius),Query);
    TSet<TWeakObjectPtr<AActor>> Seen;
    TArray<FBlastTarget> Targets;
    // Freeze cover for ALL original candidates before HitFood can replace a
    // whole ingredient with fragments or a tooth/enemy removes its collision.
    for(const FOverlapResult& Overlap:Overlaps) {
        AActor* Actor=Overlap.GetActor();
        if(!IsValid(Actor) || Actor==Causer || Actor->IsActorBeingDestroyed() || Seen.Contains(Actor)) continue;
        Seen.Add(Actor);
        FVector Point;
        if(!ContactPoint(Actor,Origin,In.bDamageArenaTeeth,Point) || Point.ContainsNaN()
            || FVector::DistSquared(Point,Origin)>FMath::Square(Radius)) continue;
        FCollisionQueryParams Cover(SCENE_QUERY_STAT(MCActiveBlastCover),false,Causer);Cover.AddIgnoredActor(Actor);
        FHitResult Block;
        const bool bVisible=FVector::DistSquared(Point,Origin)<1.f
            || !World->LineTraceSingleByChannel(Block,Origin,Point,ECC_Visibility,Cover);
        Targets.Add({Actor,Point,bVisible});
    }
    Result.Candidates=Targets.Num();
    for(const FBlastTarget& Target:Targets) {
        AActor* Actor=Target.Actor.Get();
        if(!IsValid(Actor) || Actor->IsActorBeingDestroyed()) continue;
        if(!Target.bVisible) {++Result.Occluded;continue;}
        const FVector Away=(Actor->GetActorLocation()-Origin).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
        if(auto* Food=Cast<AMCFoodActor>(Actor)) {
            if(FoodDamage>0.f && Food->ReceiveActiveSkillImpact(FoodDamage,Away,SourcePS)) ++Result.FoodHits;
        } else if(auto* Enemy=Cast<AMCNutEnemy>(Actor)) {
            const auto* Knight=Cast<AMCNutBoss>(Enemy);const float Before=Knight?Knight->ShieldHealth:0.f;
            if(EnemyDamage>0.f) {
                const float Applied=Enemy->ReceiveExternalImpact(EnemyDamage,Causer,SourcePS,Origin);
                if(Applied>0.f || (IsValid(Knight) && Knight->ShieldHealth<Before)) ++Result.EnemyHits;
            }
        } else if(auto* Boss=Cast<AMCBossCharacter>(Actor)) {
            if(EnemyDamage>0.f && Boss->ReceiveExternalImpact(EnemyDamage,Causer,SourcePS,Origin)>0.f) ++Result.EnemyHits;
        } else if(auto* Hero=Cast<AMCToothCharacter>(Actor)) {
            // Causer is the projectile action, never the Hero and never System.
            // Owner and teammates are equally subject to native incoming guards.
            if(PlayerDamage>0.f && Hero->Status && Hero->Status->Damage(PlayerDamage,Away,Causer,EMCRogueDamageKind::Ordinary)) {
                ++Result.PlayerHits;
                if(Hero->ToothPhysics && Push>0.f && Hero->Status->IsAlive())
                    Hero->ToothPhysics->ApplyHit(Away*Push+FVector(0,0,Push*.2f),Target.Point);
            }
        } else if(auto* Tooth=Cast<AMCArenaTooth>(Actor)) {
            if(ArenaDamage>0.f && Tooth->ReceiveArenaHit(ArenaDamage,Away)) ++Result.ArenaToothHits;
        }
    }
    return Result;
}

bool MCActiveSkillImpact::ResolveLaunch(const AMCActiveSkillAction* Action,FVector Offset,float Radius,float Range,
    FVector& Origin,FVector& Direction,FHitResult* BlockingHit)
{
    Origin=FVector::ZeroVector;Direction=FVector::ForwardVector;
    if(!IsValid(Action) || !Action->GetWorld() || !Action->Hero() || Offset.ContainsNaN()) return false;
    Origin=Action->GetActorLocation();Direction=Action->GetActorForwardVector();
    FVector View,Look;
    if(!Action->GetAim(View,Look)) return false;
    Radius=Safe(Radius,10.f,2.f,50.f);Range=Safe(Range,6000.f,100.f,20000.f);
    const auto* Hero=Action->Hero();
    Origin=Hero->GetActorTransform().TransformPosition(Offset);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCActiveMuzzle),false,Action);Query.AddIgnoredActor(Hero);
    FHitResult Wall;
    const FVector Start=Hero->GetActorLocation()+FVector(0,0,Offset.Z);
    if(Action->GetWorld()->SweepSingleByChannel(Wall,Start,Origin,FQuat::Identity,ECC_Visibility,
        FCollisionShape::MakeSphere(Radius),Query)) {
        if(BlockingHit) *BlockingHit=Wall;
        Origin=Wall.Location+Wall.ImpactNormal*2.f;Direction=Look;
        return false;
    }
    FHitResult Aim;
    const FVector Target=Action->GetWorld()->LineTraceSingleByChannel(Aim,View,View+Look*Range,ECC_Visibility,Query)
        ?Aim.ImpactPoint:View+Look*Range;
    Direction=(Target-Origin).GetSafeNormal(SMALL_NUMBER,Look);
    return !Origin.ContainsNaN() && !Direction.ContainsNaN() && !Direction.IsNearlyZero();
}
