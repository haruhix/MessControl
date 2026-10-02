#include "MCFoodBodyComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Chaos/Convex.h"

namespace
{
    struct FFoodCollisionKey
    {
        TWeakObjectPtr<UBodySetup> Source;
        FGuid Revision;
        FVector Scale, Origin;
        TWeakObjectPtr<UPhysicalMaterial> Material;
        bool operator==(const FFoodCollisionKey& Other) const
        {
            return Source==Other.Source && Revision==Other.Revision && Scale==Other.Scale
                && Origin==Other.Origin && Material==Other.Material;
        }
        friend uint32 GetTypeHash(const FFoodCollisionKey& Key)
        {
            return HashCombine(HashCombine(GetTypeHash(Key.Source),GetTypeHash(Key.Revision)),
                HashCombine(HashCombine(GetTypeHash(Key.Scale),GetTypeHash(Key.Origin)),GetTypeHash(Key.Material)));
        }
    };
    // Components retain the immutable setup through ShapeBodySetup. The cache must
    // not keep meshes or collision data alive after the last item is collected.
    TMap<FFoodCollisionKey,TWeakObjectPtr<UBodySetup>> FoodCollisionCache;
}

UMCFoodBodyComponent::UMCFoodBodyComponent()
{
    bUseArchetypeBodySetup=false;
}

void UMCFoodBodyComponent::SetCollisionMesh(UStaticMesh* Mesh,FVector ItemScale)
{
    if(CollisionMesh==Mesh && CollisionScale.Equals(ItemScale) && ShapeBodySetup) return;
    const bool Simulating=IsSimulatingPhysics();
    const FVector Velocity=GetPhysicsLinearVelocity(),Spin=GetPhysicsAngularVelocityInRadians();
    DestroyPhysicsState();
    ShapeBodySetup=nullptr;
    CollisionMesh=Mesh; CollisionScale=ItemScale;
    if(Mesh) InitBoxExtent(Mesh->GetBounds().BoxExtent*ItemScale.GetAbs());
    UpdateBodySetup(); UpdateBounds(); MarkRenderStateDirty();
    if(IsRegistered()) {
        RecreatePhysicsState();
        if(Simulating) {SetPhysicsLinearVelocity(Velocity);SetPhysicsAngularVelocityInRadians(Spin);}
        UpdateOverlaps();
    }
}

void UMCFoodBodyComponent::UpdateBodySetup()
{
    if(!CollisionMesh) {Super::UpdateBodySetup();return;}
    if(ShapeBodySetup) return;
    auto* Source=CollisionMesh->GetBodySetup();
    const FVector Origin=CollisionMesh->GetBounds().Origin;
    if(!Source) return;
    const FFoodCollisionKey Key{Source,Source->BodySetupGuid,CollisionScale,Origin,Source->PhysMaterial.Get()};
    for(auto It=FoodCollisionCache.CreateIterator();It;++It)
        if(!It.Key().Source.IsValid() || !It.Value().IsValid()) It.RemoveCurrent();
    if(const auto* Cached=FoodCollisionCache.Find(Key); Cached && Cached->IsValid())
    {
        ShapeBodySetup=Cached->Get();
        return;
    }
    ShapeBodySetup=NewObject<UBodySetup>(GetTransientPackage(),NAME_None,RF_Transient);
    ShapeBodySetup->CollisionTraceFlag=CTF_UseSimpleAsComplex;
    ShapeBodySetup->bNeverNeedsCookedCollisionData=true;
    auto AddHull=[&](const FKConvexElem& SourceHull) {
        TArray<Chaos::FConvex::FVec3Type> Vertices;
        Vertices.Reserve(SourceHull.VertexData.Num());
        for(const FVector& Vertex:SourceHull.VertexData)
            Vertices.Add(Chaos::FConvex::FVec3Type((SourceHull.GetTransform().TransformPosition(Vertex)-Origin)*CollisionScale));
        if(Vertices.Num()<4) return;
        // Cook only saved hulls. Chaos applies actor scale to their contacts and inertia.
        Chaos::FConvexPtr Convex=new Chaos::FConvex(Vertices,0);
        if(!Convex->IsValidGeometry()) return;
        auto& Hull=ShapeBodySetup->AggGeom.ConvexElems.AddDefaulted_GetRef();
        for(int32 I=0;I<Convex->NumVertices();++I) Hull.VertexData.Add(FVector(Convex->GetVertex(I)));
        Hull.UpdateElemBox(); Hull.SetConvexMeshObject(MoveTemp(Convex));
    };
    if(Source) {
        for(const auto& Hull:Source->AggGeom.ConvexElems) AddHull(Hull);
        // A genuinely box-shaped mesh, such as the engine cube, remains an exact box.
        for(const auto& Box:Source->AggGeom.BoxElems) {FKConvexElem Hull;Hull.ConvexFromBoxElem(Box);AddHull(Hull);}
        ShapeBodySetup->PhysMaterial=Source->PhysMaterial;
    }
    ShapeBodySetup->bCreatedPhysicsMeshes=true;
    if(ShapeBodySetup->AggGeom.ConvexElems.IsEmpty())
    {
        UE_LOG(LogTemp,Error,TEXT("Food mesh %s needs authored convex collision; refusing a bounding-box substitute"),*GetPathNameSafe(CollisionMesh));
    }
    else FoodCollisionCache.Add(Key,ShapeBodySetup.Get());
}

bool UMCFoodBodyComponent::HasMeshCollision() const
{
    return CollisionMesh && ShapeBodySetup && !ShapeBodySetup->AggGeom.ConvexElems.IsEmpty();
}
