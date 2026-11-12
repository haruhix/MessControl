#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionShape.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
constexpr float ObstacleHeight=45.f,ObstacleWidth=50.f,SettleSeconds=.35f;
constexpr float ArmSpeed=40.f,KeepSpeed=70.f,ArmAngularSpeed=15.f,KeepAngularSpeed=25.f;
constexpr float AnchorDistance=6.f,AnchorAngle=5.f;
constexpr float SeparationLimit=400.f,SeparationStep=16.f,SeparationMargin=5.f;

bool PoseStayedNear(const FTransform& Pose,const FTransform& Anchor)
{
    return !Pose.ContainsNaN() && !Anchor.ContainsNaN()
        && FVector::DistSquared(Pose.GetLocation(),Anchor.GetLocation())<=FMath::Square(AnchorDistance)
        && FMath::RadiansToDegrees(Pose.GetRotation().AngularDistance(Anchor.GetRotation()))<=AnchorAngle
        && Pose.GetScale3D().Equals(Anchor.GetScale3D(),.001f);
}

bool CapsuleTouchesBounds(const UCapsuleComponent* Capsule,const FBox& Bounds,float Margin)
{
    if(!IsValid(Capsule) || !Bounds.IsValid)return false;
    float Radius=0,HalfHeight=0;Capsule->GetScaledCapsuleSize(Radius,HalfHeight);
    const FVector Center=Capsule->GetComponentLocation();
    const FBox Expanded=Bounds.ExpandBy(FVector(Radius+Margin,Radius+Margin,HalfHeight+Margin));
    return !Center.ContainsNaN() && Expanded.IsInsideOrOn(Center);
}
}

FBox AMCFoodActor::GetWalkingCollisionBounds() const
{
    // A cut's actor origin and original source bounds may lie far outside its
    // visible remaining geometry. Presentation squash must not change this gate.
    if(bCutVisualReady && CutStackLocalBounds.IsValid)
        return CutStackLocalBounds.TransformBy(GetActorTransform());
    if(!CutPlanes.IsEmpty() || !ItemMesh)return FBox(ForceInit);
    return GetCanonicalModelBounds();
}

bool AMCFoodActor::IsSubstantialWalkingObstacle() const
{
    const FBox Bounds=GetWalkingCollisionBounds();
    if(!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN())return false;
    const FVector Size=Bounds.GetSize();
    // A broad upright slice still blocks walking when its thickness is small.
    return Size.Z>=ObstacleHeight && FMath::Max(Size.X,Size.Y)>=ObstacleWidth;
}

bool AMCFoodActor::CanUseWalkingCollision() const
{
    return HasActorBegunPlay() && !IsActorBeingDestroyed() && !IsDisposed()
        && FoodData.Kind==EMCFoodKind::Food && !bBrushTool && !bCutIntakeReceipt
        && !bCutTransactionStaged && !bCutTransactionPending && !StackCarrier && !EquippedBy
        && Holders.IsEmpty() && !IsInActiveContainer()
        && (Phase==EMCFoodPhase::Free || Phase==EMCFoodPhase::Stuck)
        && Body && ItemMesh && !GetActorTransform().ContainsNaN()
        && (CutPlanes.IsEmpty() || bCutVisualReady);
}

bool AMCFoodActor::HasWalkingObstacle() const
{
    return bWalkingObstacle && CanUseWalkingCollision() && WalkingBlocker
        && WalkingBlocker->IsRegistered()
        && WalkingBlocker->GetCollisionEnabled()==ECollisionEnabled::QueryOnly
        && WalkingBlocker->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block;
}

bool AMCFoodActor::IsWalkingBoundsClearOfPlayers(const FBox& Bounds) const
{
    if(!GetWorld() || !Bounds.IsValid || !Body || !Body->IsPhysicsStateCreated())return false;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        if(It->IsActorBeingDestroyed())continue;
        const auto* Capsule=It->GetCapsuleComponent();
        // Bounds are only broadphase. Corners of a rotated/cut slab can contain
        // a worker without its real prepared collision touching the capsule.
        // Query the physical source at its current pose, not the inactive proxy.
        if(Capsule && Capsule->GetCollisionEnabled()!=ECollisionEnabled::NoCollision
            && CapsuleTouchesBounds(Capsule,Bounds,5.f)
            && Body->OverlapComponent(Capsule->GetComponentLocation(),Capsule->GetComponentQuat(),
                Capsule->GetCollisionShape(5.f)))return false;
    }
    return true;
}

