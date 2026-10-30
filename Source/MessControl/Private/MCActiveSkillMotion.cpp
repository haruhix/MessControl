#include "MCActiveSkillMotion.h"

#include "MCArenaTooth.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCHubBasketball.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCResultPassives.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCActiveSkillMotion,Log,All);

namespace
{
const TCHAR* MotionPrefix=TEXT("MCActiveSkillMotion_");
bool MotionReviewDiagnostics()
{
    static const bool Enabled=FParse::Param(FCommandLine::Get(),TEXT("MCActiveSkillReview"));
    return Enabled;
}
bool Running(const TSharedPtr<FRootMotionSource>& Source)
{
    return Source.IsValid() && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)
        && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished);
}
FCollisionQueryParams Query(const AMCToothCharacter* Pawn,const AActor* Action)
{
    FCollisionQueryParams Result(SCENE_QUERY_STAT(MCActiveSkillMotion),false,Pawn);
    if(Action) Result.AddIgnoredActor(Action);
    return Result;
}
}

bool MCActiveSkillMotion::CanAcquire(const AMCToothCharacter* Pawn)
{
    const auto* Move=IsValid(Pawn)?Cast<UMCToothMovementComponent>(Pawn->GetCharacterMovement()):nullptr;
    if(!Move) return false;
    for(const auto* Sources:{&Move->CurrentRootMotion.RootMotionSources,&Move->CurrentRootMotion.PendingAddRootMotionSources})
        for(const auto& Source:*Sources)
            if(Running(Source) && Source->InstanceName.ToString().StartsWith(MotionPrefix)) return false;
    return true;
}

bool MCActiveSkillMotion::IsEligible(const AMCToothCharacter* Pawn,bool Ground)
{
    if(!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->CanWork() || !Pawn->GetController()
        || !Pawn->ToothPhysics || !Pawn->ToothPhysics->CanAct() || Pawn->HasFrozenLegs() || Pawn->IsFreezingToDeath()
        || Pawn->IsSeatedOnRaft() || Pawn->IsRowing() || Pawn->bInCoffee || Pawn->ClingTooth || Pawn->OrderJumpTarget
        || Pawn->HeldFood || (Pawn->FoodCollection && Pawn->FoodCollection->bCollecting) || AMCHubBasketball::HeldBy(Pawn)
        || (Pawn->Grip && Pawn->Grip->IsWorldAnchored())) return false;
    const auto* Move=Cast<UMCToothMovementComponent>(Pawn->GetCharacterMovement());
    return Move && !Move->IsDashing() && !Move->IsClimbing() && !Move->IsSwimming()
        && Move->MovementMode!=MOVE_None && (!Ground || Move->IsMovingOnGround());
}

FVector MCActiveSkillMotion::FootPoint(const AMCToothCharacter* Pawn)
{
    return IsValid(Pawn)?Pawn->GetActorLocation()-FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()):FVector::ZeroVector;
}

