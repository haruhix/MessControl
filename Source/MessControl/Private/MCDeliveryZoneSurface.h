#pragma once

#include "CoreMinimal.h"

class AMCTongue;
class UStaticMesh;
class UPrimitiveComponent;

namespace MCDeliveryGuide
{
/** The authored throat can visually overlap the playable tongue, without collision. */
struct FReceiverDepthGuard
{
    void Bind(UPrimitiveComponent* Component);
    void Restore();
private:
    TWeakObjectPtr<UPrimitiveComponent> Receiver;
    int32 PreviousStencil=0;
    bool bPreviousDepth=false,bBound=false;
};
struct FSurfaceVertex
{
    FIntVector Source;
    FVector Weights;
    float Alpha=0;
    float Lift=2;
};

struct FSurfaceOverlay
{
    TArray<FSurfaceVertex> Vertices;
    TArray<int32> Indices;
    void Reset() { Vertices.Reset();Indices.Reset(); }
};

struct FFootprintVertex
{
    FVector2D XY;
    float Alpha=0;
};

/** XY topology is fixed. Generated triangles stay inside a single native tongue face. */
struct FSurfaceCache
{
    FReceiverDepthGuard DepthGuard;
    bool Refresh(const AMCTongue* Tongue);
    void ClipQuad(FSurfaceOverlay& Out,const FVector& A,const FVector& B,const FVector& C,const FVector& D,
        float Alpha,float OtherAlpha=-1,float Lift=2) const;
    void AddRim(FSurfaceOverlay& Out,const TArray<FVector>& Outer,const TArray<FVector>& Inner) const;
    void Positions(const FSurfaceOverlay& Overlay,const FTransform& Destination,TArray<FVector>& Out) const;

    FSurfaceOverlay Floor,Arrows;
    TArray<FVector> FloorOuter,FloorInner;
    float FloorOpacity=-1;
    bool bReady=false;
private:
    struct FFace
    {
        FIntVector Source;
        FVector2D A,B,C;
        FBox2D Bounds;
        double Det=0;
    };
    struct FBoundary
    {
        int32 A=0,B=0,Face=0,Count=0;
    };
    static constexpr double CellSize=128;
    void Clip(FSurfaceOverlay& Out,TConstArrayView<FFootprintVertex> Polygon,float Lift) const;
    TWeakObjectPtr<UStaticMesh> SourceMesh;
    FTransform SourceTransform;
    int32 SourceVertexCount=0,SourceIndexCount=0;
    TArray<FFace> Faces;
    TMap<FIntPoint,TArray<int32>> Cells;
    TArray<FBoundary> Boundary;
    TArray<FVector> WorldVertices;
};
}