bool AMCFoodActor::SeparateWalkingFoodFromPlayers(const FBox& Bounds)
{
    // A cut may settle around a worker. Resolve that exceptional overlap by
    // moving the food only; enabling a blocker here would depenetrate the pawn.
    if(!HasAuthority() || !CanUseWalkingCollision() || Phase!=EMCFoodPhase::Free
        || !Bounds.IsValid || !Body->IsPhysicsStateCreated())return false;
    struct FWorker
    {
        UCapsuleComponent* Capsule=nullptr;
        bool bOverlapping=false;
        FMTDResult Penetration;
    };
    TArray<FWorker,TInlineAllocator<8>> Workers;
    FMTDResult Deepest;
    UCapsuleComponent* DeepestCapsule=nullptr;
    FComponentQueryParams WorldQuery(SCENE_QUERY_STAT(MCFoodWalkingSeparation),this);
    WorldQuery.bFindInitialOverlaps=true;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Capsule=It->GetCapsuleComponent();
        if(It->IsActorBeingDestroyed() || !Capsule
            || Capsule->GetCollisionEnabled()==ECollisionEnabled::NoCollision)continue;
        auto& Worker=Workers.AddDefaulted_GetRef();Worker.Capsule=Capsule;
        // Pawn responses intentionally ignore moving fragments. Test all workers
        // explicitly rather than letting that filter hide them in the world sweep.
        WorldQuery.AddIgnoredActor(*It);
        Worker.bOverlapping=Body->OverlapComponent(Capsule->GetComponentLocation(),
            Capsule->GetComponentQuat(),Capsule->GetCollisionShape(SeparationMargin));
        if(Worker.bOverlapping && Body->ComputePenetration(Worker.Penetration,
            Capsule->GetCollisionShape(SeparationMargin),Capsule->GetComponentLocation(),Capsule->GetComponentQuat())
            && FMath::IsFinite(Worker.Penetration.Distance) && !Worker.Penetration.Direction.ContainsNaN()
            && (!DeepestCapsule || Worker.Penetration.Distance>Deepest.Distance))
        {Deepest=Worker.Penetration;DeepestCapsule=Capsule;}
    }
    if(!DeepestCapsule)return false;
    const FVector Origin=Body->GetComponentLocation();
    struct FCandidate {FVector Direction;float Distance;};
    TArray<FCandidate,TInlineAllocator<6>> Candidates;
    auto AddCandidate=[&](FVector Direction,float Distance=0.f)
    {
        Direction=Direction.GetSafeNormal2D();
        if(Direction.IsNearlyZero() || Direction.ContainsNaN() || Candidates.Num()>=6)return;
        for(const auto& Candidate:Candidates)
            if(FVector::DotProduct(Direction,Candidate.Direction)>.99f)return;
        Candidates.Add({Direction,Distance});
    };
    // Retain the chosen escape goal between steps instead of changing sides
    // when the minimum penetration axis changes in a compound hull.
    if(bWalkingSeparationActive)
    {
        const FVector Remaining=WalkingSeparationGoal-Origin;
        if(Remaining.Size2D()<=SeparationLimit && FMath::Abs(Remaining.Z)<.1f)
            AddCandidate(Remaining,Remaining.Size2D());
    }
    const float HorizontalNormal=Deepest.Direction.Size2D();
    if(HorizontalNormal>.1f)
        AddCandidate(-Deepest.Direction,(Deepest.Distance+2.f)/HorizontalNormal);
    AddCandidate(Bounds.GetCenter()-DeepestCapsule->GetComponentLocation());
    AddCandidate(FVector::ForwardVector);AddCandidate(-FVector::ForwardVector);
    AddCandidate(FVector::RightVector);AddCandidate(-FVector::RightVector);
    auto ClearsWorkers=[&](const FVector& Delta)
    {
        for(const auto& Worker:Workers)
            if(Body->OverlapComponent(Worker.Capsule->GetComponentLocation()-Delta,
                Worker.Capsule->GetComponentQuat(),Worker.Capsule->GetCollisionShape(SeparationMargin)))return false;
        return true;
    };
    for(const auto& Candidate:Candidates)
    {
        float Distance=Candidate.Distance;
        // For fallback directions, translate the food's near bound beyond every
        // overlapping capsule. This is only a candidate; exact hull tests follow.
        const float NearBound=FVector::DotProduct(Bounds.GetCenter(),Candidate.Direction)
            -FVector::DotProduct(Bounds.GetExtent(),Candidate.Direction.GetAbs());
        float BoundsDistance=2.f;
        for(const auto& Worker:Workers)if(Worker.bOverlapping)
            BoundsDistance=FMath::Max(BoundsDistance,float(FVector::DotProduct(
                Worker.Capsule->GetComponentLocation(),Candidate.Direction))-NearBound
                +Worker.Capsule->GetScaledCapsuleRadius()+SeparationMargin+2.f);
        if(Distance<=0)Distance=BoundsDistance;
        if(!FMath::IsFinite(Distance) || Distance<=0 || Distance>SeparationLimit)continue;
        FVector Delta=Candidate.Direction*Distance;
        if(!ClearsWorkers(Delta))
        {
            Distance=FMath::Max(Distance,BoundsDistance);
            if(Distance>SeparationLimit)continue;
            Delta=Candidate.Direction*Distance;
            if(!ClearsWorkers(Delta))continue;
        }
        TArray<FHitResult> WorldHits;
        GetWorld()->ComponentSweepMulti(WorldHits,Body,Origin,Origin+Delta,Body->GetComponentQuat(),WorldQuery);
        bool Safe=true;
        for(const auto& Hit:WorldHits)
        {
            const auto* Other=Hit.GetComponent();
            const bool Blocks=Other
                && Body->GetCollisionResponseToChannel(Other->GetCollisionObjectType())==ECR_Block
                && Other->GetCollisionResponseToChannel(Body->GetCollisionObjectType())==ECR_Block;
            // Multi-sweeps can report initial penetrations as Touch to keep
            // searching for later blockers. Those solid contacts still matter.
            if(!Blocks || (!Hit.bBlockingHit && !Hit.bStartPenetrating && Hit.Time>KINDA_SMALL_NUMBER))continue;
            // Resting contact with the floor is allowed only while travelling
            // tangentially/outward. A wall further along the path rejects it.
            const bool Existing=Hit.bStartPenetrating || Hit.Time<=KINDA_SMALL_NUMBER;
            if(!Existing || Hit.Normal.ContainsNaN() || Hit.Normal.IsNearlyZero()
                || FVector::DotProduct(Candidate.Direction,Hit.Normal)<-.001f){Safe=false;break;}
        }
        if(!Safe)continue;
        const FVector Step=Candidate.Direction*FMath::Min(Distance,SeparationStep);
        for(const auto& Worker:Workers)
        {
            auto* Capsule=Worker.Capsule;
            if(!Worker.bOverlapping)
            {
                FHitResult Hit;
                // Translation duality tests the entire proposed food path
                // against this capsule without moving either component.
                if(Body->SweepComponent(Hit,Capsule->GetComponentLocation(),
                    Capsule->GetComponentLocation()-Delta,Capsule->GetComponentQuat(),
                    Capsule->GetCollisionShape(SeparationMargin))){Safe=false;break;}
            }
            else if(Body->OverlapComponent(Capsule->GetComponentLocation()-Step,Capsule->GetComponentQuat(),
                Capsule->GetCollisionShape(SeparationMargin)))
            {
                FMTDResult After;
                if(!Body->ComputePenetration(After,Capsule->GetCollisionShape(SeparationMargin),
                    Capsule->GetComponentLocation()-Step,Capsule->GetComponentQuat())
                    || !FMath::IsFinite(After.Distance) || Worker.Penetration.Distance<=0
                    || After.Distance>Worker.Penetration.Distance+.05f){Safe=false;break;}
                // Equal depth is valid for a deep overlap whose shortest MTD
                // is vertical: the full horizontal goal above is proven clear.
            }
        }
        if(!Safe)continue;
        const FVector Velocity=Body->GetPhysicsLinearVelocity();
        const FVector Angular=Body->GetPhysicsAngularVelocityInRadians();
        const bool Simulating=Body->IsSimulatingPhysics();
        if(!SetActorLocation(GetActorLocation()+Step,false,nullptr,ETeleportType::TeleportPhysics))continue;
        if(Simulating){Body->SetPhysicsLinearVelocity(Velocity);Body->SetPhysicsAngularVelocityInRadians(Angular);}
        WalkingSettleTransform=Body->GetComponentTransform();bWalkingSettleCandidate=true;
        WalkingSeparationGoal=Origin+Delta;
        if(!bWalkingSeparationActive)
            UE_LOG(LogTemp,Log,TEXT("Food walking clearance started: %s; goal %.1f cm; food moves only"),*GetName(),Distance);
        bWalkingSeparationActive=true;ForceNetUpdate();return true;
    }
    // No safe bounded route: retain pass-through until a worker exits instead
    // of pushing a worker, tunnelling through the world or forcing a blocker.
    return false;
}

