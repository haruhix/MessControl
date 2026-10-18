#include "MCDirtPlacement.h"

#include "MCTongue.h"
#include "Engine/World.h"

FBox MCDirtPlacement::WorkBounds(const AMCTongue* Tongue,float Margin)
{
    if(!IsValid(Tongue) || !Tongue->Surface || !FMath::IsFinite(Margin) || Margin<0.f) return FBox(ForceInit);
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    if(!Bounds.IsValid || Bounds.GetSize().X<=SMALL_NUMBER || Bounds.GetSize().Y<=SMALL_NUMBER) return FBox(ForceInit);
    const float Near=FMath::Clamp(FMath::IsFinite(Tongue->GameplaySpawnNearDepth)?Tongue->GameplaySpawnNearDepth:.18f,0.f,.95f);
    const float Far=FMath::Clamp(FMath::IsFinite(Tongue->GameplaySpawnFarDepth)?Tongue->GameplaySpawnFarDepth:.82f,Near+.05f,1.f);
    const double MinX=Bounds.Max.X-Bounds.GetSize().X*Far+Margin;
    const double MaxX=Bounds.Max.X-Bounds.GetSize().X*Near-Margin;
    const double HalfY=Bounds.GetExtent().Y*.72-Margin,CenterY=Bounds.GetCenter().Y;
    if(MinX>=MaxX || HalfY<=0) return FBox(ForceInit);
    return FBox(FVector(MinX,CenterY-HalfY,Bounds.Min.Z),FVector(MaxX,CenterY+HalfY,Bounds.Max.Z));
}

bool MCDirtPlacement::IsInWorkArea(const AMCTongue* Tongue,FVector Point,float Margin)
{
    const FBox Bounds=WorkBounds(Tongue,Margin);
    return !Point.ContainsNaN() && Bounds.IsValid && Bounds.IsInsideXY(Point);
}

bool MCDirtPlacement::FindSurface(const AMCTongue* Tongue,FVector Point,float Radius,FHitResult& Hit)
{
    if(!FMath::IsFinite(Radius) || Radius<0.f) return false;
    const float Reach=Radius+65.f;
    if(!IsInWorkArea(Tongue,Point,Reach) || !Tongue->InteriorSurfacePoint(Point,Reach,Hit)) return false;
    // Two reachable sides alone allow a stain's far side to sit under a gum.
    // Reserve the whole patch and its brush approach above the actual tissue.
    // This full clearance replaces the eight separate food-processing approaches.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCDirtStaticClearance),false,Tongue);
    FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    FHitResult Block;
    const FVector Center=Hit.ImpactPoint+FVector(0,0,100);
    return !Tongue->GetWorld()->SweepSingleByObjectType(Block,Center,Center+FVector(0,0,.5),FQuat::Identity,
        Objects,FCollisionShape::MakeBox(FVector(Reach,Reach,85)),Query);
}
