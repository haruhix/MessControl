#include "MCToothAnimInstance.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGazeComponent.h"
#include "MCGripComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCBrushContactComponent.h"
#include "MCExpressionComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MCFoodActor.h"
#include "MCFirePatch.h"
#include "GameFramework/GameStateBase.h"
#include "MCThroat.h"
#include "MCToothMovementComponent.h"
#include "MCCoffeeFlood.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MCLocomotionCycle.h"
#include "TwoBoneIK.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

class FMCToothAnimProxy final : public FAnimInstanceProxy
{
public:
    explicit FMCToothAnimProxy(UAnimInstance* Instance):FAnimInstanceProxy(Instance) {}
    TArray<FTransform> Pose;
    FVector PlantedFeet[2]={FVector::ZeroVector,FVector::ZeroVector};
    bool FootPlanted[2]={false,false};
    FVector FootOffsets[2]={FVector::ZeroVector,FVector::ZeroVector};
    float FootWeights[2]={0,0};
    TWeakObjectPtr<UPrimitiveComponent> FootBases[2];
    FVector FootBasePoints[2]={FVector::ZeroVector,FVector::ZeroVector};
    bool FootReleased[2]={false,false};
    FVector PreviousMeshLocation=FVector::ZeroVector;
    float ArtistHandAlpha[2]={0,0};
    float ArtistPushAlpha=0,ArtistTiredAlpha=0;
    TWeakObjectPtr<AMCThroat> BraceThroat;
    FMCFootContactDebug Feet[2];
    FVector WallContacts[4]={};
    FVector WallLocalContacts[4]={};
    TWeakObjectPtr<UPrimitiveComponent> WallBases[4];
    bool WallPlanted[4]={false,false,false,false};
    double NextWallDiagnostic=0;
    float SprayPoseAlpha=0;
    float CalculusPoseAlpha=0;
    FTransform LastCalculusHand=FTransform::Identity;
    float DashPoseProgress=-1;
private:
    // Не активное, но будет использовано в будущем в других механиках.
    // Reserved full shoulder roll; the normal dash calls DashLungePose only.
    void FutureShoulderRollPose(const AMCToothCharacter* Tooth,const FReferenceSkeleton& Ref)
    {
        if (!Tooth->IsDashing() || !Tooth->ToothPhysics->CanAct()) return;
        const float Progress=FMath::Clamp(Tooth->GetDashProgress(),0.f,1.f);
        DashPoseProgress=Progress;
        const float Tuck=FMath::SmoothStep(0.f,.18f,Progress)*(1-FMath::SmoothStep(.8f,1.f,Progress));
        const float PoseAlpha=FMath::SmoothStep(0.f,.1f,Progress)*(1-FMath::SmoothStep(.9f,1.f,Progress));
        const TArray<FTransform> Locomotion=Pose;
        Pose=Ref.GetRefBonePose();
        TArray<FTransform> RestCS;RestCS.SetNum(Pose.Num());
        for (int32 I=0;I<Pose.Num();++I) RestCS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*RestCS[Ref.GetParentIndex(I)];
        const FQuat Facing=Tooth->StandingMeshTransform().GetRotation();
        auto Rotate=[&](FName Role,FRotator Delta) {
            const int32 Bone=Ref.FindBoneIndex(Tooth->RigBone(Role));if(Bone<0) return;
            const int32 Parent=Ref.GetParentIndex(Bone);
            const FQuat Basis=Parent<0?FQuat::Identity:RestCS[Parent].GetRotation();
            const FQuat MeshDelta=Facing.Inverse()*Delta.Quaternion()*Facing;
            Pose[Bone].SetRotation((Basis.Inverse()*MeshDelta*Basis*Pose[Bone].GetRotation()).GetNormalized());
        };
        Rotate(TEXT("gaze_head"),FRotator(22*Tuck,0,0));
        for(int32 Side=0;Side<2;++Side) {
            const FString S=Side==0?TEXT("_l"):TEXT("_r");const float Sign=Side==0?-1.f:1.f;
            Rotate(FName(*(TEXT("leg")+S)),FRotator(68*Tuck,0,Sign*8*Tuck));
            Rotate(FName(*(TEXT("knee")+S)),FRotator(-112*Tuck,0,0));
            Rotate(FName(*(TEXT("foot")+S)),FRotator(42*Tuck,0,0));
            Rotate(FName(*(TEXT("arm")+S)),FRotator(-48*Tuck,0,Sign*12*Tuck));
            Rotate(FName(*(TEXT("forearm")+S)),FRotator(-72*Tuck,0,0));
        }
        TArray<FTransform> CS;CS.SetNum(Pose.Num());
        auto Rebuild=[&](){for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*CS[Ref.GetParentIndex(I)];};
        Rebuild();
        const int32 Body=Ref.FindBoneIndex(Tooth->RigBone(TEXT("body")));
        if (Body<0) {Pose=Locomotion;return;}
        // Compact floating mittens curl against the face before the crown rolls.
        // Move the wrist branch together so the artist's hand stays proportional.
        for(int32 Side=0;Side<2;++Side) {
            const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("hand_l"):TEXT("hand_r")));
            const int32 Lower=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("forearm_l"):TEXT("forearm_r")));
            if(Hand<0 || Lower<0 || Ref.GetParentIndex(Hand)!=Lower) continue;
            FTransform Goal=CS[Hand];
            const FVector Touch=CS[Body].GetLocation()+Facing.Inverse().RotateVector(FVector(32,Side==0?-22:22,24));
            Goal.SetLocation(FMath::Lerp(Goal.GetLocation(),Touch,Tuck));
            const FTransform Branch=Ref.GetRefBonePose()[Hand].Inverse()*Goal;
            const int32 Parent=Ref.GetParentIndex(Lower);
            Pose[Lower]=Parent<0?Branch:Branch.GetRelativeTransform(CS[Parent]);
            Pose[Hand]=Ref.GetRefBonePose()[Hand];Rebuild();
        }
        for(int32 I=0;I<Pose.Num();++I) {FTransform Blended;Blended.Blend(Locomotion[I],Pose[I],PoseAlpha);Pose[I]=Blended;}
        Rebuild();
        const FTransform World=Tooth->GetMesh()->GetComponentTransform();
        FVector Direction=Tooth->GetDashDirection().GetSafeNormal2D();
        if(Direction.IsNearlyZero()) Direction=Tooth->GetActorForwardVector();
        // A diagonal rolling axis makes a shoulder-like tumble readable on a tooth
        // with no shoulders. Apply the complete turn after pose blending: quaternion
        // shortest-path blending would otherwise turn a 360-degree roll into a lean.
        const FVector Axis=World.InverseTransformVectorNoScale(
            FVector::CrossProduct(FVector::UpVector,Direction)+Direction*.38f).GetSafeNormal();
        const FQuat Turn(Axis,2*PI*FMath::SmoothStep(.12f,.82f,Progress));
        const FVector Up=World.InverseTransformVectorNoScale(FVector::UpVector);
        const FVector Pivot=CS[Body].GetLocation()+Up*28;
        FTransform Rolled=CS[Body];
        Rolled.SetLocation(Pivot+Up*(-12*Tuck)+Turn.RotateVector(CS[Body].GetLocation()-Pivot));
        Rolled.SetRotation((Turn*Rolled.GetRotation()).GetNormalized());
        const int32 Parent=Ref.GetParentIndex(Body);
        Pose[Body]=Parent<0?Rolled:Rolled.GetRelativeTransform(CS[Parent]);
    }
    void DashLungePose(const AMCToothCharacter* Tooth,const FReferenceSkeleton& Ref)
    {
        if(!Tooth->IsDashing() || !Tooth->ToothPhysics->CanAct()) return;
        const float Progress=FMath::Clamp(Tooth->GetDashProgress(),0.f,1.f);
        DashPoseProgress=Progress;
        const float Dip=FMath::SmoothStep(0.f,.16f,Progress)*(1-FMath::SmoothStep(.58f,1.f,Progress));
        const TArray<FTransform> Locomotion=Pose;
        Pose=Ref.GetRefBonePose();
        TArray<FTransform> RestCS;RestCS.SetNum(Pose.Num());
        for(int32 I=0;I<Pose.Num();++I) RestCS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*RestCS[Ref.GetParentIndex(I)];
        const FQuat Facing=Tooth->StandingMeshTransform().GetRotation();
        auto Rotate=[&](FName Role,FRotator Delta) {
            const int32 Bone=Ref.FindBoneIndex(Tooth->RigBone(Role));if(Bone<0) return;
            const int32 Parent=Ref.GetParentIndex(Bone);
            const FQuat Basis=Parent<0?FQuat::Identity:RestCS[Parent].GetRotation();
            const FQuat MeshDelta=Facing.Inverse()*Delta.Quaternion()*Facing;
            Pose[Bone].SetRotation((Basis.Inverse()*MeshDelta*Basis*Pose[Bone].GetRotation()).GetNormalized());
        };
        FVector Direction=Tooth->GetDashDirection().GetSafeNormal2D();
        if(Direction.IsNearlyZero()) Direction=Tooth->GetActorForwardVector();
        const float LeadSign=FVector::DotProduct(Direction,Tooth->GetActorRightVector())<-.01f?-1.f:1.f;
        // Keep the eyes forward, with a modest split stance instead of folded legs.
        Rotate(TEXT("gaze_head"),FRotator(14*Dip,0,-LeadSign*4*Dip));
        for(int32 Side=0;Side<2;++Side) {
            const FString S=Side==0?TEXT("_l"):TEXT("_r");const float Sign=Side==0?-1.f:1.f;
            const bool Lead=Sign==LeadSign;
            Rotate(FName(*(TEXT("leg")+S)),FRotator((Lead?18:-14)*Dip,0,Sign*3*Dip));
            Rotate(FName(*(TEXT("knee")+S)),FRotator((Lead?-28:-18)*Dip,0,0));
            Rotate(FName(*(TEXT("foot")+S)),FRotator((Lead?12:8)*Dip,0,0));
            Rotate(FName(*(TEXT("arm")+S)),FRotator((Lead?-28:-18)*Dip,0,Sign*8*Dip));
            Rotate(FName(*(TEXT("forearm")+S)),FRotator((Lead?-48:-36)*Dip,0,0));
        }
        TArray<FTransform> CS;CS.SetNum(Pose.Num());
        auto Rebuild=[&](){for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*CS[Ref.GetParentIndex(I)];};
        Rebuild();
        const int32 Body=Ref.FindBoneIndex(Tooth->RigBone(TEXT("body")));
        if(Body<0) {Pose=Locomotion;return;}
        // Compact mittens brace at the leading shoulder and trail beside the hip.
        // Move each wrist branch together to retain the artist's hand proportions.
        for(int32 Side=0;Side<2;++Side) {
            const float Sign=Side==0?-1.f:1.f;const bool Lead=Sign==LeadSign;
            const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("hand_l"):TEXT("hand_r")));
            const int32 Lower=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("forearm_l"):TEXT("forearm_r")));
            if(Hand<0 || Lower<0 || Ref.GetParentIndex(Hand)!=Lower) continue;
            FTransform Goal=CS[Hand];
            const FVector Touch=CS[Body].GetLocation()+Facing.Inverse().RotateVector(FVector(Lead?36:4,Sign*(Lead?23:27),Lead?16:7));
            Goal.SetLocation(FMath::Lerp(Goal.GetLocation(),Touch,Dip));
            const FTransform Branch=Ref.GetRefBonePose()[Hand].Inverse()*Goal;
            const int32 Parent=Ref.GetParentIndex(Lower);
            Pose[Lower]=Parent<0?Branch:Branch.GetRelativeTransform(CS[Parent]);
            Pose[Hand]=Ref.GetRefBonePose()[Hand];Rebuild();
        }
        for(int32 I=0;I<Pose.Num();++I) {FTransform Blended;Blended.Blend(Locomotion[I],Pose[I],Dip);Pose[I]=Blended;}
        Rebuild();
        const FTransform World=Tooth->GetMesh()->GetComponentTransform();
        const FVector Up=World.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
        const FVector Forward=World.InverseTransformVectorNoScale(Direction).GetSafeNormal();
        const FVector Right=World.InverseTransformVectorNoScale(FVector::CrossProduct(FVector::UpVector,Direction)).GetSafeNormal();
        // A bounded shoulder-led lunge: 28-degree forward lean, 10-degree side
        // dip and 12-degree twist. The tooth stays upright and recovers smoothly.
        const FQuat Lean(Right,FMath::DegreesToRadians(28.f*Dip));
        const FQuat Shoulder(Forward,FMath::DegreesToRadians(-LeadSign*10.f*Dip));
        const FQuat Twist(Up,FMath::DegreesToRadians(-LeadSign*12.f*Dip));
        FTransform Lunge=CS[Body];
        Lunge.SetLocation(Lunge.GetLocation()+(Forward*12-Up*3)*Dip);
        Lunge.SetRotation((Twist*Shoulder*Lean*Lunge.GetRotation()).GetNormalized());
        const int32 Parent=Ref.GetParentIndex(Body);
        Pose[Body]=Parent<0?Lunge:Lunge.GetRelativeTransform(CS[Parent]);
    }