bool MCActiveSkillMotion::HasGround(const AMCToothCharacter* Pawn,FVector& Point,FVector& Normal)
{
    if(!IsValid(Pawn)) {
        if(MotionReviewDiagnostics()) UE_LOG(LogMCActiveSkillMotion,Log,TEXT("MC_ACTIVE_RIDE_GROUND_REJECT gate=pawn_invalid"));
        return false;
    }
    const auto* Move=Pawn->GetCharacterMovement();
    if(!Move || !Move->IsMovingOnGround() || !Move->CurrentFloor.IsWalkableFloor()) {
        if(MotionReviewDiagnostics()) UE_LOG(LogMCActiveSkillMotion,Log,
            TEXT("MC_ACTIVE_RIDE_GROUND_REJECT gate=movement_or_floor pawn=%s mode=%d moving_ground=%d walkable=%d floor_actor=%s floor_component=%s pawn_location=%s"),
            *Pawn->GetPathName(),Move?int32(Move->MovementMode):-1,Move&&Move->IsMovingOnGround(),Move&&Move->CurrentFloor.IsWalkableFloor(),
            *GetPathNameSafe(Move?Move->CurrentFloor.HitResult.GetActor():nullptr),
            *GetPathNameSafe(Move?Move->CurrentFloor.HitResult.GetComponent():nullptr),*Pawn->GetActorLocation().ToString());
        return false;
    }
    Point=Move->CurrentFloor.HitResult.ImpactPoint;Normal=Move->CurrentFloor.HitResult.ImpactNormal;
    const bool Valid=!Point.ContainsNaN() && Normal.Z>=Move->GetWalkableFloorZ();
    if(!Valid && MotionReviewDiagnostics()) UE_LOG(LogMCActiveSkillMotion,Log,
        TEXT("MC_ACTIVE_RIDE_GROUND_REJECT gate=point_or_normal pawn=%s point=%s normal=%s required_z=%.5f floor_actor=%s floor_component=%s"),
        *Pawn->GetPathName(),*Point.ToString(),*Normal.ToString(),Move->GetWalkableFloorZ(),
        *GetPathNameSafe(Move->CurrentFloor.HitResult.GetActor()),*GetPathNameSafe(Move->CurrentFloor.HitResult.GetComponent()));
    return Valid;
}

FTransform MCActiveSkillMotion::RidePose(const AMCToothCharacter* Pawn,FVector Forward,float Height)
{
    FVector Point,Normal;
    if(!HasGround(Pawn,Point,Normal)) return FTransform(Forward.Rotation(),FootPoint(Pawn)+FVector(0,0,Height));
    return FTransform(FRotationMatrix::MakeFromZX(Normal,Forward).ToQuat(),Point+Normal*Height);
}

bool FMCActiveSkillMotionLease::Begin(AMCToothCharacter* Pawn,const FGuid& ID,FVector Velocity,bool FreeFlight,float Duration)
{
    if(!IsValid(Pawn) || !Pawn->HasAuthority() || !ID.IsValid() || Velocity.ContainsNaN()
        || !MCActiveSkillMotion::CanAcquire(Pawn) || IsOwned()) return false;
    auto* Move=Cast<UMCToothMovementComponent>(Pawn->GetCharacterMovement());
    // Do not nest a new temporary flight inside a prior pull's terminal frame.
    if(FreeFlight && Move->MovementMode==MOVE_Flying) return false;
    Character=Pawn;Movement=Move;PreviousMode=uint8(Move->MovementMode);PreviousCustomMode=Move->CustomMovementMode;
    SourceName=FName(*(FString(MotionPrefix)+ID.ToString(EGuidFormats::Digits)));
    auto Source=MakeShared<FMCLocomotionRootMotionSource>();
    Source->InstanceName=SourceName;Source->Priority=450;Source->AccumulateMode=ERootMotionAccumulateMode::Override;
    Source->Duration=FMath::IsFinite(Duration) && Duration>0?Duration:-1.f;Source->Force=Velocity.GetClampedToMaxSize(2000.f);
    if(!FreeFlight) Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
    SourceID=Move->ApplyRootMotionSource(Source);
    if(!IsOwned()) {Character.Reset();Movement.Reset();return false;}
    if(FreeFlight) {Move->SetMovementMode(MOVE_Flying);bOwnsFlying=true;}
    return true;
}

bool FMCActiveSkillMotionLease::IsOwned() const
{
    auto* Move=Movement.Get();
    const auto Source=Move?Move->GetRootMotionSourceByID(SourceID):TSharedPtr<FRootMotionSource>();
    return Running(Source) && Source->InstanceName==SourceName;
}

bool FMCActiveSkillMotionLease::Update(FVector Velocity)
{
    if(Velocity.ContainsNaN() || !IsOwned()) return false;
    auto* Move=Movement.Get();
    static_cast<FMCLocomotionRootMotionSource*>(Move->GetRootMotionSourceByID(SourceID).Get())->Force=Velocity.GetClampedToMaxSize(2000.f);
    return true;
}

