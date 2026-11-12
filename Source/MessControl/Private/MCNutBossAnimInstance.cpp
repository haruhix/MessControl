#include "MCNutBossAnimInstance.h"

#include "MCNutBoss.h"
#include "MCSecondaryAnimationSolver.h"
#include "MCAnimationComparisonSubsystem.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "TwoBoneIK.h"

namespace
{
FCompactPoseBoneIndex PoseBone(const FCompactPose& Pose,const FMCNutBossAnimationSnapshot& Input,EMCNutPoseBone Bone)
{
    const int32 Index=Input.Bones[int32(Bone)];
    return Index==INDEX_NONE?FCompactPoseBoneIndex(INDEX_NONE):Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
}

FTransform ComponentBone(const FCompactPose& Pose,FCompactPoseBoneIndex Bone)
{
    FTransform Result=FTransform::Identity;
    const FBoneContainer& Bones=Pose.GetBoneContainer();
    // Export rigs have a shallow hierarchy. The bound also protects a malformed custom rig.
    for(int32 Depth=0;Bone.GetInt()!=INDEX_NONE && Depth<64;++Depth) {
        Result=Result*Pose[Bone]; Bone=Bones.GetParentBoneIndex(Bone);
    }
    return Result;
}

void RotateComponent(FCompactPose& Pose,FCompactPoseBoneIndex Bone,const FRotator& Rotation,const FQuat& ActorToComponent)
{
    if(Bone.GetInt()==INDEX_NONE) return;
    const auto Parent=Pose.GetBoneContainer().GetParentBoneIndex(Bone);
    const FQuat ParentRotation=ComponentBone(Pose,Parent).GetRotation();
    const FQuat ComponentDelta=ActorToComponent*Rotation.Quaternion()*ActorToComponent.Inverse();
    const FQuat Delta=ParentRotation.Inverse()*ComponentDelta*ParentRotation;
    Pose[Bone].SetRotation((Delta*Pose[Bone].GetRotation()).GetNormalized());
}

// The club is skinned to the right hand. The chain pays out from the grip beside the glove.
const FVector ChainGripOffset(0,4,0);

int32 FindRigBone(const USkeletalMeshComponent& Mesh,FName Name)
{
    const int32 Index=Mesh.GetBoneIndex(Name);
    if(Index!=INDEX_NONE) return Index;
    FString Alternate=Name.ToString();
    int32 SideSeparator=INDEX_NONE;
    if(Alternate.FindLastChar(TEXT('.'),SideSeparator)) Alternate[SideSeparator]=TEXT('_');
    else if(Alternate.FindLastChar(TEXT('_'),SideSeparator)) Alternate[SideSeparator]=TEXT('.');
    return Mesh.GetBoneIndex(FName(*Alternate));
}

enum class ENutChainPoseStage : uint8 { None, Windup, Launch, Pull, Reel, Impact };

struct FNutChainPose
{
    FVector Grip=FVector::ZeroVector;
    FVector Body=FVector::ZeroVector;
    FRotator Torso=FRotator::ZeroRotator;
    FRotator Head=FRotator::ZeroRotator;
    FRotator Shield=FRotator::ZeroRotator;
    FRotator Wrist=FRotator::ZeroRotator;
    float GripWeight=0.f;
};

FNutChainPose BlendChainPose(const FNutChainPose& From,const FNutChainPose& To,float Alpha)
{
    return {FMath::Lerp(From.Grip,To.Grip,Alpha),FMath::Lerp(From.Body,To.Body,Alpha),
        FMath::Lerp(From.Torso,To.Torso,Alpha),FMath::Lerp(From.Head,To.Head,Alpha),
        FMath::Lerp(From.Shield,To.Shield,Alpha),FMath::Lerp(From.Wrist,To.Wrist,Alpha),
        FMath::Lerp(From.GripWeight,To.GripWeight,Alpha)};
}

void WriteLimbPose(FCompactPose& Pose,FCompactPoseBoneIndex UpperBone,FCompactPoseBoneIndex LowerBone,
    FCompactPoseBoneIndex EndBone,const FTransform& Upper,const FTransform& Lower,const FTransform& End)
{
    const auto Parent=Pose.GetBoneContainer().GetParentBoneIndex(UpperBone);
    Pose[UpperBone]=Upper.GetRelativeTransform(ComponentBone(Pose,Parent));
    Pose[LowerBone]=Lower.GetRelativeTransform(Upper);
    Pose[EndBone]=End.GetRelativeTransform(Lower);
}

struct FNutChainLeg
{
    int32 Bones[3]={INDEX_NONE,INDEX_NONE,INDEX_NONE};
};

FCompactPoseBoneIndex ChainCompactBone(const FCompactPose& Pose,int32 Bone)
{
    return Bone==INDEX_NONE?FCompactPoseBoneIndex(INDEX_NONE):Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(Bone));
}

bool HasDirectLimbChain(const FCompactPose& Pose,FCompactPoseBoneIndex Upper,FCompactPoseBoneIndex Lower,FCompactPoseBoneIndex End)
{
    return Upper.GetInt()!=INDEX_NONE && Lower.GetInt()!=INDEX_NONE && End.GetInt()!=INDEX_NONE
        && Pose.GetBoneContainer().GetParentBoneIndex(Lower)==Upper && Pose.GetBoneContainer().GetParentBoneIndex(End)==Lower;
}

