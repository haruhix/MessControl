#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "MCMouthSurface.h"
#include "MCFirePatch.h"
#include "MCArenaTooth.h"
#include "MCToothStatusComponent.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"

bool UMCInventoryComponent::LocalSprayView(FVector& Origin,FVector& Direction) const
{
    const auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
    if(!PC || !PC->IsLocalController() || !PC->GetLocalPlayer() || PC->GetViewTarget()!=Hero) return false;
    FRotator Rotation;PC->GetPlayerViewPoint(Origin,Rotation);Direction=Rotation.Vector();
    return !Origin.ContainsNaN() && !Direction.ContainsNaN();
}
void UMCInventoryComponent::StoreSprayView(FVector Origin,FVector Direction)
{
    if(!Hero || Selected!=EMCToolSlot::Spray || Origin.ContainsNaN() || Direction.ContainsNaN()
        || FVector::DistSquared(Origin,Hero->GetActorLocation())>FMath::Square(3000.f) || Direction.IsNearlyZero()) return;
    SprayViewOrigin=Origin;SprayViewDirection=Direction.GetSafeNormal();bHasSprayView=true;
    Hero->ForceNetUpdate();
}
void UMCInventoryComponent::ServerUpdateSprayView_Implementation(FVector_NetQuantize Origin,FVector_NetQuantizeNormal Direction)
{StoreSprayView(Origin,Direction);}
void UMCInventoryComponent::ServerCommitSprayView_Implementation(FVector_NetQuantize Origin,FVector_NetQuantizeNormal Direction)
{StoreSprayView(Origin,Direction);}
void UMCInventoryComponent::UpdateSprayAim(bool Commit)
{
    FVector Origin,Direction;
    if(Selected!=EMCToolSlot::Spray || !LocalSprayView(Origin,Direction) || (!Commit && Now()<NextSprayViewAt)) return;
    NextSprayViewAt=Now()+.05;
    if(!Commit && bHasSprayView && FVector::DistSquared(Origin,SprayViewOrigin)<.25f
        && FVector::DotProduct(Direction,SprayViewDirection)>.99999f) return;
    // Reliable snapshots precede press/release on this actor's channel; a lost
    // periodic update must not leave the server firing along an old camera angle.
    if(Commit) ServerCommitSprayView(Origin,Direction);else ServerUpdateSprayView(Origin,Direction);
    if(!Hero->HasAuthority()) {SprayViewOrigin=Origin;SprayViewDirection=Direction;bHasSprayView=true;}
}
FVector UMCInventoryComponent::SprayAim() const
{
    if(!Hero) return FVector::ZeroVector;
    FVector Origin,Direction;
    if(!LocalSprayView(Origin,Direction)) {
        Origin=bHasSprayView?FVector(SprayViewOrigin):Hero->GetActorLocation();
        Direction=bHasSprayView?FVector(SprayViewDirection):Hero->GetActorForwardVector();
    }
    // First acquire the camera-centre target. The separate muzzle sweep keeps
    // nearby walls solid even when the camera can see around them.
    FVector End=Origin+Direction*10000;
    FCollisionObjectQueryParams Objects;
    for(auto Type:{ECC_WorldStatic,ECC_WorldDynamic,ECC_PhysicsBody,ECC_Pawn}) Objects.AddObjectTypesToQuery(Type);
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCSprayView),true,Hero);
    if(GetWorld()->LineTraceSingleByObjectType(Hit,Origin,End,Objects,Query)) End=Hit.ImpactPoint;
    if(!(HasUpgrade(EMCToolUpgrade::Watergun) && bPressureMode)) {
        // Care patches deliberately have no physical collision. Acquire their
        // visible area on the same camera ray before converging the muzzle.
        float Best=FVector::Distance(Origin,End);
        const FVector Muzzle=SprayOrigin();
        auto CareArea=[&](FVector Point,float Radius) {
            const FVector Delta=Point-Origin;const float Along=FVector::DotProduct(Delta,Direction);
            // This is view acquisition, not a treatment range test: the
            // camera may be well behind the player. Actual reach is checked
            // from the muzzle by SprayTargetDistance/FindToothRepairTarget.
            const float MuzzleDistance=FVector::DotProduct(Point-Muzzle,Direction);
            const float Coverage=FMath::Max(0.f,Radius)+SprayRadiusAtDistance(MuzzleDistance);
            if(Along>0 && Along<Best && (Delta-Direction*Along).SizeSquared()<=FMath::Square(Coverage)) {
                Best=Along;End=Origin+Direction*Along;
            }
        };
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It)
            if(It->bUlcer && !It->bTreatmentBlocked && !It->IsHealed() && !It->IsBurning() && !It->IsActorBeingDestroyed())
                CareArea(It->GetActorLocation()+FVector(0,0,10),62);
        for(TActorIterator<AMCFirePatch> It(GetWorld());It;++It)
            if(It->IsBurning()) CareArea(It->GetActorLocation()+FVector(0,0,20),It->BurnRadius);
    }
    return End;
}
FVector UMCInventoryComponent::WatergunAimPoint() const {return SprayAim();}
bool UMCInventoryComponent::SprayTargetDistance(AActor* Target,FVector Point,float Radius,float& Distance) const
{
    const FVector Origin=SprayOrigin(),Direction=SprayDirection(),Delta=Point-Origin;
    Distance=FVector::DotProduct(Delta,Direction);
    if(!FMath::IsFinite(Radius) || Point.ContainsNaN() || Distance<=0 || Distance>SprayReach()) return false;
    // Radius describes the target, not the spray. Only the beam's cone radius
    // receives SprayWidth, so a broad target never grows with the passive.
    const float Coverage=FMath::Max(0.f,Radius)+SprayRadiusAtDistance(Distance);
    if((Delta-Direction*Distance).SizeSquared()>FMath::Square(Coverage)) return false;
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCSprayTreatment),true,Hero);Query.AddIgnoredActor(Target);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Origin,Point,ECC_Visibility,Query);
}
AMCArenaTooth* UMCInventoryComponent::FindToothRepairTarget(FHitResult* Contact) const
{
    if (!Hero || !Hero->IsPrimaryHeld() || !ShouldPresentTool() || !Hero->CanWork() || Hero->bInCoffee
        || Selected!=EMCToolSlot::Spray || (bPressureMode && HasUpgrade(EMCToolUpgrade::Watergun))) return nullptr;
    const FVector Origin=SprayOrigin(),Direction=SprayDirection();
    const float Reach=SprayReach();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCToothSprayRepair),true,Hero);
    FHitResult CenterHit;
    const bool CenterBlocked=GetWorld()->LineTraceSingleByChannel(CenterHit,Origin,Origin+Direction*Reach,ECC_Visibility,Query);
    // Preserve first-contact selection. A healthy front tooth or a wall also
    // caps side candidates, so a wider cone cannot repair something behind it.
    const float Limit=CenterBlocked?FMath::Clamp(float(FVector::DotProduct(CenterHit.ImpactPoint-Origin,Direction)),0.f,Reach):Reach;
    if(auto* Tooth=Cast<AMCArenaTooth>(CenterHit.GetActor());CenterBlocked && Tooth && Tooth->IsAvailable() && Tooth->Status->NeedsCare(false)) {
        if(Contact) *Contact=CenterHit;
        return Tooth;
    }
    AMCArenaTooth* Best=nullptr;FHitResult BestHit;float BestDistance=Limit;
    int32 Checked=0;
    for(TActorIterator<AMCArenaTooth> It(GetWorld());It && Checked<64;++It) {
        auto* Tooth=*It;
        if(!Tooth->IsAvailable() || !Tooth->Status->NeedsCare(false) || !Tooth->Body) continue;
        ++Checked;
        const float Along=FVector::DotProduct(Tooth->Body->Bounds.Origin-Origin,Direction);
        const FVector AxisPoint=Origin+Direction*FMath::Clamp(Along,0.f,Limit);
        FVector Point=AxisPoint;
        if(Tooth->Body->GetClosestPointOnCollision(AxisPoint,Point)<0.f || Point.ContainsNaN()) continue;
        const FVector Delta=Point-Origin;const float Distance=FVector::DotProduct(Delta,Direction);
        if(Distance<=0 || Distance>BestDistance || (Delta-Direction*Distance).SizeSquared()>FMath::Square(SprayRadiusAtDistance(Distance))) continue;
        FHitResult Hit;
        // Trace to the real collision point, slightly inside its boundary to
        // avoid an exact-endpoint miss. Never ignore walls or other teeth.
        const FVector End=Point+(Point-Origin).GetSafeNormal()*2.f;
        if(!GetWorld()->LineTraceSingleByChannel(Hit,Origin,End,ECC_Visibility,Query) || Hit.GetActor()!=Tooth) continue;
        const float HitDistance=FVector::DotProduct(Hit.ImpactPoint-Origin,Direction);
        if(HitDistance<=0 || HitDistance>BestDistance
            || (Hit.ImpactPoint-Origin-Direction*HitDistance).SizeSquared()>FMath::Square(SprayRadiusAtDistance(HitDistance))) continue;
        Best=Tooth;BestHit=Hit;BestDistance=HitDistance;
    }
    if(Best && Contact) *Contact=BestHit;
    return Best;
}
