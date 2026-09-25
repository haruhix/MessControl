#include "MCPhysicsAssetBuilder.h"
#include "MCDataAssets.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif

UPhysicsAsset* UMCPhysicsAssetBuilder::BuildToothPhysicsAsset(USkeletalMesh* Mesh)
{
#if WITH_EDITOR
    if (!Mesh) return nullptr;
    const FString PackageName=TEXT("/Game/Art/Rig/PA_ToothHero");
    UPackage* Package=CreatePackage(*PackageName);
    UPhysicsAsset* Asset=FindObject<UPhysicsAsset>(Package,TEXT("PA_ToothHero"));
    if (!Asset) { Asset=NewObject<UPhysicsAsset>(Package,TEXT("PA_ToothHero"),RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Asset); }
    Asset->SkeletalBodySetups.Empty(); Asset->ConstraintSetup.Empty(); Asset->CollisionDisableTable.Empty();
    const auto& Ref=Mesh->GetRefSkeleton(); const auto& Local=Ref.GetRefBonePose();
    TArray<FTransform> CS; CS.SetNum(Local.Num());
    for (int32 I=0;I<Local.Num();++I) CS[I]=Ref.GetParentIndex(I)>=0 ? Local[I]*CS[Ref.GetParentIndex(I)] : Local[I];
    for (int32 I=0;I<CS.Num();++I) UE_LOG(LogTemp,Display,TEXT("MC_RIG_BONE %s %s scale=%s"),*Ref.GetBoneName(I).ToString(),*CS[I].GetLocation().ToString(),*CS[I].GetScale3D().ToString());
    const TArray<FName> Names={TEXT("body"),TEXT("arm_l"),TEXT("hand_l"),TEXT("arm_r"),TEXT("hand_r"),TEXT("leg_l"),TEXT("leg_r")};
    for (const FName Name:Names)
    {
        const int32 Bone=Ref.FindBoneIndex(Name); if (Bone==INDEX_NONE) return nullptr;
        auto* Body=NewObject<USkeletalBodySetup>(Asset); Body->BoneName=Name; Body->PhysicsType=PhysType_Default;
        Body->bSkipScaleFromAnimation=true; Body->CollisionTraceFlag=CTF_UseSimpleAsComplex;
        Body->DefaultInstance.SetCollisionProfileName(TEXT("Ragdoll")); Body->DefaultInstance.bUseCCD=true;
        Body->DefaultInstance.LinearDamping=0.3f; Body->DefaultInstance.AngularDamping=1.5f;
        Body->DefaultInstance.PositionSolverIterationCount=12; Body->DefaultInstance.VelocitySolverIterationCount=4;
        Body->DefaultInstance.SetMassOverride(Name==TEXT("body")?5.f:0.5f);
        if (Name==TEXT("body"))
        {
            FKBoxElem Box; Box.Center=CS[Bone].InverseTransformPosition(FVector(0,0,67));
            Box.Rotation=CS[Bone].GetRotation().Inverse().Rotator(); Box.X=54; Box.Y=62; Box.Z=62;
            Body->AggGeom.BoxElems.Add(Box);
        }
        else
        {
            const bool bLeg=Name.ToString().StartsWith(TEXT("leg")); const bool bHand=Name.ToString().StartsWith(TEXT("hand"));
            FKSphylElem Capsule; Capsule.Radius=bLeg?12.f:8.f; Capsule.Length=bLeg?16.f:bHand?10.f:12.f;
            // Bone local Y follows the Blender bone length; the FBX reference transform determines its world direction.
            const FString TipName=bLeg?Name.ToString().Replace(TEXT("leg"),TEXT("foot")):bHand?Name.ToString():Name.ToString().Replace(TEXT("arm"),TEXT("hand"));
            FVector End=CS[Bone].GetLocation();
            if (!bHand) End=CS[Ref.FindBoneIndex(FName(*TipName))].GetLocation();
            else End+=CS[Bone].GetRotation().RotateVector(FVector(0,12,0));
            const FVector Start=CS[Bone].GetLocation();
            const FTransform Shape(FRotationMatrix::MakeFromZ((End-Start).GetSafeNormal()).ToQuat(),(Start+End)*0.5f);
            const FTransform Relative=Shape.GetRelativeTransform(CS[Bone]); Capsule.Center=Relative.GetLocation(); Capsule.Rotation=Relative.Rotator();
            Body->AggGeom.SphylElems.Add(Capsule);
        }
        Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes(); Asset->SkeletalBodySetups.Add(Body);
    }
    for (int32 I=1;I<Names.Num();++I)
    {
        const FName Child=Names[I]; const FName Parent=Child.ToString().StartsWith(TEXT("hand"))?FName(*Child.ToString().Replace(TEXT("hand"),TEXT("arm"))):FName(TEXT("body"));
        auto* Joint=NewObject<UPhysicsConstraintTemplate>(Asset);
        auto& C=Joint->DefaultInstance; C.JointName=Child; C.ConstraintBone1=Child; C.ConstraintBone2=Parent;
        const FTransform Anchor(FQuat::Identity,CS[Ref.FindBoneIndex(Child)].GetLocation());
        C.SetRefFrame(EConstraintFrame::Frame1,Anchor.GetRelativeTransform(CS[Ref.FindBoneIndex(Child)]));
        C.SetRefFrame(EConstraintFrame::Frame2,Anchor.GetRelativeTransform(CS[Ref.FindBoneIndex(Parent)]));
        C.SetLinearLimits(LCM_Locked,LCM_Locked,LCM_Locked,0);
        C.SetAngularSwing1Limit(ACM_Limited,65); C.SetAngularSwing2Limit(ACM_Limited,60); C.SetAngularTwistLimit(ACM_Limited,50);
        // Zero-stiffness soft limits are treated as free by Chaos. Keep hard stops;
        // procedural targets are bounded in the animation proxy before the drive.
        C.ProfileInstance.LinearLimit.bSoftConstraint=false;
        C.ProfileInstance.ConeLimit.bSoftConstraint=false;
        C.ProfileInstance.TwistLimit.bSoftConstraint=false;
        // UE 5.8's drive-target clamp can misread equivalent negative-hemisphere
        // quaternions near rest as a full turn. Do not clamp the target again here.
        C.ProfileInstance.AngularDrive.LimitViolationResponse=EAngularDriveLimitViolationResponse::None;
        C.SetDisableCollision(true); C.SetProjectionParams(true,0.1f,0.1f,10,20);
        // Serialize saves DefaultProfile, not the currently edited ProfileInstance.
        Joint->SetDefaultProfile(C);
        Asset->ConstraintSetup.Add(Joint);
    }
    // The compact silhouette overlaps naturally at the shoulders. Disable self collision, retain world collision.
    for (int32 A=0;A<Names.Num();++A) for (int32 B=A+1;B<Names.Num();++B) Asset->DisableCollision(A,B);
    Asset->UpdateBodySetupIndexMap(); Asset->UpdateBoundsBodiesArray(); Asset->SetPreviewMesh(Mesh);
    Mesh->SetPhysicsAsset(Asset); Asset->MarkPackageDirty(); Mesh->MarkPackageDirty();
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    UPackage::SavePackage(Package,Asset,*FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()),Args);
    return Asset;
