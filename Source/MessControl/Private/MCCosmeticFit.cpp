#include "MCCosmeticFit.h"
#include "Engine/SkeletalMesh.h"
#include "MCCosmeticDeformation.h"

FTransform MCCosmeticFit::ResolveAttachmentTransform(const USkeletalMesh* BodyMesh,FName AttachBone,const FTransform& AuthoredTransform,bool bMeshSpaceFit)
{
    if(!bMeshSpaceFit || !BodyMesh) return AuthoredTransform;
    const auto& Reference=BodyMesh->GetRefSkeleton();FTransform ReferenceBone=FTransform::Identity;
    for(int32 Bone=Reference.FindBoneIndex(AttachBone);Bone>=0;Bone=Reference.GetParentIndex(Bone))
        ReferenceBone=ReferenceBone*Reference.GetRefBonePose()[Bone];
    return AuthoredTransform.GetRelativeTransform(ReferenceBone);
}
void MCCosmeticFit::UpdateDeformation(UStaticMeshComponent* Part,const USkinnedMeshComponent* Body)
{
    MCCosmeticDeformation::Update(Part,Body);
}