void FMCActiveSkillMotionLease::End()
{
    auto* Move=Movement.Get();auto* Pawn=Character.Get();
    const auto Source=Move?Move->GetRootMotionSourceByID(SourceID):TSharedPtr<FRootMotionSource>();
    if(Source.IsValid() && Source->InstanceName==SourceName) Move->RemoveRootMotionSourceByID(SourceID);
    // Death, swimming, leg ice, a launch or another system may have taken the
    // movement mode since this pull began. Only release our still-owned Flying.
    const bool CanRestore=IsValid(Pawn) && Pawn->Status && Pawn->Status->IsAlive() && Pawn->ToothPhysics && Pawn->ToothPhysics->CanAct()
        && !Pawn->HasFrozenLegs() && !Pawn->SwallowedBy && !Pawn->IsMimicCaptured() && !Pawn->IsSeatedOnRaft();
    if(bOwnsFlying && Move && Move->MovementMode==MOVE_Flying && CanRestore && MCActiveSkillMotion::CanAcquire(Pawn)) {
        if(PreviousMode==MOVE_Walking || PreviousMode==MOVE_NavWalking) {
            FFindFloorResult Floor;Move->FindFloor(Pawn->GetActorLocation(),Floor,false);
            Move->SetMovementMode(Floor.IsWalkableFloor() && Floor.FloorDist<=Move->MaxStepHeight
                ?EMovementMode(PreviousMode):MOVE_Falling);
        } else Move->SetMovementMode(EMovementMode(PreviousMode),PreviousCustomMode);
    }
    Character.Reset();Movement.Reset();SourceName=NAME_None;SourceID=0;bOwnsFlying=false;
}

