#pragma once

#include "CoreMinimal.h"
#include "MCFreezeCaptureLimits.h"
#include "SkeletalRenderPublic.h"

/** Read-only fatal-freeze capture. Plain CPU data; no asset creation, DI or render resource. */
namespace MCFreezeDeathSnapshot
{
    inline constexpr int32 MaxVertices=MCFreezeCaptureLimits::MaxVertices,MaxTriangles=MCFreezeCaptureLimits::MaxTriangles,MaxSlots=MCFreezeCaptureLimits::MaxSlots,CellCount=36;
    struct FTriangleIndex {uint32 A=0,B=0,C=0;uint8 Slot=0;};
    /** These are the existing native FShard samples. This code never samples RNG. */
    struct FNativeShard {FVector Home=FVector::ZeroVector,Velocity=FVector::ZeroVector,Spin=FVector::ZeroVector;float Floor=0;};
    struct FTriangle
    {
        FVector3f A,B,C,NA,NB,NC;
        FVector2f UA,UB,UC;
        uint16 Cell=0;
        uint8 Slot=0;
    };

    /** Prepare under the location barrier; Seal exactly once after the native pose capture. */
    class FBuffer final
    {
    public:
        bool Prepare(int32 InVertexCount,int32 InSlotCount,TConstArrayView<FTriangleIndex> InTriangles,FString& Error);
        bool Seal(TConstArrayView<FFinalSkinVertex> ActualPose,const FTransform& MeshWorld,const FTransform& EffectWorld,
            double AcceptedAt,TConstArrayView<FNativeShard> ActualShards,FString& Error);
        bool IsSealed() const {return bSealed;}
        TConstArrayView<FTriangle> GetTriangles() const {return bSealed?MakeArrayView(Triangles):TConstArrayView<FTriangle>();}
        TConstArrayView<FNativeShard> GetShards() const {return bSealed?MakeArrayView(Shards):TConstArrayView<FNativeShard>();}
        const FTransform& GetEffectFrame() const {check(bSealed);return EffectFrame;}
        FVector GetCubeCenter() const {check(bSealed);return CubeCenter;}
        float GetHalfSize() const {check(bSealed);return HalfSize;}
        double GetAcceptedAt() const {check(bSealed);return StartedAt;}
        int32 GetSlotCount() const {return SlotCount;}
        SIZE_T GetAllocatedBytes() const;
    private:
        TArray<FTriangleIndex> Topology;
        TArray<FTriangle> Triangles;
        TArray<FVector> Positions,Normals;
        TArray<FVector2D> UV;
        TArray<FNativeShard> Shards;
        FTransform EffectFrame=FTransform::Identity;
        FVector CubeCenter=FVector::ZeroVector;
        double StartedAt=0;
        float HalfSize=0;
        int32 VertexCount=0,SlotCount=0;
        bool bPrepared=false,bSealed=false;
    };

    /** Exactly24 floats fit Dynamic0..3, Color, Velocity and MaterialRandom. */
    struct FShaderTriangle
    {
        /** LWC position; never serialize world location as a packed Vector3f. */
        FVector Origin;
        FQuat4f Orientation;
        FVector3f Scale,EncodedVelocity;
        FVector4f Dynamic0,Dynamic1,Dynamic2,Dynamic3,Color;
        float MaterialRandom=0;
    };
    /** Frame is an already evaluated shard-to-world transform, with native Fade in its scale. */
    FShaderTriangle Pack(const FTriangle& Triangle,const FTransform& Frame);
}