public:
    void CollectionAndYawnPose(const AMCToothCharacter* Tooth,const FReferenceSkeleton& Ref)
    {
        const bool Yawn=Tooth->IsYawning();
        if(!Yawn && (!Tooth->FoodCollection || !Tooth->FoodCollection->bCollecting)) return;
        const FTransform World=Tooth->GetMesh()->GetComponentTransform();
        const float Alpha=Yawn?Tooth->YawnPoseAlpha():1.f;
        const auto* GS=Tooth->GetWorld()->GetGameState();const double Now=GS?GS->GetServerWorldTimeSeconds():Tooth->GetWorld()->GetTimeSeconds();
        const float Age=Now-Tooth->YawnStartedAt;
        float CatchDip=0;
        if(!Yawn) for(const auto& Piece:Tooth->FoodCollection->Pieces) if(IsValid(Piece)) {
            const float LandAge=(Now-Piece->StackPickup.StartedAt)*FMCStackPickup::PlayRate-.075f-Piece->StackPickup.FlightSeconds;
            if(LandAge>=0 && LandAge<.22f)
                CatchDip+=FMath::Sin(LandAge*26)*FMath::Exp(-18*LandAge)*5*(1-FMath::SmoothStep(.12f,.22f,LandAge));
        }
        TArray<FTransform> CS;CS.SetNum(Pose.Num());
        auto Rebuild=[&](){for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*CS[Ref.GetParentIndex(I)];};
        TArray<FTransform> ReferenceCS;ReferenceCS.SetNum(Pose.Num());
        for(int32 I=0;I<Pose.Num();++I) ReferenceCS[I]=Ref.GetParentIndex(I)<0?Ref.GetRefBonePose()[I]:Ref.GetRefBonePose()[I]*ReferenceCS[Ref.GetParentIndex(I)];
        auto Rotate=[&](const TCHAR* Role,FRotator R){
            const int32 I=Ref.FindBoneIndex(Tooth->RigBone(Role));if(I<0) return;
            const int32 Parent=Ref.GetParentIndex(I);const FQuat Basis=Parent>=0?ReferenceCS[Parent].GetRotation():FQuat::Identity;
            const FQuat Facing=Tooth->StandingMeshTransform().GetRotation(),Delta=Facing.Inverse()*R.Quaternion()*Facing;
            Pose[I].SetRotation((Basis.Inverse()*Delta*Basis*Pose[I].GetRotation()).GetNormalized());
        };
        const int32 Body=Ref.FindBoneIndex(Tooth->RigBone(TEXT("body")));
        if(Yawn && !Tooth->ToothPhysics->CanAct()) return;
        if(Yawn && Body>=0) {
            // Catch the inhale, plant the mittens, then lean away from the grip.
            // The torso elastically lags the swept capsule rather than tipping
            // the entire tooth onto its face. Slow strain pulses read as effort.
            const float Brace=FMath::SmoothStep(.25f,.95f,Age);
            const float Anticipation=FMath::Sin(FMath::Clamp(Age/.38f,0.f,1.f)*PI)*(1-Brace);
            const float Strain=FMath::Sin(Age*2*PI*1.35f)*Brace;
            const FVector Offset=-Tooth->YawnPullDirection*(24*Brace*Alpha)
                -FVector(0,0,(14+5*Brace+1.5f*Strain)*Alpha+4*Anticipation);
            const int32 Parent=Ref.GetParentIndex(Body);
            const FVector MeshOffset=World.InverseTransformVectorNoScale(Offset);
            Pose[Body].AddToTranslation(Parent<0?MeshOffset:ReferenceCS[Parent].InverseTransformVectorNoScale(MeshOffset));
            Rotate(TEXT("body"),FRotator((-22*Brace+7*Anticipation+2.5f*Strain)*Alpha,0,FMath::Sin(Age*2*PI*.65f)*1.8f*Alpha));
            Rotate(TEXT("gaze_head"),FRotator((14*Brace-7*Anticipation)*Alpha,0,0));
        }
        Rebuild();
        if(Yawn && Tooth->YawnTongue) {
            // A braced foot stays on the tongue while the other makes a small
            // recovery step. Solve the full chain so lowering the body does not
            // bury the feet or turn them into a pair of unrelated fast kicks.
            for(int32 Side=0;Side<2;++Side) {
                const FString S=Side==0?TEXT("_l"):TEXT("_r");
                const int32 Upper=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("leg")+S))));
                const int32 Lower=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("knee")+S))));
                const int32 End=Ref.FindBoneIndex(Tooth->RigBone(FName(*(TEXT("foot")+S))));
                if(Upper<0 || Lower<0 || End<0) continue;
                const float Phase=FMath::Frac(FMath::Max(0.f,Age-.8f)*.8f+Side*.5f);
                const float Step=Phase>.68f?FMath::Sin((Phase-.68f)/.32f*PI):0.f;
                const float Slide=FMath::SmoothStep(.6f,1.1f,Age);
                const float Sign=Side==0?-1.f:1.f;
                FVector Touch=World.TransformPosition(ReferenceCS[End].GetLocation())
                    +Tooth->GetActorRightVector()*Sign*7
                    -Tooth->YawnPullDirection*(8+Step*15)*Slide;
                FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCYawnFoot),false,Tooth);
                if(!Tooth->GetWorld()->LineTraceSingleByObjectType(Hit,Touch+FVector(0,0,55),Touch-FVector(0,0,90),FCollisionObjectQueryParams(ECC_WorldStatic),Q) || Hit.ImpactNormal.Z<.6f) continue;
                const float Sole=FMath::Clamp(Tooth->StandingMeshTransform().TransformPosition(ReferenceCS[End].GetLocation()).Z+Tooth->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),3.,20.);
                Touch.Z=Hit.ImpactPoint.Z+Sole+Step*7*Slide;
                FTransform U=CS[Upper],L=CS[Lower],F=CS[End];
                const float Reach=(FVector::Dist(U.GetLocation(),L.GetLocation())+FVector::Dist(L.GetLocation(),F.GetLocation()))*.97f;
                const FVector Goal=U.GetLocation()+(World.InverseTransformPosition(Touch)-U.GetLocation()).GetClampedToMaxSize(Reach);
                const FVector Pole=U.GetLocation()+World.InverseTransformVectorNoScale(Tooth->GetActorForwardVector()*40+Tooth->GetActorRightVector()*Sign*14);
                AnimationCore::SolveTwoBoneIK(U,L,F,Pole,Goal,false,1.,1.);
                const int32 Bones[]={Upper,Lower,End};const FTransform Solved[]={U,L,F};
                for(int32 J=0;J<3;++J) {
                    const int32 Parent=Ref.GetParentIndex(Bones[J]);
                    const FTransform Local=Parent<0?Solved[J]:Solved[J].GetRelativeTransform(J>0?Solved[J-1]:CS[Parent]);
                    FTransform Blended;Blended.Blend(Pose[Bones[J]],Local,Alpha);Pose[Bones[J]]=Blended;
                }
                Rebuild();
            }
        }
        for(int32 Side=0;Side<2;++Side) {
            const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("hand_l"):TEXT("hand_r")));
            const int32 Lower=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("forearm_l"):TEXT("forearm_r")));
            if(Hand<0 || Lower<0) continue;
            const FVector Point=Yawn?Tooth->YawnHandPoint(Side):Tooth->FoodCollection->HandPoint()+Tooth->GetActorRightVector()*(Side==0?-20:20)-FVector(0,0,FMath::Clamp(CatchDip,-3.f,3.f));
            // This rig uses floating mittens. Position their branch directly so
            // the skin's compact arm lengths do not prevent a real ground grip.
            FTransform Goal=CS[Hand];Goal.SetLocation(World.InverseTransformPosition(Point));
            if(Yawn) Goal.SetRotation(World.GetRotation().Inverse()*FRotationMatrix::MakeFromXZ(-Tooth->YawnPullDirection,FVector::UpVector).ToQuat());
            FTransform Blended;Blended.Blend(CS[Hand],Goal,Alpha);
            const FTransform Branch=Ref.GetRefBonePose()[Hand].Inverse()*Blended;
            const int32 Parent=Ref.GetParentIndex(Lower);
            Pose[Lower]=Parent<0?Branch:Branch.GetRelativeTransform(CS[Parent]);Pose[Hand]=Ref.GetRefBonePose()[Hand];Rebuild();
        }
    }
    void SprayTreatment(const AMCToothCharacter* Tooth,const FReferenceSkeleton& Ref,float Dt)
    {
        const auto* Inventory=Tooth->Inventory.Get();
        const bool HasTarget=Inventory && (Inventory->HealingTarget || Inventory->FireTarget);
        // A delayed target packet must not take back a hand already owned by
        // swimming, climbing or a food grip. Reset the old aiming blend too.
        if(!Inventory || Inventory->Selected!=EMCToolSlot::Spray || !Inventory->ShouldPresentTool() || !Tooth->ToothPhysics->CanAct()) {SprayPoseAlpha=0;return;}
        const bool Active=Tooth->CanWork() && (HasTarget || Tooth->IsPrimaryHeld());
        SprayPoseAlpha=FMath::FInterpTo(SprayPoseAlpha,Active?1.f:0.f,Dt,10.f);
        if(SprayPoseAlpha<.001f) return;
        const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(TEXT("hand_r"))),Lower=Ref.FindBoneIndex(Tooth->RigBone(TEXT("forearm_r")));
        if(Hand<0 || Lower<0 || Ref.GetParentIndex(Hand)!=Lower) return;
        TArray<FTransform> CS;CS.SetNum(Pose.Num());
        for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*CS[Ref.GetParentIndex(I)];
        const auto World=Tooth->GetMesh()->GetComponentTransform();
        // Follow the posed face so artist rig changes and body lean retain an
        // eye-height spray grip, with room beside the face for the can.
        FVector Face=FVector::ZeroVector;int32 EyeCount=0;
        for(const TCHAR* Role:{TEXT("eye_l"),TEXT("eye_r")}) {
            const int32 Eye=Ref.FindBoneIndex(Tooth->RigBone(Role));
            if(Eye!=INDEX_NONE) {Face+=World.TransformPosition(CS[Eye].GetLocation());++EyeCount;}
        }
        Face=EyeCount?Face/EyeCount:Tooth->GetActorLocation()+FVector(0,0,25);
        const FVector Palm=Face+Tooth->GetActorForwardVector()*45+Tooth->GetActorRightVector()*30;
        const FVector Aim=HasTarget?(Inventory->SprayAim()-Palm).GetSafeNormal():Tooth->GetActorForwardVector();
        const FQuat CanRotation=FRotationMatrix::MakeFromXZ(Aim,FVector::UpVector).ToQuat();
        FTransform Goal=CS[Hand];
        Goal.SetLocation(World.InverseTransformPosition(Palm));
        Goal.SetRotation(World.GetRotation().Inverse()*CanRotation*Tooth->BrushPivot->GetRelativeRotation().Quaternion().Inverse());
        FTransform Blended;Blended.Blend(CS[Hand],Goal,SprayPoseAlpha);
        const FTransform Branch=Ref.GetRefBonePose()[Hand].Inverse()*Blended;
        const int32 Parent=Ref.GetParentIndex(Lower);
        Pose[Lower]=Parent<0?Branch:Branch.GetRelativeTransform(CS[Parent]);
        Pose[Hand]=Ref.GetRefBonePose()[Hand];
    }
    void TraversalContacts(const AMCToothCharacter* Tooth,const FReferenceSkeleton& Ref,float Dt)
    {
        const auto* Move=Cast<UMCToothMovementComponent>(Tooth->GetCharacterMovement());
        if(!Move || !Tooth->ToothPhysics->CanAct()) return;
        const float Climb=Tooth->AnimationClimb,Swim=Tooth->AnimationSwim;
        if(Climb<.001f && Swim<.001f) { for(bool& Planted:WallPlanted) Planted=false; return; }
        const FTransform World=Tooth->GetMesh()->GetComponentTransform();
        const FVector Forward=Tooth->GetActorForwardVector(),Right=Tooth->GetActorRightVector();
        TArray<FTransform> CS;CS.SetNum(Pose.Num());
        auto Rebuild=[&](){for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*CS[Ref.GetParentIndex(I)];};
        Rebuild();
        auto Mitten=[&](int32 Side,FVector Touch,float Alpha) {
            const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("hand_l"):TEXT("hand_r")));
            const int32 Lower=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("forearm_l"):TEXT("forearm_r")));
            if(Hand<0 || Lower<0 || Ref.GetParentIndex(Hand)!=Lower) return;
            FTransform Goal=CS[Hand];Goal.SetLocation(FMath::Lerp(Goal.GetLocation(),World.InverseTransformPosition(Touch),Alpha));
            const FTransform Branch=Ref.GetRefBonePose()[Hand].Inverse()*Goal;
            const int32 Parent=Ref.GetParentIndex(Lower);
            Pose[Lower]=Parent<0?Branch:Branch.GetRelativeTransform(CS[Parent]);
            Pose[Hand]=Ref.GetRefBonePose()[Hand];Rebuild();
        };
        auto Foot=[&](int32 Side,FVector Touch,float Alpha) {
            const int32 Upper=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("leg_l"):TEXT("leg_r")));
            const int32 Lower=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("knee_l"):TEXT("knee_r")));
            const int32 End=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("foot_l"):TEXT("foot_r")));
            if(Upper<0 || Lower<0 || End<0) return;
            FTransform U=CS[Upper],L=CS[Lower],F=CS[End];
            const double Reach=(FVector::Distance(U.GetLocation(),L.GetLocation())+FVector::Distance(L.GetLocation(),F.GetLocation()))*.97;
            const FVector Goal=U.GetLocation()+(World.InverseTransformPosition(Touch)-U.GetLocation()).GetClampedToMaxSize(Reach);
            const FVector Pole=U.GetLocation()+World.InverseTransformVectorNoScale(-Forward*40+Right*(Side==0?-12:12));
            AnimationCore::SolveTwoBoneIK(U,L,F,Pole,Goal,false,1.,1.);
            const int32 Bones[]={Upper,Lower,End};const FTransform Solved[]={U,L,F};
            for(int32 J=0;J<3;++J) {const int32 Parent=Ref.GetParentIndex(Bones[J]);const FTransform Local=Parent<0?Solved[J]:Solved[J].GetRelativeTransform(J>0?Solved[J-1]:CS[Parent]);FTransform Blend;Blend.Blend(Pose[Bones[J]],Local,Alpha);Pose[Bones[J]]=Blend;}
            Rebuild();
        };
        if(Climb>.001f) {
            const FVector Normal=FVector(Move->ClimbNormal).GetSafeNormal();
            const FVector Lateral=FVector::CrossProduct(FVector::UpVector,-Normal).GetSafeNormal();
            const FVector Direction=Tooth->GetVelocity().GetSafeNormal();
            const bool Idle=Tooth->GetVelocity().Size()<5;
            static const bool DiagnoseClimb=[](){FString Case;return FParse::Value(FCommandLine::Get(),TEXT("MCApproval="),Case) && Case==TEXT("Climb");}();
            const bool Diagnose=DiagnoseClimb && Tooth->GetWorld()->GetTimeSeconds()>=NextWallDiagnostic;
            if(Diagnose) NextWallDiagnostic=Tooth->GetWorld()->GetTimeSeconds()+.2;
            for(int32 Limb=0;Limb<4;++Limb) {
                const bool Hand=Limb<2;const int32 Side=Limb%2;const float Sign=Side==0?-1.f:1.f;
                const auto Cycle=FMCLocomotionCycle::Sample(Tooth->AnimationClimbPhase/(2*PI)+Side*.5f+(Hand?0.f:.5f),.64f);
                const bool Stance=Idle || Cycle.bStance;
                if(!Stance || !WallBases[Limb].IsValid()) WallPlanted[Limb]=false;
                if(WallPlanted[Limb] && WallBases[Limb].IsValid()) WallContacts[Limb]=WallBases[Limb]->GetComponentTransform().TransformPosition(WallLocalContacts[Limb]);
                FVector Probe=Tooth->GetActorLocation()+Lateral*(Sign*(Hand?38:24))+FVector::UpVector*(Hand?34:-37);
                Probe+=Direction*Cycle.Sweep*(Hand?24:17);
                FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCTraversalContact),false,Tooth);
                FCollisionQueryParams Accurate(SCENE_QUERY_STAT(MCClimbEnamel),true,Tooth);
                auto FindContact=[&](const FVector& Candidate) {
                    FHitResult Contact;
                    if(!Tooth->GetWorld()->LineTraceSingleByChannel(Contact,Candidate+Normal*30,Candidate-Normal*140,ECC_Visibility,Q)
                        || Contact.bStartPenetrating || FVector::DotProduct(Contact.ImpactNormal,Normal)<.65f) return false;
                    // A gameplay box can extend beyond the curved crown. Never
                    // present it as a palm contact if the visible enamel is absent.
                    if(const auto* Arena=Cast<AMCArenaTooth>(Contact.GetActor())) {
                        if(!Arena->IsAvailable() || !Arena->BrushSurface->LineTraceComponent(Contact,Candidate+Normal*70,Candidate-Normal*180,Accurate)
                            || Contact.bStartPenetrating || FVector::DotProduct(Contact.ImpactNormal,Normal)<.35f) return false;
                    }
                    Hit=Contact;return true;
                };
                bool Found=FindContact(Probe);const bool PreferredFound=Found;
                FVector SelectedProbe=Probe;
                // A swimmer reaches the wall near the crown. A chest-height
                // palm ray can be above the lip: find a real grip just below it,
                // within the same bounded reach, instead of leaving a free hand.
                if(Hand && !Found) for(const FVector& Offset:{FVector(0,0,-20),FVector(0,0,-40),-Lateral*Sign*20,-Lateral*Sign*20-FVector(0,0,20),-Lateral*Sign*20-FVector(0,0,40)}) {
                    SelectedProbe=Probe+Offset;if(FindContact(SelectedProbe)) {Found=true;break;}
                }
                if(WallPlanted[Limb]) {
                    auto* Base=WallBases[Limb].Get();FHitResult Retained;
                    const auto* Arena=Base?Cast<AMCArenaTooth>(Base->GetOwner()):nullptr;
                    const bool EnamelValid=Base && (!Arena || (Arena->IsAvailable() && Base==Arena->BrushSurface.Get()));
                    // Keep a planted grip when the next preferred ray clears the
                    // lip, but confirm the moving surface and its actual reach.
                    if(!EnamelValid || FVector::Dist(WallContacts[Limb],Probe)>100
                        || !Base->LineTraceComponent(Retained,WallContacts[Limb]+Normal*20,WallContacts[Limb]-Normal*35,Arena?Accurate:Q)
                        || Retained.bStartPenetrating || FVector::DotProduct(Retained.ImpactNormal,Normal)<.35f
                        || FVector::Dist(WallContacts[Limb],Retained.ImpactPoint)>18) WallPlanted[Limb]=false;
                }
                const FVector Touch=Found?Hit.ImpactPoint+Hit.ImpactNormal*(Hand?7:6):FVector::ZeroVector;
                if(Found && Stance && !WallPlanted[Limb]) {WallContacts[Limb]=Touch;WallBases[Limb]=Hit.GetComponent();WallLocalContacts[Limb]=Hit.GetComponent()->GetComponentTransform().InverseTransformPosition(Touch);WallPlanted[Limb]=true;}
                const bool Solve=WallPlanted[Limb] || Found;
                const FVector Goal=WallPlanted[Limb]?WallContacts[Limb]:Touch+Normal*Cycle.Lift*18;
                if(Solve) {if(Hand) Mitten(Side,Goal,Climb);else Foot(Side,Goal,Climb);}
                if(Diagnose && Limb==1) UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_PLANNER right preferred=%d found=%d planted=%d stance=%d solve=%d climb=%.3f swim=%.3f P=%s probe=%s selected=%s hit=%s normal=%s base=%s goal=%s"),
                    PreferredFound,Found,WallPlanted[Limb],Stance,Solve,Climb,Swim,*Tooth->GetActorLocation().ToString(),*Probe.ToString(),*SelectedProbe.ToString(),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString(),*GetNameSafe(Hit.GetComponent()),*Goal.ToString());
            }
        } else {
            for(bool& Planted:WallPlanted) Planted=false;
            if(Swim>.001f && !Tooth->ClingTooth) {
                const float Effort=Tooth->AnimationSwimEffort,Phase=Tooth->AnimationStroke;
                const auto* Water=Move->DeepWaterAt(Tooth->GetActorLocation(),true);
                const float Surface=Water?Water->SurfaceHeightAt(Tooth->GetActorLocation()):Tooth->GetActorLocation().Z;
                for(int32 Side=0;Side<2;++Side) {
                    const float Sign=Side==0?-1.f:1.f,P=Phase+Side*PI;
                    // The mittens trace a real reach / pull / recovery loop instead
                    // of rotating a nearly invisible upper arm inside the tooth.
                    FVector Goal=Tooth->GetActorLocation()+Forward*(15+FMath::Sin(P)*(24+18*Effort))+Right*(Sign*(43+FMath::Cos(P)*12));
                    Goal.Z=Surface+4+FMath::Max(0.f,FMath::Cos(P))*(10+12*Effort);
                    Mitten(Side,Goal,Swim);
                }
            }
        }
    }
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
                    // A large load is contacted with the palm. Preserve relaxed finger bones
                    // rather than curling them through a flat surface with the grab clip.
                    const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("hand_l"):TEXT("hand_r")));
                    if (Tooth->Grip->Frame.Food && !Tooth->Grip->CanCarry(Tooth->Grip->Frame.Food) && I!=Hand)
                        for (int32 P=I;P>=0;P=Ref.GetParentIndex(P)) if (P==Hand) { Include=false; break; }
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
        const bool Reset=Tooth->IsDashing() || !Tooth->ToothPhysics->CanAct() || FVector::DistSquared(World.GetLocation(),PreviousMeshLocation)>FMath::Square(200.);
        PreviousMeshLocation=World.GetLocation();
        if (Reset)
        {
            for (int32 Side=0;Side<2;++Side) { FootPlanted[Side]=false; FootReleased[Side]=false; FootBases[Side].Reset(); FootWeights[Side]=0; FootOffsets[Side]=FVector::ZeroVector; }
            return;
        }
        const bool Allowed=Tooth->GetCharacterMovement()->IsMovingOnGround() && Tooth->AnimationClimb<.05f && Tooth->AnimationSwim<.05f && !Tooth->bPreviewAnimation
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
            const auto Cycle=FMCLocomotionCycle::Sample(Tooth->AnimationGait/(2*PI)+Side*.5f,Tooth->AnimationStance);
            const bool Stance=Allowed && (Tooth->AnimationSpeed<.05f || Cycle.bStance);
            if (!Stance || Tooth->AnimationSpeed<.05f) FootReleased[Side]=false;
            if (FootPlanted[Side] && FootBases[Side].IsValid()) PlantedFeet[Side]=FootBases[Side]->GetComponentTransform().TransformPosition(FootBasePoints[Side]);
            const FVector Probe=FootPlanted[Side] && Stance?PlantedFeet[Side]:Animated;
            FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFootGround),false,Tooth);
            const bool Grounded=Stance && !FootReleased[Side] && Tooth->GetWorld()->LineTraceSingleByObjectType(Hit,Probe+FVector(0,0,45),Probe-FVector(0,0,55),
                FCollisionObjectQueryParams(ECC_WorldStatic),Params) && Hit.ImpactNormal.Z>.65f;
            float Weight=0;
            if (Grounded)
            {
                Feet[Side].bHit=true; Feet[Side].Surface=Hit.GetComponent()->GetFName();
                const double LockDistance=FMath::Min(20.,(FVector::Distance(CS[Upper].GetLocation(),CS[Lower].GetLocation())+FVector::Distance(CS[Lower].GetLocation(),CS[Foot].GetLocation()))*.5);
                if (!FootPlanted[Side])
                {
                    PlantedFeet[Side]=Animated; FootBases[Side]=Hit.GetComponent();
                    FootBasePoints[Side]=Hit.GetComponent()->GetComponentTransform().InverseTransformPosition(Animated);
                }
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
                Weight=1-Tooth->AnimationSlip*.8f;
                // Release an overextended step once, then wait for swing. Repeatedly
                // relocating a planted foot was the source of visible contact searching.
                if (FVector::Dist2D(PlantedFeet[Side],Animated)>LockDistance*1.5)
                { FootReleased[Side]=true; FootPlanted[Side]=false; Weight=0; }
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
            if (Grounded)
            {
                const FVector Normal=World.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();
                const FVector Up=World.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
                FQuat Tilt=FQuat::FindBetweenNormals(Up,Normal);
                const float Angle=Tilt.GetAngle();
                if (Angle>PI/6) Tilt=FQuat::Slerp(FQuat::Identity,Tilt,(PI/6)/Angle);
                F.SetRotation((Tilt*F.GetRotation()).GetNormalized());
            }
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
        DashPoseProgress=-1;
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
        Rotate(TEXT("body"),FRotator(Tooth->AnimationPitch*Tooth->AnimationDirection.X-Tooth->AnimationBrake*7+Tooth->AnimationInertia.X*9,
            Tooth->AnimationTurn*-5,FMath::Sin(G)*Speed*A.Lean*.2f-Tooth->AnimationTurn*6-Tooth->AnimationInertia.Y*9));
        Translate(TEXT("body"),FVector(0,0,Tooth->AnimationBob-Tooth->AnimationLanding*4));
        for (int32 Side=0;Side<2;++Side)
        {
            const FString S=Side==0?TEXT("_l"):TEXT("_r");
            const auto Cycle=FMCLocomotionCycle::Sample(G/(2*PI)+Side*.5f,Tooth->AnimationStance);
            const float Step=Cycle.Sweep*Speed*(24+Tooth->AnimationRun*7)*(1-Tooth->AnimationSticky*.25f);
            const float Lift=Cycle.Lift*Speed*(22+Tooth->AnimationRun*16+Tooth->AnimationSticky*14);
            Rotate(FName(*(TEXT("leg")+S)),FRotator(Step*Tooth->AnimationDirection.X+Tooth->AnimationAir*18,0,-Step*Tooth->AnimationDirection.Y));
            Rotate(FName(*(TEXT("knee")+S)),FRotator(-Lift-Tooth->AnimationAir*30-Tooth->AnimationLanding*10,0,0));
            Rotate(FName(*(TEXT("foot")+S)),FRotator(-Step*.35f+Lift*.3f,0,Step*Tooth->AnimationDirection.Y*.25f));
        }
        // Free hands trail acceleration and spread on slippery ground to recover balance.
        Rotate(TEXT("gaze_head"),FRotator(-Tooth->AnimationInertia.X*3,Tooth->AnimationTurn*4,Tooth->AnimationInertia.Y*3));
        Rotate(TEXT("arm_l"),FRotator(-FMath::Sin(G-.25f)*24*Speed-Tooth->AnimationInertia.X*8,0,-10-Tooth->AnimationSlip*18));
        // Chopping tools move the wrist through an overhead arc below.
        const bool WideSwing=Tooth->Inventory && (Tooth->Inventory->Selected==EMCToolSlot::Pickaxe || Tooth->Inventory->Selected==EMCToolSlot::Knife) && Tooth->Inventory->ShouldPresentTool();
        FTransform CalculusHand; float CalculusBlend=0;
        const bool CalculusAllowed=WideSwing && Tooth->Inventory->Selected==EMCToolSlot::Pickaxe && Tooth->ToothPhysics->CanAct();
        const bool CalculusContact=CalculusAllowed
            && Tooth->Inventory->CalculusHandGoal(CalculusHand,CalculusBlend);
        if(CalculusContact) LastCalculusHand=CalculusHand.GetRelativeTransform(Tooth->GetActorTransform());
        CalculusPoseAlpha=CalculusAllowed?FMath::FInterpConstantTo(CalculusPoseAlpha,CalculusContact?1.f:0.f,Dt,CalculusContact?6.25f:3.33f):0.f;
        CalculusHand=LastCalculusHand*Tooth->GetActorTransform(); CalculusBlend=CalculusPoseAlpha;
        // Hold the ready pose across repeated swings. Only losing the aimed
        // cycle fades to the normal grip, rather than dropping it every hit.
        const bool AimedCalculus=CalculusAllowed && (CalculusContact || CalculusBlend>.001f);
        Rotate(TEXT("arm_r"),FRotator(FMath::Clamp((WideSwing?0:Tooth->AnimationBrushAngle)+FMath::Sin(G-.25f)*18*Speed-Tooth->AnimationInertia.X*8,-60.f,65.f),0,10+Tooth->AnimationSlip*18));
        Rotate(TEXT("hand_r"),FRotator(FMath::Clamp((AimedCalculus?-12.f*A.Exaggeration:Tooth->AnimationBrushAngle)*(WideSwing?1.f:.3f),WideSwing?-115.f:-35.f,WideSwing?115.f:35.f),0,0));
        // The current character has compact floating mittens. Move the wrist branch
        // through a visible overhead arc while preserving palm/finger proportions.
        if(WideSwing && !AimedCalculus && !Tooth->AnimationToolOffset.IsNearlyZero()) {
            Translate(TEXT("forearm_r"),Tooth->GetMesh()->GetComponentTransform().InverseTransformVectorNoScale(Tooth->AnimationToolOffset));
            Rotate(TEXT("body"),FRotator(Tooth->AnimationToolOffset.X*.10f,0,-Tooth->AnimationToolOffset.Z*.04f));
            Rotate(TEXT("arm_l"),FRotator(-Tooth->AnimationToolOffset.Z*.22f,0,-Tooth->AnimationToolOffset.Z*.10f));
        }
        if(AimedCalculus) {
            const float T=Tooth->GetToolSwingElapsed(),Contact=Tooth->Inventory->SwingContactTime();
            const float WindEnd=FMath::Max(.1f,Contact-.14f);
            const float Wind=FMath::SmoothStep(0.f,WindEnd,T)*(1-FMath::SmoothStep(WindEnd,Contact,T));
            const float Impact=FMath::SmoothStep(WindEnd,Contact,T)*(1-FMath::SmoothStep(Contact,Contact+.10f,T));
            Rotate(TEXT("body"),FRotator((-6*Wind+4*Impact)*CalculusBlend,0,-2*Wind*CalculusBlend));
            Rotate(TEXT("arm_l"),FRotator((-12*Wind+6*Impact)*CalculusBlend,0,-4*Wind*CalculusBlend));
        }
        // Blend a readable dog-paddle over locomotion; contact IK still owns a held hand.
        if (Tooth->AnimationSwim>.001f)
        {
            const TArray<FTransform> Ground=Pose; Pose=Ref.GetRefBonePose();
            const float Phase=Tooth->AnimationStroke,Effort=Tooth->AnimationSwimEffort;
            Rotate(TEXT("body"),FRotator(-28-24*Effort,0,FMath::Sin(Phase)*6));
            Rotate(TEXT("gaze_head"),FRotator(18+18*Effort,0,0));
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
        if(Tooth->AnimationClimb>.001f) {
            const float Alpha=Tooth->AnimationClimb,Phase=Tooth->AnimationClimbPhase;
            Rotate(TEXT("body"),FRotator(-8*Alpha,0,FMath::Sin(Phase)*3*Alpha));
            Translate(TEXT("body"),Tooth->GetMesh()->GetComponentTransform().InverseTransformVectorNoScale(-FVector(CastChecked<UMCToothMovementComponent>(Tooth->GetCharacterMovement())->ClimbNormal)*18*Alpha));
            Rotate(TEXT("gaze_head"),FRotator(-12*Alpha,0,0));
            for(int32 Side=0;Side<2;++Side) {
                const FString S=Side==0?TEXT("_l"):TEXT("_r");
                const float Stroke=FMath::Sin(Phase+Side*PI);
                Rotate(FName(*(TEXT("arm")+S)),FRotator((-48-Stroke*18)*Alpha,0,(Side==0?-10:10)*Alpha));
                Rotate(FName(*(TEXT("forearm")+S)),FRotator(-24*Alpha,0,0));
                Translate(FName(*(TEXT("forearm")+S)),Tooth->StandingMeshTransform().InverseTransformVectorNoScale(FVector(14,0,28+Stroke*16)*Alpha));
                Rotate(FName(*(TEXT("leg")+S)),FRotator((22-Stroke*16)*Alpha,0,0));
                Rotate(FName(*(TEXT("knee")+S)),FRotator((-38+Stroke*12)*Alpha,0,0));
            }
        }
        const float Prepare=Tooth->AnimationOrderPrepare,Flight=Tooth->AnimationOrderFlight,Press=Tooth->AnimationOrderPress;
        // Anticipation, reach in flight, then a short weighted crouch on the uvula.
        Translate(TEXT("body"),FVector(0,0,-10*Prepare-4*Press));
        Rotate(TEXT("body"),FRotator(10*Prepare-5*Flight+6*Press,0,0));
        for(int32 Side=0;Side<2;++Side) {
            const FString S=Side==0?TEXT("_l"):TEXT("_r"); const float Sign=Side==0?-1.f:1.f;
            Rotate(FName(*(TEXT("arm")+S)),FRotator(22*Prepare-48*Flight,0,Sign*(10*Flight+6*Press)));
            Rotate(FName(*(TEXT("forearm")+S)),FRotator(-14*Prepare-20*Flight,0,0));
            Rotate(FName(*(TEXT("leg")+S)),FRotator(22*Prepare+8*Flight+12*Press,0,0));
            Rotate(FName(*(TEXT("knee")+S)),FRotator(-36*Prepare-15*Flight-18*Press,0,0));
        }
        if (Tooth->Grip) Tooth->Grip->BuildPose(Pose,Ref,Dt);
        if (Tooth->BrushContact) Tooth->BrushContact->BuildPose(Pose,Ref,Dt);
        PlaceFeet(Tooth,Ref,Dt);
        if (Tooth->ToothPhysics) Tooth->ToothPhysics->BuildPresentationPose(Pose);
        TraversalContacts(Tooth,Ref,Dt);
        SprayTreatment(Tooth,Ref,Dt);
        CollectionAndYawnPose(Tooth,Ref);
        if(WideSwing && Tooth->Inventory->Selected==EMCToolSlot::Pickaxe && Tooth->ToothPhysics->CanAct()) {
            TArray<FTransform> CS; CS.SetNum(Pose.Num());
            for(int32 I=0;I<Pose.Num();++I) { const int32 Parent=Ref.GetParentIndex(I); CS[I]=Parent<0?Pose[I]:Pose[I]*CS[Parent]; }
            const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(TEXT("hand_r"))),Arm=Ref.FindBoneIndex(Tooth->RigBone(TEXT("forearm_r")));
            if(Hand>=0 && Arm>=0) {
                const FTransform MeshWorld=Tooth->GetMesh()->GetComponentTransform();
                const int32 Parent=Ref.GetParentIndex(Arm);
                if(AimedCalculus && CalculusBlend>.001f && Ref.GetParentIndex(Hand)==Arm) {
                    FTransform Blended;Blended.Blend(CS[Hand],CalculusHand.GetRelativeTransform(MeshWorld),CalculusBlend);
                    const FTransform Branch=Ref.GetRefBonePose()[Hand].Inverse()*Blended;
                    Pose[Arm]=Parent<0?Branch:Branch.GetRelativeTransform(CS[Parent]);
                    Pose[Hand]=Ref.GetRefBonePose()[Hand];
                } else {
                    const FVector Correction=Tooth->Inventory->ConstrainPickaxeGrip(CS[Hand]*MeshWorld);
                    const FTransform ParentWorld=Parent<0?MeshWorld:CS[Parent]*MeshWorld;
                    Pose[Arm].AddToTranslation(ParentWorld.InverseTransformVector(Correction));
                }
            }
        }
        // Brace both compact mittens against the uvula while the body stays in
        // front of the stalk. Move each weighted wrist branch rigidly.
        const auto* Base=Cast<UPrimitiveComponent>(Tooth->GetMovementBaseObject());
        auto* Uvula=Base?Cast<AMCThroat>(Base->GetOwner()):nullptr;
        if(Uvula && Uvula->UvulaLanding==Base) BraceThroat=Uvula;
        if(Press<=.001f || !Tooth->ToothPhysics->CanAct()) BraceThroat.Reset();
        // Keep the last support during release so the mittens return with the
        // same press blend instead of snapping home when the movement base clears.
        if(BraceThroat.IsValid() && Press>.001f) {
            TArray<FTransform> CS;CS.SetNum(Pose.Num());
            for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)>=0?Pose[I]*CS[Ref.GetParentIndex(I)]:Pose[I];
            const FTransform World=Tooth->GetMesh()->GetComponentTransform();
            for(int32 Side=0;Side<2;++Side) {
                const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("hand_l"):TEXT("hand_r")));
                const int32 Lower=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("forearm_l"):TEXT("forearm_r")));
                if(Hand<0 || Lower<0 || Ref.GetParentIndex(Hand)!=Lower) continue;
                const FVector Touch=BraceThroat->Uvula->GetComponentTransform().TransformPosition(FVector(-30,Side==0?-29:29,-48));
                FTransform Goal=CS[Hand];
                Goal.SetLocation(FMath::Lerp(Goal.GetLocation(),World.InverseTransformPosition(Touch),Press));
                const int32 Parent=Ref.GetParentIndex(Lower);
                const FTransform Branch=Ref.GetRefBonePose()[Hand].Inverse()*Goal;
                Pose[Lower]=Parent>=0?Branch.GetRelativeTransform(CS[Parent]):Branch;
                Pose[Hand]=Ref.GetRefBonePose()[Hand];
            }
        }
        DashLungePose(Tooth,Ref);
        if (Tooth->ToothPhysics) Tooth->ToothPhysics->SubmitAnimationTargets(Pose,Ref,Dt);
        if (Tooth->Expression) Tooth->Expression->BuildFacePose(Pose,Ref,Dt);
        if (Tooth->Gaze) Tooth->Gaze->BuildPose(Pose,Ref,Dt);
        if (auto* Diagnostics=Cast<UMCToothAnimInstance>(Instance); Diagnostics && Diagnostics->bRecordMotion)
        {
            Diagnostics->FootContacts[0]=Feet[0]; Diagnostics->FootContacts[1]=Feet[1];
            Diagnostics->DiagnosticDashProgress=DashPoseProgress;
            Diagnostics->DiagnosticMeshUp=Tooth->GetMesh()->GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
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
