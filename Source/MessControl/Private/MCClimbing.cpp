#include "MCToothMovementComponent.h"
#include "MCToothCharacter.h"
#include "MCFoodActor.h"
#include "MCArenaTooth.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"

bool UMCToothMovementComponent::FindClimbWall(FHitResult& Hit) const
{
    if(!CharacterOwner || !UpdatedComponent) return false;
    const FVector Start=UpdatedComponent->GetComponentLocation()+FVector(0,0,15);
    const FVector Facing=IsClimbing()?-FVector(ClimbNormal):CharacterOwner->GetActorForwardVector();
    // Gameplay teeth use a simple box; a complex-only trace misses that body.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCClimbWall),false,CharacterOwner);
    const float Reach=CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius()+75;
    for(const float Angle:{0.f,-25.f,25.f,-60.f,60.f,-90.f,90.f})
    {
        const FVector Direction=Facing.RotateAngleAxis(Angle,FVector::UpVector);
        FHitResult Wall;
        if(!GetWorld()->LineTraceSingleByChannel(Wall,Start,Start+Direction*Reach,ECC_Visibility,Q)) continue;
        const auto* A=Wall.GetActor(); const auto* C=Wall.GetComponent();
        if(!A || !C || A->ActorHasTag(TEXT("MCNoClimb")) || Cast<AMCFoodActor>(A) || Cast<AMCToothCharacter>(A)
            || (!Cast<AMCArenaTooth>(A) && C->GetCollisionObjectType()!=ECC_WorldStatic && !A->ActorHasTag(TEXT("MCClimbable")))
            || FMath::Abs(Wall.ImpactNormal.Z)>.6f) continue;
        Hit=Wall; return true;
    }
    return false;
}
bool UMCToothMovementComponent::CanAttemptJump() const
{
    return IsClimbing() || Super::CanAttemptJump();
}
bool UMCToothMovementComponent::DoJump(bool bReplayingMoves,float DeltaTime)
{
    if(IsClimbing()) { JumpFromWall(); return true; }
    return Super::DoJump(bReplayingMoves,DeltaTime);
}
void UMCToothMovementComponent::JumpFromWall()
{
    if(!IsClimbing()) return;
    const FVector Away=FVector(ClimbNormal)*260+FVector(0,0,420);
    ClimbCooldown=.5f; bWantsToClimb=false; SetMovementMode(MOVE_Falling); Velocity=Away;
}
bool UMCToothMovementComponent::TryMantle()
{
    const FVector P=UpdatedComponent->GetComponentLocation(),Forward=-FVector(ClimbNormal);
    const float Half=CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),R=CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCClimbTop),false,CharacterOwner); FHitResult Top;
    bool Found=false;
    for(float Depth:{18.f,35.f,55.f}) {
        const FVector Probe=P+Forward*(R+Depth)+FVector(0,0,Half+45);
        if(GetWorld()->LineTraceSingleByChannel(Top,Probe,Probe-FVector(0,0,Half+65),ECC_Visibility,Q) && Top.ImpactNormal.Z>=GetWalkableFloorZ()) { Found=true; break; }
    }
    if(!Found) return false;
    const FVector Goal=Top.ImpactPoint+FVector(0,0,Half+3);
    if(GetWorld()->OverlapBlockingTestByChannel(Goal,FQuat::Identity,UpdatedComponent->GetCollisionObjectType(),FCollisionShape::MakeCapsule(R,Half),Q)) return false;
    FHitResult Sweep;
    // Go over the lip with full capsule sweeps; no teleport through a ceiling.
    SafeMoveUpdatedComponent(FVector(0,0,FMath::Max(0.,Goal.Z-P.Z)),UpdatedComponent->GetComponentQuat(),true,Sweep);
    if(Sweep.bBlockingHit) return false;
    SafeMoveUpdatedComponent(Goal-UpdatedComponent->GetComponentLocation(),UpdatedComponent->GetComponentQuat(),true,Sweep);
    if(Sweep.bBlockingHit) return false;
    SetMovementMode(MOVE_Walking); Velocity=FVector::ZeroVector; ClimbCooldown=.35f; return true;
}
void UMCToothMovementComponent::PhysCustom(float Dt,int32 Iterations)
{
    if(!IsClimbing()) { Super::PhysCustom(Dt,Iterations); return; }
    if(Dt<MIN_TICK_TIME || !HasValidData()) return;
    auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    if(!Hero || !Hero->CanWork() || !bWantsToClimb) { SetMovementMode(MOVE_Falling); StartNewPhysics(Dt,Iterations); return; }
    FHitResult Wall;
    if(!FindClimbWall(Wall)) {
        if(Acceleration.Z>0 && TryMantle()) return;
        SetMovementMode(MOVE_Falling); StartNewPhysics(Dt,Iterations); return;
    }
    ClimbNormal=Wall.ImpactNormal;
    const FVector N=ClimbNormal;
    const FVector Input=FVector::VectorPlaneProject(Acceleration,N).GetClampedToMaxSize(GetMaxAcceleration())/FMath::Max(1.f,GetMaxAcceleration());
    Velocity=Input*FMath::Clamp(ClimbSpeed,60.f,350.f);
    const float R=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float Gap=FVector::DotProduct(UpdatedComponent->GetComponentLocation()-Wall.ImpactPoint,N);
    const FVector Stick=N*FMath::Clamp(float(R+5-Gap),-10.f,10.f);
    FHitResult MoveHit;
    SafeMoveUpdatedComponent(Velocity*Dt+Stick,FRotator(0,(-N).Rotation().Yaw,0).Quaternion(),true,MoveHit);
    if(MoveHit.IsValidBlockingHit()) { HandleImpact(MoveHit,Dt,Velocity*Dt); SlideAlongSurface(Velocity*Dt,1-MoveHit.Time,MoveHit.Normal,MoveHit,true); }
    if(Input.Z<-.1f) {
        FHitResult Floor; const FVector P=UpdatedComponent->GetComponentLocation();
        FCollisionQueryParams Q(SCENE_QUERY_STAT(MCClimbGround),false,Hero);
        if(GetWorld()->LineTraceSingleByChannel(Floor,P,P-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+8),ECC_Visibility,Q) && Floor.ImpactNormal.Z>GetWalkableFloorZ())
        { SetMovementMode(MOVE_Walking); ClimbCooldown=.35f; }
    }
}
