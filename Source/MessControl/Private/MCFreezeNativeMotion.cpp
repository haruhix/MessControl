#include "MCFreezeNativeMotion.h"

bool FMCFreezeNativeMotion::IssueOnce(const FBox& Bounds,FString& Error)
{
    if(bIssued||!Bounds.IsValid||Bounds.Min.ContainsNaN()||Bounds.Max.ContainsNaN())
    {Error=TEXT("Fatal-freeze motion needs one finite accepted bound; native RNG cannot be replayed.");return false;}
    HalfSize=FMath::Max(70.f,float(Bounds.GetExtent().GetMax()+15.f));
    const FVector Center=Bounds.GetCenter();CubeCenter=FVector(Center.X,Center.Y,Bounds.Min.Z+HalfSize-4.f);
    if(!FMath::IsFinite(HalfSize)||HalfSize<=0||CubeCenter.ContainsNaN())
    {Error=TEXT("Fatal-freeze bound cannot produce finite native cells.");return false;}
    FRandomStream Random(9173);const FVector Cell(HalfSize*2/3,HalfSize*2/3,HalfSize*2/4);
    // Keep native nested-loop and argument/draw order, including compiler's
    // evaluation of FVector constructor arguments. No independent NS sampler.
    for(int32 Z=0;Z<4;++Z)for(int32 Y=0;Y<3;++Y)for(int32 X=0;X<3;++X)
    {
        const int32 I=X+3*(Y+3*Z);auto& M=Shards[I];
        M.Home=CubeCenter-FVector(HalfSize)+Cell*FVector(X+.5f,Y+.5f,Z+.5f);
        const FVector Direction=(M.Home-CubeCenter).GetSafeNormal();
        M.Velocity=Direction*Random.FRandRange(105.f,225.f)+FVector(Random.FRandRange(-25,25),Random.FRandRange(-25,25),Random.FRandRange(110,230));
        M.Spin=FVector(Random.FRandRange(-130,130),Random.FRandRange(-160,160),Random.FRandRange(-120,120));
        M.Floor=Bounds.Min.Z+Cell.GetMin()*.32f;
    }
    bIssued=true;return true;
}
