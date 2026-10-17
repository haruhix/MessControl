#include "MCBossAnimInstance.h"
#include "MCAnimationComparisonSubsystem.h"
#include "MCBossCharacter.h"
#include "MCSecondaryAnimationSolver.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace
{
float SequenceTime(const FMCBossAnimationLayer& Layer,double Now)
{
    const float Length=Layer.Sequence->GetPlayLength();
    const float Time=FMath::Max(0.f,float(Now-Layer.StartedAt))*Layer.PlayRate;
    return Layer.bLoop && Length>0.f ? FMath::Fmod(Time,Length) : FMath::Min(Time,Length);
}

TArray<FName> SecondaryMotionBones(const USkeletalMeshComponent* Mesh)
{
    // Keep the two role slots even when a profile has no compatible torso or head.
    TArray<FName> Bones={NAME_None,NAME_None};
    if (!Mesh || !Mesh->GetSkeletalMeshAsset()) return Bones;
    // The independent Zombie and Phase3 ARP rigs use root_x as their authored
    // body joint; the top-level root is never a secondary-motion fallback.
    if (Mesh->GetBoneIndex(TEXT("root_x"))!=INDEX_NONE
        && Mesh->GetParentBone(TEXT("neck_x"))==FName(TEXT("root_x"))
        && Mesh->GetParentBone(TEXT("head_x"))==FName(TEXT("neck_x")))
    {
        Bones[0]=TEXT("root_x");
        Bones[1]=TEXT("head_x");
        return Bones;
    }
    for (const FName Name:{FName(TEXT("body")),FName(TEXT("chest")),FName(TEXT("spine_03")),
        FName(TEXT("spine_02")),FName(TEXT("spine"))})
        if (Mesh->GetBoneIndex(Name)!=INDEX_NONE) {Bones[0]=Name;break;}
    for (const FName Name:{FName(TEXT("head")),FName(TEXT("head_x")),FName(TEXT("gaze_head"))})
        if (Mesh->GetBoneIndex(Name)!=INDEX_NONE) {Bones[1]=Name;break;}
    return Bones;
}
}

struct FMCBossAnimInstanceProxy : FAnimInstanceProxy
{
    explicit FMCBossAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

    virtual void UpdateAnimationNode(const FAnimationUpdateContext&) override
    {
        UpdateCounter.Increment();
    }

    virtual void PreUpdate(UAnimInstance* Instance,float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
        const auto* BossInstance=CastChecked<UMCBossAnimInstance>(Instance);
        // Copy on the game thread; animation workers only read this frame's snapshot.
        Layers=BossInstance->Layers;
        SampleTime=BossInstance->SampleTime;
        if (auto* Mesh=Instance->GetSkelMeshComponent())
        {
            const auto* Boss=Cast<AMCBossCharacter>(Mesh->GetOwner());
            const bool bAllowMotion=(!Boss || (Boss->IsBossAlive()
                && Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Death)) && !Mesh->IsSimulatingPhysics();
            SecondaryMotion.PreUpdate(Instance,*this,DeltaSeconds,SecondaryMotionBones(Mesh),bAllowMotion);
            if (auto* World=Instance->GetWorld())
                if (auto* Comparison=World->GetSubsystem<UMCAnimationComparisonSubsystem>())
                    Comparison->RecordSample(Mesh,Comparison->GetMode(),SecondaryMotion.GetActiveBoneCount(),
                        SecondaryMotion.GetEvaluationCount(),SecondaryMotion.GetLastMaxAngleDegrees(),SecondaryMotion.GetLastMaxTranslation());
        }
        else SecondaryMotion.Reset();
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        TArray<FCompactPose,TInlineAllocator<4>> Poses;
        TArray<FBlendedCurve,TInlineAllocator<4>> Curves;
        TArray<UE::Anim::FStackAttributeContainer,TInlineAllocator<4>> Attributes;
        TArray<float,TInlineAllocator<4>> Weights;
        for (const auto& Layer:Layers)
        {
            if (!Layer.Sequence || Layer.Weight<=ZERO_ANIMWEIGHT_THRESH) continue;
            FCompactPose& Pose=Poses.AddDefaulted_GetRef();
            Pose.SetBoneContainer(&Output.Pose.GetBoneContainer());
            FBlendedCurve& Curve=Curves.AddDefaulted_GetRef();
            Curve.InitFrom(Output.Curve);
            auto& Attribute=Attributes.AddDefaulted_GetRef();
            FAnimationPoseData PoseData(Pose,Curve,Attribute);
            const FAnimExtractContext Context(double(SequenceTime(Layer,SampleTime)),false,{},Layer.bLoop);
            Layer.Sequence->GetAnimationPose(PoseData,Context);
            Weights.Add(Layer.Weight);
        }
        if (Poses.IsEmpty())
        {
            Output.ResetToRefPose();
            SecondaryMotion.Evaluate(Output);
            return true;
        }
        // Normalize after culling tiny weights. Bone transforms, curves and attributes all crossfade.
        float Total=0.f;
        for (float Weight:Weights) Total+=Weight;
        for (float& Weight:Weights) Weight/=Total;
        FAnimationPoseData OutputData(Output);
        FAnimationRuntime::BlendPosesTogether(Poses,Curves,Attributes,Weights,OutputData);
        SecondaryMotion.Evaluate(Output);
        return true;
    }
private:
    FMCSecondaryAnimationSolver SecondaryMotion;
    TArray<FMCBossAnimationLayer> Layers;
    double SampleTime=0.;
};

