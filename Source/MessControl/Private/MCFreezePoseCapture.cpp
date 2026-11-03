#include "MCFreezePoseCapture.h"
#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"

namespace MCFreezeCapturePrivate
{
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
    TArray<FMorphRange> Ranges;TArray<FMorphDelta> Deltas;
    for(const auto& Target:Asset->GetMorphTargets())
    {
        if(!Target){Error=TEXT("Freeze appearance has a null actual morph target.");return false;}
        const int32 Count=Target->GetNumDeltasForLOD(0);
        if(Count<0||Count>MaxMorphDeltas-Deltas.Num()){Error=TEXT("Actual freeze morph delta count exceeds its bounded CPU cache.");return false;}
        FMorphRange Range;Range.First=Deltas.Num();Range.Count=Count;
        for(auto It=Target->GetDeltaIteratorForLOD(0);!It.AtEnd();++It)
        {if(Deltas.Num()-Range.First>=Count){Error=TEXT("Actual cooked morph iterator exceeded its declared count.");return false;}
         FMorphDelta D;D.SourceIdx=It->SourceIdx;D.PositionDelta=It->PositionDelta;D.TangentZDelta=It->TangentZDelta;Deltas.Add(D);}
        if(Deltas.Num()-Range.First!=Count){Error=TEXT("Resident cooked morph data is incomplete; no deferred decompression at fatal use.");return false;}
        Ranges.Add(Range);MorphTargets.Add(Target);
    }
    if(!Sampler.Prepare(Reference,Ranges,Deltas,BoneCount,Influences,Error)
        ||!Snapshot.Prepare(VertexCount,Slots,Topology,Error))return false;
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
