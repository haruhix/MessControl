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
    {3.75,0,11},{5.1764,0,20.46},{4.9735,0,29.7},{4.6255,0,39.16},{4.205,0,48.4},
    {3.7265,0,57.86},{3.19,0,67.1},{2.61,0,76.56},{1.972,0,85.8},{1.2905,0,95.26},
    {0.58,0,104.72},{-0.1885,0,113.96},{-1.0005,0,123.42},{-1.8415,0,132.66},{-2.726,0,142.12},
    {-3.654,0,151.36},{-4.611,0,160.82},{-5.597,0,170.06},{-6.641,0,179.52},
    {-10.5705,0,188.54},{-14.5,0,197.56}
};

bool Sample(const UAnimSequence& Clip,float Time,FName BodyName,FName HandName,
    const FTransform& ProceduralBody,const FTransform& MeshWorld,FTransform& Out);
}
