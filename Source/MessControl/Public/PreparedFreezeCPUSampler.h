#pragma once

#include "CoreMinimal.h"
#include "SkeletalRenderPublic.h"

/** E-only source proposal, UNBUILT. No UObject/mesh/render-resource creation. */
namespace MCPreparedFreezeCapture
{
    inline constexpr int32 MaxVertices=32768,MaxBones=512,MaxMorphs=128,MaxMorphDeltas=262144,MaxInfluences=12;
    struct FReferenceVertex
    {
        FVector3f Position=FVector3f::ZeroVector;
        FVector4f TangentX,TangentZ; // Packed only under the native capture rounding scope.
        FVector2f UV0=FVector2f::ZeroVector;
        // Resolved through the actual section BoneMap during preparation.
        uint16 Bones[MaxInfluences]={},Weights[MaxInfluences]={};
    };
    struct FMorphDelta {uint32 SourceIdx=0;FVector3f PositionDelta,TangentZDelta;};
    struct FMorphRange {int32 First=0,Count=0;};
    /** Preserve actual ActiveMorphTargets traversal order; do not sort by name. */
    struct FAcceptedMorph {int32 PreparedIndex=INDEX_NONE;float Weight=0;};

    class FSampler final
    {
    public:
        bool Prepare(TConstArrayView<FReferenceVertex> InReference,TConstArrayView<FMorphRange> InMorphs,
            TConstArrayView<FMorphDelta> InDeltas,int32 InBoneCount,int32 InInfluenceCount,FString& Error);
        /** Captures one already-accepted, coherent bone/morph pose. No Refresh/LOD/CPU-skin toggles. */
        bool Capture(TConstArrayView<FMatrix44f> ActualRefToLocal,TConstArrayView<FAcceptedMorph> ActualMorphOrder,
            float ActualMaxMorphBlendWeight,FString& Error);
        TConstArrayView<FFinalSkinVertex> GetCapturedPose() const {return bCaptured?MakeArrayView(Output):TConstArrayView<FFinalSkinVertex>();}
        SIZE_T GetAllocatedBytes() const;
    private:
        TArray<FReferenceVertex> Reference;
        TArray<FMorphRange> Morphs;
        TArray<FMorphDelta> Deltas;
        TArray<FFinalSkinVertex> Output;
        int32 Cursor[MaxMorphs]={};
        bool ValidMorph[MaxMorphs]={};
        int32 BoneCount=0,InfluenceCount=0;
        bool bPrepared=false,bCaptured=false;
    };
}
