#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UProceduralMeshComponent;
DECLARE_LOG_CATEGORY_EXTERN(LogMCFoodCuts,Log,All);

namespace MCFoodCutGeometry
{
/** Clip with the stock triangle slicer, then cap independent valid closed contours. */
bool SliceAndCap(UProceduralMeshComponent* Mesh,FVector WorldPoint,FVector WorldNormal,UMaterialInterface* CapMaterial);

/** One edge-connected part in mesh-local space. A material/normal/UV seam is not a break. */
struct FIsland
{
    /** Stable for the same sections/index buffers: first surviving triangle order. */
    int32 Index=INDEX_NONE;
    FBox Bounds=FBox(ForceInit);
    TArray<FVector> Vertices;
    int32 TriangleCount=0;
};

/** Pure inspection. Invalid/oversized topology fails atomically; no small parts are discarded. */
bool FindIslands(const UProceduralMeshComponent* Mesh,TArray<FIsland>& Out,
    FString* Error=nullptr,int32 MaxIslands=256);

/** Retain one deterministic island, preserving section slots/materials and all vertex attributes. */
bool FilterIsland(UProceduralMeshComponent* Mesh,int32 IslandIndex,FString* Error=nullptr);
}