bool MCActiveSkillMotion::ClearRideFootprint(const AMCToothCharacter* Pawn,const AActor* Action,
    FVector Forward,FVector Half,float Ahead,float Speed)
{
    FVector Point,Normal;if(!HasGround(Pawn,Point,Normal) || Half.ContainsNaN()) {
        if(MotionReviewDiagnostics()) UE_LOG(LogMCActiveSkillMotion,Log,TEXT("MC_ACTIVE_RIDE_FOOTPRINT_REJECT gate=ground_or_halfsize pawn=%s action=%s half=%s"),
            *GetPathNameSafe(Pawn),*GetPathNameSafe(Action),*Half.ToString());
        return false;
    }
    auto Params=Query(Pawn,Action);
    for(TActorIterator<AMCToothCharacter> It(Pawn->GetWorld());It;++It) Params.AddIgnoredActor(*It);
    Half=Half.ComponentMax(FVector(5));
    const FVector Center=Point+Normal*(Half.Z+6.f);
    const FQuat Heading=FRotationMatrix::MakeFromZX(Normal,Forward).ToQuat();
    const FVector Along=FVector::VectorPlaneProject(Forward,Normal).GetSafeNormal();
    const auto* Move=Pawn->GetCharacterMovement();
    const auto& Ground=Move->CurrentFloor.HitResult;
    auto* Support=Ground.GetComponent();
    const FVector Advance=Along*Speed*FMath::Clamp(Ahead,0.f,.1f);
    // A tangent box can enter a nearby raised triangle of the SAME curved
    // support, even while CMC's narrower capsule has a valid walkable floor.
    // Lift only the query, within the native step allowance (and a 45cm cap).
    // Never ignore the support: each complete resweep still sees its walls.
    const float MaxSupportLift=FMath::IsFinite(Move->MaxStepHeight)?FMath::Clamp(Move->MaxStepHeight,0.f,45.f):0.f;
    float Lift=0;
    constexpr int32 MaxSupportResweeps=4;
    for(int32 Attempt=0;Attempt<=MaxSupportResweeps;++Attempt) {
        const FVector Start=Center+Normal*Lift,End=Start+Advance;
        FHitResult Hit;
        if(!Pawn->GetWorld()->SweepSingleByChannel(Hit,Start,End,Heading,ECC_Pawn,FCollisionShape::MakeBox(Half),Params)) {
            if(Lift>0 && MotionReviewDiagnostics()) UE_LOG(LogMCActiveSkillMotion,Verbose,
                TEXT("MC_ACTIVE_RIDE_SUPPORT_CLEAR pawn=%s lift=%.5f resweeps=%d support=%s"),
                *Pawn->GetPathName(),Lift,Attempt,*GetPathNameSafe(Support));
            return true;
        }
        auto Reject=[&](const TCHAR* Gate) {
            if(MotionReviewDiagnostics()) UE_LOG(LogMCActiveSkillMotion,Log,
                TEXT("MC_ACTIVE_RIDE_FOOTPRINT_REJECT gate=%s pawn=%s action=%s blocker_actor=%s blocker_component=%s start_penetrating=%d penetration=%.5f time=%.5f distance=%.5f impact_point=%s impact_normal=%s normal=%s hit_location=%s start=%s end=%s half=%s expected_ground_actor=%s expected_ground_component=%s ground_point=%s ground_normal=%s support_lift=%.5f attempt=%d"),
                Gate,*Pawn->GetPathName(),*GetPathNameSafe(Action),*GetPathNameSafe(Hit.GetActor()),*GetPathNameSafe(Hit.GetComponent()),
                Hit.bStartPenetrating,Hit.PenetrationDepth,Hit.Time,Hit.Distance,*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString(),
                *Hit.Normal.ToString(),*Hit.Location.ToString(),*Start.ToString(),*End.ToString(),*Half.ToString(),
                *GetPathNameSafe(Ground.GetActor()),*GetPathNameSafe(Support),*Point.ToString(),*Normal.ToString(),Lift,Attempt);
            return false;
        };
        if(!Support || Hit.GetComponent()!=Support) return Reject(TEXT("foreign_obstacle"));
        const float WalkableZ=Support->GetWalkableSlopeOverride().ModifyWalkableFloorZ(Move->GetWalkableFloorZ());
        if(!FMath::IsFinite(Hit.PenetrationDepth) || Hit.PenetrationDepth<0) return Reject(TEXT("invalid_support_depth"));
        FHitResult Plane=Hit;
        if(Hit.bStartPenetrating) {
            // Chaos exposes an initial overlap's minimum translation direction
            // as BOTH normals. At a triangle edge that direction can be nearly
            // tangent to a walkable floor. Recover a real face only beside the
            // reported contact; a distant floor below a wall is not permission.
            if(Hit.ImpactPoint.ContainsNaN() || Hit.ImpactNormal.ContainsNaN()) return Reject(TEXT("invalid_support_contact"));
            constexpr float ContactProbeRadius=6.f;
            FCollisionQueryParams PlaneParams(SCENE_QUERY_STAT(MCActiveRideSupportPlane),true);
            PlaneParams.bReturnFaceIndex=true;
            auto ProbePlane=[&](FVector Axis,const TCHAR* Kind) {
                Axis=Axis.GetSafeNormal();
                const FVector RayStart=Hit.ImpactPoint+Axis*ContactProbeRadius,RayEnd=Hit.ImpactPoint-Axis*ContactProbeRadius;
                FHitResult Probe;
                const bool Found=!Axis.IsNearlyZero() && Support->LineTraceComponent(Probe,RayStart,RayEnd,PlaneParams);
                if(MotionReviewDiagnostics()) UE_LOG(LogMCActiveSkillMotion,Log,
                    TEXT("MC_ACTIVE_RIDE_SUPPORT_PLANE kind=%s found=%d pawn=%s support=%s contact=%s mtd=%s query_start=%s query_end=%s ray_start=%s ray_end=%s plane_component=%s plane_point=%s plane_normal=%s face=%d initial_overlap=%d blocking=%d lift=%.5f attempt=%d"),
                    Kind,Found,*Pawn->GetPathName(),*GetPathNameSafe(Support),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString(),
                    *Start.ToString(),*End.ToString(),*RayStart.ToString(),*RayEnd.ToString(),*GetPathNameSafe(Probe.GetComponent()),
                    *Probe.ImpactPoint.ToString(),*Probe.ImpactNormal.ToString(),Probe.FaceIndex,Probe.bStartPenetrating,Probe.bBlockingHit,Lift,Attempt);
                if(Found) Plane=Probe;
                return Found;
            };
            // A sideways ray finds a genuine wall when the MTD is horizontal.
            // If it misses a tangent floor, try the independent native up axis.
            if(!ProbePlane(Hit.ImpactNormal,TEXT("mtd")) && !ProbePlane(Normal,TEXT("ground")))
                return Reject(TEXT("near_contact_plane_absent"));
            if(Plane.GetComponent()!=Support || Plane.bStartPenetrating
                || Plane.ImpactPoint.ContainsNaN() || Plane.ImpactNormal.ContainsNaN()
                || FVector::DistSquared(Plane.ImpactPoint,Hit.ImpactPoint)>FMath::Square(ContactProbeRadius+.1f))
                return Reject(TEXT("near_contact_plane_invalid"));
            if(Plane.ImpactNormal.GetSafeNormal().Z<WalkableZ) return Reject(TEXT("near_contact_plane_steep"));
        } else if(Hit.ImpactNormal.ContainsNaN() || Hit.Normal.ContainsNaN()
            || Hit.ImpactNormal.Z<WalkableZ || Hit.Normal.Z<WalkableZ) return Reject(TEXT("unwalkable_support_contact"));
        const FVector ContactNormal=Plane.ImpactNormal.GetSafeNormal();
        const float UpDot=float(FVector::DotProduct(Normal,ContactNormal));
        if(UpDot<=UE_KINDA_SMALL_NUMBER) return Reject(TEXT("support_separation_direction"));
        // Also cover the remainder of the short forward sweep on this plane;
        // otherwise raising clear at the start can re-hit an uphill triangle.
        // Never combine the old MTD depth with a different geometric normal:
        // project the entire oriented box onto the independently found plane.
        const double PlaneRadius=FMath::Abs(FVector::DotProduct(ContactNormal,Heading.GetAxisX()))*Half.X
            +FMath::Abs(FVector::DotProduct(ContactNormal,Heading.GetAxisY()))*Half.Y
            +FMath::Abs(FVector::DotProduct(ContactNormal,Heading.GetAxisZ()))*Half.Z;
        const float StartDepth=Hit.bStartPenetrating
            ?float(FMath::Max(0.,PlaneRadius-FVector::DotProduct(Start-Plane.ImpactPoint,ContactNormal)))
            :Hit.PenetrationDepth;
        const float EndDepth=StartDepth+float(FMath::Max(0.,-FVector::DotProduct(End-Hit.Location,ContactNormal)));
        const float Needed=(EndDepth+1.f)/UpDot;
        const float Remaining=MaxSupportLift-Lift;
        if(Hit.bStartPenetrating && MotionReviewDiagnostics()) UE_LOG(LogMCActiveSkillMotion,Log,
            TEXT("MC_ACTIVE_RIDE_SUPPORT_RECOVERY pawn=%s support=%s contact=%s mtd=%s geometric_point=%s geometric_normal=%s face=%d projected_radius=%.5f start_depth=%.5f end_depth=%.5f required_lift=%.5f remaining_lift=%.5f lift=%.5f attempt=%d"),
            *Pawn->GetPathName(),*GetPathNameSafe(Support),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString(),
            *Plane.ImpactPoint.ToString(),*ContactNormal.ToString(),Plane.FaceIndex,PlaneRadius,StartDepth,EndDepth,Needed,Remaining,Lift,Attempt);
        if(!FMath::IsFinite(Needed) || EndDepth>MaxSupportLift || Needed>Remaining
            || Attempt==MaxSupportResweeps) return Reject(TEXT("support_lift_limit"));
        // Successive query volumes overlap vertically; do not jump a thin
        // ceiling while resolving a deeper but still bounded floor contact.
        const float Step=FMath::Min(Needed,FMath::Max(1.f,float(Half.Z)));
        if(Step<=0) return Reject(TEXT("support_lift_limit"));
        Lift+=Step;
    }
    return false;
}

