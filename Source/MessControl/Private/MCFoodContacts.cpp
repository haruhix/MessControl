#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodCollectionComponent.h"
#include "Components/SkeletalMeshComponent.h"

bool AMCFoodActor::ResolveCanonicalPawnContact(AMCToothCharacter* Hero,FVector FoodContactNormal,FVector ContactPoint)
{
    if(!HasAuthority() || !IsValid(Hero) || !Hero->Status || !Hero->Status->IsAlive() || IsDisposed()
        || bFragment || bBrushTool || StackCarrier || !Holders.IsEmpty()
        || (Phase!=EMCFoodPhase::Falling && Phase!=EMCFoodPhase::Free)
        || FoodContactNormal.ContainsNaN() || ContactPoint.ContainsNaN()) return false;
    const double Now=GetWorld()->GetTimeSeconds();
    if((bEntryImpactManaged && (bMouthEntry || Now<EntryImpactSafeUntil)) || Now<StackReleaseSafeUntil
        || Hero->FoodCollection->IsSettlingRelease(this)) return false;
    if(const double* Previous=LastHit.Find(Hero);Previous && Now-*Previous<Settings.HitCooldown) return false;
    const FVector Approach=-FoodContactNormal.GetSafeNormal();
    const float Incoming=FVector::DotProduct(PrePhysicsVelocity,Approach);
    const float Relative=FVector::DotProduct(PrePhysicsVelocity-Hero->GetSimulationFrame().CanonicalVelocity,Approach);
    if(!FMath::IsFinite(Incoming) || !FMath::IsFinite(Relative) || Incoming<Settings.ImpactSpeed || Relative<Settings.ImpactSpeed) return false;
    LastHit.Add(Hero,Now);++ConfirmedImpacts;
    FVector Direction=(Hero->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();if(Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
    if(bMouthEntry && FoodData.Kind==EMCFoodKind::Food) {
        if(ActorHasTag(TEXT("MCNutRain"))) Hero->Status->Damage(FMath::Min(Settings.MaxDamage,Relative*Settings.DamagePerSpeed*Settings.Mass/9.f),Direction);
        if(!Hero->IsMimicCaptured() && Hero->ToothPhysics->GetBodyState()!=EMCBodyState::Recovering) {
            FVector IncomingDirection=PrePhysicsVelocity.GetSafeNormal2D();if(IncomingDirection.IsNearlyZero()) IncomingDirection=Direction;
            const float Push=MouthEntryPushSpeed*FMath::Clamp(Incoming/600.f,.5f,1.f);
            const FVector Velocity=(IncomingDirection*Push+FVector(0,0,Push*(25.f/140.f))).GetClampedToMaxSize(Hero->ToothPhysics->Settings.FallThreshold*.8f);
            Hero->NotifyCameraImpact(Relative,IncomingDirection);
            if(Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) Hero->GetMesh()->AddImpulseToAllBodiesBelow(Velocity,Hero->RigBone(TEXT("body")),true,true);
            else Hero->LaunchCharacter(Velocity,false,false);
            Hero->ForceNetUpdate();
        }
        return true;
    }
    if(!LastHandledBy) Hero->Status->Damage(FMath::Min(Settings.MaxDamage,Relative*Settings.DamagePerSpeed*Settings.Mass/9.f),Direction,this);
    Hero->DropFood();Hero->ToothPhysics->ApplyHit(Direction*Settings.Knockback+FVector(0,0,Settings.Knockback*.5f),ContactPoint);
    return true;
}
