#include "MCDriverSkillAction.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCSuperCarVisualComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"

AMCDriverSkillAction::AMCDriverSkillAction()
{
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    PropAsset=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/Props/SM_SuperCar_Chassis.SM_SuperCar_Chassis"));
    // Source RH canonical anchors become UE LH centimeters: (X,-Y,Z).
    PropTransform=FTransform(FVector(0,0,71.802843f));
    StartVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Driver_Start.NS_Driver_Start"));
    LoopVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Driver_Loop.NS_Driver_Loop"));
    EndVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Driver_End.NS_Driver_End"));
    SuperCarVisual=CreateDefaultSubobject<UMCSuperCarVisualComponent>(TEXT("SuperCarVisual"));SuperCarVisual->SetupAttachment(SceneRoot);
    PartAssets=SuperCarVisual->PartAssets;PartAssets.Remove(FName(TEXT("Chassis")));
    PartTransforms=SuperCarVisual->PartTransforms;PartTransforms.Remove(FName(TEXT("Chassis")));
}
UMCDriverSkillEffect::UMCDriverSkillEffect() {ActionClass=AMCDriverSkillAction::StaticClass();}
void AMCDriverSkillAction::BeginPlay()
{
    Super::BeginPlay();PreviousVisual=GetActorLocation();PreviousVisualHeading=GetActorForwardVector();
    // Keep the base prop reference for compatibility/preload; the reusable
    // component owns the single visible chassis and all eight moving parts.
    if(PropMesh) {PropMesh->SetHiddenInGame(true);PropMesh->SetVisibility(false);}
    if(SuperCarVisual && GetNetMode()!=NM_DedicatedServer) {
        const auto* Native=GetDefault<AMCDriverSkillAction>();
        const bool Legacy=PartAssets.Contains(FName(TEXT("WheelsFront"))) || PartAssets.Contains(FName(TEXT("WheelsRear")))
            || PartAssets.Contains(FName(TEXT("Rim"))) || PartAssets.Contains(FName(TEXT("FrontRoller"))) || PartAssets.Contains(FName(TEXT("RearRoller")));
        if(Legacy) {
            // Existing Blueprint map overrides keep their authored assembly.
            // A paired mesh is never steered around the center between wheels.
            SuperCarVisual->PartAssets=PartAssets;SuperCarVisual->PartTransforms=PartTransforms;
        } else {
            for(const auto& Entry:PartAssets) if(Entry.Value!=Native->PartAssets.FindRef(Entry.Key)) SuperCarVisual->PartAssets.Add(Entry.Key,Entry.Value);
            for(const auto& Entry:PartTransforms) if(!Entry.Value.Equals(Native->PartTransforms.FindRef(Entry.Key))) SuperCarVisual->PartTransforms.Add(Entry.Key,Entry.Value);
        }
        if(Legacy || PropAsset!=Native->PropAsset) SuperCarVisual->PartAssets.Add(FName(TEXT("Chassis")),PropAsset);
        if(Legacy || !PropTransform.Equals(Native->PropTransform)) SuperCarVisual->PartTransforms.Add(FName(TEXT("Chassis")),PropTransform);
        SuperCarVisual->FrontWheelRadius=Param(TEXT("FrontWheelRadius"),33.9f);SuperCarVisual->RearWheelRadius=Param(TEXT("RearWheelRadius"),40.8f);
        SuperCarVisual->LegacyRollerRadius=Param(TEXT("RollerRadius"),24.f);
        SuperCarVisual->MaxSteeringAngle=FMath::Clamp(Param(TEXT("WheelSteeringAngle"),25.f),0.f,45.f);
        SuperCarVisual->SteeringResponse=FMath::Max(Param(TEXT("WheelSteeringResponse"),10.f),0.f);
        SuperCarVisual->SetBrushSpeed(Param(TEXT("BrushRPM"),180.f));SuperCarVisual->RebuildVisuals();
        Parts.Reset();for(const auto& Entry:PartAssets) if(auto* Part=SuperCarVisual->GetPart(Entry.Key)) Parts.Add(Part);
    }
}
FTransform AMCDriverSkillAction::GetRideFrame() const
{
    const auto* Native=GetDefault<AMCDriverSkillAction>();
    const FVector Seat=!SeatLocal.Equals(Native->SeatLocal)?SeatLocal:(SuperCarVisual?SuperCarVisual->SeatLocal:SeatLocal);
    return FTransform(FQuat::Identity,FVector(Param(TEXT("SeatX"),float(Seat.X)),Param(TEXT("SeatY"),float(Seat.Y)),
        Param(TEXT("SeatZ"),float(Seat.Z))))*(SuperCarVisual?SuperCarVisual->GetComponentTransform():GetActorTransform());
}
FTransform AMCDriverSkillAction::GetHandFrame(bool bLeft) const
{
    const FTransform Parent=SuperCarVisual?SuperCarVisual->GetComponentTransform():GetActorTransform();
    FTransform Frame=SuperCarVisual?SuperCarVisual->GetHandFrame(bLeft).GetRelativeTransform(Parent):FTransform::Identity;
    FVector Local=Frame.GetLocation();
    const FString Prefix=bLeft?TEXT("LeftGrip"):TEXT("RightGrip");
    Local.X=Param(FName(*(Prefix+TEXT("X"))),float(Local.X));Local.Y=Param(FName(*(Prefix+TEXT("Y"))),float(Local.Y));Local.Z=Param(FName(*(Prefix+TEXT("Z"))),float(Local.Z));
    Frame.SetLocation(Local);return Frame*Parent;
}
FTransform AMCDriverSkillAction::GetFootFrame(bool bLeft) const
{
    return SuperCarVisual?SuperCarVisual->GetFootFrame(bLeft):GetActorTransform();
}
bool AMCDriverSkillAction::CanStart() const
{
    if(!Super::CanStart() || !MCActiveSkillMotion::IsEligible(Hero(),true) || !MCActiveSkillMotion::CanAcquire(Hero())) return false;
    return MCActiveSkillMotion::ClearRideFootprint(Hero(),this,Hero()->GetActorForwardVector(),
        FVector(FMath::Clamp(Param(TEXT("RideHalfLength"),180.f),30.f,250.f),FMath::Clamp(Param(TEXT("RideHalfWidth"),135.f),30.f,200.f),68),0,0);
}
void AMCDriverSkillAction::StartCommitted()
{
    Super::StartCommitted();
    Heading=Hero()->GetActorForwardVector().GetSafeNormal2D();PreviousFoot=MCActiveSkillMotion::FootPoint(Hero());
    const FTransform Pose=MCActiveSkillMotion::RidePose(Hero(),Heading);
    PreviousClean=Pose.TransformPosition(FVector(FMath::Clamp(Param(TEXT("CleanForwardOffset"),136.7746f),0.f,200.f),0,0));
    // Capture the row's duration once so the deadline and movement source agree.
    const float RideDuration=FMath::Clamp(Param(TEXT("RideSeconds"),3.f),.2f,30.f);
    EndsAt=Now()+RideDuration;LastSampleAt=Now();
    SetActorLocationAndRotation(Pose.GetLocation(),Pose.GetRotation());
    PreviousVisual=Pose.GetLocation();PreviousVisualHeading=Heading;
    if(SuperCarVisual) {SuperCarVisual->SetTravelDistance(0);SuperCarVisual->SetSteeringAngle(0);}
    const float Speed=FMath::Clamp(Param(TEXT("RideSpeed"),720.f),100.f,1200.f);
    if(!Motion.Begin(Hero(),GetActionID(),Heading*Speed,false,RideDuration)) Finish(EMCActiveSkillEndReason::InputCanceled);
    else {UpdateRideVFX();PlaySkillCue(TEXT("Start"),PreviousClean+FVector(0,0,3),Heading);StartLoop();UpdateRideVFX();}
}
void AMCDriverSkillAction::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(IsFinished() || !IsCommitted() || !Hero()) return;
    UpdateRideVFX();
    if(!HasAuthority()) {UpdateParts(float(FVector::Dist2D(PreviousVisual,GetActorLocation())),DeltaSeconds);PreviousVisual=GetActorLocation();return;}
    if(Now()>=EndsAt) {Finish(EMCActiveSkillEndReason::Completed);return;}
    if(!MCActiveSkillMotion::IsEligible(Hero(),true) || !Motion.IsOwned()) {Finish(EMCActiveSkillEndReason::InputCanceled);return;}
    const float Seconds=float(Now()-LastSampleAt),Speed=FMath::Clamp(Param(TEXT("RideSpeed"),720.f),100.f,1200.f);
    const FVector Foot=MCActiveSkillMotion::FootPoint(Hero());
    if(Seconds>0 && !MCActiveSkillMotion::ContinuousPath(PreviousFoot,Foot,Seconds,Speed)) {Finish(EMCActiveSkillEndReason::InputCanceled);return;}
    const FVector Clean=MCActiveSkillMotion::RidePose(Hero(),Heading).TransformPosition(FVector(FMath::Clamp(Param(TEXT("CleanForwardOffset"),136.7746f),0.f,200.f),0,0));
    MCActiveSkillMotion::CleanSweep(Hero(),this,PreviousClean,Clean,Seconds,FMath::Clamp(Param(TEXT("CleanRadius"),78.5f),5.f,150.f),
        FMath::Clamp(Param(TEXT("CleanRate"),2.f),.01f,20.f),Speed+500.f);
    const float Travel=float(FVector::Dist2D(PreviousFoot,Foot));RunOver(PreviousFoot,Foot,Seconds);
    PreviousFoot=Foot;PreviousClean=Clean;LastSampleAt=Now();
    Heading=MCActiveSkillMotion::Steering(Hero(),Heading,FMath::Clamp(Param(TEXT("TurnRate"),130.f),20.f,720.f),DeltaSeconds);
    if(!MCActiveSkillMotion::ClearRideFootprint(Hero(),this,Heading,
        FVector(FMath::Clamp(Param(TEXT("RideHalfLength"),180.f),30.f,250.f),FMath::Clamp(Param(TEXT("RideHalfWidth"),135.f),30.f,200.f),68),FMath::Min(DeltaSeconds,.05f),Speed)) {
        Finish(EMCActiveSkillEndReason::Completed);return;
    }
    Motion.Update(Heading*Speed);const FTransform Pose=MCActiveSkillMotion::RidePose(Hero(),Heading);
    SetActorLocationAndRotation(Pose.GetLocation(),Pose.GetRotation());
    UpdateParts(Travel,DeltaSeconds);
    UpdateRideVFX();
}
void AMCDriverSkillAction::RunOver(FVector From,FVector To,float Seconds)
{
    if(!MCActiveSkillMotion::ContinuousPath(From,To,Seconds,1200.f) || FVector::DistSquared2D(From,To)<1.f) return;
    const float Radius=FMath::Clamp(Param(TEXT("HitRadius"),135.f),20.f,180.f);
    for(auto It=ContactedAllies.CreateIterator();It;++It) {
        const auto* Ally=It->Get();
        if(!Ally || FVector::Dist2D(Ally->GetActorLocation(),To)>Radius+Ally->GetCapsuleComponent()->GetScaledCapsuleRadius()+45.f
            || FMath::Abs(MCActiveSkillMotion::FootPoint(Ally).Z-To.Z)>100.f) It.RemoveCurrent();
    }
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Ally=*It;if(Ally==Hero() || !Ally->Status || !Ally->Status->IsAlive() || ContactedAllies.Contains(Ally)) continue;
        const float HalfHeight=Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector Sole=Ally->GetActorLocation()-FVector(0,0,HalfHeight);
        if(FMath::Abs(Sole.Z-To.Z)>70.f) continue;
        FVector A=From,B=To,P=Sole;A.Z=B.Z=P.Z=0;
        const FVector Closest=FMath::ClosestPointOnSegment(P,A,B);
        if(FVector::DistSquared(P,Closest)>FMath::Square(Radius+Ally->GetCapsuleComponent()->GetScaledCapsuleRadius())) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCDriverAllyContact),false,Hero());Query.AddIgnoredActor(this);Query.AddIgnoredActor(Ally);
        FHitResult Wall;
        if(GetWorld()->LineTraceSingleByChannel(Wall,To+FVector(0,0,45),Ally->GetActorLocation(),ECC_Visibility,Query)) continue;
        ContactedAllies.Add(Ally); // Seal before damage callbacks/ragdoll/spill.
        const float Damage=FMath::Clamp(Param(TEXT("AllyDamage"),8.f),0.f,100.f);
        const FVector Direction=(To-From).GetSafeNormal2D();
        if((Damage<=0 || Ally->Status->Damage(Damage,Direction,this)) && Ally->ToothPhysics)
            Ally->ToothPhysics->ApplyHit(Direction*FMath::Clamp(Param(TEXT("AllyKnockback"),320.f),0.f,900.f)
                +FVector(0,0,FMath::Clamp(Param(TEXT("AllyLift"),150.f),0.f,400.f)),Ally->GetActorLocation());
    }
}
void AMCDriverSkillAction::UpdateParts(float Distance,float DeltaSeconds)
{
    const FVector Forward=GetActorForwardVector().GetSafeNormal2D();
    const float Turn=FMath::FindDeltaAngleDegrees(PreviousVisualHeading.Rotation().Yaw,Forward.Rotation().Yaw);
    PreviousVisualHeading=Forward;
    if(!SuperCarVisual) return;
    const float Rate=Turn/FMath::Max(DeltaSeconds,.001f),TurnRate=FMath::Clamp(Param(TEXT("TurnRate"),130.f),20.f,720.f);
    const float Steering=FMath::Clamp(Rate/TurnRate,-1.f,1.f)*SuperCarVisual->MaxSteeringAngle;
    SuperCarVisual->UpdateTravel(Distance,Steering,DeltaSeconds);
}
void AMCDriverSkillAction::UpdateRideVFX()
{
    if(!Hero() || !LoopComponent) return;
    const FVector Forward=GetActorForwardVector().GetSafeNormal2D();
    const float Offset=FMath::Clamp(Param(TEXT("CleanForwardOffset"),136.7746f),0.f,200.f);
    LoopComponent->SetWorldLocation(MCActiveSkillMotion::FootPoint(Hero())+Forward*Offset+FVector(0,0,3));
    UpdateLoopVFX(Forward);
}
void AMCDriverSkillAction::OnFinished(EMCActiveSkillEndReason Reason)
{
    Motion.End();ContactedAllies.Reset();
    if(HasAuthority() && Reason!=EMCActiveSkillEndReason::WorldEnded) PlaySkillCue(TEXT("End"),PreviousClean+FVector(0,0,3),Heading);
    Super::OnFinished(Reason);
}
