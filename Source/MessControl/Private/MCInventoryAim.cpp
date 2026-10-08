#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "MCMouthSurface.h"
#include "MCFirePatch.h"
#include "EngineUtils.h"
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
        auto CareArea=[&](FVector Point,float Radius) {
            const FVector Delta=Point-Origin;const float Along=FVector::DotProduct(Delta,Direction);
            if(Along>0 && Along<Best && (Delta-Direction*Along).SizeSquared()<=FMath::Square(Radius)) {
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
    if(Distance<=0 || Distance>SprayReach() || (Delta-Direction*Distance).SizeSquared()>FMath::Square(Radius)) return false;
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCSprayTreatment),true,Hero);Query.AddIgnoredActor(Target);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Origin,Point,ECC_Visibility,Query);
}
