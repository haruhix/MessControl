#include "MCToothAnimInstance.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCGazeComponent.h"
#include "MCGripComponent.h"
#include "MCExpressionComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MCFoodActor.h"
#include "TwoBoneIK.h"

class FMCToothAnimProxy final : public FAnimInstanceProxy
{
public:
    explicit FMCToothAnimProxy(UAnimInstance* Instance):FAnimInstanceProxy(Instance) {}
    TArray<FTransform> Pose;
    FVector PlantedFeet[2]={FVector::ZeroVector,FVector::ZeroVector};
    bool FootPlanted[2]={false,false};
    void PlaceFeet(const AMCToothCharacter* Tooth,const FReferenceSkeleton& Ref,float Dt)
    {
        if (!Tooth->ToothPhysics->CanAct() || Tooth->GetCharacterMovement()->IsFalling() || Tooth->AnimationSwim>.05f || Tooth->bPreviewAnimation
            || (Tooth->Expression && Tooth->Expression->BodyAlpha()>.01f)) { FootPlanted[0]=FootPlanted[1]=false; return; }
        const FTransform World=Tooth->GetMesh()->GetComponentTransform();
        TArray<FTransform> CS; CS.SetNum(Pose.Num());
        auto Rebuild=[&](){for (int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)>=0?Pose[I]*CS[Ref.GetParentIndex(I)]:Pose[I];};
        Rebuild();
        for (int32 Side=0;Side<2;++Side)
        {
            const FString S=Side==0?TEXT("_l"):TEXT("_r");
            const int32 Upper=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("leg")+S))));
            const int32 Lower=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("knee")+S))));
            const int32 Foot=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("foot")+S))));
            if (Upper<0 || Lower<0 || Foot<0) continue;
            const FVector Animated=World.TransformPosition(CS[Foot].GetLocation());
            const float Phase=Tooth->AnimationGait+(Side==0?0:PI);
            const bool Stance=Tooth->AnimationSpeed<.05f || FMath::Sin(Phase)<=0;
            if (!Stance) { FootPlanted[Side]=false; continue; }
            FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFootGround),false,Tooth);
            if (Tooth->HeldFood) Params.AddIgnoredActor(Tooth->HeldFood);
            if (!Tooth->GetWorld()->LineTraceSingleByChannel(Hit,Animated+FVector(0,0,22),Animated-FVector(0,0,35),ECC_Visibility,Params)
                || Hit.ImpactNormal.Z<.65f) { FootPlanted[Side]=false; continue; }
            FVector Target=Animated;
            if (!FootPlanted[Side] || FVector::Dist2D(PlantedFeet[Side],Animated)>24) PlantedFeet[Side]=Animated;
            FootPlanted[Side]=true;
            Target.X=PlantedFeet[Side].X; Target.Y=PlantedFeet[Side].Y;
            // A small sole clearance, with bounded correction on moving tongue geometry.
            Target.Z=Animated.Z+FMath::Clamp(float(Hit.ImpactPoint.Z+3-Animated.Z),-12.f,12.f);
            FTransform U=CS[Upper],L=CS[Lower],F=CS[Foot];
            const FVector Pole=CS[Upper].GetLocation()+World.InverseTransformVectorNoScale(Tooth->GetActorForwardVector()*45);
            AnimationCore::SolveTwoBoneIK(U,L,F,Pole,World.InverseTransformPosition(Target),false,1.,1.);
            const float Weight=Tooth->AnimationSpeed<.05f?1.f:FMath::Clamp(-FMath::Sin(Phase)*4.f,0.f,1.f);
            const int32 Bones[]={Upper,Lower,Foot}; const FTransform Solved[]={U,L,F};
            for (int32 J=0;J<3;++J)
            {
                const int32 B=Bones[J],Parent=Ref.GetParentIndex(B); FTransform Blended;
                Blended.Blend(CS[B],Solved[J],Weight); Pose[B]=Parent>=0?Blended.GetRelativeTransform(CS[Parent]):Blended; Rebuild();
            }
        }
    }
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
            const int32 I=Ref.FindBoneIndex(Tooth->RigBone(Name)); if (I==INDEX_NONE) return;
            const int32 Parent=Ref.GetParentIndex(I); const FQuat Basis=Parent>=0?ReferenceCS[Parent].GetRotation():FQuat::Identity;
            const FQuat Facing=Tooth->StandingMeshTransform().GetRotation();
            const FQuat MeshDelta=Facing.Inverse()*Delta.Quaternion()*Facing;
            Pose[I].SetRotation((Basis.Inverse()*MeshDelta*Basis*Pose[I].GetRotation()).GetNormalized());
        };
        auto Translate=[&](FName Name,FVector Delta)
        {
            const int32 I=Ref.FindBoneIndex(Tooth->RigBone(Name)); if (I==INDEX_NONE) return;
            const int32 Parent=Ref.GetParentIndex(I);
            Pose[I].AddToTranslation(Parent>=0?ReferenceCS[Parent].InverseTransformVectorNoScale(Delta):Delta);
        };
        const auto& A=Tooth->AnimationSettings; const float G=Tooth->AnimationGait;
        const float Speed=Tooth->AnimationSpeed;
        Rotate(TEXT("body"),FRotator(Tooth->AnimationPitch-Tooth->AnimationBrake*7,Tooth->AnimationTurn*-5,FMath::Sin(G)*Speed*A.Lean*0.35f-Tooth->AnimationTurn*6));
        Translate(TEXT("body"),FVector(0,0,Tooth->AnimationBob));
        Rotate(TEXT("leg_l"),FRotator(FMath::Sin(G)*28*Speed,0,0));
        Rotate(TEXT("leg_r"),FRotator(-FMath::Sin(G)*28*Speed,0,0));
        Rotate(TEXT("knee_l"),FRotator(-FMath::Max(0.f,FMath::Sin(G))*18*Speed,0,0));
        Rotate(TEXT("knee_r"),FRotator(-FMath::Max(0.f,-FMath::Sin(G))*18*Speed,0,0));
        Rotate(TEXT("foot_l"),FRotator(-FMath::Sin(G-0.35f)*13*Speed,0,0));
        Rotate(TEXT("foot_r"),FRotator(FMath::Sin(G-0.35f)*13*Speed,0,0));
        Rotate(TEXT("arm_l"),FRotator(-FMath::Sin(G-0.25f)*24*Speed,0,-10));
        // Stay inside the shoulder/wrist stops even at the strongest F1 preset.
        Rotate(TEXT("arm_r"),FRotator(FMath::Clamp(Tooth->AnimationBrushAngle+FMath::Sin(G-0.25f)*18*Speed,-50.f,50.f),0,10));
        Rotate(TEXT("hand_r"),FRotator(FMath::Clamp(Tooth->AnimationBrushAngle*0.3f,-35.f,35.f),0,0));
        // Blend a readable dog-paddle over locomotion; contact IK still owns a held hand.
        if (Tooth->AnimationSwim>.001f)
        {
            const TArray<FTransform> Ground=Pose; Pose=Ref.GetRefBonePose();
            const float Phase=Tooth->AnimationStroke,Effort=Tooth->AnimationSwimEffort;
            Rotate(TEXT("body"),FRotator(-12-24*Effort,0,FMath::Sin(Phase)*3));
            Rotate(TEXT("gaze_head"),FRotator(8+18*Effort,0,0));
            for (int32 Side=0;Side<2;++Side)
            {
                const FString S=Side==0?TEXT("_l"):TEXT("_r"); const float Sign=Side==0?-1.f:1.f;
                const float Stroke=FMath::Sin(Phase+Side*PI),Lift=FMath::Cos(Phase+Side*PI);
                Rotate(FName(*(TEXT("arm")+S)),FRotator(-12+Stroke*(18+22*Effort),0,Sign*(22+Lift*12)));
                Rotate(FName(*(TEXT("forearm")+S)),FRotator(-15-FMath::Max(0.f,-Stroke)*25,0,0));
                Rotate(FName(*(TEXT("hand")+S)),FRotator(Lift*14,0,0));
                Rotate(FName(*(TEXT("leg")+S)),FRotator(12-Stroke*(12+16*Effort),0,Sign*5));
                Rotate(FName(*(TEXT("knee")+S)),FRotator(-15-FMath::Max(0.f,Stroke)*20,0,0));
                Rotate(FName(*(TEXT("foot")+S)),FRotator(15+Lift*10,0,0));
            }
            for (int32 I=0;I<Pose.Num();++I) { FTransform Blended; Blended.Blend(Ground[I],Pose[I],Tooth->AnimationSwim); Pose[I]=Blended; }
        }
        if (Tooth->Expression) Tooth->Expression->BuildBodyPose(Pose,Ref);
        if (Tooth->Grip) Tooth->Grip->BuildPose(Pose,Ref,Dt);
        PlaceFeet(Tooth,Ref,Dt);
        if (Tooth->ToothPhysics) Tooth->ToothPhysics->BuildPresentationPose(Pose);
        if (Tooth->Expression) Tooth->Expression->BuildFacePose(Pose,Ref,Dt);
        if (Tooth->Gaze) Tooth->Gaze->BuildPose(Pose,Ref,Dt);
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
