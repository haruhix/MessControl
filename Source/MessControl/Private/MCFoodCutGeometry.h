#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Chaos/Convex.h"

class UMaterialInterface;
class UProceduralMeshComponent;
DECLARE_LOG_CATEGORY_EXTERN(LogMCFoodCuts,Log,All);

namespace MCFoodCutGeometry
{
/** Plain immutable collision data. Prepared without UObjects; installed only on the game thread. */
struct FPreparedCollision
{
    Chaos::FConvexPtr Convex;
    TArray<FVector> HullVertices;
    TArray<int32> HullIndices;
    FBox Bounds=FBox(ForceInit);
    bool bPlanarProxy=false;
    bool IsValid() const;
};
struct FConvexRequest
{
    TArray<FVector> Vertices;
    bool bAllowPlanarProxy=false;
};
/** Joins all plain-data jobs before return. Failure clears Out; no partial result is published. */
bool PrepareConvexBatch(const TArray<FConvexRequest>& Requests,TArray<TUniquePtr<FPreparedCollision>>& Out);
/** Immutable, compact mesh-local artwork with actor-local gameplay metrics. No UObject is retained by a cache. */
struct FPreparedMesh
{
    TArray<FProcMeshSection> Sections;
    TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
    TArray<FVector> Vertices;
    FBox Bounds=FBox(ForceInit);
    double LocalVolume=-1;
    /** Lazy derived data: accessed/published on GT only, never by ParallelFor workers. */
    mutable TSharedPtr<const FPreparedCollision> Collision;
    mutable bool bCollisionPrepared=false;
    mutable bool bCollisionAllowedPlanarProxy=false;
    bool MaterialsValid() const;
};
struct FPreparationStats
{
    uint64 RootImports=0,HistorySlices=0,TopologyPasses=0,IslandExtractions=0,Materializations=0,CacheHits=0,Installations=0,PhysicsBuilds=0;
    uint64 ConvexBuilds=0,ConvexCacheHits=0,ConvexBatches=0,ConvexParallelItems=0,CollisionInstalls=0;
    uint64 QueuedCuts=0,CommittedCuts=0,CancelledCuts=0,StagingTicks=0,StagedActors=0;
};
/** Game-thread diagnostics; counters describe work, never timing or gameplay decisions. */
FPreparationStats& PreparationStats();
FPreparationStats GetPreparationStats();
void ResetPreparationStats();
bool CapturePreparedMesh(const UProceduralMeshComponent* Mesh,const FTransform& SourceTransform,FPreparedMesh& Out);
bool MaterializePreparedMesh(UProceduralMeshComponent* Mesh,const FPreparedMesh& Prepared);
/** One topology/volume pass and one triangle partition for every independent part. */
bool ExtractPreparedIslands(const UProceduralMeshComponent* Mesh,const FTransform& SourceTransform,
    TArray<TSharedPtr<const FPreparedMesh>>& Out,FString* Error=nullptr);
/** Clip with the stock triangle slicer, then cap independent valid closed contours. */
bool SliceAndCap(UProceduralMeshComponent* Mesh,FVector WorldPoint,FVector WorldNormal,UMaterialInterface* CapMaterial);

/** How the accepted volume was obtained; Invalid must never mean an edible zero-volume part. */
enum class EVolumeMethod : uint8
{
    Invalid,
    ClosedMesh,
    ConvexHull,
    PlanarSurface
};

/** One edge-connected part in mesh-local space. A material/normal/UV seam is not a break. */
struct FIsland
{
    /** Stable for the same sections/index buffers: first surviving triangle order. */
    int32 Index=INDEX_NONE;
    FBox Bounds=FBox(ForceInit);
    TArray<FVector> Vertices;
    int32 TriangleCount=0;
    /** Cubic cm after MeasurementTransform; closed triangle geometry is preferred to its hull. */
    double Volume=0;
    /** Actual referenced triangle area in transformed square cm (including both sides if authored). */
    double SurfaceArea=0;
    EVolumeMethod VolumeMethod=EVolumeMethod::Invalid;
};

/** Pure inspection. Bounds/vertices remain mesh-local; scalar metrics use MeasurementTransform.
 * Open 3D artwork uses a conservative convex hull; planar artwork uses 0.1 cm nominal thickness.
 * Invalid/oversized topology or unmeasurable volume fails atomically; no small parts are discarded. */
bool FindIslands(const UProceduralMeshComponent* Mesh,TArray<FIsland>& Out,
    FString* Error=nullptr,int32 MaxIslands=256,const FTransform& MeasurementTransform=FTransform::Identity);

/** Retain one deterministic island, preserving section slots/materials and all vertex attributes. */
bool FilterIsland(UProceduralMeshComponent* Mesh,int32 IslandIndex,FString* Error=nullptr);
}