void PlantChainFoot(FCompactPose& Pose,const FNutChainLeg& Leg,const FTransform& Foot)
{
    const auto Thigh=ChainCompactBone(Pose,Leg.Bones[0]);
    const auto Shin=ChainCompactBone(Pose,Leg.Bones[1]);
    const auto Ankle=ChainCompactBone(Pose,Leg.Bones[2]);
    if(!HasDirectLimbChain(Pose,Thigh,Shin,Ankle)) return;
    FTransform Upper=ComponentBone(Pose,Thigh),Lower=ComponentBone(Pose,Shin),End=ComponentBone(Pose,Ankle);
    AnimationCore::SolveTwoBoneIK(Upper,Lower,End,Lower.GetLocation(),Foot.GetLocation(),false,1.,1.);
    End.SetRotation(Foot.GetRotation());
    WriteLimbPose(Pose,Thigh,Shin,Ankle,Upper,Lower,End);
}

void MatchCastHand(FCompactPose& Pose,const FMCNutBossAnimationSnapshot& Input,float Weight)
{
    if(Weight<=.01f) return;
    const auto Arm=PoseBone(Pose,Input,EMCNutPoseBone::RightArm);
    const auto Forearm=PoseBone(Pose,Input,EMCNutPoseBone::RightForearm);
    const auto Hand=PoseBone(Pose,Input,EMCNutPoseBone::RightHand);
    if(Arm.GetInt()==INDEX_NONE || Forearm.GetInt()==INDEX_NONE || Hand.GetInt()==INDEX_NONE) return;
    FTransform Upper=ComponentBone(Pose,Arm),Lower=ComponentBone(Pose,Forearm),End=ComponentBone(Pose,Hand);
    const FTransform BeforeUpper=Upper,BeforeLower=Lower,BeforeEnd=End;
    const FVector Target=Input.CastEmitter-End.TransformVectorNoScale(Input.HandTipOffset);
    const FVector Elbow=Lower.GetLocation()+Input.ActorToComponent.RotateVector(FVector(0,-40,12));
    // No stretching: the authored shoulder and casting palm retain a natural reach.
    AnimationCore::SolveTwoBoneIK(Upper,Lower,End,Elbow,Target,false,1.,1.);
    Upper.Blend(BeforeUpper,Upper,Weight); Lower.Blend(BeforeLower,Lower,Weight); End.Blend(BeforeEnd,End,Weight);
    const auto Parent=Pose.GetBoneContainer().GetParentBoneIndex(Arm);
    Pose[Arm]=Upper.GetRelativeTransform(ComponentBone(Pose,Parent));
    Pose[Forearm]=Lower.GetRelativeTransform(Upper); Pose[Hand]=End.GetRelativeTransform(Lower);
}

