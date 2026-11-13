#pragma once

#include "CoreMinimal.h"
#include "MCKatanaSwing.h"

class UAnimSequence;
class UMCAnimationProfile;
struct FAnimExtractContext;

namespace MCKatanaBlade
{
const UAnimSequence* ClipFor(const UMCAnimationProfile& Profile,MCKatanaSwing::EVariant Variant);
bool ReadMount(const UAnimSequence& Clip,const FAnimExtractContext& Context,FTransform& Out);
FQuat BodyTilt(const UAnimSequence& Clip,const FAnimExtractContext& Context,int32 BodyBone);

// +X is the cutting edge. Stations follow its curved outline in mesh-local cm.
inline const FVector Edge[]={
    {3.75,0,11},{3.63,0,15.3},{3.43,0,19.5},{3.19,0,23.8},{2.90,0,28},
    {2.57,0,32.3},{2.20,0,36.5},{1.80,0,40.8},{1.36,0,45},{.89,0,49.3},
    {.40,0,53.6},{-.13,0,57.8},{-.69,0,62.1},{-1.27,0,66.3},{-1.88,0,70.6},
    {-2.52,0,74.8},{-3.18,0,79.1},{-3.86,0,83.3},{-4.58,0,87.6},
    {-7.29,0,91.7},{-10,0,95.8}
};

bool Sample(const UAnimSequence& Clip,float Time,FName BodyName,FName HandName,
    const FTransform& ProceduralBody,const FTransform& MeshWorld,FTransform& Out);
}