bool MCActiveSkillMotion::ClearPullStep(const AMCToothCharacter* Pawn,const AActor* Action,const AActor* Goal,FVector Velocity,float Seconds,FHitResult* BlockingHit)
{
    if(!IsValid(Pawn) || Velocity.ContainsNaN()) return false;
    auto Params=Query(Pawn,Action);if(Goal) Params.AddIgnoredActor(Goal);
    const auto* Capsule=Pawn->GetCapsuleComponent();const auto* Move=Pawn->GetCharacterMovement();FHitResult Hit;
    const auto Shape=FCollisionShape::MakeCapsule(FMath::Max(1.f,Capsule->GetScaledCapsuleRadius()-2.f),FMath::Max(2.f,Capsule->GetScaledCapsuleHalfHeight()-2.f));
    const FVector Advance=Velocity*FMath::Clamp(Seconds,0.f,.05f);float Lift=0;
    const float MaxLift=FMath::IsFinite(Move->MaxStepHeight)?FMath::Clamp(Move->MaxStepHeight,0.f,45.f):0.f;
    for(int32 Attempt=0;Attempt<6;++Attempt) {
        const FVector Start=Pawn->GetActorLocation()+FVector(0,0,Lift);
        if(!Pawn->GetWorld()->SweepSingleByChannel(Hit,Start,Start+Advance,FQuat::Identity,ECC_Pawn,Shape,Params)) return true;
        if(BlockingHit) *BlockingHit=Hit;
        // Only a native grounded pull may defer a shallow walkable support
        // contact to CMC's swept MoveAlongFloor/StepUp. Every complete lifted
        // QUERY still sees the same tongue, including its walls and ceilings.
        // The pawn itself is never lifted/teleported or put through a floor.
        auto* Support=Move->CurrentFloor.HitResult.GetComponent();
        if(!Move->IsMovingOnGround() || !Move->CurrentFloor.IsWalkableFloor() || !Support
            || Hit.GetComponent()!=Support || Hit.ImpactNormal.ContainsNaN() || Hit.ImpactPoint.ContainsNaN()) return false;
        const float WalkableZ=Support->GetWalkableSlopeOverride().ModifyWalkableFloorZ(Move->GetWalkableFloorZ());
        // Accept only a genuinely upward walkable contact. Do not reinterpret
        // down-facing triangle normals: malformed/back-facing collision must
        // remain blocking rather than being treated as floor support.
        const FHitResult& Plane=Hit;
        const FVector Normal=Hit.ImpactNormal.GetSafeNormal();
        if(Normal.Z<WalkableZ) return false;
        // Capsule support radius on this plane includes both end spheres and
        // its vertical segment. Cover START and END, not just depth at the hit:
        // a nonpenetrating seam has depth=0 but can lead to an uphill corridor.
        const double Radius=Shape.GetCapsuleRadius()+(Shape.GetCapsuleHalfHeight()-Shape.GetCapsuleRadius())*FMath::Abs(Normal.Z);
        const double Depth=FMath::Max(0.,FMath::Max(Radius-FVector::DotProduct(Start-Plane.ImpactPoint,Normal),
            Radius-FVector::DotProduct(Start+Advance-Plane.ImpactPoint,Normal)));
        const float Needed=float((Depth+1.)/Normal.Z);
        const float Step=FMath::Min(Needed,8.f);
        if(!FMath::IsFinite(Step) || Step<=0 || Lift+Needed>MaxLift) return false;
        Lift+=Step;
    }
    return false;
}