void ApplyMageGravityPose(FCompactPose& Pose,const FMCNutBossAnimationSnapshot& Input)
{
    const bool bLeft=Input.GravityLeftGesture>=0.f;
    const float Progress=bLeft?Input.GravityLeftGesture:Input.GravityRightGesture;
    const float Weight=FMath::SmoothStep(0.f,.20f,Progress)*(1.f-FMath::SmoothStep(.89f,1.f,Progress));
    if(Weight<=KINDA_SMALL_NUMBER) return;

    const auto Arm=PoseBone(Pose,Input,bLeft?EMCNutPoseBone::LeftArm:EMCNutPoseBone::RightArm);
    const auto Forearm=PoseBone(Pose,Input,bLeft?EMCNutPoseBone::LeftForearm:EMCNutPoseBone::RightForearm);
    const auto Hand=PoseBone(Pose,Input,bLeft?EMCNutPoseBone::LeftHand:EMCNutPoseBone::RightHand);
    if(!HasDirectLimbChain(Pose,Arm,Forearm,Hand)) return;

    const float Side=bLeft?-1.f:1.f;
    // The shoulder leads the lift and the elbow leads the wrist on the release at .82.
    const float ShoulderLift=FMath::SmoothStep(.16f,.56f,Progress);
    const float Lift=FMath::SmoothStep(.22f,.65f,Progress);
    const float Brace=FMath::SmoothStep(.66f,.78f,Progress);
    const float ElbowDrop=FMath::SmoothStep(.76f,.85f,Progress);
    const float WristDrop=FMath::SmoothStep(.795f,.885f,Progress);
    const float PalmFlip=FMath::SmoothStep(.755f,.900f,Progress);
    const float Raise=ShoulderLift*(1.f-ElbowDrop);
    const auto ShoulderBone=Pose.GetBoneContainer().GetParentBoneIndex(Arm);
    RotateComponent(Pose,ShoulderBone,FRotator(Weight*(6.f*Raise-4.f*ElbowDrop),0.f,-Side*Weight*(4.f+6.f*Raise)),Input.ActorToComponent);
    RotateComponent(Pose,PoseBone(Pose,Input,EMCNutPoseBone::Head),
        FRotator((Lift*11.f*(1.f-WristDrop)-WristDrop*8.f)*Weight,Side*Weight*11.f,0.f),Input.ActorToComponent);

    FTransform Upper=ComponentBone(Pose,Arm),Lower=ComponentBone(Pose,Forearm),End=ComponentBone(Pose,Hand);
    const FVector Shoulder=Upper.GetLocation();
    const double Reach=FVector::Distance(Shoulder,Lower.GetLocation())+FVector::Distance(Lower.GetLocation(),End.GetLocation());
    // Positions are fractions of this rig's arm length in actor-facing axes.
    const FVector Gather(.27,-.13*Side,-.45),Scoop(.76,.32*Side,-.22),Raised(.36,.34*Side,.69);
    const FVector RaiseArc=FMath::Lerp(FMath::Lerp(Gather,Scoop,Lift),FMath::Lerp(Scoop,Raised,Lift),Lift);
    const FVector Held=RaiseArc+FVector(-.10,.06*Side,.07)*Brace;
    const FVector Strike(.70,.49*Side,.04),Released(.37,.45*Side,-.58);
    const FVector Offset=FMath::Lerp(FMath::Lerp(Held,Strike,WristDrop),FMath::Lerp(Strike,Released,WristDrop),WristDrop);
    const FVector Goal=Shoulder+Input.ActorToComponent.RotateVector(Offset*Reach);
    const FVector ElbowOffset(.11+.23*Raise,(.60+.15*Raise+.15*ElbowDrop)*Side,-.62+.25*Raise-.09*ElbowDrop);
    const FVector ElbowGoal=Shoulder+Input.ActorToComponent.RotateVector(ElbowOffset*Reach);
    const FVector Elbow=FMath::Lerp(Lower.GetLocation(),ElbowGoal,Weight);
    AnimationCore::SolveTwoBoneIK(Upper,Lower,End,Elbow,FMath::Lerp(End.GetLocation(),Goal,Weight),false,1.,1.);

    const float Uncup=FMath::SmoothStep(.83f,.91f,Progress);
    const FVector GatherPalm(.15,-.85*Side,.52),HeldPalm(-.35,-.15*Side,.92);
    const FVector TurnPalm(.24,.95*Side,.10),DownPalm(.24,.10*Side,-.96);
    const float PalmTurn=PI*PalmFlip;
    const FVector Palm=FMath::Lerp(FMath::Lerp(GatherPalm,HeldPalm,Lift),DownPalm,.5f-.5f*FMath::Cos(PalmTurn))
        +TurnPalm*FMath::Sin(PalmTurn);
    const FVector DigitDirection(.91,(.26+.26*WristDrop)*Side,-.39+.75*Lift*(1.f-WristDrop)-.14*WristDrop);
    const FVector ForearmAxis=(End.GetLocation()-Lower.GetLocation()).GetSafeNormal();
    const FVector Digits=FMath::Lerp(Input.ActorToComponent.RotateVector(DigitDirection),ForearmAxis,.3).GetSafeNormal();
    // The imported Wizard's fingers extend along -Y and the palm faces -Z.
    const FQuat HandTarget=FRotationMatrix::MakeFromYZ(-Digits,-Input.ActorToComponent.RotateVector(Palm)).ToQuat();
    const FQuat HandRotation=FQuat::Slerp(End.GetRotation(),HandTarget,Weight).GetNormalized();
    // Drive forearm roll continuously across the palm turn without changing the IK joint positions.
    const double ForearmRoll=Side*Weight*FMath::Lerp(-10.f-25.f*Lift,27.f,PalmFlip);
    const FQuat ForearmTwist(ForearmAxis,FMath::DegreesToRadians(ForearmRoll));
    Lower.SetRotation((ForearmTwist*Lower.GetRotation()).GetNormalized());
    End.SetRotation(HandRotation);
    WriteLimbPose(Pose,Arm,Forearm,Hand,Upper,Lower,End);

    static const FName DigitsBySide[2][3]={{TEXT("thumb1_l"),TEXT("ring1_l"),TEXT("pinky1_l")},
        {TEXT("thumb1_r"),TEXT("ring1_r"),TEXT("pinky1_r")}};
    const float CurlDegrees=Weight*(18.f+13.f*Brace-25.f*Uncup);
    const FVector CurlAxis=HandRotation.GetAxisX();
    const FBoneContainer& Bones=Pose.GetBoneContainer();
    for(const FName DigitName:DigitsBySide[bLeft?0:1]) {
        const auto Digit=ChainCompactBone(Pose,Bones.GetPoseBoneIndexForBoneName(DigitName));
        if(Digit.GetInt()==INDEX_NONE) continue;
        const FQuat ParentRotation=ComponentBone(Pose,Bones.GetParentBoneIndex(Digit)).GetRotation();
        const float DigitCurl=DigitName==DigitsBySide[bLeft?0:1][0]?.45f*CurlDegrees:CurlDegrees;
        const FQuat Curl(ParentRotation.UnrotateVector(CurlAxis),FMath::DegreesToRadians(DigitCurl));
        Pose[Digit].SetRotation((Curl*Pose[Digit].GetRotation()).GetNormalized());
    }
}

