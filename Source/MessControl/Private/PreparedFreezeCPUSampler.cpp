#include "PreparedFreezeCPUSampler.h"
#include "RenderMath.h"

namespace MCPreparedFreezeCapture
{
namespace Private
{
    // Local copies of the narrow packed-normal arithmetic in Engine's PRIVATE
    // SkeletalRender.h: no dependency on or exported use of that private header.
    FORCEINLINE VectorRegister Unpack3(const uint32* P)
    {return VectorMultiply(VectorLoadSignedByte4(P),VectorSetFloat3(1.f/127.f,1.f/127.f,1.f/127.f));}
    FORCEINLINE VectorRegister Unpack4(const uint32* P)
    {return VectorMultiply(VectorLoadSignedByte4(P),VectorSetFloat1(1.f/127.f));}
    FORCEINLINE void Pack3(VectorRegister4Float N,uint32* P)
    {VectorStoreSignedByte4(VectorMultiply(N,VectorSetFloat3(127.f,127.f,127.f)),P);}
    FORCEINLINE void Pack3(VectorRegister4Double N,uint32* P)
    {VectorStoreSignedByte4(MakeVectorRegisterFloatFromDouble(VectorMultiply(N,VectorSetFloat3(127.f,127.f,127.f))),P);}
    FORCEINLINE void Pack4(VectorRegister4Float N,uint32* P)
    {VectorStoreSignedByte4(VectorMultiply(N,VectorSetFloat1(127.f)),P);}
    FORCEINLINE void Pack4(VectorRegister4Double N,uint32* P)
    {VectorStoreSignedByte4(MakeVectorRegisterFloatFromDouble(VectorMultiply(N,VectorSetFloat1(127.f))),P);}
}
bool FSampler::Prepare(TConstArrayView<FReferenceVertex> InReference,TConstArrayView<FMorphRange> InMorphs,
    TConstArrayView<FMorphDelta> InDeltas,int32 InBoneCount,int32 InInfluenceCount,FString& Error)
{
    if(bPrepared||InReference.IsEmpty()||InReference.Num()>MaxVertices||InMorphs.Num()>MaxMorphs
        ||InDeltas.Num()>MaxMorphDeltas||InBoneCount<=0||InBoneCount>MaxBones||InInfluenceCount<=0||InInfluenceCount>MaxInfluences)
    {Error=TEXT("Prepared freeze CPU capture exceeds its explicit supported capacity.");return false;}
    for(const auto& V:InReference)
    {
        if(V.Position.ContainsNaN()||V.UV0.ContainsNaN()||V.TangentX.ContainsNaN()||V.TangentZ.ContainsNaN()) {Error=TEXT("Nonfinite resident reference vertex.");return false;}
        uint32 WeightSum=0;
        for(int32 I=0;I<InInfluenceCount;++I)
        {
            if(V.Bones[I]>=InBoneCount) {Error=TEXT("Resolved section bone index is invalid.");return false;}
            WeightSum+=V.Weights[I];
        }
        if(WeightSum==0) {Error=TEXT("Reference vertex has no real skin weights.");return false;}
    }
    for(const auto& M:InMorphs)
    {
        if(M.First<0||M.Count<0||M.First>InDeltas.Num()-M.Count)
        {Error=TEXT("Prepared real morph range is invalid.");return false;}
        uint32 Previous=0;
        for(int32 I=0;I<M.Count;++I)
        {
            const auto& D=InDeltas[M.First+I];
            if(D.SourceIdx>=uint32(InReference.Num())||(I>0&&D.SourceIdx<=Previous)
                ||D.PositionDelta.ContainsNaN()||D.TangentZDelta.ContainsNaN())
            {Error=TEXT("Real morph deltas must be finite and strictly ordered by source vertex.");return false;}
            Previous=D.SourceIdx;
        }
    }
    Reference.Append(InReference);Morphs.Append(InMorphs);Deltas.Append(InDeltas);
    Output.SetNumZeroed(InReference.Num());BoneCount=InBoneCount;InfluenceCount=InInfluenceCount;bPrepared=true;return true;
}

bool FSampler::Capture(TConstArrayView<FMatrix44f> ActualRefToLocal,TConstArrayView<FAcceptedMorph> ActualMorphOrder,
    float ActualMaxMorphBlendWeight,FString& Error)
{
    if(!bPrepared||bCaptured||ActualRefToLocal.Num()!=BoneCount||ActualMorphOrder.Num()>MaxMorphs
        ||!FMath::IsFinite(ActualMaxMorphBlendWeight)||ActualMaxMorphBlendWeight<UE_SMALL_NUMBER)
    {Error=TEXT("Freeze capture needs one prepared coherent accepted pose.");return false;}
    for(const auto& Matrix:ActualRefToLocal)if(Matrix.ContainsNaN())
    {Error=TEXT("Actual accepted bone matrix is not finite.");return false;}
    bool bAnyValidMorph=false;
    for(int32 I=0;I<ActualMorphOrder.Num();++I)
    {
        const auto& Active=ActualMorphOrder[I];
        if(!Morphs.IsValidIndex(Active.PreparedIndex)||!FMath::IsFinite(Active.Weight))
        {Error=TEXT("Accepted morph was not cached under the appearance readiness barrier.");return false;}
        for(int32 J=0;J<I;++J)if(ActualMorphOrder[J].PreparedIndex==Active.PreparedIndex)
        {Error=TEXT("Accepted morph map contains a duplicate target.");return false;}
        const auto& Range=Morphs[Active.PreparedIndex];Cursor[I]=Range.First;
        const float Abs=FMath::Abs(Active.Weight);
        ValidMorph[I]=Abs>=UE_SMALL_NUMBER&&Abs<=ActualMaxMorphBlendWeight&&Range.Count>0;
        bAnyValidMorph|=ValidMorph[I];
    }
    // Keep the Engine CPU skinner's repeated packed-normal quantization and
    // tangent rebuild order; summing all float deltas then packing once differs.
    struct FNativeRoundingScope {uint32 Old=VectorGetControlRegister();
        FNativeRoundingScope(){VectorSetControlRegister(Old|VECTOR_ROUND_TOWARD_ZERO);}
        ~FNativeRoundingScope(){VectorSetControlRegister(Old);}} RoundingScope;
    static const VectorRegister Inv65535=MakeVectorRegisterDoubleConstant(1.0/65535,1.0/65535,1.0/65535,1.0/65535);
    static const VectorRegister WOnly=MakeVectorRegister(0.f,0.f,0.f,1.f);
    for(int32 Vertex=0;Vertex<Reference.Num();++Vertex)
    {
        const auto& Ref=Reference[Vertex];FVector3f Position=Ref.Position;
        FPackedNormal TX(Ref.TangentX),NZ(Ref.TangentZ);
        if(bAnyValidMorph)
        {
            for(int32 I=0;I<ActualMorphOrder.Num();++I)if(ValidMorph[I])
            {
                const auto& Active=ActualMorphOrder[I];const auto& Range=Morphs[Active.PreparedIndex];
                const int32 End=Range.First+Range.Count;
                if(Cursor[I]<End&&Deltas[Cursor[I]].SourceIdx==uint32(Vertex))
                {
                    const auto& Delta=Deltas[Cursor[I]++];Position+=Delta.PositionDelta*Active.Weight;
                    const auto W=NZ.Vector.W;
                    NZ=(NZ.ToFVector()+FVector(Delta.TangentZDelta*FMath::Min(Active.Weight,1.f))).GetUnsafeNormal();
                    NZ.Vector.W=W;
                }
            }
            FVector Tangent=TX.ToFVector();const FVector Normal=NZ.ToFVector();
            Tangent-=((Tangent|Normal)*Normal);Tangent.Normalize();TX=Tangent;
        }
        const auto P=VectorLoadFloat3_W1(&Position);
        const auto T=Private::Unpack3(&TX.Vector.Packed);
        const auto N=Private::Unpack4(&NZ.Vector.Packed);
        const VectorRegister WeightGroups[3]={VectorMultiply(VectorLoadURGBA16N(Ref.Weights),Inv65535),
            VectorMultiply(VectorLoadURGBA16N(Ref.Weights+4),Inv65535),VectorMultiply(VectorLoadURGBA16N(Ref.Weights+8),Inv65535)};
        VectorResetFloatRegisters();
        const auto& M0=ActualRefToLocal[Ref.Bones[0]];const auto Weight0=VectorReplicate(WeightGroups[0],0);
        VectorRegister Rows[4]={VectorMultiply(VectorLoadAligned(&M0.M[0][0]),Weight0),VectorMultiply(VectorLoadAligned(&M0.M[1][0]),Weight0),
            VectorMultiply(VectorLoadAligned(&M0.M[2][0]),Weight0),VectorMultiply(VectorLoadAligned(&M0.M[3][0]),Weight0)};
        for(int32 I=1;I<InfluenceCount;++I)
        {
            const auto& M=ActualRefToLocal[Ref.Bones[I]];
            // VectorReplicate requires a compile-time component on some targets.
            const auto& G=WeightGroups[I/4];VectorRegister Weight;
            switch(I%4){case 0:Weight=VectorReplicate(G,0);break;case 1:Weight=VectorReplicate(G,1);break;case 2:Weight=VectorReplicate(G,2);break;default:Weight=VectorReplicate(G,3);break;}
            for(int32 R=0;R<4;++R)Rows[R]=VectorMultiplyAdd(VectorLoadAligned(&M.M[R][0]),Weight,Rows[R]);
        }
        auto Transform=[&Rows](VectorRegister Input,VectorRegister Last)
        {return VectorMultiplyAdd(VectorReplicate(Input,0),Rows[0],VectorMultiplyAdd(VectorReplicate(Input,1),Rows[1],
            VectorMultiplyAdd(VectorReplicate(Input,2),Rows[2],Last)));};
        auto& Out=Output[Vertex];VectorStoreFloat3(Transform(P,Rows[3]),&Out.Position);
        // Match native normal XYZ operation nesting, then preserve determinant W.
        auto TransformNormal=[&Rows](VectorRegister Input)
        {return VectorNormalize(VectorMultiplyAdd(VectorReplicate(Input,0),Rows[0],VectorMultiplyAdd(VectorReplicate(Input,1),Rows[1],VectorMultiply(VectorReplicate(Input,2),Rows[2]))));};
        Private::Pack3(TransformNormal(T),&Out.TangentX.Vector.Packed);
        Private::Pack4(VectorMultiplyAdd(WOnly,N,TransformNormal(N)),&Out.TangentZ.Vector.Packed);
        VectorResetFloatRegisters();Out.TextureCoordinates[0]=FVector2D(Ref.UV0);
    }
    bCaptured=true;return true;
}

SIZE_T FSampler::GetAllocatedBytes() const
{return Reference.GetAllocatedSize()+Morphs.GetAllocatedSize()+Deltas.GetAllocatedSize()+Output.GetAllocatedSize();}
}