bool MCActiveSkillMotion::ContinuousPath(FVector From,FVector To,float Seconds,float Speed)
{
    return !From.ContainsNaN() && !To.ContainsNaN() && FMath::IsFinite(Seconds) && Seconds>0 && Seconds<=.25f
        && FMath::IsFinite(Speed) && Speed>0 && FVector::Dist(From,To)<=FMath::Max(45.f,Speed*Seconds*1.75f);
}

bool MCActiveSkillMotion::CleanSweep(AMCToothCharacter* Worker,const AActor* Action,FVector From,FVector To,
    float Seconds,float Radius,float Rate,float Speed)
{
    if(!IsValid(Worker) || !Worker->HasAuthority() || !ContinuousPath(From,To,Seconds,Speed)
        || !FMath::IsFinite(Radius) || Radius<=0 || !FMath::IsFinite(Rate) || Rate<=0) return false;
    Radius=FMath::Clamp(Radius,5.f,150.f);Rate=FMath::Clamp(Rate,.01f,20.f);
    TArray<TWeakObjectPtr<AMCMouthSurface>> Surfaces;
    for(TActorIterator<AMCMouthSurface> It(Worker->GetWorld());It;++It) if(!It->bUlcer && !It->IsClean()) Surfaces.Add(*It);
    const int32 Count=FMath::Clamp(FMath::CeilToInt(FMath::Max(FVector::Dist(From,To)/(Radius*.5f),double(Seconds/.05f))),1,64);
    const float Dose=Seconds/Count;bool Changed=false;
    for(int32 I=0;I<Count;++I) {
        const FVector Sample=FMath::Lerp(From,To,(I+.5)/Count);
        auto Params=Query(Worker,Action);FHitResult Floor;
        if(!Worker->GetWorld()->LineTraceSingleByChannel(Floor,Sample+FVector(0,0,35),Sample-FVector(0,0,65),ECC_Visibility,Params)
            || Floor.ImpactNormal.Z<Worker->GetCharacterMovement()->GetWalkableFloorZ()) continue;
        const FVector Point=Floor.ImpactPoint;
        // Trim the circular footprint near vertical obstacles instead of
        // cleaning a mask through a wall merely because its center is near.
        Params.AddIgnoredActor(Floor.GetActor());float ClearRadius=Radius;
        for(int32 Ray=0;Ray<8;++Ray) {
            const float Angle=Ray*UE_TWO_PI/8;FHitResult Wall;
            const FVector Start=Point+FVector(0,0,8);
            if(Worker->GetWorld()->LineTraceSingleByChannel(Wall,Start,Start+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius,ECC_Visibility,Params))
                ClearRadius=FMath::Min(ClearRadius,FMath::Max(0.f,Wall.Distance-3.f));
        }
        if(ClearRadius<2.f) continue;
        for(const auto& Weak:Surfaces) if(auto* Surface=Weak.Get()) {
            const FVector Local=Surface->GetActorTransform().InverseTransformPosition(Point);
            const bool OnPaintTongue=Surface->bPaintTile && Floor.GetActor()==Surface->GetTongue();
            if((!OnPaintTongue && FMath::Abs(Local.Z)>18.f) || FMath::Abs(Local.X)>Surface->LiquidHalfSize+ClearRadius || FMath::Abs(Local.Y)>Surface->LiquidHalfSize+ClearRadius) continue;
            Changed|=Surface->CleanLiquidAt(Worker,Point,ClearRadius,Dose,Rate,EMCStainCleanReason::ActiveSkill);
        }
        if(auto* Tooth=Cast<AMCArenaTooth>(Floor.GetActor()))
            Changed|=Tooth->CleanGrimeAt(Worker,Point,ClearRadius,Dose,Rate,EMCStainCleanReason::ActiveSkill);
    }
    return Changed;
}

FVector MCActiveSkillMotion::Steering(const AMCToothCharacter* Pawn,FVector Previous,float Rate,float Seconds)
{
    if(!IsValid(Pawn) || Previous.ContainsNaN() || !FMath::IsFinite(Rate) || !FMath::IsFinite(Seconds)) return FVector::ForwardVector;
    const FVector Input=Pawn->GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D();
    if(Input.IsNearlyZero()) return Previous.GetSafeNormal2D();
    return FRotator(0,FMath::FixedTurn(Previous.Rotation().Yaw,Input.Rotation().Yaw,Rate*FMath::Clamp(Seconds,0.f,.1f)),0).Vector();
}