#else
    return nullptr;
#endif
}

UPhysicsAsset* UMCPhysicsAssetBuilder::BuildPlayerPhysicsAsset(UMCPlayerAppearance* Appearance)
{
#if WITH_EDITOR
    if (!Appearance || !Appearance->SkeletalMesh) return nullptr;
    auto* Mesh=Appearance->SkeletalMesh.Get(); const auto& Ref=Mesh->GetRefSkeleton();
    const TArray<FName> Roles={TEXT("body"),TEXT("arm_l"),TEXT("forearm_l"),TEXT("hand_l"),TEXT("arm_r"),TEXT("forearm_r"),TEXT("hand_r"),TEXT("leg_l"),TEXT("knee_l"),TEXT("foot_l"),TEXT("leg_r"),TEXT("knee_r"),TEXT("foot_r")};
    const TArray<int32> Parents={-1,0,1,2,0,4,5,0,7,8,0,10,11};
    for (FName Role:Roles) if (Ref.FindBoneIndex(Appearance->Bone(Role))==INDEX_NONE)
    { UE_LOG(LogTemp,Error,TEXT("Missing rig role %s (%s)"),*Role.ToString(),*Appearance->Bone(Role).ToString()); return nullptr; }
    for (const FName Role:{FName("fingertip_l"),FName("fingertip_r"),FName("toe_l"),FName("toe_r")})
        if (Ref.FindBoneIndex(Appearance->Bone(Role))==INDEX_NONE) return nullptr;
    const FString PackageName=TEXT("/Game/Art/Rig/PA_TeethPlayer");
    UPackage* Package=CreatePackage(*PackageName);
    UPhysicsAsset* Asset=FindObject<UPhysicsAsset>(Package,TEXT("PA_TeethPlayer"));
    if (!Asset) { Asset=NewObject<UPhysicsAsset>(Package,TEXT("PA_TeethPlayer"),RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Asset); }
    Asset->SkeletalBodySetups.Empty(); Asset->ConstraintSetup.Empty(); Asset->CollisionDisableTable.Empty();
    TArray<FTransform> CS; CS.SetNum(Ref.GetNum());
    for (int32 I=0;I<CS.Num();++I) CS[I]=Ref.GetParentIndex(I)>=0?Ref.GetRefBonePose()[I]*CS[Ref.GetParentIndex(I)]:Ref.GetRefBonePose()[I];
    for (int32 I=0;I<Roles.Num();++I)
    {
        const FName Name=Appearance->Bone(Roles[I]); const int32 Bone=Ref.FindBoneIndex(Name);
        auto* Body=NewObject<USkeletalBodySetup>(Asset); Body->BoneName=Name; Body->PhysicsType=PhysType_Default;
        Body->bSkipScaleFromAnimation=true; Body->CollisionTraceFlag=CTF_UseSimpleAsComplex;
        Body->DefaultInstance.SetCollisionProfileName(TEXT("Ragdoll")); Body->DefaultInstance.bUseCCD=true;
        Body->DefaultInstance.LinearDamping=.3f; Body->DefaultInstance.AngularDamping=1.5f;
        Body->DefaultInstance.PositionSolverIterationCount=12; Body->DefaultInstance.VelocitySolverIterationCount=4;
        Body->DefaultInstance.SetMassOverride(I==0?5.f:.25f);
        if (I==0)
        {
            for (const FVector Center:{FVector(0,-3,75),FVector(0,-4,35)})
            {
                FKBoxElem Box; Box.Center=CS[Bone].InverseTransformPosition(Center);
                Box.Rotation=CS[Bone].GetRotation().Inverse().Rotator();
                Box.X=Center.Z>50?64:42; Box.Y=Center.Z>50?56:36; Box.Z=Center.Z>50?64:22;
                Body->AggGeom.BoxElems.Add(Box);
            }
        }
        else
        {
            const FString Role=Roles[I].ToString(); FVector Start=CS[Bone].GetLocation(), End;
            if (Role.StartsWith(TEXT("hand")))
                End=CS[Ref.FindBoneIndex(Appearance->Bone(FName(*Role.Replace(TEXT("hand"),TEXT("fingertip")))))].GetLocation();
            else if (Role.StartsWith(TEXT("foot")))
                End=CS[Ref.FindBoneIndex(Appearance->Bone(FName(*Role.Replace(TEXT("foot"),TEXT("toe")))))].GetLocation();
            else End=CS[Ref.FindBoneIndex(Appearance->Bone(Roles[I+1]))].GetLocation();
            FKSphylElem Capsule; Capsule.Radius=Role.StartsWith(TEXT("leg"))?8.f:5.5f;
            Capsule.Length=FMath::Max(2.f,float((End-Start).Size())-Capsule.Radius);
            const FTransform Shape(FRotationMatrix::MakeFromZ((End-Start).GetSafeNormal()).ToQuat(),(Start+End)*.5);
            const FTransform Local=Shape.GetRelativeTransform(CS[Bone]); Capsule.Center=Local.GetLocation(); Capsule.Rotation=Local.Rotator();
            Body->AggGeom.SphylElems.Add(Capsule);
        }
        Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes(); Asset->SkeletalBodySetups.Add(Body);
        if (I==0) continue;
        const int32 ParentBone=Ref.FindBoneIndex(Appearance->Bone(Roles[Parents[I]]));
        auto* Joint=NewObject<UPhysicsConstraintTemplate>(Asset); auto& C=Joint->DefaultInstance;
        C.JointName=Name; C.ConstraintBone1=Name; C.ConstraintBone2=Ref.GetBoneName(ParentBone);
        const FTransform Anchor(FQuat::Identity,CS[Bone].GetLocation());
        C.SetRefFrame(EConstraintFrame::Frame1,Anchor.GetRelativeTransform(CS[Bone]));
        C.SetRefFrame(EConstraintFrame::Frame2,Anchor.GetRelativeTransform(CS[ParentBone]));
        C.SetLinearLimits(LCM_Locked,LCM_Locked,LCM_Locked,0);
        C.SetAngularSwing1Limit(ACM_Limited,55); C.SetAngularSwing2Limit(ACM_Limited,50); C.SetAngularTwistLimit(ACM_Limited,35);
        C.ProfileInstance.LinearLimit.bSoftConstraint=false; C.ProfileInstance.ConeLimit.bSoftConstraint=false; C.ProfileInstance.TwistLimit.bSoftConstraint=false;
        C.ProfileInstance.AngularDrive.LimitViolationResponse=EAngularDriveLimitViolationResponse::None;
        C.SetDisableCollision(true); C.SetProjectionParams(true,.1f,.1f,10,20); Joint->SetDefaultProfile(C);
        Asset->ConstraintSetup.Add(Joint);
    }
    for (int32 A=0;A<Roles.Num();++A) for (int32 B=A+1;B<Roles.Num();++B) Asset->DisableCollision(A,B);
    Asset->UpdateBodySetupIndexMap(); Asset->UpdateBoundsBodiesArray(); Asset->SetPreviewMesh(Mesh); Asset->MarkPackageDirty();
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    UPackage::SavePackage(Package,Asset,*FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()),Args);
    return Asset;
#else
    return nullptr;
#endif
}