void ApplyMageHoverLegs(FCompactPose& Pose,const FMCNutBossAnimationSnapshot& Input,float Weight,
    float ForwardSpeed,float SideSpeed)
{
    if(Weight<=.001f) return;

    static const FName Legs[2][3]={{TEXT("thigh_stretch_l"),TEXT("leg_stretch_l"),TEXT("foot_l")},
        {TEXT("thigh_stretch_r"),TEXT("leg_stretch_r"),TEXT("foot_r")}};
    const FBoneContainer& Bones=Pose.GetBoneContainer();
    const float Glide=FMath::Min(1.f,FMath::Sqrt(FMath::Square(ForwardSpeed)+FMath::Square(SideSpeed)));
    const double Phase=Input.ServerTime*1.9;
    for(int32 Side=0;Side<2;++Side) {
        const auto Thigh=ChainCompactBone(Pose,Bones.GetPoseBoneIndexForBoneName(Legs[Side][0]));
        const auto Shin=ChainCompactBone(Pose,Bones.GetPoseBoneIndexForBoneName(Legs[Side][1]));
        const auto Foot=ChainCompactBone(Pose,Bones.GetPoseBoneIndexForBoneName(Legs[Side][2]));
        if(!HasDirectLimbChain(Pose,Thigh,Shin,Foot)) continue;

        const float Drift=float(FMath::Sin(Phase+Side*.85));
        // Knees stay soft and the ankles hang behind, with a small lag as the body glides.
        RotateComponent(Pose,Thigh,FRotator(Weight*(6.f-7.f*ForwardSpeed+2.f*Drift),0.f,-3.f*Weight*SideSpeed),Input.ActorToComponent);
        RotateComponent(Pose,Shin,FRotator(-Weight*(19.f+6.f*Glide+2.f*Drift),0.f,0.f),Input.ActorToComponent);
        RotateComponent(Pose,Foot,FRotator(-Weight*(6.f+2.f*Glide),0.f,0.f),Input.ActorToComponent);
    }
}

struct FMCNutBossAnimProxy final : FAnimInstanceProxy
{
    explicit FMCNutBossAnimProxy(UAnimInstance* Instance):FAnimInstanceProxy(Instance) {}
protected:
    virtual void CacheBones() override
    {
        const bool bResetPose=bBoneCachesInvalidated;
        FAnimInstanceProxy::CacheBones();
        if(bResetPose) {
            // A new compact-bone layout cannot reuse transforms indexed by the old LOD.
            LastPose.Reset(); PreviousPose.Reset(); bNewTransition=false;
            bLastPoseHasChainScale=bPreviousPoseHasChainScale=false;
            bBoneCachesInvalidated=false;
        }
    }

