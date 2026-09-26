#include "MCGripComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/SkeletalMesh.h"
#include "Net/UnrealNetwork.h"
#include "TwoBoneIK.h"

void FMCGripSettings::Sanitize()
{
    auto C=[](float V,float D,float A,float B){return FMath::Clamp(FMath::IsFinite(V)?V:D,A,B);};
    ReachSeconds=C(ReachSeconds,.28f,.12f,.6f); ReleaseSeconds=C(ReleaseSeconds,.22f,.1f,.6f);
    BodyReach=C(BodyReach,18,0,24); CrouchDepth=C(CrouchDepth,25,0,30); ContactTolerance=C(ContactTolerance,6,0,10);
    MaxArmStretch=C(MaxArmStretch,1.08f,1,1.15f); BreakSlack=C(BreakSlack,14,2,20);
    PalmLength=C(PalmLength,7,2,12); PalmThickness=C(PalmThickness,2.5f,0,6);
    FrontAngle=C(FrontAngle,55,30,70); RearAngle=C(RearAngle,130,110,160); AngleHysteresis=C(AngleHysteresis,8,2,15);
    DriveForce=C(DriveForce,17000,1000,40000); Spring=C(Spring,750,100,1500); Damping=C(Damping,95,10,250); Lean=C(Lean,10,0,20);
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
    Tooth->OnCharacterMovementUpdated.AddDynamic(this,&UMCGripComponent::ConstrainMovement);
}
float UMCGripComponent::Now() const
{
    const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
void UMCGripComponent::OnRep_Settings() { Settings.Sanitize(); CacheRig(); }
FVector UMCGripComponent::ReachFor(const AMCFoodActor* Food) const
{
    FVector Reach=(Food->Visual->Bounds.Origin-Tooth->GetActorLocation()).GetSafeNormal2D()*Settings.BodyReach;
    const float ShoulderZ=Tooth->GetActorTransform().TransformPosition((Arms[0].Shoulder+Arms[1].Shoulder)*.5).Z;
    Reach.Z=FMath::Clamp(float(Food->Visual->Bounds.GetBox().Max.Z+12-ShoulderZ),-Settings.CrouchDepth,0.f);
    return Reach;
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
        if (FVector::Distance(Shoulder,Wrist)>(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch)
        { DebugFailure=FString::Printf(TEXT("Hand %d needs %.1f, reach %.1f; shoulder %s contact %s"),I,FVector::Distance(Shoulder,Wrist),(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch,*Shoulder.ToCompactString(),*Hit.ImpactPoint.ToCompactString()); return false; }
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MCGripLOS),false,Tooth); Params.AddIgnoredActor(Food);
        FHitResult Obstacle;
        if (GetWorld()->LineTraceSingleByChannel(Obstacle,Shoulder,Hit.ImpactPoint,ECC_Visibility,Params)) { DebugFailure=TEXT("Contact occluded by ")+GetNameSafe(Obstacle.GetActor()); return false; }
        const FTransform T=Food->Visual->GetComponentTransform();
        if (I==0) { Result.LeftPoint=T.InverseTransformPosition(Hit.ImpactPoint); Result.LeftNormal=T.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal(); }
        else { Result.RightPoint=T.InverseTransformPosition(Hit.ImpactPoint); Result.RightNormal=T.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal(); }
    }
    Result.Pose=Mode; return true;
}
bool UMCGripComponent::BeginGrip(AMCFoodActor* Food)
{
    if (!Tooth || !Tooth->HasAuthority() || !Food || !Tooth->CanWork() || Now()<NextAttemptAt) return false;
    const FVector D=Tooth->GetActorTransform().InverseTransformVectorNoScale(Food->Visual->Bounds.Origin-Tooth->GetActorLocation());
    const float Angle=FMath::RadiansToDegrees(FMath::Atan2(D.Y,D.X));
    FMCGripFrame NewFrame;
    const auto Mode=SelectPose(EMCGripPose::FrontPull,Angle,0,Settings);
    if (!FindAnchors(Food,Mode,NewFrame))
    {
        // A rotated edge or small fragment may expose only one reachable contact.
        // Keep that natural grip instead of rejecting the whole object behind the capsule.
        if (!UsesHand(Mode,true) || !UsesHand(Mode,false)) return false;
        const auto Near=D.Y<0?EMCGripPose::LeftHand:EMCGripPose::RightHand;
        const auto Other=Near==EMCGripPose::LeftHand?EMCGripPose::RightHand:EMCGripPose::LeftHand;
        if (!FindAnchors(Food,Near,NewFrame) && !FindAnchors(Food,Other,NewFrame)) return false;
    }
    NewFrame.Food=Food; NewFrame.StartedAt=Now(); NewFrame.RestOffset=Food->GetActorLocation()-Tooth->GetActorLocation();
    NewFrame.Serial=Frame.Serial+1; Frame=NewFrame; LostContact=0; NextModeAt=Now()+.35;
    Tooth->ForceNetUpdate(); return true;
}
void UMCGripComponent::EndGrip()
{
    if (!Tooth || !Tooth->HasAuthority()) return;
    Frame.Food=nullptr; Frame.bContact=false; ++Frame.Serial; LostContact=0;
    NextAttemptAt=Now()+.25f; Tooth->ForceNetUpdate();
}
FVector UMCGripComponent::ContactPoint(bool Left) const
{
    return IsValid(Frame.Food)?Frame.Food->Visual->GetComponentTransform().TransformPosition(Left?FVector(Frame.LeftPoint):FVector(Frame.RightPoint)):Targets[Left?0:1];
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
    for (int32 I=0;I<2;++I) if (UsesHand(Frame.Pose,I==0)) Error=FMath::Max(Error,float(FVector::Distance(PalmPoint(I==0),ContactPoint(I==0))));
    return Error;
}
FVector UMCGripComponent::InputDirection() const
{
    if (!Tooth) return FVector::ZeroVector;
    const FVector A=Tooth->GetCharacterMovement()->GetCurrentAcceleration();
    return A.SizeSquared2D()>1?A.GetSafeNormal2D():Tooth->GetPendingMovementInputVector().GetSafeNormal2D();
}
FVector UMCGripComponent::DriveForce() const
{
    if (!IsReady()) return FVector::ZeroVector;
    const FVector Error=Tooth->GetActorLocation()+FVector(Frame.RestOffset)-Frame.Food->GetActorLocation();
    // Tension builds over centimetres, not the former metres-long invisible spring.
    const FVector Intent=InputDirection();
    // No motor input means damping only. A rotating contact must not turn the root's
    // old rest offset into a self-propelling spring.
    const float Tension=FMath::Max(0.f,float(FVector::DotProduct(Error,Intent)));
    FVector Force=Intent*(Settings.DriveForce+Tension*Settings.Spring)-Frame.Food->GetVelocity()*Settings.Damping;
    Force.Z=FMath::Clamp(Force.Z,-Settings.DriveForce*.15f,Settings.DriveForce*.15f);
    return Force.GetClampedToMaxSize(Settings.DriveForce*1.2f);
}
void UMCGripComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,Type,TickFunction); if (!Tooth || !bRigReady) return;
    auto* Food=Frame.Food.Get(); const bool Active=IsValid(Food) && !Food->IsDisposed() && Tooth->ToothPhysics->CanAct();
    if (Active)
    {
        if (!bOrientationSaved) { bPreviousOrient=Tooth->GetCharacterMovement()->bOrientRotationToMovement; bOrientationSaved=true; }
        Tooth->GetCharacterMovement()->bOrientRotationToMovement=false;
        ReachOffset=ReachFor(Food);
        // Follow the finished rigid-body step before solving the rendered hands.
        ConstrainMovement(0,Tooth->GetActorLocation(),Tooth->GetVelocity());
        if (Frame.Food!=Food) return;
        if (Tooth->HasAuthority())
        {
            const FVector Delta=Food->Visual->Bounds.Origin-Tooth->GetActorLocation();
            const FVector D=Tooth->GetActorTransform().InverseTransformVectorNoScale(Delta);
            const float Approach=FVector::DotProduct(InputDirection(),Delta.GetSafeNormal2D())*100;
            const auto Desired=SelectPose(Frame.Pose,FMath::RadiansToDegrees(FMath::Atan2(D.Y,D.X)),Approach,Settings);
            if (Desired!=Frame.Pose && Now()>NextModeAt)
            {
                FMCGripFrame Next=Frame;
                // Push/pull share the same two fixed contacts; changing hand count requires a regrip.
                if (UsesHand(Desired,true)==UsesHand(Frame.Pose,true) && UsesHand(Desired,false)==UsesHand(Frame.Pose,false))
                    Frame.Pose=Desired;
                else if (FindAnchors(Food,Desired,Next)) { Frame=Next; Frame.StartedAt=Now(); Frame.bContact=false; ++Frame.Serial; }
                NextModeAt=Now()+.3; Tooth->ForceNetUpdate();
            }
            float ReachError=0;
            for (int32 I=0;I<2;++I) if (UsesHand(Frame.Pose,I==0))
            {
                const FVector N=Food->Visual->GetComponentTransform().TransformVectorNoScale(I==0?FVector(Frame.LeftNormal):FVector(Frame.RightNormal));
                const FVector Wrist=ContactPoint(I==0)-HandRotation(I,N).RotateVector(Arms[I].PalmLocal);
                ReachError=FMath::Max(ReachError,float(FVector::Distance(ShoulderPoint(I),Wrist))-(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch);
            }
            // Previous-frame bone transforms are stale after physics; validate the IK reach here.
            // The smooth reach finishes before force can engage.
            const bool Ready=Now()-Frame.StartedAt>=Settings.ReachSeconds && Blend()>.99f && ReachError<=Settings.ContactTolerance;
            if (Frame.bContact!=Ready) { Frame.bContact=Ready; Tooth->ForceNetUpdate(); }
            LostContact=Ready?0:LostContact+Dt;
            if (LostContact>Settings.ReachSeconds+.65f || !Tooth->bHandling)
            {
#if !UE_BUILD_SHIPPING
                if (FParse::Param(FCommandLine::Get(),TEXT("MCGripTest"))) UE_LOG(LogTemp,Display,TEXT("MC_GRIP_LOST %s error=%.1f lost=%.2f handling=%d"),*Tooth->GetName(),ContactError(),LostContact,Tooth->bHandling);
#endif
                Food->Release(Tooth); return; }
        }
        PresentationPose=Frame.Pose;
        for (int32 I=0;I<2;++I)
        {
            Targets[I]=ContactPoint(I==0);
            Normals[I]=Food->Visual->GetComponentTransform().TransformVectorNoScale(I==0?FVector(Frame.LeftNormal):FVector(Frame.RightNormal)).GetSafeNormal();
        }
    }
    else if (bOrientationSaved) { Tooth->GetCharacterMovement()->bOrientRotationToMovement=bPreviousOrient; bOrientationSaved=false; }
    for (int32 I=0;I<2;++I)
    {
        const float Target=Active && UsesHand(Frame.Pose,I==0)?1:0;
        HandAlpha[I]=FMath::FInterpConstantTo(HandAlpha[I],Target,Dt,1/(Target>0?Settings.ReachSeconds:Settings.ReleaseSeconds));
        if (!Tooth->ToothPhysics->CanAct()) HandAlpha[I]=0;
    }
    Tooth->ToothPhysics->SetGripArms(HandAlpha[0]>.001f,HandAlpha[1]>.001f);
}
void UMCGripComponent::ConstrainMovement(float Dt,FVector OldLocation,FVector OldVelocity)
{
    if (bConstraining || !bRigReady || !IsValid(Frame.Food) || !Tooth->ToothPhysics->CanAct()
        || (!Tooth->HasAuthority() && !Tooth->IsLocallyControlled()) || !Tooth->GetCharacterMovement()->IsMovingOnGround()) return;
    FVector Candidate=Tooth->GetActorLocation();
    for (int32 Pass=0;Pass<3;++Pass) for (int32 I=0;I<2;++I) if (UsesHand(Frame.Pose,I==0))
    {
        const FVector N=Frame.Food->Visual->GetComponentTransform().TransformVectorNoScale(I==0?FVector(Frame.LeftNormal):FVector(Frame.RightNormal));
        const FVector Wrist=ContactPoint(I==0)-HandRotation(I,N).RotateVector(Arms[I].PalmLocal);
        const FVector Shoulder=ShoulderPoint(I)+Candidate-Tooth->GetActorLocation();
        const float Reach=(Arms[I].UpperLength+Arms[I].LowerLength)*Settings.MaxArmStretch-1;
        const float Dz=Shoulder.Z-Wrist.Z;
        if (FMath::Abs(Dz)>Reach) continue;
        const float R=FMath::Sqrt(Reach*Reach-Dz*Dz);
        const FVector D=Shoulder-Wrist;
        if (D.Size2D()>R) Candidate-=D.GetSafeNormal2D()*(D.Size2D()-R);
    }
    const FVector Correction=Candidate-Tooth->GetActorLocation();
    // Clamp attempted walking even on a long frame. Reserve the break threshold for
    // separation beyond this frame's movement, rather than confusing it with a teleport.
    const float Travel=Dt>0?FVector::Dist2D(Tooth->GetActorLocation(),OldLocation)
        :Frame.Food->GetVelocity().Size2D()*FMath::Min(GetWorld()->GetDeltaSeconds(),.25f);
    if (Correction.Size2D()>Settings.BreakSlack+20+FMath::Min(Travel,80.f))
    {
        if (Tooth->HasAuthority())
        {
#if !UE_BUILD_SHIPPING
            if (FParse::Param(FCommandLine::Get(),TEXT("MCGripTest"))) UE_LOG(LogTemp,Display,TEXT("MC_GRIP_STRAIN %s correction=%.1f"),*Tooth->GetName(),Correction.Size2D());
#endif
            Frame.Food->Release(Tooth);
        }
        return;
    }
    if (!Correction.IsNearlyZero(.01f))
    {
        TGuardValue<bool> Guard(bConstraining,true); FHitResult Hit;
        Tooth->SetActorLocation(Candidate,true,&Hit);
        if (Dt>SMALL_NUMBER)
        {
            auto* Move=Tooth->GetCharacterMovement(); const FVector V=(Tooth->GetActorLocation()-OldLocation)/Dt;
            Move->Velocity.X=V.X; Move->Velocity.Y=V.Y;
        }
    }
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
        const float Sign=PresentationPose==EMCGripPose::Push?1.f:-1.f;
        const float Effort=IsReady() && !InputDirection().IsNearlyZero()?FMath::Clamp(Frame.Food->Settings.Mass/28.f,.2f,1.f):.1f;
        const FVector Axis=MeshWorld.InverseTransformVectorNoScale(Tooth->GetActorRightVector());
        const FQuat Tilt(Axis,FMath::DegreesToRadians(Sign*Settings.Lean*Effort*Blend()));
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
        AnimationCore::SolveTwoBoneIK(Upper,Lower,Hand,Pole,Goal,true,1.,double(Settings.MaxArmStretch));
        Hand.SetRotation((MeshWorld.GetRotation().Inverse()*WorldRotation).GetNormalized());
        const FTransform Solved[]={Upper,Lower,Hand}; const int32 Bones[]={A.Upper,A.Lower,A.Hand};
        const float Alpha=FMath::SmoothStep(0.f,1.f,HandAlpha[I]);
        // Blend in component space, then rebuild local transforms in parent order.
        for (int32 J=0;J<3;++J)
        {
            const int32 Bone=Bones[J],Parent=Ref.GetParentIndex(Bone); FTransform Blended;
            Blended.Blend(CS[Bone],Solved[J],Alpha); Pose[Bone]=Parent>=0?Blended.GetRelativeTransform(CS[Parent]):Blended; Rebuild();
        }
    }
}
void UMCGripComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCGripComponent,Settings); DOREPLIFETIME(UMCGripComponent,Frame);
}
