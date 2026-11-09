#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
constexpr float ObstacleHeight=45.f,ObstacleWidth=50.f,SettleSeconds=.35f;
constexpr float ArmSpeed=40.f,KeepSpeed=70.f,ArmAngularSpeed=15.f,KeepAngularSpeed=25.f;
constexpr float AnchorDistance=6.f,AnchorAngle=5.f;

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
    return Size.Z>=ObstacleHeight && FMath::Min(Size.X,Size.Y)>=ObstacleWidth;
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
    if(!GetWorld() || !Bounds.IsValid)return false;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        if(It->IsActorBeingDestroyed())continue;
        const auto* Capsule=It->GetCapsuleComponent();
        if(Capsule && Capsule->GetCollisionEnabled()!=ECollisionEnabled::NoCollision
            && CapsuleTouchesBounds(Capsule,Bounds,5.f))return false;
    }
    return true;
}

void AMCFoodActor::ResetWalkingObstacle()
{
    const bool WasBlocking=bWalkingObstacle || (WalkingBlocker
        && WalkingBlocker->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block);
    if(HasAuthority())bWalkingObstacle=false;
    WalkingSettleSeconds=0;WalkingClearanceScanSeconds=0;bWalkingSettleCandidate=false;
    if(WalkingBlocker)
    {
        WalkingBlocker->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
        if(IsDisposed() || bCutIntakeReceipt)WalkingBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    if(Body)Body->SetCollisionResponseToChannel(ECC_Pawn,
        bFragment || Phase==EMCFoodPhase::Absorbing?ECR_Ignore:ECR_Block);
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
        Block || bFragment || Phase==EMCFoodPhase::Absorbing?ECR_Ignore:ECR_Block);
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
    if(!IsWalkingBoundsClearOfPlayers(GetWalkingCollisionBounds()))return;
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