FAnimInstanceProxy* UMCBossAnimInstance::CreateAnimInstanceProxy()
{
    return new FMCBossAnimInstanceProxy(this);
}

void UMCBossAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    delete static_cast<FMCBossAnimInstanceProxy*>(InProxy);
}

void UMCBossAnimInstance::UpdateWeights(double Now)
{
    TransitionAlpha=TransitionSeconds>0.f ? FMath::Clamp(float(Now-TransitionStartedAt)/TransitionSeconds,0.f,1.f) : 1.f;
    for (int32 I=0;I<Layers.Num();++I)
        Layers[I].Weight=FMath::Lerp(Layers[I].StartWeight,I==Layers.Num()-1 ? 1.f : 0.f,TransitionAlpha);
    if (TransitionAlpha>=1.f && Layers.Num()>1)
    {
        const auto Target=Layers.Last();
        Layers.Reset();
        Layers.Add(Target);
    }
}

void UMCBossAnimInstance::PresentSequence(UAnimSequence* Sequence,double StartedAt,double Now,float PlayRate,bool bLoop,float BlendSeconds,bool bPlayToEnd,bool bForceInterrupt)
{
    if (!Sequence) return;
    UpdateWeights(Now);
    const bool bChanged=Layers.IsEmpty() || Layers.Last().Sequence!=Sequence || Layers.Last().StartedAt!=StartedAt;
    if (bChanged && bFinishCurrentSequence && !bForceInterrupt && !Layers.IsEmpty())
    {
        const auto& Current=Layers.Last();
        const double EndsAt=Current.StartedAt+Current.Sequence->GetPlayLength()/FMath::Max(.001f,Current.PlayRate);
        if (Now<EndsAt)
        {
            // Locomotion/preview changes cannot fade out a strike before its final frame.
            SampleTime=Now;
            SequencePosition=SequenceTime(Current,Now);
            return;
        }
    }
    if (bChanged)
    {
        // Preserve current contributions when a second transition interrupts the first.
        Layers.RemoveAll([](const FMCBossAnimationLayer& Layer) { return Layer.Weight<=ZERO_ANIMWEIGHT_THRESH; });
        for (auto& Layer:Layers) Layer.StartWeight=Layer.Weight;
        TransitionStartedAt=Now;
        // Preserve the authored windup/impact pose; only the transition OUT of an attack blends.
        TransitionSeconds=bPlayToEnd || Layers.IsEmpty() ? 0.f : FMath::Max(0.f,BlendSeconds);
        auto& Target=Layers.AddDefaulted_GetRef();
        Target.Sequence=Sequence;
        Target.StartedAt=StartedAt;
        UpdateWeights(Now);
    }
    auto& Target=Layers.Last();
    Target.PlayRate=PlayRate;
    Target.bLoop=bLoop;
    bFinishCurrentSequence=bPlayToEnd && !bLoop;
    SampleTime=Now;
    CurrentSequence=Sequence;
    SequencePosition=SequenceTime(Target,Now);
}
