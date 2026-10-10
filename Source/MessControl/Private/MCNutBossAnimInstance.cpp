#include "MCNutBossAnimInstance.h"

#include "MCNutBoss.h"
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
            bBoneCachesInvalidated=false;
        }
    }

    virtual void PreUpdate(UAnimInstance* Instance,float Dt) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Dt);
        Input=FMCNutBossAnimationSnapshot();
        if(auto* Mesh=Instance->GetSkelMeshComponent())
            if(const auto* Boss=Cast<AMCNutBoss>(Mesh->GetOwner())) Boss->BuildAnimationSnapshot(Mesh,Input);
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
        if(Input.VisualKey!=LastKey) {LastKey=Input.VisualKey;bNewTransition=true;TransitionAge=Step;}
        // Pose samples use server time. The blend must also consume the full
        // elapsed frame, including skipped or slow animation updates.
        else TransitionAge+=Step;
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        FAnimationPoseData Result(Output);
        float Sum=0;
        for(const FMCNutBossClipSample& Sample:Input.Samples) {
            if(!Sample.Clip || Sample.Weight<=.001f) continue;
            FPoseContext SamplePose(Output); SamplePose.ResetToRefPose();
            FAnimationPoseData SampleData(SamplePose);
            Sample.Clip->GetAnimationPose(SampleData,FAnimExtractContext(Sample.Seconds,false,{},Sample.bLoop));
            const float Weight=Sample.Weight;
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,SampleData,Sum/(Sum+Weight)); Sum+=Weight;
        }
        ApplyOverlays(Output.Pose);
        const int32 Count=Output.Pose.GetNumBones();
        if(bNewTransition) {
            PreviousPose=LastPose; bNewTransition=false;
        }
        const float Alpha=Input.BlendSeconds>KINDA_SMALL_NUMBER?FMath::SmoothStep(0.f,Input.BlendSeconds,TransitionAge):1.f;
        if(PreviousPose.Num()==Count && Alpha<1.f)
            for(auto Bone:Output.Pose.ForEachBoneIndex()) {
                FTransform Blended; Blended.Blend(PreviousPose[Bone.GetInt()],Output.Pose[Bone],Alpha); Output.Pose[Bone]=Blended;
            }
        Output.Pose.NormalizeRotations();
        LastPose.SetNumUninitialized(Count);
        for(auto Bone:Output.Pose.ForEachBoneIndex()) LastPose[Bone.GetInt()]=Output.Pose[Bone];
        return true;
    }
private:
    void ApplyOverlays(FCompactPose& Pose)
    {
        auto Rotate=[&](FCompactPoseBoneIndex Bone,const FRotator& Rotation){RotateComponent(Pose,Bone,Rotation,Input.ActorToComponent);};
        const auto Torso=PoseBone(Pose,Input,EMCNutPoseBone::Torso);
        const auto Head=PoseBone(Pose,Input,EMCNutPoseBone::Head);
        const auto LeftArm=PoseBone(Pose,Input,EMCNutPoseBone::LeftArm);
        const auto RightArm=PoseBone(Pose,Input,EMCNutPoseBone::RightArm);
        const auto LeftHand=PoseBone(Pose,Input,EMCNutPoseBone::LeftHand);
        const auto RightHand=PoseBone(Pose,Input,EMCNutPoseBone::RightHand);
        const float Alive=1-Input.Death;
        const float Breath=FMath::Sin(float(Input.ServerTime)*2.4f)*Alive;
        const float Stride=FMath::Sin(float(Input.ServerTime)*6.28f);
        const float Cast=Input.Cast*Alive;
        const float Channel=FMath::Sin(float(Input.ServerTime)*5)*Input.Channel;
        const float Recoil=Hit*(1-.6f*FMath::SmoothStep(0.f,.2f,ShieldHit))*Alive;
        Rotate(Torso,FRotator(Breath*.8f+ForwardSpeed*-3+AimPitch*.12f*Cast-Recoil*9,
            AimYaw*.2f*Cast,SideSpeed*-4+Recoil*HitDirection.Y*10));
        Rotate(Head,FRotator(AimPitch*.2f*Alive-Recoil*5,AimYaw*.25f*Alive,Breath*.6f));
        Rotate(LeftArm,FRotator((-Guard*13-ShieldHit*10)*Alive+AimPitch*.25f*Cast,-Guard*8*Alive+AimYaw*.25f*Cast,
            Input.bMage?Cast*(-5+Channel*2):0));
        Rotate(RightArm,FRotator(AimPitch*.55f*Cast-Input.Release*14,
            AimYaw*.55f*Cast,Input.bMage?Channel*3:Stride*ForwardSpeed*2));
        Rotate(LeftHand,FRotator(Channel*3+AimPitch*.2f*Cast,AimYaw*.2f*Cast+Cast*FMath::Sin(Input.CastProgress*PI)*8,0));
        Rotate(RightHand,FRotator(AimPitch*.2f*Cast+Input.Release*8,AimYaw*.2f*Cast,Channel*4));
        if(Torso.GetInt()!=INDEX_NONE) {
            FVector Translation=Pose[Torso].GetTranslation();
            Translation=FMath::Lerp(Translation,Pose.GetBoneContainer().GetRefPoseTransform(Torso).GetTranslation(),double(Airborne));
            Translation.Z+=Breath*.45f+FMath::Abs(Stride)*FMath::Abs(ForwardSpeed)*.6f;
            Pose[Torso].SetTranslation(Translation);
        }
        if(Input.bMage) MatchCastHand(Pose,Input,Cast*.85f);
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
        if(Input.Death>0) {
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
    FVector HitDirection=FVector::ZeroVector;
    bool bNewTransition=false;
};
}

UMCNutBossAnimInstance::UMCNutBossAnimInstance()
{
    bUseMultiThreadedAnimationUpdate=true;
    RootMotionMode=ERootMotionMode::NoRootMotionExtraction;
}

FAnimInstanceProxy* UMCNutBossAnimInstance::CreateAnimInstanceProxy() { return new FMCNutBossAnimProxy(this); }
void UMCNutBossAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
