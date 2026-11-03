#pragma once
#include "CoreMinimal.h"
#include "PreparedFreezeCPUSampler.h"
#include "MCFreezeNativeMotion.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UMorphTarget;
class FSkinWeightVertexBuffer;

/** Actual public-API preparation adapter. No GPU MeshObject/CPU-skin switch. */
class FMCFreezePoseCapture final
{
public:
    bool Prepare(USkeletalMeshComponent* Mesh,FString& Error);
    bool Capture(USkeletalMeshComponent* Mesh,const FTransform& EffectFrame,double AcceptedAt,FString& Error);
    const MCFreezeDeathSnapshot::FBuffer& GetSnapshot() const {return Snapshot;}
    TConstArrayView<uint8> GetTriangleSlots() const {return MakeArrayView(TriangleSlots);}
    TConstArrayView<FFinalSkinVertex> GetPoseForParityTest() const {return Sampler.GetCapturedPose();}
    bool IsCaptured() const {return Snapshot.IsSealed();}
    bool HasIssuedMotion() const {return Motion.GetShards().Num()==36;}
    /** Legacy fallback shares this same once-only producer; never reroll after capture. */
    bool IssueMotionForNativeFallback(const FBox& Bounds,FString& Error) {return Motion.IssueOnce(Bounds,Error);}
    TConstArrayView<MCFreezeDeathSnapshot::FNativeShard> GetIssuedMotion() const {return Motion.GetShards();}
private:
    TWeakObjectPtr<USkeletalMeshComponent> Source;
    TWeakObjectPtr<USkeletalMesh> Appearance;
    const void* RenderIdentity=nullptr;
    const FSkinWeightVertexBuffer* WeightIdentity=nullptr;
    TArray<TWeakObjectPtr<UMorphTarget>> MorphTargets;
    TArray<FMatrix44f> Matrices;
    TArray<FBoneIndexType> RequiredBones;
    TArray<uint8> TriangleSlots;
    MCPreparedFreezeCapture::FSampler Sampler;
    MCFreezeDeathSnapshot::FBuffer Snapshot;
    FMCFreezeNativeMotion Motion;
    int32 VertexCount=0,BoneCount=0;
    bool bPrepared=false;
};
