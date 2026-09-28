#include "MCToothAnimInstance.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCGazeComponent.h"
#include "MCGripComponent.h"
#include "MCExpressionComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MCFoodActor.h"
#include "TwoBoneIK.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"

class FMCToothAnimProxy final : public FAnimInstanceProxy
{
public:
    explicit FMCToothAnimProxy(UAnimInstance* Instance):FAnimInstanceProxy(Instance) {}
    TArray<FTransform> Pose;
    FVector PlantedFeet[2]={FVector::ZeroVector,FVector::ZeroVector};
    bool FootPlanted[2]={false,false};
    FVector FootOffsets[2]={FVector::ZeroVector,FVector::ZeroVector};
    float FootWeights[2]={0,0};
    FVector PreviousMeshLocation=FVector::ZeroVector;
    float ArtistHandAlpha[2]={0,0};
    float ArtistPushAlpha=0,ArtistTiredAlpha=0;
    FMCFootContactDebug Feet[2];
    void ArtistWorkPose(const AMCToothCharacter* Tooth,const FReferenceSkeleton& Ref,float Dt)
    {
        if (!Tooth->AnimationProfile || !Tooth->Grip) return;
        const auto* Profile=Tooth->AnimationProfile.Get();
        auto Apply=[&](UAnimSequence* Clip,float Alpha,int32 Side,bool BodyOnly)
        {
            if (!Clip || Alpha<.001f || !Clip->GetSkeleton()) return;
            // The 13-frame artist clips are reach/hold poses, not repeating loops.
            FAnimExtractContext Context(double(Clip->GetPlayLength())*FMath::Clamp(Alpha,0.f,1.f),false);
            for (int32 I=0;I<Pose.Num();++I)
            {
                const FName Name=Ref.GetBoneName(I); const FString Bone=Name.ToString();
                bool Include=false;
                if (BodyOnly) Include=Name==Tooth->RigBone(TEXT("body")) || Name==Tooth->RigBone(TEXT("gaze_head"));
                else
                {
                    const int32 Arm=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("arm_l"):TEXT("arm_r")));
                    for (int32 P=I;P>=0;P=Ref.GetParentIndex(P)) if (P==Arm) { Include=true; break; }
                }
                if (!Include) continue;
                const int32 Index=Clip->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Name); if (Index<0) continue;
                FTransform Sample,Blended; Clip->GetBoneTransform(Sample,FSkeletonPoseBoneIndex(Index),Context,false);
                Blended.Blend(Pose[I],Sample,Alpha); Pose[I]=Blended;
            }
        };
        const bool Active=Tooth->ToothPhysics->CanAct();
        for (int32 Side=0;Side<2;++Side)
        {
            ArtistHandAlpha[Side]=FMath::FInterpConstantTo(ArtistHandAlpha[Side],Active && Tooth->Grip->HandOccupied(Side==0)?1.f:0.f,Dt,4.f);
            Apply(Side==0?Profile->GrabLeft:Profile->GrabRight,ArtistHandAlpha[Side],Side,false);
        }
        ArtistPushAlpha=FMath::FInterpTo(ArtistPushAlpha,Active && Tooth->Grip->Frame.Food && Tooth->Grip->Frame.Pose==EMCGripPose::Push?.55f:0.f,Dt,8.f);
        Apply(Profile->Push,ArtistPushAlpha,0,true);
        ArtistTiredAlpha=FMath::FInterpTo(ArtistTiredAlpha,Active && Tooth->Grip->LoadMass()>8 && Tooth->GetVelocity().Size2D()<25?.18f:0.f,Dt,3.f);
        Apply(Profile->Tired,ArtistTiredAlpha,0,true);
    }
    void PlaceFeet(const AMCToothCharacter* Tooth,const FReferenceSkeleton& Ref,float Dt)
    {
        Feet[0]=Feet[1]=FMCFootContactDebug();
        const FTransform World=Tooth->GetMesh()->GetComponentTransform();
        const bool Reset=!Tooth->ToothPhysics->CanAct() || FVector::DistSquared(World.GetLocation(),PreviousMeshLocation)>FMath::Square(200.);
        PreviousMeshLocation=World.GetLocation();
        if (Reset)
        {
            for (int32 Side=0;Side<2;++Side) { FootPlanted[Side]=false; FootWeights[Side]=0; FootOffsets[Side]=FVector::ZeroVector; }
            return;
        }
        const bool Allowed=!Tooth->GetCharacterMovement()->IsFalling() && Tooth->AnimationSwim<.05f && !Tooth->bPreviewAnimation
            && (!Tooth->Expression || Tooth->Expression->BodyAlpha()<.01f);
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
            Feet[Side].Animated=Feet[Side].Target=Animated;
            const float Phase=Tooth->AnimationGait+(Side==0?0:PI);
            const bool Stance=Allowed && (Tooth->AnimationSpeed<.05f || FMath::Sin(Phase)<=0);
            FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFootGround),false,Tooth);
            const bool Grounded=Stance && Tooth->GetWorld()->LineTraceSingleByObjectType(Hit,Animated+FVector(0,0,45),Animated-FVector(0,0,55),
                FCollisionObjectQueryParams(ECC_WorldStatic),Params) && Hit.ImpactNormal.Z>.65f;
            float Weight=0;
            if (Grounded)
            {
                Feet[Side].bHit=true; Feet[Side].Surface=Hit.GetComponent()->GetFName();
                const double LockDistance=FMath::Min(10.,(FVector::Distance(CS[Upper].GetLocation(),CS[Lower].GetLocation())+FVector::Distance(CS[Lower].GetLocation(),CS[Foot].GetLocation()))*.3);
                if (!FootPlanted[Side] || FVector::Dist2D(PlantedFeet[Side],Animated)>LockDistance) PlantedFeet[Side]=Animated;
                FootPlanted[Side]=true;
                FVector Target=PlantedFeet[Side];
                // An ankle is above the sole. Driving the ankle itself into the
                // floor made the physics foot push back against the IK every frame.
                FTransform Rest=Ref.GetRefBonePose()[Foot];
                for (int32 Parent=Ref.GetParentIndex(Foot);Parent>=0;Parent=Ref.GetParentIndex(Parent)) Rest=Rest*Ref.GetRefBonePose()[Parent];
                const double SoleHeight=FMath::Clamp(Tooth->StandingMeshTransform().TransformPosition(Rest.GetLocation()).Z+Tooth->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),3.,20.);
                Target.Z=Animated.Z+FMath::Clamp(Hit.ImpactPoint.Z+SoleHeight*Tooth->GetActorScale3D().Z-Animated.Z,-12.,12.);
                FVector Offset=Target-Animated;
                const FVector Horizontal=FVector(Offset.X,Offset.Y,0).GetClampedToMaxSize(LockDistance);
                Offset.X=Horizontal.X; Offset.Y=Horizontal.Y;
                FootOffsets[Side]=FMath::Lerp(FootOffsets[Side],Offset,1.f-FMath::Exp(-18.f*Dt));
                Weight=Tooth->AnimationSpeed<.05f?1.f:FMath::Clamp(-FMath::Sin(Phase)*4.f,0.f,1.f);
            }
            else FootPlanted[Side]=false;
            // Losing a ray or entering swing fades the last correction instead
            // of replacing the entire leg pose in one frame.
            FootWeights[Side]=FMath::FInterpConstantTo(FootWeights[Side],Weight,Dt,10.f);
            if (FootWeights[Side]<.001f) { FootOffsets[Side]=FVector::ZeroVector; continue; }
            const FVector Target=Animated+FootOffsets[Side];
            Feet[Side].Target=Target; Feet[Side].bPlanted=FootPlanted[Side];
            FTransform U=CS[Upper],L=CS[Lower],F=CS[Foot];
            const FVector Pole=CS[Upper].GetLocation()+World.InverseTransformVectorNoScale(Tooth->GetActorForwardVector()*45);
            // Keep a small knee bend: the two-bone solution is singular at full
            // extension, so tiny contact changes otherwise move the knee sharply.
            const double Reach=(FVector::Distance(U.GetLocation(),L.GetLocation())+FVector::Distance(L.GetLocation(),F.GetLocation()))*.97;
            const FVector Goal=U.GetLocation()+(World.InverseTransformPosition(Target)-U.GetLocation()).GetClampedToMaxSize(Reach);
            AnimationCore::SolveTwoBoneIK(U,L,F,Pole,Goal,false,1.,1.);
            const int32 Bones[]={Upper,Lower,Foot}; const FTransform Solved[]={U,L,F};
            for (int32 J=0;J<3;++J)
            {
                const int32 B=Bones[J],Parent=Ref.GetParentIndex(B); FTransform Blended;
                // Blend complete local chains. Rebuilding after each world-space
                // blend applies the parent's correction again to its children and
                // changes segment lengths at partial IK weights.
                const FTransform Local=Parent<0?Solved[J]:Solved[J].GetRelativeTransform(J>0?Solved[J-1]:CS[Parent]);
                Blended.Blend(Pose[B],Local,FootWeights[Side]); Pose[B]=Blended;
            }
            Rebuild();
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
        ArtistWorkPose(Tooth,Ref,Dt);
        if (Tooth->Grip) Tooth->Grip->BuildPose(Pose,Ref,Dt);
        PlaceFeet(Tooth,Ref,Dt);
        if (Tooth->ToothPhysics) Tooth->ToothPhysics->BuildPresentationPose(Pose);
        if (Tooth->Expression) Tooth->Expression->BuildFacePose(Pose,Ref,Dt);
        if (Tooth->Gaze) Tooth->Gaze->BuildPose(Pose,Ref,Dt);
        if (auto* Diagnostics=Cast<UMCToothAnimInstance>(Instance); Diagnostics && Diagnostics->bRecordMotion)
        {
            Diagnostics->FootContacts[0]=Feet[0]; Diagnostics->FootContacts[1]=Feet[1];
            Diagnostics->DiagnosticPose.SetNum(Pose.Num());
            for (int32 I=0;I<Pose.Num();++I)
                Diagnostics->DiagnosticPose[I]=Ref.GetParentIndex(I)>=0?Pose[I]*Diagnostics->DiagnosticPose[Ref.GetParentIndex(I)]:Pose[I];
        }
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