    virtual void PreUpdate(UAnimInstance* Instance,float Dt) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Dt);
        Input=FMCNutBossAnimationSnapshot();
        if(auto* Mesh=Instance->GetSkelMeshComponent()) {
            if(const auto* Boss=Cast<AMCNutBoss>(Mesh->GetOwner())) {
                Boss->BuildAnimationSnapshot(Mesh,Input);
                UpdateChainPose(*Boss,*Mesh,Dt);
            }
            TArray<FName> MotionBones;
            for(int32 Role=0;Role<2;++Role) if(Input.Bones[Role]!=INDEX_NONE) MotionBones.Add(Mesh->GetBoneName(Input.Bones[Role]));
            SecondaryMotion.PreUpdate(Instance,*this,Dt,MotionBones,Input.Death<.01f && !Input.bAuthoredChainThrow);
            if(auto* Comparison=Instance->GetWorld()->GetSubsystem<UMCAnimationComparisonSubsystem>())
                Comparison->RecordSample(Mesh,Comparison->GetMode(),SecondaryMotion.GetActiveBoneCount(),SecondaryMotion.GetEvaluationCount(),
                    SecondaryMotion.GetLastMaxAngleDegrees(),SecondaryMotion.GetLastMaxTranslation());
        }
    }

    virtual void Update(float Dt) override
    {
        FAnimInstanceProxy::Update(Dt);
        const float Step=FMath::Max(0.f,Dt);
        auto Smooth=[Step](float Current,float Target,float Rate){return FMath::Lerp(Current,Target,1-FMath::Exp(-Rate*Step));};
        AimYaw=Smooth(AimYaw,Input.AimYaw,12);
        AimPitch=Smooth(AimPitch,Input.AimPitch,12);
        ForwardSpeed=Smooth(ForwardSpeed,Input.ForwardSpeed,14);
        SideSpeed=Smooth(SideSpeed,Input.SideSpeed,14);
        Guard=Smooth(Guard,Input.Guard,18);
        Airborne=Smooth(Airborne,Input.Airborne,24);
        // Repeated damage restarts the server pulse at zero; retain a short,
        // continuous recoil instead of snapping the arms and torso back to rest.
        Hit=Smooth(Hit,Input.Hit,35);
        ShieldHit=Smooth(ShieldHit,Input.ShieldHit,35);
        HitDirection=FMath::Lerp(HitDirection,Input.HitDirection,1-FMath::Exp(-25.f*Step));
        if(Input.bMoving!=bWasMoving) {
            bWasMoving=Input.bMoving;MovementFrom=MovementAlpha;MovementAge=0;
        }
        MovementAge+=Step;
        MovementAlpha=FMath::Lerp(MovementFrom,bWasMoving?1.f:0.f,
            FMath::SmoothStep(0.f,Input.BlendSeconds,MovementAge));
        if(Input.VisualKey!=LastKey) {LastKey=Input.VisualKey;bNewTransition=true;TransitionAge=Step;}
        // Pose samples use server time. The blend must also consume the full
        // elapsed frame, including skipped or slow animation updates.
        else TransitionAge+=Step;
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        FAnimationPoseData Result(Output);
        float Sum=0,Authored=0,Hover=0;
        for(const FMCNutBossClipSample& Sample:Input.Samples) {
            const float Weight=Sample.Weight*(Sample.LocomotionMode==1?1-MovementAlpha:Sample.LocomotionMode==2?MovementAlpha:1.f);
            if(!Sample.Clip || Weight<=.001f) continue;
            FPoseContext SamplePose(Output); SamplePose.ResetToRefPose();
            FAnimationPoseData SampleData(SamplePose);
            Sample.Clip->GetAnimationPose(SampleData,FAnimExtractContext(Sample.Seconds,false,{},Sample.bLoop));
            if(!Input.bMage && !Sample.bAllowTankScale)
                for(auto Bone:SamplePose.Pose.ForEachBoneIndex())
                    SamplePose.Pose[Bone].SetScale3D(SamplePose.Pose.GetBoneContainer().GetRefPoseTransform(Bone).GetScale3D());
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,SampleData,Sum/(Sum+Weight)); Sum+=Weight;
            if(!Input.bMage && Sample.Clip->EvaluateCurveData(TEXT("MCAuthoredCartoon"),FAnimExtractContext(0.,false))>.5f) Authored+=Weight;
            if(Input.bMage && Sample.bLoop) Hover+=Weight;
        }
        if(!Input.bMage && Sum>KINDA_SMALL_NUMBER) Input.AuthoredTankMotionWeight=FMath::Clamp(Authored/Sum,0.f,1.f);
        if(!Input.bAuthoredChainThrow) {
            ApplyOverlays(Output.Pose);
            if(Input.bMage && Input.Death<=0.f && Sum>KINDA_SMALL_NUMBER)
                ApplyMageHoverLegs(Output.Pose,Input,Hover/Sum,ForwardSpeed,SideSpeed);
        }
        const int32 Count=Output.Pose.GetNumBones();
        if(bNewTransition) {
            PreviousPose=LastPose; bNewTransition=false;
            bPreviousPoseHasChainScale=bLastPoseHasChainScale;
        }
        const float Alpha=Input.BlendSeconds>KINDA_SMALL_NUMBER?FMath::SmoothStep(0.f,Input.BlendSeconds,TransitionAge):1.f;
        const bool bBlending=PreviousPose.Num()==Count && Alpha<1.f;
        if(bBlending)
            for(auto Bone:Output.Pose.ForEachBoneIndex()) {
                FTransform Blended; Blended.Blend(PreviousPose[Bone.GetInt()],Output.Pose[Bone],Alpha); Output.Pose[Bone]=Blended;
            }
        // Also constrain the final crossfade: cached poses from a transform
        // animation or a scaled reaction must never leak squash into combat.
        // The chain throw's planted-leg stretch fades out with its authored pose.
        bLastPoseHasChainScale=!Input.bMage && (Input.bAuthoredChainThrow || (bBlending && bPreviousPoseHasChainScale));
        if(!Input.bMage && !Input.bAllowTankScale && !bLastPoseHasChainScale)
            for(auto Bone:Output.Pose.ForEachBoneIndex())
                Output.Pose[Bone].SetScale3D(Output.Pose.GetBoneContainer().GetRefPoseTransform(Bone).GetScale3D());
        Output.Pose.NormalizeRotations();
        LastPose.SetNumUninitialized(Count);
        for(auto Bone:Output.Pose.ForEachBoneIndex()) LastPose[Bone.GetInt()]=Output.Pose[Bone];
        if(!Input.bAuthoredChainThrow) {
            SecondaryMotion.Evaluate(Output);
            ApplyChainPose(Output.Pose);
        }
        return true;
    }
