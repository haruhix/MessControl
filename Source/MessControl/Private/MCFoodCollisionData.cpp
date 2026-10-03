#include "MCFoodCollisionData.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"

void FMCFoodCollisionSettings::Sanitize()
{
    WholeHullLimit=FMath::Clamp(WholeHullLimit,1,32);
    FragmentHullLimit=FMath::Clamp(FragmentHullLimit,1,16);
    HullVertexLimit=FMath::Clamp(HullVertexLimit,8,256);
    VoxelResolution=FMath::Clamp(VoxelResolution,10000,1000000);
}

bool UMCFoodCollisionData::MatchesSource(const UStaticMesh* Mesh) const
{
    return Mesh && SourceMesh.ToSoftObjectPath()==FSoftObjectPath(Mesh);
}

bool UMCFoodCollisionData::HasValidCollision() const
{
    if(!BodySetup || BodySetup->AggGeom.ConvexElems.IsEmpty()
        || BodySetup->AggGeom.ConvexElems.Num()>HullLimit) return false;
    for(const FKConvexElem& Hull:BodySetup->AggGeom.ConvexElems)
    {
        if(Hull.VertexData.Num()<4 || Hull.VertexData.Num()>HullVertexLimit) return false;
        for(const FVector& Vertex:Hull.VertexData) if(Vertex.ContainsNaN()) return false;
    }
    return true;
}
