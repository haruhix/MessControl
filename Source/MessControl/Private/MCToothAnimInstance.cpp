#include "MCToothAnimInstance.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

class FMCToothAnimProxy final : public FAnimInstanceProxy
{
public:
    explicit FMCToothAnimProxy(UAnimInstance* Instance):FAnimInstanceProxy(Instance) {}
    TArray<FTransform> Pose;
    virtual void PreUpdate(UAnimInstance* Instance,float Dt) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Dt);
        const auto* Tooth=Cast<AMCToothCharacter>(Instance->TryGetPawnOwner());
        const auto* Mesh=Instance->GetSkelMeshComponent()->GetSkeletalMeshAsset();
        if (!Tooth || !Mesh) return;
        const auto& Ref=Mesh->GetRefSkeleton(); Pose=Ref.GetRefBonePose();
        TArray<FTransform> ReferenceCS; ReferenceCS.SetNum(Pose.Num());
        for (int32 I=0;I<Pose.Num();++I) ReferenceCS[I]=Ref.GetParentIndex(I)>=0?Pose[I]*ReferenceCS[Ref.GetParentIndex(I)]:Pose[I];
        auto Rotate=[&](FName Name,FRotator Delta)
        {
            const int32 I=Ref.FindBoneIndex(Name); if (I==INDEX_NONE) return;
            const int32 Parent=Ref.GetParentIndex(I); const FQuat Basis=Parent>=0?ReferenceCS[Parent].GetRotation():FQuat::Identity;
            Pose[I].SetRotation((Basis.Inverse()*Delta.Quaternion()*Basis*Pose[I].GetRotation()).GetNormalized());
        };
        auto Translate=[&](FName Name,FVector Delta)
        {
            const int32 I=Ref.FindBoneIndex(Name); if (I==INDEX_NONE) return;
            const int32 Parent=Ref.GetParentIndex(I);
            Pose[I].AddToTranslation(Parent>=0?ReferenceCS[Parent].InverseTransformVectorNoScale(Delta):Delta);
        };
        const auto& A=Tooth->AnimationSettings; const float G=Tooth->AnimationGait;
        const float Speed=Tooth->AnimationSpeed;
        Rotate(TEXT("body"),FRotator(Tooth->AnimationPitch,0,FMath::Sin(G)*Speed*A.Lean*0.35f));
        Translate(TEXT("body"),FVector(0,0,Tooth->AnimationBob));
        Rotate(TEXT("leg_l"),FRotator(FMath::Sin(G)*28*Speed,0,0));
        Rotate(TEXT("leg_r"),FRotator(-FMath::Sin(G)*28*Speed,0,0));
        Rotate(TEXT("foot_l"),FRotator(-FMath::Sin(G-0.35f)*13*Speed,0,0));
        Rotate(TEXT("foot_r"),FRotator(FMath::Sin(G-0.35f)*13*Speed,0,0));
        Rotate(TEXT("arm_l"),FRotator(-FMath::Sin(G-0.25f)*24*Speed,0,-10));
        Rotate(TEXT("arm_r"),FRotator(Tooth->AnimationBrushAngle+FMath::Sin(G-0.25f)*18*Speed,0,10));
        Rotate(TEXT("hand_r"),FRotator(Tooth->AnimationBrushAngle*0.3f,0,0));
        if (Tooth->ToothPhysics) Tooth->ToothPhysics->BuildPresentationPose(Pose);
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose(); const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
        for (int32 I=0;I<Pose.Num();++I)
        {
            const FCompactPoseBoneIndex Compact=Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I));
            if (Compact.IsValid()) Output.Pose[Compact]=Pose[I];
        }
        return true;
    }
};
FAnimInstanceProxy* UMCToothAnimInstance::CreateAnimInstanceProxy() { return new FMCToothAnimProxy(this); }
void UMCToothAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