private:
    FMCSecondaryAnimationSolver SecondaryMotion;
    ENutChainPoseStage ChainStage=ENutChainPoseStage::None;
    FNutChainPose ChainPose,ChainStageStart;
    FNutChainLeg ChainLegs[2];
    FVector ChainGripOriginCS=FVector::ZeroVector;
    FTransform ChainActorToComponent=FTransform::Identity;
    float ChainStageAge=0.f;

    void UpdateChainPose(const AMCNutBoss& Boss,const USkeletalMeshComponent& Mesh,float Dt)
    {
        if(Input.bAuthoredChainThrow) {
            // Procedural hauling fades in from neutral after the sampled release.
            // The usual animation transition retains the outgoing authored pose.
            ChainStage=ENutChainPoseStage::None;
            ChainPose=ChainStageStart={};
            ChainStageAge=.15f;
            return;
        }
        ENutChainPoseStage Stage=ENutChainPoseStage::None;
        if(Boss.BossRole==EMCNutBossRole::Tank && Input.Death<=0.f) {
            switch(Boss.ChainPhase) {
            case EMCNutBossChainPhase::Flight: Stage=ENutChainPoseStage::Launch; break;
            case EMCNutBossChainPhase::PullPlayer:
            case EMCNutBossChainPhase::PullNut: Stage=ENutChainPoseStage::Pull; break;
            case EMCNutBossChainPhase::Reeling: Stage=ENutChainPoseStage::Reel; break;
            case EMCNutBossChainPhase::Impact: Stage=ENutChainPoseStage::Impact; break;
            case EMCNutBossChainPhase::Inactive:
                if(Boss.Attack==EMCNutBossAttack::Chain && Boss.State==EMCNutBossState::Telegraph)
                    Stage=ENutChainPoseStage::Windup;
                break;
            }
        }
        if(Stage!=ChainStage) {
            ChainStageStart=ChainPose;
            ChainStage=Stage;
            ChainStageAge=0.f;
        }
        ChainStageAge+=FMath::Max(0.f,Dt);
        if(Stage==ENutChainPoseStage::None && (Boss.BossRole!=EMCNutBossRole::Tank || Input.Death>0.f || ChainStageAge>=.15f)) {
            ChainPose={};
            ChainStageAge=.15f;
            return;
        }

        const FTransform MeshWorld=Mesh.GetComponentTransform();
        const FVector GripForward=Boss.Attack==EMCNutBossAttack::Chain?Boss.AttackForward:Boss.GetActorForwardVector();
        const FVector GripOffset=GripForward.Rotation().RotateVector(FVector(1.,.72,.14)*Boss.Settings.BodyRadius);
        ChainGripOriginCS=MeshWorld.InverseTransformPosition(Boss.GetActorLocation()+GripOffset);
        ChainActorToComponent=Boss.GetActorTransform().GetRelativeTransform(MeshWorld);
        ChainActorToComponent.SetScale3D(ChainActorToComponent.GetScale3D()*Boss.Settings.BodyRadius);
        ChainLegs[0]={{FindRigBone(Mesh,TEXT("thigh_stretch_l")),FindRigBone(Mesh,TEXT("leg_stretch_l")),FindRigBone(Mesh,TEXT("foot_l"))}};
        ChainLegs[1]={{FindRigBone(Mesh,TEXT("thigh_stretch_r")),FindRigBone(Mesh,TEXT("leg_stretch_r")),FindRigBone(Mesh,TEXT("foot_r"))}};

        const FNutChainPose Rest;
        const FNutChainPose Ready={{-1.20,.24,.44},{-.10,0,-.12},{13,-26,-8},{-6,21,8},{-18,10,-19},{-17,-23,24},1.f};
        const FNutChainPose Release={{0,0,0},{.17,0,-.07},{-17,12,7},{8,-10,-7},{-13,-12,-16},{-13,10,-17},1.f};
        const FNutChainPose Recoil={{-.14,.09,.05},{.09,0,-.09},{-9,5,10},{6,-4,-9},{-18,-11,-20},{-18,16,-26},1.f};
        const FNutChainPose Haul={{-.61,.21,-.19},{-.16,0,-.13},{16,-17,-13},{-9,12,8},{-26,14,-23},{17,-12,-18},1.f};
        const float Alpha=Boss.GetChainPhaseAlpha();
        switch(Stage) {
        case ENutChainPoseStage::Windup:
            ChainPose=BlendChainPose(Rest,Ready,FMath::SmoothStep(0.f,.64f,Alpha));
            ChainPose=BlendChainPose(ChainPose,Release,FMath::SmoothStep(.73f,1.f,Alpha));
            break;
        case ENutChainPoseStage::Launch:
            ChainPose=BlendChainPose(Release,Recoil,FMath::SmoothStep(.18f,.85f,Alpha));
            break;
        case ENutChainPoseStage::Pull: {
            ChainPose=BlendChainPose(ChainStageStart,Haul,FMath::SmoothStep(0.f,.51f,Alpha));
            const float Heave=FMath::Sin(Alpha*PI)*FMath::Sin(Alpha*PI*2.f);
            ChainPose.Grip.X-=.15f*Heave;
            ChainPose.Torso.Pitch+=5.f*Heave;
            break;
        }
        case ENutChainPoseStage::Reel:
            ChainPose=BlendChainPose(ChainStageStart,Haul,FMath::SmoothStep(0.f,.32f,Alpha));
            ChainPose=BlendChainPose(ChainPose,Rest,FMath::SmoothStep(.25f,1.f,Alpha));
            break;
        case ENutChainPoseStage::Impact: {
            // Sharp shell recoil followed by a lower, rocking settle into the authored stun.
            const float ImpactAge=FMath::Max(0.f,float(Input.ServerTime-Boss.ChainPhaseStartedAt));
            const float Kick=1.f-FMath::SmoothStep(0.f,.40f,ImpactAge);
            const float Rock=FMath::Sin(ImpactAge*19.f)*(1.f-FMath::SmoothStep(.06f,.68f,ImpactAge));
            const FNutChainPose Stagger={{-.62,.42,-.06},{-.23*Kick,-.05*Kick,-.17*Kick},
                {26*Kick+11*Rock,-16*Kick,-17*Kick-8*Rock},{-19*Kick,10*Kick,16*Kick+13*Rock},
                {17*Kick,-20*Kick,28*Kick},{22*Kick,0,-30*Kick},.72f*Kick};
            ChainPose=BlendChainPose(ChainStageStart,Stagger,FMath::SmoothStep(0.f,.065f,ImpactAge));
            break;
        }
        case ENutChainPoseStage::None:
            ChainPose=BlendChainPose(ChainStageStart,Rest,FMath::SmoothStep(0.f,.15f,ChainStageAge));
            break;
        }
    }

    void ApplyChainPose(FCompactPose& Pose) const
    {
        if(ChainStage==ENutChainPoseStage::None && ChainStageAge>=.15f) return;
        if(Input.Death>0.f) return;
        const auto Torso=PoseBone(Pose,Input,EMCNutPoseBone::Torso);
        if(Torso.GetInt()==INDEX_NONE) return;
        if(!ChainPose.Body.IsNearlyZero() || !ChainPose.Torso.IsNearlyZero()) {
            const FTransform Feet[]={ComponentBone(Pose,ChainCompactBone(Pose,ChainLegs[0].Bones[2])),
                ComponentBone(Pose,ChainCompactBone(Pose,ChainLegs[1].Bones[2]))};
            RotateComponent(Pose,Torso,ChainPose.Torso,Input.ActorToComponent);
            const auto TorsoParent=Pose.GetBoneContainer().GetParentBoneIndex(Torso);
            Pose[Torso].AddToTranslation(ComponentBone(Pose,TorsoParent).InverseTransformVector(ChainActorToComponent.TransformVector(ChainPose.Body)));
            PlantChainFoot(Pose,ChainLegs[0],Feet[0]);
            PlantChainFoot(Pose,ChainLegs[1],Feet[1]);
        }
        RotateComponent(Pose,PoseBone(Pose,Input,EMCNutPoseBone::Head),ChainPose.Head,Input.ActorToComponent);
        RotateComponent(Pose,PoseBone(Pose,Input,EMCNutPoseBone::LeftArm),ChainPose.Shield,Input.ActorToComponent);
        if(ChainPose.GripWeight<=.001f) return;

        const auto Arm=PoseBone(Pose,Input,EMCNutPoseBone::RightArm);
        const auto Forearm=PoseBone(Pose,Input,EMCNutPoseBone::RightForearm);
        const auto Hand=PoseBone(Pose,Input,EMCNutPoseBone::RightHand);
        if(!HasDirectLimbChain(Pose,Arm,Forearm,Hand)) return;
        FTransform Upper=ComponentBone(Pose,Arm),Lower=ComponentBone(Pose,Forearm),End=ComponentBone(Pose,Hand);
        const FQuat Wrist=Input.ActorToComponent*ChainPose.Wrist.Quaternion()*Input.ActorToComponent.Inverse()*End.GetRotation();
        const FVector Grip=ChainGripOriginCS+ChainActorToComponent.TransformVector(ChainPose.Grip);
        const FVector Target=FMath::Lerp(End.GetLocation(),Grip-Wrist.RotateVector(End.GetScale3D()*ChainGripOffset),ChainPose.GripWeight);
        const FVector Elbow=Lower.GetLocation()+ChainActorToComponent.TransformVector(FVector(-.20,.44,.06));
        const double Reach=FVector::Distance(Upper.GetLocation(),Lower.GetLocation())+FVector::Distance(Lower.GetLocation(),End.GetLocation());
        const FVector ReachDelta=Target-Upper.GetLocation();
        // A small shoulder lead keeps the contact in reach without stretching forearm or wrist bones.
        const double ShoulderLead=FMath::Clamp(ReachDelta.Size()-Reach*.97,0.,Reach*.42);
        const FVector Lead=ReachDelta.GetSafeNormal()*ShoulderLead;
        Upper.AddToTranslation(Lead);
        Lower.AddToTranslation(Lead);
        End.AddToTranslation(Lead);
        AnimationCore::SolveTwoBoneIK(Upper,Lower,End,Elbow,Target,false,1.,1.);
        End.SetRotation(FQuat::Slerp(End.GetRotation(),Wrist,ChainPose.GripWeight).GetNormalized());
        WriteLimbPose(Pose,Arm,Forearm,Hand,Upper,Lower,End);
    }

    void ApplyOverlays(FCompactPose& Pose)
    {
        if(Input.bMage && (Input.GravityLeftGesture>=0.f || Input.GravityRightGesture>=0.f)) {
            ApplyMageGravityPose(Pose,Input);
            return;
        }
        if(Input.bMage && Input.AuthoredMageMotionWeight>.999f) return;
        auto Rotate=[&](FCompactPoseBoneIndex Bone,const FRotator& Rotation){RotateComponent(Pose,Bone,Rotation,Input.ActorToComponent);};
        const auto Torso=PoseBone(Pose,Input,EMCNutPoseBone::Torso);
        const auto Head=PoseBone(Pose,Input,EMCNutPoseBone::Head);
        const auto LeftArm=PoseBone(Pose,Input,EMCNutPoseBone::LeftArm);
        const auto RightArm=PoseBone(Pose,Input,EMCNutPoseBone::RightArm);
        const auto LeftHand=PoseBone(Pose,Input,EMCNutPoseBone::LeftHand);
        const auto RightHand=PoseBone(Pose,Input,EMCNutPoseBone::RightHand);
        const float Alive=1-Input.Death;
        const float Procedural=1-(Input.bMage?Input.AuthoredMageMotionWeight:Input.AuthoredTankMotionWeight);
        const float Breath=FMath::Sin(float(Input.ServerTime)*2.4f)*Alive*Procedural;
        const float Stride=FMath::Sin(float(Input.ServerTime)*6.28f)*Procedural;
        const float Cast=Input.Cast*Alive;
        const float Channel=FMath::Sin(float(Input.ServerTime)*5)*Input.Channel;
        const float Recoil=Hit*(1-Input.AuthoredTankHitWeight)*(1-.6f*FMath::SmoothStep(0.f,.2f,ShieldHit))*Alive;
        Rotate(Torso,FRotator(Breath*.8f+ForwardSpeed*-3*Procedural+AimPitch*.12f*Cast-Recoil*9,
            AimYaw*.2f*Cast,SideSpeed*-4*Procedural+Recoil*HitDirection.Y*10));
        Rotate(Head,FRotator(AimPitch*.2f*Alive-Recoil*5,AimYaw*.25f*Alive,Breath*.6f));
        if(!Input.bAuthoredSpellGestures) {
        Rotate(LeftArm,FRotator((-Guard*13*Procedural-ShieldHit*10*(1-Input.AuthoredTankHitWeight))*Alive+AimPitch*.25f*Cast,-Guard*8*Alive*Procedural+AimYaw*.25f*Cast,
            Input.bMage?Cast*(-5+Channel*2):0));
        Rotate(RightArm,FRotator(AimPitch*.55f*Cast-Input.Release*14,
            AimYaw*.55f*Cast,Input.bMage?Channel*3:Stride*ForwardSpeed*2));
        Rotate(LeftHand,FRotator(Channel*3+AimPitch*.2f*Cast,AimYaw*.2f*Cast+Cast*FMath::Sin(Input.CastProgress*PI)*8,0));
        Rotate(RightHand,FRotator(AimPitch*.2f*Cast+Input.Release*8,AimYaw*.2f*Cast,Channel*4));
        }
        if(Torso.GetInt()!=INDEX_NONE) {
            FVector Translation=Pose[Torso].GetTranslation();
            Translation=FMath::Lerp(Translation,Pose.GetBoneContainer().GetRefPoseTransform(Torso).GetTranslation(),double(Airborne));
            Translation.Z+=Breath*.45f+FMath::Abs(Stride)*FMath::Abs(ForwardSpeed)*.6f;
            Pose[Torso].SetTranslation(Translation);
        }
        if(Input.bMage) MatchCastHand(Pose,Input,Input.CastHandAlignmentWeight()*Alive);
        if(Input.bMage && Input.Melee>.001f && !Input.bHasMeleeClip) {
            // A custom profile without a compatible melee clip still has a
            // physical windup/thrust/recovery, never the spell-emitter palm IK.
            const float Anticipation=FMath::Sin(FMath::Clamp(Input.MeleePhase/.55f,0.f,1.f)*PI);
            const float Strike=FMath::SmoothStep(.32f,.55f,Input.MeleePhase)*(1-FMath::SmoothStep(.55f,1.f,Input.MeleePhase));
            const float Weight=Input.Melee*Alive;
            Rotate(Torso,FRotator((-Anticipation*5+Strike*8)*Weight,(-Anticipation*12+Strike*16)*Weight,0));
            Rotate(RightArm,FRotator((Anticipation*15-Strike*48)*Weight,Strike*14*Weight,0));
            Rotate(PoseBone(Pose,Input,EMCNutPoseBone::RightForearm),FRotator(-Strike*24*Weight,0,0));
            Rotate(RightHand,FRotator(Strike*12*Weight,0,0));
        }
        if(Input.Death>0 && !Input.bHasDeathClip) {
            const float Fall=FMath::SmoothStep(.1f,.85f,Input.Death);
            Rotate(Torso,FRotator(-Fall*(Input.bHasDeathClip?10:65),0,Fall*12));
            if(Torso.GetInt()!=INDEX_NONE) Pose[Torso].AddToTranslation(FVector(0,0,-Fall*(Input.bHasDeathClip?2:12)));
            Rotate(LeftArm,FRotator(Fall*25,0,0)); Rotate(RightArm,FRotator(Fall*22,0,0));
        }
    }
    FMCNutBossAnimationSnapshot Input;
    TArray<FTransform> LastPose,PreviousPose;
    int32 LastKey=INDEX_NONE;
    float TransitionAge=1,AimYaw=0,AimPitch=0,ForwardSpeed=0,SideSpeed=0,Guard=0,Airborne=0,Hit=0,ShieldHit=0;
    float MovementAlpha=0,MovementFrom=0,MovementAge=1;
    bool bWasMoving=false;
    FVector HitDirection=FVector::ZeroVector;
    bool bNewTransition=false;
    bool bLastPoseHasChainScale=false,bPreviousPoseHasChainScale=false;
};
}

FTransform AMCNutBoss::GetChainGripTransform() const
{
    if(TankModel) {
        const int32 Index=FindRigBone(*TankModel,BossSettings.RightHandBone);
        if(Index!=INDEX_NONE) {
            FTransform Grip=TankModel->GetSocketTransform(TankModel->GetBoneName(Index),RTS_World);
            Grip.SetLocation(Grip.TransformPosition(ChainGripOffset));
            Grip.SetScale3D(FVector::OneVector);
            return Grip;
        }
    }
    return FTransform(GetActorQuat(),GetChainOrigin());
}

UMCNutBossAnimInstance::UMCNutBossAnimInstance()
{
    bUseMultiThreadedAnimationUpdate=true;
    RootMotionMode=ERootMotionMode::NoRootMotionExtraction;
}

FAnimInstanceProxy* UMCNutBossAnimInstance::CreateAnimInstanceProxy() { return new FMCNutBossAnimProxy(this); }
void UMCNutBossAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
