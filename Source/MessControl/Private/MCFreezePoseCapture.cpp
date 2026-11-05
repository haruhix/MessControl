#include "MCFreezePoseCapture.h"
#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Rendering/MorphTargetVertexInfoBuffers.h"
#include "Rendering/MorphTargetVertexCodec.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCToothCharacter.h"
#include "Engine/World.h"
#include "RHIGPUReadback.h"
#include "RHICommandList.h"
#include "RenderingThread.h"

namespace MCFreezeCapturePrivate
{
struct FCookedMorphCache
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<USkeletalMesh> Appearance;
    const void* Render=nullptr;
    const void* GPUBuffer=nullptr;
    TArray<MCPreparedFreezeCapture::FMorphRange> Ranges;
    TArray<MCPreparedFreezeCapture::FMorphDelta> Deltas;
};

bool PrepareCookedMorphs(USkeletalMeshComponent* Mesh,const FSkeletalMeshLODRenderData& LOD,const void* Render,
    TArray<MCPreparedFreezeCapture::FMorphRange>& Ranges,TArray<MCPreparedFreezeCapture::FMorphDelta>& Deltas,FString& Error)
{
    using namespace MCPreparedFreezeCapture;
    using namespace UE::MorphTargetVertexCodec;
    auto* World=Mesh->GetWorld();auto* Asset=Mesh->GetSkeletalMeshAsset();
    const auto* Hero=Cast<AMCToothCharacter>(Mesh->GetOwner());
    if(!World || World->bIsTearingDown || (Hero && Hero->IsFreezingToDeath()))
    {Error=TEXT("Cooked morph preparation requires an uncommitted live appearance.");return false;}
    for(const auto& Target:Asset->GetMorphTargets())if(!Target || !Target->UsesBuiltinMorphTargetCompression())
    {Error=TEXT("Cooked freeze capture cannot decode unknown custom morph compression.");return false;}
    const auto& Morph=LOD.MorphTargetVertexInfoBuffers;
    const FBufferRHIRef Buffer=Morph.MorphDataBuffer;
    static TArray<FCookedMorphCache> Cache;
    Cache.RemoveAllSwap([](const FCookedMorphCache& Entry){return !Entry.World.IsValid() || !Entry.Appearance.IsValid();});
    for(const auto& Entry:Cache)if(Entry.World.Get()==World && Entry.Appearance.Get()==Asset
        && Entry.Render==Render && Entry.GPUBuffer==Buffer.GetReference() && Entry.Ranges.Num()==Asset->GetMorphTargets().Num())
    {Ranges=Entry.Ranges;Deltas=Entry.Deltas;return true;}
    const auto* Preparation=World->GetSubsystem<UMCWorldPreloadSubsystem>();
    if(!Preparation || !Preparation->IsPreparing() || Preparation->IsReady() || Preparation->HasFailed())
    {Error=TEXT("A missing cooked morph cache may only be read back during location preparation, never at fatal use.");return false;}
    if(Cache.Num()>=32 || Morph.GetNumMorphs()!=uint32(Asset->GetMorphTargets().Num()))
    {Error=TEXT("Cooked morph appearance/cache identity or bounded capacity is invalid.");return false;}
    if(Morph.GetNumBatches()==0) {
        for(uint32 Index=0;Index<Morph.GetNumMorphs();++Index)if(Morph.GetNumBatches(Index)!=0 || Morph.GetBatchStartOffset(Index)!=0)
        {Error=TEXT("Empty cooked morph target metadata is inconsistent.");return false;}
        // The real GPU codec can quantize every source delta to zero.
        Ranges.Reset();Ranges.SetNumZeroed(Asset->GetMorphTargets().Num());Deltas.Reset();
        auto& Entry=Cache.AddDefaulted_GetRef();Entry.World=World;Entry.Appearance=Asset;Entry.Render=Render;
        Entry.GPUBuffer=Buffer.GetReference();Entry.Ranges=Ranges;
        UE_LOG(LogTemp,Log,TEXT("MC_FREEZE_COOKED_MORPHS_PREPARED mesh=%s morphs=%d deltas=0 bytes=0 loadingReadback=0"),*Asset->GetPathName(),Ranges.Num());
        return true;
    }
    if(!Buffer.IsValid()){Error=TEXT("Cooked morph GPU buffer is not resident during preparation.");return false;}
    constexpr uint64 MaxBytes=uint64(MaxMorphDeltas)*(sizeof(FMorphTargetDelta)+NumBatchHeaderDwords*sizeof(uint32))+16;
    const uint64 Bytes=Buffer->GetSize();const uint32 Batches=Morph.GetNumBatches();
    const uint64 HeaderWords=uint64(Batches)*NumBatchHeaderDwords;
    if(Bytes==0 || Bytes%sizeof(uint32) || Bytes>MaxBytes || Bytes>MAX_uint32 || Batches>uint32(MaxMorphDeltas)
        || HeaderWords>Bytes/sizeof(uint32) || !FMath::IsFinite(Morph.GetPositionPrecision()) || Morph.GetPositionPrecision()<=0
        || !FMath::IsFinite(Morph.GetTangentZPrecision()) || Morph.GetTangentZPrecision()<=0)
    {Error=TEXT("Cooked GPU morph buffer size/header/precision exceeds its prepared bounds.");return false;}
    TArray<uint32> Words;Words.SetNumUninitialized(int32(Bytes/sizeof(uint32)));bool Copied=false;
    ENQUEUE_RENDER_COMMAND(MCPrepareFreezeMorphReadback)([Buffer,Bytes,&Words,&Copied](FRHICommandListImmediate& RHICmdList){
        FRHIGPUBufferReadback Readback(TEXT("MCFreezeLoadingMorphs"));
        RHICmdList.Transition(FRHITransitionInfo(Buffer.GetReference(),ERHIAccess::SRVMask,ERHIAccess::CopySrc));
        Readback.EnqueueCopy(RHICmdList,Buffer.GetReference(),uint32(Bytes));
        RHICmdList.Transition(FRHITransitionInfo(Buffer.GetReference(),ERHIAccess::CopySrc,ERHIAccess::SRVMask));
        RHICmdList.SubmitAndBlockUntilGPUIdle();
        if(Readback.IsReady())if(const void* Data=Readback.Lock(uint32(Bytes))) {
            FMemory::Memcpy(Words.GetData(),Data,SIZE_T(Bytes));Readback.Unlock();Copied=true;
        }
    });
    FlushRenderingCommands();
    if(!Copied){Error=TEXT("Loading-time cooked morph GPU readback did not complete.");return false;}
    TArray<FDeltaBatchHeader> Headers;Headers.SetNumUninitialized(Batches);
    uint64 TotalElements=0;
    for(uint32 Index=0;Index<Batches;++Index) {
        auto& Header=Headers[Index];ReadHeader(Header,MakeArrayView<const uint32>(Words.GetData()+uint64(Index)*NumBatchHeaderDwords,NumBatchHeaderDwords));
        auto BitsValid=[](int8 Bits,int8 Max){return Bits>=0 && Bits<=Max;};
        if(Header.NumElements==0 || Header.NumElements>BatchSize || !BitsValid(Header.IndexBits,31)
            || !BitsValid(Header.PositionBits.X,28) || !BitsValid(Header.PositionBits.Y,28) || !BitsValid(Header.PositionBits.Z,28)
            || !BitsValid(Header.TangentZBits.X,16) || !BitsValid(Header.TangentZBits.Y,16) || !BitsValid(Header.TangentZBits.Z,16)
            || Header.DataOffset%sizeof(uint32) || uint64(Header.DataOffset)/sizeof(uint32)<HeaderWords
            || uint64(Header.DataOffset)/sizeof(uint32)+CalculateBatchDwords(Header)>uint64(Words.Num())
            || (TotalElements+=Header.NumElements)>uint64(MaxMorphDeltas))
        {Error=TEXT("Cooked GPU morph batch is malformed or exceeds the prepared delta bounds.");return false;}
        auto SignedSpanValid=[](int32 Min,int8 Bits){return int64(Min)+int64((uint64(1)<<Bits)-1)<=MAX_int32;};
        if(uint64(Header.IndexMin)+(uint64(1)<<Header.IndexBits)-1+Header.NumElements-1>MAX_uint32
            || !SignedSpanValid(Header.PositionMin.X,Header.PositionBits.X)
            || !SignedSpanValid(Header.PositionMin.Y,Header.PositionBits.Y)
            || !SignedSpanValid(Header.PositionMin.Z,Header.PositionBits.Z)
            || (Header.bTangents && (!SignedSpanValid(Header.TangentZMin.X,Header.TangentZBits.X)
                || !SignedSpanValid(Header.TangentZMin.Y,Header.TangentZBits.Y)
                || !SignedSpanValid(Header.TangentZMin.Z,Header.TangentZBits.Z))))
        {Error=TEXT("Cooked GPU morph quantized arithmetic exceeds its native integer bounds.");return false;}
    }
    Ranges.Reset();Deltas.Reset();Ranges.Reserve(Asset->GetMorphTargets().Num());Deltas.Reserve(int32(TotalElements));
    uint32 ExpectedBatch=0;
    for(int32 MorphIndex=0;MorphIndex<Asset->GetMorphTargets().Num();++MorphIndex) {
        const auto& Target=Asset->GetMorphTargets()[MorphIndex];
        if(!Target || !Target->UsesBuiltinMorphTargetCompression())
        {Error=TEXT("Cooked freeze capture cannot decode unknown custom morph compression.");return false;}
        const uint32 First=Morph.GetBatchStartOffset(MorphIndex),Count=Morph.GetNumBatches(MorphIndex);
        if(First!=ExpectedBatch || uint64(First)+Count>uint64(Headers.Num())){Error=TEXT("Cooked morph target batch range is invalid.");return false;}
        ExpectedBatch+=Count;
        FMorphRange Range;Range.First=Deltas.Num();
        // GPU batches are DWORD-padded and carry absolute offsets into the full buffer.
        // The CPU iterative stream decoder does not follow those offsets.
        for(uint32 Batch=First;Batch<First+Count;++Batch) {
            const auto& Header=Headers[Batch];FQuantizedDelta Quantized[BatchSize];
            ReadQuantizedDeltas(TArrayView<FQuantizedDelta>(Quantized,Header.NumElements),Header,
                TConstArrayView<uint32>(Words.GetData()+Header.DataOffset/sizeof(uint32),CalculateBatchDwords(Header)));
            for(uint32 Element=0;Element<Header.NumElements;++Element) {
                FMorphTargetDelta Delta;DequantizeDelta(Delta,Header.bTangents,Quantized[Element],Morph.GetPositionPrecision(),Morph.GetTangentZPrecision());
                if(Deltas.Num()>=MaxMorphDeltas || Delta.SourceIdx>=uint32(LOD.GetNumVertices()) || Delta.PositionDelta.ContainsNaN() || Delta.TangentZDelta.ContainsNaN())
                {Error=TEXT("Decoded cooked morph delta is invalid or exceeds prepared capacity.");return false;}
                FMorphDelta Out;Out.SourceIdx=Delta.SourceIdx;Out.PositionDelta=Delta.PositionDelta;Out.TangentZDelta=Delta.TangentZDelta;Deltas.Add(Out);
            }
        }
        Range.Count=Deltas.Num()-Range.First;
        if(Range.Count>Target->GetNumDeltasForLOD(0)){Error=TEXT("Cooked GPU morph contains more deltas than its actual source target.");return false;}
        Ranges.Add(Range);
    }
    if(ExpectedBatch!=Batches || uint64(Deltas.Num())!=TotalElements){Error=TEXT("Cooked morph decoder did not consume the validated GPU batches.");return false;}
    auto& Entry=Cache.AddDefaulted_GetRef();Entry.World=World;Entry.Appearance=Asset;Entry.Render=Render;
    Entry.GPUBuffer=Buffer.GetReference();Entry.Ranges=Ranges;Entry.Deltas=Deltas;
    UE_LOG(LogTemp,Log,TEXT("MC_FREEZE_COOKED_MORPHS_PREPARED mesh=%s morphs=%d deltas=%d bytes=%llu loadingReadback=1"),
        *Asset->GetPathName(),Ranges.Num(),Deltas.Num(),Bytes);
    return true;
}

