#include "MCGripComponent.h"
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCExpressionComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/SkeletalMesh.h"
#include "Net/UnrealNetwork.h"
#include "TwoBoneIK.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

void FMCGripSettings::Sanitize()
{
    auto C=[](float V,float D,float A,float B){return FMath::Clamp(FMath::IsFinite(V)?V:D,A,B);};
    ReachSeconds=C(ReachSeconds,.28f,.12f,.6f); ReleaseSeconds=C(ReleaseSeconds,.22f,.1f,.6f);
    BodyReach=C(BodyReach,18,0,24); CrouchDepth=C(CrouchDepth,25,0,30); ContactTolerance=C(ContactTolerance,6,0,10);
    MaxArmStretch=C(MaxArmStretch,1.08f,1,1.15f); BreakSlack=C(BreakSlack,14,2,20);
    DragDistanceScale=C(DragDistanceScale,1.7f,1,2); CarryMaxMass=C(CarryMaxMass,6,0,20); CarryMaxHalfExtent=C(CarryMaxHalfExtent,30,10,50);
    PalmLength=C(PalmLength,7,2,12); PalmThickness=C(PalmThickness,2.5f,0,6);
    FrontAngle=C(FrontAngle,55,30,70); RearAngle=C(RearAngle,130,110,160); AngleHysteresis=C(AngleHysteresis,8,2,15);
    DriveForce=C(DriveForce,17000,1000,40000); Spring=C(Spring,750,100,1500); Damping=C(Damping,95,10,250); Lean=C(Lean,10,0,20);
    TurnRate=C(TurnRate,45,5,90); TurnTorque=C(TurnTorque,900000,0,2000000); TurnDamping=C(TurnDamping,750000,10000,2000000);
}
UMCGripComponent::UMCGripComponent()
{
    SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PostPhysics;
    Profile=TSoftObjectPtr<UMCGripProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_Grip.DA_Grip")));
    for (int32 I=0;I<2;++I) { Targets[I]=FVector::ZeroVector; Normals[I]=FVector::ForwardVector; }
}
void UMCGripComponent::BeginPlay()
{
    Super::BeginPlay(); Tooth=CastChecked<AMCToothCharacter>(GetOwner());
    if (Tooth->HasAuthority()) { if (const auto* P=Profile.LoadSynchronous()) Settings=P->Settings; Settings.Sanitize(); }
    CacheRig();
    Tooth->GetMesh()->AddTickPrerequisiteComponent(this);
}
float UMCGripComponent::Now() const
{
    const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
void UMCGripComponent::OnRep_Settings() { Settings.Sanitize(); CacheRig(); }
FVector UMCGripComponent::ReachFor(const AMCFoodActor* Food) const
{
    FVector Reach=(Food->Visual->Bounds.Origin-Tooth->GetActorLocation()).GetSafeNormal2D()*Settings.BodyReach/Settings.DragDistanceScale;
    const float ShoulderZ=Tooth->GetActorTransform().TransformPosition((Arms[0].Shoulder+Arms[1].Shoulder)*.5).Z;
    Reach.Z=FMath::Clamp(float(Food->Visual->Bounds.GetBox().Max.Z+12-ShoulderZ),-Settings.CrouchDepth,0.f);
    return Reach;
}
bool UMCGripComponent::CanCarry(const AMCFoodActor* Food) const
{
    if (!Food || Food->bBrushTool || Food->Phase==EMCFoodPhase::Stuck || Food->IsDisposed() || Food->Holders.Num()>1) return false;
    const FVector Extent=Food->Body->GetScaledBoxExtent();
    const float VisualRadius=Food->Visual->Bounds.SphereRadius;
    return Food->Settings.Mass<=Settings.CarryMaxMass && Extent.GetMax()<=Settings.CarryMaxHalfExtent
        && VisualRadius<=Settings.CarryMaxHalfExtent*1.75f;
}
FVector UMCGripComponent::CarryLocation(const AMCFoodActor* Food) const
{
    const FMCGripFrame* Selected=FrameFor(Food?Food:Frame.Food.Get());
    if (!Selected) return FVector::ZeroVector;
    const FMCGripFrame& ContactFrame=*Selected;
    if (!Tooth || !ContactFrame.Food) return FVector::ZeroVector;
    const FVector Extent=ContactFrame.Food->Body->GetScaledBoxExtent();
    const FVector Shoulders=Tooth->GetActorTransform().TransformPosition((Arms[0].Shoulder+Arms[1].Shoulder)*.5);
    FVector P=Tooth->GetActorLocation()+Tooth->GetActorForwardVector()*(Tooth->GetCapsuleComponent()->GetScaledCapsuleRadius()+Extent.X+8);
    P.Z=FMath::Max(Shoulders.Z,Tooth->GetActorLocation().Z+8);
    if (ContactFrame.Pose==EMCGripPose::LeftHand || ContactFrame.Pose==EMCGripPose::RightHand)
        P+=Tooth->GetActorRightVector()*(ContactFrame.Pose==EMCGripPose::LeftHand?-1:1)*(Extent.Y+20);
    // A contact picked near the floor may sit high on a small item after lifting.
    // Fit the carry target to those same anchors instead of forcing an unreachable grip.
    const FTransform VisualLocal=ContactFrame.Food->Visual->GetComponentTransform().GetRelativeTransform(ContactFrame.Food->GetActorTransform());
    for (int32 Pass=0;Pass<3;++Pass) for (int32 I=0;I<2;++I) if (UsesHand(ContactFrame.Pose,I==0))
    {
        const FTransform Target=VisualLocal*FTransform(Tooth->GetActorQuat(),P,ContactFrame.Food->GetActorScale3D());
        const FVector N=Target.TransformVectorNoScale(I==0?FVector(ContactFrame.LeftNormal):FVector(ContactFrame.RightNormal));
        const FVector Wrist=Target.TransformPosition(I==0?FVector(ContactFrame.LeftPoint):FVector(ContactFrame.RightPoint))-HandRotation(I,N).RotateVector(Arms[I].PalmLocal);
        const FVector D=Wrist-ShoulderPoint(I);
        const float Reach=(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch*Settings.DragDistanceScale-2;
        if (D.Size()>Reach) P-=D.GetSafeNormal()*(D.Size()-Reach);
    }
    return P;
}
void UMCGripComponent::CacheRig()
{
    if (!Tooth || !Tooth->GetMesh()->GetSkeletalMeshAsset()) return;
    const auto& Ref=Tooth->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton(); TArray<FTransform> CS;
    CS.SetNum(Ref.GetNum());
    for (int32 I=0;I<CS.Num();++I) CS[I]=Ref.GetParentIndex(I)>=0?Ref.GetRefBonePose()[I]*CS[Ref.GetParentIndex(I)]:Ref.GetRefBonePose()[I];
    bRigReady=true;
    const FVector Forward=Tooth->StandingMeshTransform().InverseTransformVectorNoScale(FVector::ForwardVector);
    for (int32 I=0;I<2;++I)
    {
        auto& A=Arms[I]; const FString Side=I==0?TEXT("_l"):TEXT("_r");
        A.Upper=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("arm")+Side))));
        A.Lower=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("forearm")+Side))));
        A.Hand=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("hand")+Side))));
        const int32 Tip=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("fingertip")+Side))));
        if (A.Upper<0 || A.Lower<0 || A.Hand<0 || Tip<0) { bRigReady=false; continue; }
        A.Shoulder=Tooth->StandingMeshTransform().TransformPosition(CS[A.Upper].GetLocation());
        A.UpperLength=FVector::Distance(CS[A.Upper].GetLocation(),CS[A.Lower].GetLocation());
        A.LowerLength=FVector::Distance(CS[A.Lower].GetLocation(),CS[A.Hand].GetLocation());
        A.FingerLocal=CS[A.Hand].InverseTransformVectorNoScale(CS[Tip].GetLocation()-CS[A.Hand].GetLocation()).GetSafeNormal();
        A.NormalLocal=FVector::VectorPlaneProject(CS[A.Hand].InverseTransformVectorNoScale(Forward),A.FingerLocal).GetSafeNormal();
        A.PalmLocal=A.FingerLocal*Settings.PalmLength+A.NormalLocal*Settings.PalmThickness;
        bRigReady&=A.UpperLength>1 && A.LowerLength>1;
    }
}
bool UMCGripComponent::UsesHand(EMCGripPose Pose,bool Left)
{
    return Pose!=EMCGripPose::LeftHand && Pose!=EMCGripPose::RightHand || (Left?Pose==EMCGripPose::LeftHand:Pose==EMCGripPose::RightHand);
}
EMCGripPose UMCGripComponent::SelectPose(EMCGripPose Previous,float Angle,float Approach,const FMCGripSettings& S)
{
    const float A=FMath::Abs(Angle);
    const bool WasFront=Previous==EMCGripPose::FrontPull || Previous==EMCGripPose::Push;
    if (A<S.FrontAngle+(WasFront?S.AngleHysteresis:-S.AngleHysteresis))
        return Approach>(Previous==EMCGripPose::Push?-5:12)?EMCGripPose::Push:EMCGripPose::FrontPull;
    if (A>S.RearAngle+(Previous==EMCGripPose::RearPull?-S.AngleHysteresis:S.AngleHysteresis)) return EMCGripPose::RearPull;
    return Angle<0?EMCGripPose::LeftHand:EMCGripPose::RightHand;
}
FVector UMCGripComponent::ShoulderPoint(int32 I) const
{
    return Tooth->GetActorTransform().TransformPosition(Arms[I].Shoulder)+ReachOffset;
}
FQuat UMCGripComponent::HandRotation(int32 I,FVector Normal) const
{
    const FVector PalmNormal=-Normal.GetSafeNormal();
    FVector Fingers=FVector::VectorPlaneProject(-FVector::UpVector,PalmNormal).GetSafeNormal();
    if (Fingers.IsNearlyZero()) Fingers=FVector::VectorPlaneProject(Tooth->GetActorForwardVector(),PalmNormal).GetSafeNormal();
    const FQuat Source=FRotationMatrix::MakeFromXZ(Arms[I].FingerLocal,Arms[I].NormalLocal).ToQuat();
    return (FRotationMatrix::MakeFromXZ(Fingers,PalmNormal).ToQuat()*Source.Inverse()).GetNormalized();
}
bool UMCGripComponent::FindAnchors(AMCFoodActor* Food,EMCGripPose Mode,FMCGripFrame& Result)
{
    DebugFailure.Empty();
    if (!bRigReady || !Food) { DebugFailure=TEXT("Missing arm rig or food"); return false; }
    const FVector Reach=ReachFor(Food);
    for (int32 I=0;I<2;++I)
    {
        if (!UsesHand(Mode,I==0)) continue;
        const FVector Shoulder=Tooth->GetActorTransform().TransformPosition(Arms[I].Shoulder)+Reach;
        FHitResult Hit; if (!Food->FindGripSurface(Shoulder,Hit)) { DebugFailure=TEXT("No visible surface hit"); return false; }
        const FVector Wrist=Hit.ImpactPoint-HandRotation(I,Hit.ImpactNormal).RotateVector(Arms[I].PalmLocal);
        if (FVector::Distance(Shoulder,Wrist)>(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch*Settings.DragDistanceScale)
        { DebugFailure=FString::Printf(TEXT("Hand %d needs %.1f, reach %.1f; shoulder %s contact %s"),I,FVector::Distance(Shoulder,Wrist),(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch*Settings.DragDistanceScale,*Shoulder.ToCompactString(),*Hit.ImpactPoint.ToCompactString()); return false; }
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MCGripLOS),false,Tooth); Params.AddIgnoredActor(Food);
        FHitResult Obstacle;
        if (GetWorld()->LineTraceSingleByChannel(Obstacle,Shoulder,Hit.ImpactPoint,ECC_Visibility,Params)) { DebugFailure=TEXT("Contact occluded by ")+GetNameSafe(Obstacle.GetActor()); return false; }
        const FTransform T=Food->Visual->GetComponentTransform();
        if (I==0) { Result.LeftPoint=T.InverseTransformPosition(Hit.ImpactPoint); Result.LeftNormal=T.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal(); }
        else { Result.RightPoint=T.InverseTransformPosition(Hit.ImpactPoint); Result.RightNormal=T.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal(); }
    }
    Result.Pose=Mode; return true;
}
const FMCGripFrame* UMCGripComponent::FrameFor(const AMCFoodActor* Food) const
{
    if (!Food) return nullptr;
    return Frame.Food==Food?&Frame:Secondary.Food==Food?&Secondary:nullptr;
}
const FMCGripFrame* UMCGripComponent::HandFrame(bool Left) const
{
    if (IsValid(Frame.Food) && UsesHand(Frame.Pose,Left)) return &Frame;
    if (IsValid(Secondary.Food) && UsesHand(Secondary.Pose,Left)) return &Secondary;
    return nullptr;
}
bool UMCGripComponent::HandOccupied(bool Left) const { return GrabbedPlayer || HandFrame(Left); }
bool UMCGripComponent::HasFreeHand() const { return !HandOccupied(true) || !HandOccupied(false); }
bool UMCGripComponent::Holds(const AMCFoodActor* Food) const { return FrameFor(Food)!=nullptr; }
bool UMCGripComponent::IsReady(const AMCFoodActor* Food) const
{
    const auto* F=FrameFor(Food?Food:Frame.Food.Get()); return F && F->bContact;
}
bool UMCGripComponent::CanAcquire(const AMCFoodActor* Food) const
{
    return Food && !Holds(Food) && HasFreeHand() && (!Frame.Food || (CanCarry(Food) && CanCarry(Frame.Food)));
}
float UMCGripComponent::LoadMass() const
{
    return (Frame.Food?Frame.Food->Settings.Mass:0)+(Secondary.Food?Secondary.Food->Settings.Mass:0)
        +(GrabbedPlayer?GrabbedPlayer->ToothPhysics->Settings.Mass:0);
}
bool UMCGripComponent::BeginGrip(AMCFoodActor* Food)
{
    if (!Tooth || !Tooth->HasAuthority() || !CanAcquire(Food) || !Tooth->CanWork() || Now()<NextAttemptAt) return false;
    const FVector D=Tooth->GetActorTransform().InverseTransformVectorNoScale(Food->Visual->Bounds.Origin-Tooth->GetActorLocation());
    const float Angle=FMath::RadiansToDegrees(FMath::Atan2(D.Y,D.X));
    FMCGripFrame NewFrame;
    auto Mode=SelectPose(EMCGripPose::FrontPull,Angle,0,Settings);
    if (CanCarry(Food))
    {
        bool Left=D.Y<0;
        if (HandOccupied(Left)) Left=!Left;
        Mode=Left?EMCGripPose::LeftHand:EMCGripPose::RightHand;
    }
    if (!FindAnchors(Food,Mode,NewFrame))
    {
        const auto Near=D.Y<0?EMCGripPose::LeftHand:EMCGripPose::RightHand;
        const auto Other=Near==EMCGripPose::LeftHand?EMCGripPose::RightHand:EMCGripPose::LeftHand;
        const bool NearOK=!HandOccupied(Near==EMCGripPose::LeftHand) && FindAnchors(Food,Near,NewFrame);
        if (!NearOK && (HandOccupied(Other==EMCGripPose::LeftHand) || !FindAnchors(Food,Other,NewFrame))) return false;
    }
    NewFrame.Food=Food; NewFrame.StartedAt=Now(); NewFrame.RestOffset=Food->GetActorLocation()-Tooth->GetActorLocation();
    NewFrame.RelativeYaw=FMath::FindDeltaAngleDegrees(Tooth->GetActorRotation().Yaw,Food->GetActorRotation().Yaw);
    auto& Destination=Frame.Food?Secondary:Frame;
    NewFrame.Serial=Destination.Serial+1; Destination=NewFrame;
    if (&Destination==&Secondary) SecondaryLostContact=0; else LostContact=0;
    NextModeAt=Now()+.35; Tooth->ForceNetUpdate(); return true;
}
void UMCGripComponent::EndGrip(AMCFoodActor* Food)
{
    if (!Tooth || !Tooth->HasAuthority()) return;
    if (!Food) { Frame=FMCGripFrame(); Secondary=FMCGripFrame(); }
    else if (Secondary.Food==Food) Secondary=FMCGripFrame();
    else if (Frame.Food==Food) { Frame=Secondary; Secondary=FMCGripFrame(); LostContact=SecondaryLostContact; }
    Tooth->HeldFood=Frame.Food; NextAttemptAt=Now()+.25f; Tooth->ForceNetUpdate();
}
FVector UMCGripComponent::ContactPoint(bool Left) const
{
    if (GrabbedPlayer)
    {
        const FTransform T=GrabbedPlayer->GetMesh()->GetSocketTransform(GrabbedPlayer->RigBone(TEXT("body")));
        return T.TransformPosition(PlayerAnchor)+Tooth->GetActorRightVector()*(Left?-10:10);
    }
    const auto* F=HandFrame(Left);
    return F?F->Food->Visual->GetComponentTransform().TransformPosition(Left?FVector(F->LeftPoint):FVector(F->RightPoint)):Targets[Left?0:1];
}
FVector UMCGripComponent::ForcePoint(const AMCFoodActor* Food) const
{
    const auto* F=FrameFor(Food); if (!F) return FVector::ZeroVector;
    FVector P=FVector::ZeroVector; float Count=0;
    for (int32 I=0;I<2;++I) if (UsesHand(F->Pose,I==0)) { P+=ContactPoint(I==0); ++Count; }
    return P/FMath::Max(1.f,Count);
}
FVector UMCGripComponent::PalmPoint(bool Left) const
{
    if (!bRigReady) return Tooth?Tooth->GetActorLocation():FVector::ZeroVector;
    const int32 I=Left?0:1;
    return Tooth->GetMesh()->GetSocketTransform(Tooth->RigBone(Left?TEXT("hand_l"):TEXT("hand_r"))).TransformPosition(Arms[I].PalmLocal);
}
float UMCGripComponent::ContactError() const
{
    float Error=0;
    for (int32 I=0;I<2;++I) if (HandOccupied(I==0)) Error=FMath::Max(Error,float(FVector::Distance(PalmPoint(I==0),ContactPoint(I==0))));
    return Error;
}
FVector UMCGripComponent::InputDirection() const
{
    if (!Tooth) return FVector::ZeroVector;
    if (Tooth->GetLocalRole()==ROLE_SimulatedProxy) return CastChecked<UMCToothMovementComponent>(Tooth->GetCharacterMovement())->Intent();
    const FVector A=Tooth->GetCharacterMovement()->GetCurrentAcceleration();
    return A.SizeSquared2D()>1?A.GetSafeNormal2D():Tooth->GetPendingMovementInputVector().GetSafeNormal2D();
}
FVector UMCGripComponent::DriveForce(const AMCFoodActor* Food) const
{
    const auto* Selected=FrameFor(Food?Food:Frame.Food.Get());
    if (!Selected || !Selected->bContact) return FVector::ZeroVector;
    const FMCGripFrame& ContactFrame=*Selected;
    if (ContactFrame.Food->Phase==EMCFoodPhase::Carried)
    {
        const float Mass=ContactFrame.Food->Body->GetMass();
        const float Dt=FMath::Max(.008f,GetWorld()->GetDeltaSeconds());
        const float Omega=9.f;
        const FVector Error=CarryLocation(ContactFrame.Food)-ContactFrame.Food->GetActorLocation();
        const FVector RelativeVelocity=Tooth->GetVelocity()-ContactFrame.Food->GetVelocity();
        // Implicit spring coefficients remain bounded through low frame rates. Chaos
        // keeps gravity, collisions and momentum while one hand suspends the item.
        const FVector A=(Error*Omega*Omega+RelativeVelocity*(2*Omega))/(1+2*Omega*Dt+Omega*Omega*Dt*Dt);
        return (A*Mass+FVector(0,0,-GetWorld()->GetGravityZ()*Mass)).GetClampedToMaxSize(Settings.DriveForce);
    }
    if (ContactFrame.Food->Phase!=EMCFoodPhase::Free) return FVector::ZeroVector;
    const FVector Error=Tooth->GetActorLocation()+FVector(ContactFrame.RestOffset)-ContactFrame.Food->GetActorLocation();
    // Tension builds over centimetres, not the former metres-long invisible spring.
    const FVector Intent=InputDirection();
    // No motor input means damping only. A rotating contact must not turn the root's
    // old rest offset into a self-propelling spring.
    const float Tension=FMath::Max(0.f,float(FVector::DotProduct(Error,Intent)));
    // Forces are held over the rigid-body step. Bound this explicit velocity
    // controller on long frames, keeping the same target speed and normal-frame feel.
    const float StableDamping=FMath::Min(Settings.Damping,.8f*ContactFrame.Food->Body->GetMass()/FMath::Max(.008f,GetWorld()->GetDeltaSeconds()));
    FVector Force=(Intent*(Settings.DriveForce+Tension*Settings.Spring)/Settings.Damping-ContactFrame.Food->GetVelocity())*StableDamping;
    if (StableDamping<Settings.Damping && !Intent.IsNearlyZero())
        Force+=Intent*FloorFrictionForce(ContactFrame.Food)*(1-StableDamping/Settings.Damping);
    Force.Z=FMath::Clamp(Force.Z,-Settings.DriveForce*.15f,Settings.DriveForce*.15f);
    return Force.GetClampedToMaxSize(Settings.DriveForce*1.2f)*CastChecked<UMCToothMovementComponent>(Tooth->GetCharacterMovement())->Traction();
}
float UMCGripComponent::FloorFrictionForce(const AMCFoodActor* Food) const
{
    FHitResult Floor; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCGripFloor),false,Food); Query.AddIgnoredActor(Tooth); Query.bReturnPhysicalMaterial=true;
    const FVector Center=Food->Body->GetCenterOfMass();
    if (!GetWorld()->LineTraceSingleByChannel(Floor,Center,Center-FVector(0,0,Food->Body->Bounds.BoxExtent.Z+4),ECC_WorldStatic,Query) || Floor.ImpactNormal.Z<=.65f) return 0;
    const auto* Material=Food->Body->BodyInstance.GetSimplePhysicalMaterial();
    const float Friction=(Material->Friction+(Floor.PhysMaterial.IsValid()?Floor.PhysMaterial->Friction:Material->Friction))*.5f;
    return Food->Body->GetMass()*FMath::Abs(GetWorld()->GetGravityZ())*Friction;
}
FVector UMCGripComponent::DriveTorque(const AMCFoodActor* Food) const
{
    const auto* Selected=FrameFor(Food?Food:Frame.Food.Get());
    if (!Selected || !Selected->bContact) return FVector::ZeroVector;
    const FMCGripFrame& ContactFrame=*Selected;
    if (ContactFrame.Food->Phase!=EMCFoodPhase::Free && ContactFrame.Food->Phase!=EMCFoodPhase::Carried) return FVector::ZeroVector;
    const float Error=FMath::FindDeltaAngleDegrees(ContactFrame.Food->GetActorRotation().Yaw,Tooth->GetActorRotation().Yaw+ContactFrame.RelativeYaw);
    const bool Carried=ContactFrame.Food->Phase==EMCFoodPhase::Carried;
    const float TurnRate=Settings.TurnRate*(Carried?3.f:1.f);
    const float Rate=FMath::DegreesToRadians(FMath::Clamp(Error*(Carried?6.f:2.2f),-TurnRate,TurnRate));
    const float Spin=ContactFrame.Food->Body->GetPhysicsAngularVelocityInRadians().Z;
    const float Strength=UsesHand(ContactFrame.Pose,true) && UsesHand(ContactFrame.Pose,false)?1.f:.85f;
    const FVector LocalAxis=ContactFrame.Food->Body->GetComponentQuat().UnrotateVector(FVector::UpVector);
    const FVector Inertia=ContactFrame.Food->Body->GetInertiaTensor();
    const float AxialInertia=Inertia.X*LocalAxis.X*LocalAxis.X+Inertia.Y*LocalAxis.Y*LocalAxis.Y+Inertia.Z*LocalAxis.Z*LocalAxis.Z;
    // Bound the velocity servo by the real inertia and frame step so light food
    // cannot oscillate as the stronger motor overcomes surface friction.
    const float Gain=FMath::Min(Settings.TurnDamping,.8f*AxialInertia/FMath::Max(.008f,GetWorld()->GetDeltaSeconds()));
    const float FloorResistance=FloorFrictionForce(ContactFrame.Food)*ContactFrame.Food->Body->GetScaledBoxExtent().Size2D()*FMath::Clamp(Error/6.f,-1.f,1.f);
    return FVector(0,0,FMath::Clamp((Rate-Spin)*Gain+FloorResistance,-Settings.TurnTorque,Settings.TurnTorque)*Strength);
}
FVector UMCGripComponent::ReactionAcceleration() const
{
    if (!Tooth || !Tooth->ToothPhysics->CanAct()) return FVector::ZeroVector;
    FVector Force=FVector::ZeroVector;
    for (const auto* F:{&Frame,&Secondary}) if (F->Food)
    {
        const FVector Goal=F->Food->Phase==EMCFoodPhase::Carried?CarryLocation(F->Food):Tooth->GetActorLocation()+FVector(F->RestOffset);
        FVector Error=F->Food->GetActorLocation()-Goal; Error.Z=0;
        // Return the load to the character rather than snapping the capsule to an arm radius.
        const float Mass=FMath::Max(1.f,F->Food->Settings.Mass);
        Force+=(Error*Mass*12+(F->Food->GetVelocity()-Tooth->GetVelocity())*Mass*2).GetClampedToMaxSize(1200);
        for (int32 I=0;I<2;++I) if (F->Food->Phase!=EMCFoodPhase::Carried && UsesHand(F->Pose,I==0))
        {
            const FVector D=ContactPoint(I==0)-ShoulderPoint(I);
            const float Reach=(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch*Settings.DragDistanceScale;
            Force+=D.GetSafeNormal2D()*FMath::Max(0.f,float(D.Size())-Reach)*300;
        }
    }
    return (Force/FMath::Max(3.f,Tooth->ToothPhysics->Settings.Mass)).GetClampedToMaxSize(1500);
}
FVector UMCGripComponent::ConstrainGripVelocity(FVector Velocity,float Dt) const
{
    // A taut arm limits separation velocity, never teleports either body. The
    // capsule still sweeps against walls and the food keeps its Chaos solver.
    if (!Tooth || Dt<=0) return Velocity;
    for (const auto* F:{&Frame,&Secondary}) if (F->Food && F->Food->Phase!=EMCFoodPhase::Carried)
        for (int32 I=0;I<2;++I) if (UsesHand(F->Pose,I==0))
        {
            const FVector Nworld=F->Food->Visual->GetComponentTransform().TransformVectorNoScale(I==0?FVector(F->LeftNormal):FVector(F->RightNormal));
            const FVector D=ContactPoint(I==0)-HandRotation(I,Nworld).RotateVector(Arms[I].PalmLocal)-ShoulderPoint(I);
            const FVector N=D.GetSafeNormal2D();
            const float Reach=(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch*Settings.DragDistanceScale;
            const float Limit=FMath::Max(0.f,(Reach-2-float(D.Size()))/Dt);
            const float Outward=FVector::DotProduct(F->Food->GetVelocity()-Velocity,N);
            if (Outward>Limit) Velocity+=N*(Outward-Limit);
        }
    return Velocity;
}
FVector UMCGripComponent::PlayerPullAcceleration() const
{
    if (!Tooth || !IsValid(GrabbedPlayer) || Now()-PlayerGrabAt<Settings.ReachSeconds) return FVector::ZeroVector;
    const FVector Center=(ContactPoint(true)+ContactPoint(false))*.5;
    const FVector Goal=Tooth->GetActorLocation()+(Center-Tooth->GetActorLocation()).GetSafeNormal2D()*75;
    const FVector V=GrabbedPlayer->ToothPhysics->CanAct()?GrabbedPlayer->GetVelocity():GrabbedPlayer->GetMesh()->GetPhysicsLinearVelocity(GrabbedPlayer->RigBone(TEXT("body")));
    const float Dt=FMath::Max(.008f,GetWorld()->GetDeltaSeconds());
    return (((Goal-Center)*28+(Tooth->GetVelocity()-V)*8)/(1+8*Dt+28*Dt*Dt)).GetClampedToMaxSize(700);
}
void UMCGripComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,Type,TickFunction); if (!Tooth || !bRigReady) return;
    if (!Tooth->HasAuthority())
        for (auto* Food:{Frame.Food.Get(),Secondary.Food.Get()}) if (IsValid(Food)) Food->UpdateCarryPresentation(Dt);
    FVector DesiredReach=Frame.Food?ReachFor(Frame.Food):FVector::ZeroVector;
    if (Secondary.Food) DesiredReach=(DesiredReach+ReachFor(Secondary.Food))*.5;
    // Adding/removing the other hand changes the centre of the load. Move the
    // torso towards it continuously, including while the last grip fades out.
    ReachOffset=FMath::Lerp(ReachOffset,DesiredReach,1.f-FMath::Exp(-10.f*Dt));
    for (int32 Slot=0;Slot<2;++Slot)
    {
        auto& F=Slot==0?Frame:Secondary; auto* Food=F.Food.Get();
        if (!IsValid(Food)) continue;
        if (Tooth->HasAuthority())
        {
            if (Food->IsDisposed() || !Tooth->ToothPhysics->CanAct() || !Tooth->bHandling) { Food->Release(Tooth); break; }
            // Small items keep their assigned hand. Heavy items can regrip only
            // when the other hand is free, preserving an already held surface point.
            if (!CanCarry(Food) && !Secondary.Food && Now()>NextModeAt)
            {
                const FVector D=Tooth->GetActorTransform().InverseTransformVectorNoScale(Food->Visual->Bounds.Origin-Tooth->GetActorLocation());
                const auto Desired=SelectPose(F.Pose,FMath::RadiansToDegrees(FMath::Atan2(D.Y,D.X)),FVector::DotProduct(InputDirection(),Food->GetActorLocation()-Tooth->GetActorLocation()),Settings);
                if (Desired!=F.Pose)
                {
                    FMCGripFrame Next=F;
                    const bool AddsHand=(!UsesHand(F.Pose,true) && UsesHand(Desired,true)) || (!UsesHand(F.Pose,false) && UsesHand(Desired,false));
                    if (!AddsHand) F.Pose=Desired;
                    else if (FindAnchors(Food,Desired,Next))
                    {
                        if (UsesHand(F.Pose,true) && UsesHand(Desired,true)) { Next.LeftPoint=F.LeftPoint; Next.LeftNormal=F.LeftNormal; }
                        if (UsesHand(F.Pose,false) && UsesHand(Desired,false)) { Next.RightPoint=F.RightPoint; Next.RightNormal=F.RightNormal; }
                        bool Reachable=true;
                        for (int32 I=0;I<2;++I) if (UsesHand(Desired,I==0))
                        {
                            const auto T=Food->Visual->GetComponentTransform();
                            const FVector Normal=T.TransformVectorNoScale(I==0?FVector(Next.LeftNormal):FVector(Next.RightNormal));
                            const FVector Wrist=T.TransformPosition(I==0?FVector(Next.LeftPoint):FVector(Next.RightPoint))-HandRotation(I,Normal).RotateVector(Arms[I].PalmLocal);
                            Reachable&=FVector::Distance(ShoulderPoint(I),Wrist)<=(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch*Settings.DragDistanceScale;
                        }
                        if (Reachable) { F=Next; F.StartedAt=Now(); F.bContact=false; ++F.Serial; }
                    }
                    NextModeAt=Now()+.3; Tooth->ForceNetUpdate();
                }
            }
            float Error=0; bool HandsReady=true;
            for (int32 I=0;I<2;++I) if (UsesHand(F.Pose,I==0))
            {
                const FVector N=Food->Visual->GetComponentTransform().TransformVectorNoScale(I==0?FVector(F.LeftNormal):FVector(F.RightNormal));
                const FVector Wrist=ContactPoint(I==0)-HandRotation(I,N).RotateVector(Arms[I].PalmLocal);
                Error=FMath::Max(Error,float(FVector::Distance(ShoulderPoint(I),Wrist))-(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch*Settings.DragDistanceScale);
                HandsReady&=HandAlpha[I]>.99f;
            }
            const bool Ready=Now()-F.StartedAt>=Settings.ReachSeconds && HandsReady && Error<=Settings.ContactTolerance;
            if (Ready && !F.bContact) { F.bContact=true; Tooth->ForceNetUpdate(); }
            float& Lost=Slot==0?LostContact:SecondaryLostContact;
            Lost=Error>Settings.BreakSlack || !F.bContact?Lost+Dt:0;
            if (Lost>Settings.ReachSeconds+.5f)
            {
                UE_LOG(LogTemp,Verbose,TEXT("MC_GRIP_BREAK food=%s error=%.1f ready=%d"),*Food->GetName(),Error,F.bContact);
                Food->Release(Tooth); break;
            }
            if (Ready && CanCarry(Food) && Food->Phase!=EMCFoodPhase::Carried) Food->BeginCarry(Tooth);
        }
    }
    if (GrabbedPlayer && Tooth->HasAuthority())
    {
        auto* P=GrabbedPlayer.Get();
        const FVector Center=(ContactPoint(true)+ContactPoint(false))*.5;
        const FVector Goal=Tooth->GetActorLocation()+(Center-Tooth->GetActorLocation()).GetSafeNormal2D()*75;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(MCPlayerGrip),false,Tooth); Q.AddIgnoredActor(P); FHitResult Wall;
        if (!Tooth->bHandling || !Tooth->CanWork() || !P->Status->IsAlive() || FVector::Dist(Center,Goal)>120
            || GetWorld()->LineTraceSingleByChannel(Wall,Tooth->GetActorLocation(),Center,ECC_WorldStatic,Q)) ReleasePlayer();
        else if (Now()-PlayerGrabAt>Settings.ReachSeconds)
        {
            if (!P->ToothPhysics->CanAct()) P->GetMesh()->AddForce(PlayerPullAcceleration()*P->ToothPhysics->Settings.Mass,P->RigBone(TEXT("body")));
        }
    }
    PresentationPose=Frame.Pose;
    const float LeanSign=PresentationPose==EMCGripPose::Push?1.f:-1.f;
    const float Effort=Frame.Food && CanCarry(Frame.Food)?.15f:IsReady() && !InputDirection().IsNearlyZero()?FMath::Clamp(Tooth->AnimationEffort+.2f,.2f,1.f):.1f;
    PresentationLean=FMath::Lerp(PresentationLean,LeanSign*Settings.Lean*Effort,1.f-FMath::Exp(-10.f*Dt));
    for (int32 I=0;I<2;++I)
    {
        const auto* F=HandFrame(I==0);
        if (F) { Targets[I]=ContactPoint(I==0); Normals[I]=F->Food->Visual->GetComponentTransform().TransformVectorNoScale(I==0?FVector(F->LeftNormal):FVector(F->RightNormal)).GetSafeNormal(); }
        else if (GrabbedPlayer) { Targets[I]=ContactPoint(I==0); Normals[I]=-Tooth->GetActorForwardVector(); }
        const float Target=(F || GrabbedPlayer) && Tooth->ToothPhysics->CanAct()?1:0;
        HandAlpha[I]=FMath::FInterpConstantTo(HandAlpha[I],Target,Dt,1/(Target>0?Settings.ReachSeconds:Settings.ReleaseSeconds));
        if (!Tooth->ToothPhysics->CanAct()) HandAlpha[I]=0;
    }
    const bool Emote=Tooth->Expression && Tooth->Expression->BodyAlpha()>.001f;
    const bool Swimming=Tooth->AnimationSwim>.05f;
    const bool ToolSwing=!Tooth->AnimationToolOffset.IsNearlyZero();
    Tooth->ToothPhysics->SetGripArms(HandAlpha[0]>.001f || Emote || Swimming || ToolSwing,
        HandAlpha[1]>.001f || Emote || Swimming || ToolSwing || (Tooth->BrushContact && Tooth->BrushContact->IsPresenting()));
}
bool UMCGripComponent::BeginPlayerGrip(AMCToothCharacter* Player)
{
    if (!Tooth || !Tooth->HasAuthority() || !Tooth->CanWork() || !IsValid(Player) || Player==Tooth || Frame.Food || GrabbedPlayer
        || !Player->Status->IsAlive() || FVector::Dist(Tooth->GetActorLocation(),Player->ToothPhysics->PhysicalLocation())>135) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(MCPlayerGrab),false,Tooth); Q.AddIgnoredActor(Player);
    if (GetWorld()->LineTraceSingleByChannel(Hit,Tooth->GetActorLocation(),Player->ToothPhysics->PhysicalLocation(),ECC_WorldStatic,Q)) return false;
    GrabbedPlayer=Player; PlayerGrabAt=Now();
    const FTransform T=Player->GetMesh()->GetSocketTransform(Player->RigBone(TEXT("body")));
    PlayerAnchor=T.InverseTransformPosition(T.GetLocation()+(Tooth->GetActorLocation()-T.GetLocation()).GetSafeNormal()*22);
    Tooth->ForceNetUpdate(); return true;
}
void UMCGripComponent::ReleasePlayer()
{
    if (Tooth && Tooth->HasAuthority()) { GrabbedPlayer=nullptr; Tooth->ForceNetUpdate(); }
}
void UMCGripComponent::ThrowPlayer()
{
    if (!Tooth || !Tooth->HasAuthority() || !GrabbedPlayer) return;
    auto* P=GrabbedPlayer.Get(); ReleasePlayer();
    P->ToothPhysics->ApplyHit(Tooth->GetVelocity()*.6+Tooth->GetActorForwardVector()*340+FVector(0,0,210),P->ToothPhysics->PhysicalLocation());
}
void UMCGripComponent::BuildPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt)
{
    if (!bRigReady || !Tooth || Blend()<.001f || !Tooth->ToothPhysics->CanAct()) return;
    const FTransform MeshWorld=Tooth->GetMesh()->GetComponentTransform(); TArray<FTransform> CS; CS.SetNum(Pose.Num());
    auto Rebuild=[&](){for (int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)>=0?Pose[I]*CS[Ref.GetParentIndex(I)]:Pose[I];};
    Rebuild();
    const int32 Body=Ref.FindBoneIndex(Tooth->RigBone(TEXT("body")));
    if (Body>=0)
    {
        const int32 Parent=Ref.GetParentIndex(Body); FTransform B=CS[Body];
        B.AddToTranslation(MeshWorld.InverseTransformVector(ReachOffset)*Blend());
        const FVector Axis=MeshWorld.InverseTransformVectorNoScale(Tooth->GetActorRightVector());
        const FQuat Tilt(Axis,FMath::DegreesToRadians(PresentationLean*Blend()));
        const FVector Pivot=(CS[Arms[0].Upper].GetLocation()+CS[Arms[1].Upper].GetLocation())*.5+MeshWorld.InverseTransformVector(ReachOffset)*Blend();
        B.SetLocation(Pivot+Tilt.RotateVector(B.GetLocation()-Pivot));
        B.SetRotation((Tilt*B.GetRotation()).GetNormalized());
        Pose[Body]=Parent>=0?B.GetRelativeTransform(CS[Parent]):B; Rebuild();
    }
    for (int32 I=0;I<2;++I)
    {
        const auto& A=Arms[I]; if (HandAlpha[I]<.001f) continue;
        const FQuat WorldRotation=HandRotation(I,Normals[I]);
        const FVector Goal=MeshWorld.InverseTransformPosition(Targets[I]-WorldRotation.RotateVector(A.PalmLocal));
        const FVector Pole=CS[A.Upper].GetLocation()+MeshWorld.InverseTransformVectorNoScale(Tooth->GetActorRightVector()*(I==0?-35:35)-FVector::UpVector*25-Tooth->GetActorForwardVector()*10);
        FTransform Upper=CS[A.Upper],Lower=CS[A.Lower],Hand=CS[A.Hand];
        AnimationCore::SolveTwoBoneIK(Upper,Lower,Hand,Pole,Goal,true,1.,double(Settings.MaxArmStretch*Settings.DragDistanceScale));
        Hand.SetRotation((MeshWorld.GetRotation().Inverse()*WorldRotation).GetNormalized());
        const FTransform Solved[]={Upper,Lower,Hand}; const int32 Bones[]={A.Upper,A.Lower,A.Hand};
        const float Alpha=FMath::SmoothStep(0.f,1.f,HandAlpha[I]);
        // Blend the local chain together so partial reach does not apply the
        // upper arm's correction a second time to the forearm and wrist.
        for (int32 J=0;J<3;++J)
        {
            const int32 Bone=Bones[J],Parent=Ref.GetParentIndex(Bone); FTransform Blended;
            const FTransform Local=Parent<0?Solved[J]:Solved[J].GetRelativeTransform(J>0?Solved[J-1]:CS[Parent]);
            Blended.Blend(Pose[Bone],Local,Alpha); Pose[Bone]=Blended;
        }
        Rebuild();
    }
}
void UMCGripComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCGripComponent,Settings); DOREPLIFETIME(UMCGripComponent,Frame); DOREPLIFETIME(UMCGripComponent,Secondary);
    DOREPLIFETIME(UMCGripComponent,GrabbedPlayer); DOREPLIFETIME(UMCGripComponent,PlayerAnchor); DOREPLIFETIME(UMCGripComponent,PlayerGrabAt);
}