void AMCFoodActor::ResetWalkingObstacle()
{
    const bool WasBlocking=bWalkingObstacle || (WalkingBlocker
        && WalkingBlocker->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block);
    if(HasAuthority())bWalkingObstacle=false;
    if(bWalkingSeparationActive)
    {
        UE_LOG(LogTemp,Log,TEXT("Food walking clearance ended: %s; interrupted by state/motion"),*GetName());
        bWalkingSeparationActive=false;
    }
    WalkingSettleSeconds=0;WalkingClearanceScanSeconds=0;bWalkingSettleCandidate=false;
    if(WalkingBlocker)
    {
        WalkingBlocker->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
        if(IsDisposed() || bCutIntakeReceipt)WalkingBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    if(Body)Body->SetCollisionResponseToChannel(ECC_Pawn,
        bWizardGravityDrop || bFragment || Phase==EMCFoodPhase::Absorbing?ECR_Ignore:ECR_Block);
    if(WasBlocking)
    {
        if(HasAuthority())ForceNetUpdate();
        AMCToothCharacter::NotifyFoodMovementContact(this);
    }
}

void AMCFoodActor::RefreshWalkingCollision()
{
    if(!WalkingBlocker || !Body)return;
    const bool WasBlocking=WalkingBlocker->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block;
    const bool CanBlock=CanUseWalkingCollision() && IsSubstantialWalkingObstacle();
    if(bWalkingObstacle && !CanBlock)ResetWalkingObstacle();

    auto* Source=Cast<UMCFoodBodyComponent>(Body.Get());
    // Warm only final source/cut shapes. Recovery/staging must not create a
    // constructor-box proxy while its real geometry is still being prepared.
    const bool GeometryReady=Source && ItemMesh && FoodData.Kind==EMCFoodKind::Food && !bBrushTool
        && IsSubstantialWalkingObstacle() && !IsDisposed() && !bCutIntakeReceipt
        && (Source->HasMeshCollision() || bCutVisualReady)
        && !(bStagedCutPortion && CutPlanes.IsEmpty() && !bCutVisualReady);
    const bool Shared=GeometryReady && WalkingBlocker->SharePreparedCollision(Source);
    if(!Shared && bWalkingObstacle)ResetWalkingObstacle();
    const bool Block=bWalkingObstacle && CanBlock && Shared && !WalkingObstacleTransform.ContainsNaN();
    WalkingBlocker->SetCollisionEnabled(Shared?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    WalkingBlocker->SetCollisionResponseToChannel(ECC_Pawn,Block?ECR_Block:ECR_Ignore);
    if(Block && !WalkingBlocker->GetComponentTransform().Equals(WalkingObstacleTransform,.001f))
        WalkingBlocker->SetWorldTransform(WalkingObstacleTransform,false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetCollisionResponseToChannel(ECC_Pawn,
        Block || bWizardGravityDrop || bFragment || Phase==EMCFoodPhase::Absorbing?ECR_Ignore:ECR_Block);
    if(WasBlocking!=Block)AMCToothCharacter::NotifyFoodMovementContact(this);
}

void AMCFoodActor::OnRep_WalkingCollision()
{
    // Coalesced updates may carry a new anchor without an observed false flag.
    // Release the old cached ledge before moving this absolute query shape.
    if(WalkingBlocker && WalkingBlocker->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block
        && !WalkingBlocker->GetComponentTransform().Equals(WalkingObstacleTransform,.001f))
    {
        WalkingBlocker->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
        AMCToothCharacter::NotifyFoodMovementContact(this);
    }
    RefreshWalkingCollision();
    AMCToothCharacter::NotifyFoodMovementContact(this);
}

void AMCFoodActor::UpdateWalkingCollision(float Dt)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodWalkingCollision);
    RefreshWalkingCollision();
    if(!HasAuthority())return;
    const float Step=FMath::IsFinite(Dt)?FMath::Clamp(Dt,0.f,.1f):0.f;
    if(!CanUseWalkingCollision() || !IsSubstantialWalkingObstacle())
    {
        ResetWalkingObstacle();NudgeSmallWalkingDebris(Step);return;
    }
    const FVector Linear=GetSimulationFrame().CanonicalVelocity;
    const FVector Angular=Body->IsSimulatingPhysics()?Body->GetPhysicsAngularVelocityInDegrees():FVector::ZeroVector;
    const FTransform Pose=Body->GetComponentTransform();
    const bool ValidMotion=!Linear.ContainsNaN() && !Angular.ContainsNaN() && !Pose.ContainsNaN();
    if(bWalkingObstacle)
    {
        if(!ValidMotion || Linear.SizeSquared()>FMath::Square(KeepSpeed)
            || Angular.SizeSquared()>FMath::Square(KeepAngularSpeed)
            || !PoseStayedNear(Pose,WalkingObstacleTransform))ResetWalkingObstacle();
        return;
    }
    if(!ValidMotion || Linear.SizeSquared()>FMath::Square(ArmSpeed)
        || Angular.SizeSquared()>FMath::Square(ArmAngularSpeed))
    {
        WalkingSettleSeconds=0;bWalkingSettleCandidate=false;return;
    }
    if(!bWalkingSettleCandidate || !PoseStayedNear(Pose,WalkingSettleTransform))
    {
        WalkingSettleSeconds=0;WalkingSettleTransform=Pose;bWalkingSettleCandidate=true;
    }
    WalkingSettleSeconds+=Step;
    WalkingClearanceScanSeconds=FMath::Max(0.f,WalkingClearanceScanSeconds-Step);
    if(WalkingSettleSeconds<SettleSeconds || WalkingClearanceScanSeconds>0)return;
    // A worker standing inside many remnants must not cause a per-food player
    // scan every frame while those pieces wait for a clear footprint.
    WalkingClearanceScanSeconds=.1f;
    const FBox Bounds=GetWalkingCollisionBounds();
    if(!IsWalkingBoundsClearOfPlayers(Bounds)){SeparateWalkingFoodFromPlayers(Bounds);return;}
    if(bWalkingSeparationActive)
    {
        UE_LOG(LogTemp,Log,TEXT("Food walking clearance ended: %s; clear, walking blocker armed"),*GetName());
        bWalkingSeparationActive=false;
    }
    WalkingObstacleTransform=Pose;bWalkingObstacle=true;
    RefreshWalkingCollision();ForceNetUpdate();
}

void AMCFoodActor::NudgeSmallWalkingDebris(float Dt)
{
    if(!HasAuthority() || !CanUseWalkingCollision() || !bFragment || IsSubstantialWalkingObstacle()
        || IsNutEncounterFragment() || bManuallyThrown || bManualThrowInFlight
        || !Body->IsSimulatingPhysics())return;
    WalkingNudgeScanSeconds-=Dt;
    const double Now=GetWorld()->GetTimeSeconds();
    if(WalkingNudgeScanSeconds>0 || Now<NextWalkingNudgeAt)return;
    WalkingNudgeScanSeconds=.1f;
    const FVector Motion=Body->GetPhysicsLinearVelocity();
    if(Motion.ContainsNaN() || Motion.SizeSquared()>FMath::Square(KeepSpeed))return;
    const FBox Bounds=GetWalkingCollisionBounds();
    if(!Bounds.IsValid)return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        const auto* Move=It->GetCharacterMovement();
        if(It->IsActorBeingDestroyed() || !It->CanWork() || !It->Status || !It->Status->IsAlive()
            || !It->ToothPhysics || !It->ToothPhysics->CanAct() || !Move || !Move->IsMovingOnGround())continue;
        const FVector HeroVelocity=It->GetSimulationFrame().CanonicalVelocity;
        if(HeroVelocity.ContainsNaN() || HeroVelocity.SizeSquared2D()<FMath::Square(10.f))continue;
        if(!CapsuleTouchesBounds(It->GetCapsuleComponent(),Bounds,12.f))continue;
        FVector Away=(Bounds.GetCenter()-It->GetActorLocation()).GetSafeNormal2D();
        if(Away.IsNearlyZero())Away=HeroVelocity.GetSafeNormal2D();
        const float OutSpeed=FMath::Clamp(float(HeroVelocity.Size2D())*.35f,80.f,110.f);
        const float Change=FMath::Clamp(OutSpeed-float(FVector::DotProduct(Motion,Away)),0.f,110.f);
        if(Change<=0)return;
        Body->AddImpulse(Away*Change,NAME_None,true);
        NextWalkingNudgeAt=Now+.5;return;
    }
}