bool Supported(USkeletalMeshComponent* Mesh,FString& Error)
{
    if(!IsInGameThread()||!IsValid(Mesh)||!Mesh->GetSkeletalMeshAsset()||Mesh->LeaderPoseComponent.IsValid()
        ||Mesh->GetMeshDeformerInstanceForLOD(0))
    {Error=TEXT("Freeze CPU capture requires the real primary skeletal component without a leader/GPU deformer.");return false;}
    for(const auto& Set:Mesh->GetExternalMorphSetsArray())if(!Set.IsEmpty())
    {Error=TEXT("Freeze CPU capture cannot replace unknown external GPU morph data with bind pose.");return false;}
    return true;
}
}

bool FMCFreezePoseCapture::Prepare(USkeletalMeshComponent* Mesh,FString& Error)
{
    using namespace MCPreparedFreezeCapture;
    if(bPrepared||!MCFreezeCapturePrivate::Supported(Mesh,Error))return false;
    auto* Asset=Mesh->GetSkeletalMeshAsset();auto* Render=Asset->GetResourceForRendering();auto* Weights=Mesh->GetSkinWeightBuffer(0);
    if(!Render||Render->LODRenderData.IsEmpty()||!Weights){Error=TEXT("Freeze LOD0/weights are not resident before readiness.");return false;}
    const auto& LOD=Render->LODRenderData[0];const auto* Index=LOD.MultiSizeIndexContainer.GetIndexBuffer();
    if(LOD.HasClothData()||!Index||!Index->GetNeedsCPUAccess()||!Weights->GetNeedsCPUAccess()
        ||!LOD.StaticVertexBuffers.PositionVertexBuffer.GetVertexData()
        ||!LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetAllowCPUAccess()||LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()<1)
    {Error=TEXT("Freeze requires resident CPU position/tangent/UV/index/weight data; cloth is unsupported. Enable/retain during asset preparation, never at fatal use.");return false;}
    VertexCount=LOD.GetNumVertices();BoneCount=Asset->GetRefSkeleton().GetNum();
    const int32 Influences=Weights->GetMaxBoneInfluences(),Slots=Mesh->GetNumMaterials();
    if(VertexCount<=0||VertexCount>MaxVertices||BoneCount<=0||BoneCount>MaxBones||Slots<=0||Slots>MCFreezeDeathSnapshot::MaxSlots
        ||Influences<=0||Influences>MaxInfluences||Asset->GetMorphTargets().Num()>MaxMorphs)
    {Error=TEXT("Actual freeze appearance exceeds the explicit supported CPU capture capacity.");return false;}
    TArray<FReferenceVertex> Reference;Reference.SetNum(VertexCount);
    TArray<MCFreezeDeathSnapshot::FTriangleIndex> Topology;
    int32 ExpectedVertex=0;
    for(const auto& Section:LOD.RenderSections)
    {
        if(Section.BaseVertexIndex!=uint32(ExpectedVertex)||Section.MaterialIndex>=Slots)
        {Error=TEXT("Freeze requires complete contiguous real section vertices and valid material partitions.");return false;}
        for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V)
        {
            if(V>=uint32(VertexCount)){Error=TEXT("Freeze section vertex exceeds actual LOD0.");return false;}
            auto& Out=Reference[V];Out.Position=LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V);
            Out.TangentX=LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(V);
            Out.TangentZ=LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(V);
            Out.UV0=LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(V,0);
            const auto W=Weights->GetVertexSkinWeights(V);
            for(int32 I=0;I<Influences;++I)
            {
                if(!Section.BoneMap.IsValidIndex(W.InfluenceBones[I])){Error=TEXT("Freeze skin weight has an invalid actual section bone map.");return false;}
                Out.Bones[I]=Section.BoneMap[W.InfluenceBones[I]];Out.Weights[I]=W.InfluenceWeights[I];
            }
        }
        ExpectedVertex+=Section.NumVertices;
        if(Section.NumTriangles>uint32(MCFreezeDeathSnapshot::MaxTriangles-Topology.Num())
            ||Section.BaseIndex+Section.NumTriangles*3>uint32(Index->Num()))
        {Error=TEXT("Freeze original index topology exceeds its real buffer/capacity.");return false;}
        for(uint32 T=0;T<Section.NumTriangles;++T)
        {MCFreezeDeathSnapshot::FTriangleIndex Tri;Tri.A=Index->Get(Section.BaseIndex+T*3);Tri.B=Index->Get(Section.BaseIndex+T*3+1);
         Tri.C=Index->Get(Section.BaseIndex+T*3+2);Tri.Slot=uint8(Section.MaterialIndex);Topology.Add(Tri);TriangleSlots.Add(Tri.Slot);}
    }
    if(ExpectedVertex!=VertexCount){Error=TEXT("Freeze cannot omit source render vertices.");return false;}
    TArray<FMorphRange> Ranges;TArray<FMorphDelta> Deltas;bool bUsedCookedMorphs=false;
    for(const auto& Target:Asset->GetMorphTargets())
    {
        if(!Target){Error=TEXT("Freeze appearance has a null actual morph target.");return false;}
        const int32 Count=Target->GetNumDeltasForLOD(0);
        if(Count<0||Count>MaxMorphDeltas-Deltas.Num()){Error=TEXT("Actual freeze morph delta count exceeds its bounded CPU cache.");return false;}
        FMorphRange Range;Range.First=Deltas.Num();Range.Count=Count;
        for(auto It=Target->GetDeltaIteratorForLOD(0);!It.AtEnd();++It)
        {if(Deltas.Num()-Range.First>=Count){Error=TEXT("Actual cooked morph iterator exceeded its declared count.");return false;}
         FMorphDelta D;D.SourceIdx=It->SourceIdx;D.PositionDelta=It->PositionDelta;D.TangentZDelta=It->TangentZDelta;Deltas.Add(D);}
        if(Deltas.Num()-Range.First!=Count){
            if(!MCFreezeCapturePrivate::PrepareCookedMorphs(Mesh,LOD,Render,Ranges,Deltas,Error))return false;
            bUsedCookedMorphs=true;
            MorphTargets.Reset();for(const auto& Actual:Asset->GetMorphTargets())MorphTargets.Add(Actual);
            break;
        }
        Ranges.Add(Range);MorphTargets.Add(Target);
    }
    if(!Sampler.Prepare(Reference,Ranges,Deltas,BoneCount,Influences,Error)
        ||!Snapshot.Prepare(VertexCount,Slots,Topology,Error))return false;
    UE_LOG(LogTemp,Log,TEXT("MC_FREEZE_MORPH_PREPARED mesh=%s source=%s morphs=%d deltas=%d"),
        *Asset->GetPathName(),bUsedCookedMorphs?TEXT("CookedGPUCache"):TEXT("ResidentCPU"),Ranges.Num(),Deltas.Num());
    Matrices.SetNumUninitialized(BoneCount);RequiredBones.SetNumUninitialized(BoneCount);
    for(int32 I=0;I<BoneCount;++I)RequiredBones[I]=FBoneIndexType(I);
    Source=Mesh;Appearance=Asset;RenderIdentity=Render;WeightIdentity=Weights;bPrepared=true;return true;
}

