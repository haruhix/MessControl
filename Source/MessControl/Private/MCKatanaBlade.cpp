#include "MCKatanaBlade.h"

#include "MCDataAssets.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"

namespace MCKatanaBlade
{
const UAnimSequence* ClipFor(const UMCAnimationProfile& Profile,MCKatanaSwing::EVariant Variant)
{
    using MCKatanaSwing::EVariant;
    switch(Variant) {
    case EVariant::DiagonalRight:return Profile.KatanaDiagonalRight.Get();
    case EVariant::DiagonalLeft:return Profile.KatanaDiagonalLeft.Get();
    case EVariant::Horizontal:return Profile.KatanaHorizontal.Get();
    case EVariant::Charged:return Profile.KatanaCharged.Get();
    default:return nullptr;
    }
}

bool ReadMount(const UAnimSequence& Clip,const FAnimExtractContext& Context,FTransform& Out)
{
    auto Curve=[&](FName Name){return Clip.EvaluateCurveData(Name,Context);};
    if(Curve(TEXT("MCWeaponAuthored"))<.5f) return false;
    FQuat Rotation(Curve(TEXT("MCWeaponQX")),Curve(TEXT("MCWeaponQY")),
        Curve(TEXT("MCWeaponQZ")),Curve(TEXT("MCWeaponQW")));
    const FVector Position(Curve(TEXT("MCWeaponX")),Curve(TEXT("MCWeaponY")),Curve(TEXT("MCWeaponZ")));
    const FVector Scale(Curve(TEXT("MCWeaponSX")),Curve(TEXT("MCWeaponSY")),Curve(TEXT("MCWeaponSZ")));
    if(Rotation.ContainsNaN() || Rotation.SizeSquared()<.01f || Position.ContainsNaN()
        || Scale.ContainsNaN() || Scale.GetMin()<=0.f) return false;
    Rotation.Normalize();
    Out=FTransform(Rotation,Position,Scale);
    return true;
}

FQuat BodyTilt(const UAnimSequence& Clip,const FAnimExtractContext& Context,int32 BodyBone)
{
    if(BodyBone==INDEX_NONE) return FQuat::Identity;
    const auto& Reference=Clip.GetSkeleton()->GetReferenceSkeleton().GetRefBonePose();
    FTransform Sample=Reference[BodyBone],Base=Sample;
    Clip.GetBoneTransform(Sample,FSkeletonPoseBoneIndex(BodyBone),Context,false);
    Clip.GetBoneTransform(Base,FSkeletonPoseBoneIndex(BodyBone),FAnimExtractContext(0.,false),false);
    const FQuat Delta=(Sample.GetRotation()*Base.GetRotation().Inverse()).GetNormalized();
    return FQuat::Slerp(FQuat::Identity,Delta,.65f).GetNormalized();
}

bool Sample(const UAnimSequence& Clip,float Time,FName BodyName,FName HandName,
    const FTransform& ProceduralBody,const FTransform& MeshWorld,FTransform& Out)
{
    if(!Clip.GetSkeleton()) return false;
    const auto& Ref=Clip.GetSkeleton()->GetReferenceSkeleton();
    const int32 Body=Ref.FindBoneIndex(BodyName),Hand=Ref.FindBoneIndex(HandName);
    if(Hand==INDEX_NONE || Body==INDEX_NONE) return false;
    const FAnimExtractContext Context(FMath::Clamp(double(Time),0.,double(Clip.GetPlayLength())),false);
    FTransform Mount;
    if(!ReadMount(Clip,Context,Mount)) return false;
    TArray<FTransform,TInlineAllocator<8>> Chain;
    int32 Bone=Hand;
    for(;Bone!=INDEX_NONE && Bone!=Body;Bone=Ref.GetParentIndex(Bone)) {
        FTransform Local=Ref.GetRefBonePose()[Bone];
        Clip.GetBoneTransform(Local,FSkeletonPoseBoneIndex(Bone),Context,false);
        Chain.Add(Local);
    }
    if(Bone!=Body) return false;
    FTransform BodyLocal=ProceduralBody;
    BodyLocal.SetRotation((BodyTilt(Clip,Context,Body)*BodyLocal.GetRotation()).GetNormalized());
    // The procedural proxy leaves the root ancestry above the body at rest.
    FTransform ParentInMesh=FTransform::Identity;
    TArray<int32,TInlineAllocator<4>> BodyParents;
    for(int32 Parent=Ref.GetParentIndex(Body);Parent!=INDEX_NONE;Parent=Ref.GetParentIndex(Parent))
        BodyParents.Add(Parent);
    for(int32 Index=BodyParents.Num()-1;Index>=0;--Index)
        ParentInMesh=Ref.GetRefBonePose()[BodyParents[Index]]*ParentInMesh;
    FTransform InMesh=BodyLocal*ParentInMesh;
    for(int32 Index=Chain.Num()-1;Index>=0;--Index) InMesh=Chain[Index]*InMesh;
    Out=Mount*(InMesh*MeshWorld);
    return !Out.ContainsNaN();
}
}
