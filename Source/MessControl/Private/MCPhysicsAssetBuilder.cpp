#include "MCPhysicsAssetBuilder.h"
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
        C.SetDisableCollision(true); C.SetProjectionParams(true,0.1f,0.1f,10,20);
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