bool FMCFreezePoseCapture::Capture(USkeletalMeshComponent* Mesh,const FTransform& EffectFrame,double At,FString& Error)
{
    using namespace MCPreparedFreezeCapture;
    if(!bPrepared||Snapshot.IsSealed()||Source.Get()!=Mesh||!MCFreezeCapturePrivate::Supported(Mesh,Error)
        ||Appearance.Get()!=Mesh->GetSkeletalMeshAsset()||RenderIdentity!=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering()
        ||WeightIdentity!=Mesh->GetSkinWeightBuffer(0)||Mesh->GetPredictedLODLevel()!=0||!FMath::IsFinite(At)
        ||EffectFrame.ContainsNaN()||!EffectFrame.GetRotation().IsNormalized()||!EffectFrame.GetScale3D().Equals(FVector::OneVector))
    {if(Error.IsEmpty())Error=TEXT("Freeze accepted component/appearance/weights/LOD changed after preparation.");return false;}
    const auto* BeforeObject=Mesh->GetMeshObject();const bool BeforeCPU=Mesh->GetCPUSkinningEnabled();const int32 BeforeLOD=Mesh->GetForcedLOD();
    // Finish only the ALREADY RUNNING animation task. Do not Refresh/evaluate a
    // new pose or force a LOD/CPU rendering mode. Public completion also applies
    // its real pending morph curves before taking the one coherent local pose.
    Mesh->HandleExistingParallelEvaluationTask(true,true);
    if(Mesh->IsRunningParallelEvaluation()||Mesh->GetComponentSpaceTransforms().Num()!=BoneCount||!BeforeObject
        ||Appearance.Get()!=Mesh->GetSkeletalMeshAsset()||RenderIdentity!=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering()
        ||WeightIdentity!=Mesh->GetSkinWeightBuffer(0)||Mesh->GetPredictedLODLevel()!=0)
    {Error=TEXT("Actual final component-space pose is not coherent/resident.");return false;}
    const uint32 Revision=Mesh->GetBoneTransformRevisionNumber();const FTransform MeshWorld=Mesh->GetComponentTransform();
    Mesh->GetCurrentRefToLocalMatrices(Matrices,0,&RequiredBones);
    FAcceptedMorph Active[MaxMorphs];int32 NumActive=0;
    for(const auto& Pair:Mesh->ActiveMorphTargets)
    {
        if(NumActive>=MaxMorphs||!Mesh->MorphTargetWeights.IsValidIndex(Pair.Value))
        {Error=TEXT("Actual accepted morph map/weights exceed the prepared contract.");return false;}
        int32 Prepared=INDEX_NONE;for(int32 I=0;I<MorphTargets.Num();++I)if(MorphTargets[I].Get()==Pair.Key){Prepared=I;break;}
        if(Prepared==INDEX_NONE){Error=TEXT("Accepted morph target was not cached under readiness.");return false;}
        Active[NumActive++]={Prepared,Mesh->MorphTargetWeights[Pair.Value]};
    }
    if(Revision!=Mesh->GetBoneTransformRevisionNumber()||BeforeObject!=Mesh->GetMeshObject()
        ||BeforeCPU!=Mesh->GetCPUSkinningEnabled()||BeforeLOD!=Mesh->GetForcedLOD()||!MeshWorld.Equals(Mesh->GetComponentTransform()))
    {Error=TEXT("Actual fatal pose changed while sampled; no mixed-pose capture or render-mode fallback was used.");return false;}
    if(!Sampler.Capture(Matrices,TConstArrayView<FAcceptedMorph>(Active,NumActive),UE::SkeletalRender::Settings::GetMorphTargetMaxBlendWeight(),Error))return false;
    FBox Bounds(ForceInit);
    for(const auto& V:Sampler.GetCapturedPose()) {
        const FVector P=EffectFrame.InverseTransformPosition(MeshWorld.TransformPosition(FVector(V.Position)));
        const FVector N=EffectFrame.InverseTransformVectorNoScale(MeshWorld.TransformVectorNoScale(FVector(V.TangentZ.ToFVector3f()))).GetSafeNormal();
        if(P.ContainsNaN()||N.ContainsNaN()||V.TextureCoordinates[0].ContainsNaN()) {
            Error=TEXT("Actual freeze pose is nonfinite before any native motion issuance.");return false;
        }
        Bounds+=P;
    }
    if(!Motion.IssueOnce(Bounds,Error))return false;
    return Snapshot.Seal(Sampler.GetCapturedPose(),MeshWorld,EffectFrame,At,Motion.GetShards(),Error);
}
