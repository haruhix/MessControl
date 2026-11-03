#include "MCFreezeDeathSnapshot.h"

namespace MCFreezeDeathSnapshot
{
bool FBuffer::Prepare(int32 InVertexCount,int32 InSlotCount,TConstArrayView<FTriangleIndex> InTriangles,FString& Error)
{
    if(bPrepared||InVertexCount<=0||InVertexCount>MaxVertices||InSlotCount<=0||InSlotCount>MaxSlots
        ||InTriangles.IsEmpty()||InTriangles.Num()>MaxTriangles)
    {Error=TEXT("Freeze snapshot preparation was repeated or exceeds the declared CPU geometry limits.");return false;}
    for(const auto& T:InTriangles)if(T.A>=uint32(InVertexCount)||T.B>=uint32(InVertexCount)||T.C>=uint32(InVertexCount)||T.Slot>=InSlotCount)
    {Error=TEXT("Freeze snapshot topology has an invalid real vertex or material slot.");return false;}
    VertexCount=InVertexCount;SlotCount=InSlotCount;Topology.Append(InTriangles);
    Triangles.SetNumUninitialized(InTriangles.Num());Positions.SetNumUninitialized(VertexCount);
    Normals.SetNumUninitialized(VertexCount);UV.SetNumUninitialized(VertexCount);Shards.SetNumUninitialized(CellCount);
    bPrepared=true;return true;
}

bool FBuffer::Seal(TConstArrayView<FFinalSkinVertex> ActualPose,const FTransform& MeshWorld,const FTransform& EffectWorld,
    double AcceptedAt,TConstArrayView<FNativeShard> ActualShards,FString& Error)
{
    if(!bPrepared||bSealed||ActualPose.Num()!=VertexCount||ActualShards.Num()!=CellCount
        ||!FMath::IsFinite(AcceptedAt)||MeshWorld.ContainsNaN()||EffectWorld.ContainsNaN())
    {Error=TEXT("Freeze snapshot needs exactly one accepted pose and all36 existing native shard samples.");return false;}
    for(const auto& S:ActualShards)if(S.Home.ContainsNaN()||S.Velocity.ContainsNaN()||S.Spin.ContainsNaN()||!FMath::IsFinite(S.Floor))
    {Error=TEXT("Freeze snapshot native shard sample is not finite.");return false;}
    FBox Bounds(ForceInit);
    for(int32 I=0;I<VertexCount;++I)
    {
        const auto& V=ActualPose[I];
        Positions[I]=EffectWorld.InverseTransformPosition(MeshWorld.TransformPosition(FVector(V.Position)));
        Normals[I]=EffectWorld.InverseTransformVectorNoScale(MeshWorld.TransformVectorNoScale(FVector(V.TangentZ.ToFVector3f()))).GetSafeNormal();
        UV[I]=V.TextureCoordinates[0];
        if(Positions[I].ContainsNaN()||Normals[I].ContainsNaN()||UV[I].ContainsNaN())
        {Error=TEXT("Freeze snapshot actual skinned/morphed position, normal or UV0 is not finite.");return false;}
        Bounds+=Positions[I];
    }
    // Same bounds, origin and36-cell partition as AMCFreezeDeathVFX::Capture.
    HalfSize=FMath::Max(70.f,float(Bounds.GetExtent().GetMax()+15.f));
    const FVector Center=Bounds.GetCenter();CubeCenter=FVector(Center.X,Center.Y,Bounds.Min.Z+HalfSize-4.f);
    const FVector Cell(HalfSize*2/3,HalfSize*2/3,HalfSize*2/4);
    for(int32 I=0;I<CellCount;++I)Shards[I]=ActualShards[I];
    for(int32 I=0;I<Topology.Num();++I)
    {
        const auto& T=Topology[I];const FVector Q=((Positions[T.A]+Positions[T.B]+Positions[T.C])/3-CubeCenter+FVector(HalfSize))/Cell;
        const int32 C=FMath::Clamp(FMath::FloorToInt(Q.X),0,2)+3*(FMath::Clamp(FMath::FloorToInt(Q.Y),0,2)+3*FMath::Clamp(FMath::FloorToInt(Q.Z),0,3));
        const FVector Home=Shards[C].Home;auto& Out=Triangles[I];
        Out.A=FVector3f(Positions[T.A]-Home);Out.B=FVector3f(Positions[T.B]-Home);Out.C=FVector3f(Positions[T.C]-Home);
        Out.NA=FVector3f(Normals[T.A]);Out.NB=FVector3f(Normals[T.B]);Out.NC=FVector3f(Normals[T.C]);
        Out.UA=FVector2f(UV[T.A]);Out.UB=FVector2f(UV[T.B]);Out.UC=FVector2f(UV[T.C]);Out.Cell=uint16(C);Out.Slot=T.Slot;
    }
    EffectFrame=EffectWorld;StartedAt=AcceptedAt;bSealed=true;return true;
}

SIZE_T FBuffer::GetAllocatedBytes() const
{return Topology.GetAllocatedSize()+Triangles.GetAllocatedSize()+Positions.GetAllocatedSize()+Normals.GetAllocatedSize()+UV.GetAllocatedSize()+Shards.GetAllocatedSize();}

FShaderTriangle Pack(const FTriangle& T,const FTransform& Frame)
{
    FShaderTriangle Out;Out.Origin=Frame.GetTranslation();Out.Orientation=FQuat4f(Frame.GetRotation());
    Out.Scale=FVector3f(Frame.GetScale3D());Out.EncodedVelocity=T.NC;
    Out.Dynamic0=FVector4f(T.A,T.UA.X);Out.Dynamic1=FVector4f(T.B,T.UA.Y);
    Out.Dynamic2=FVector4f(T.C,T.UB.X);Out.Dynamic3=FVector4f(T.NA,T.UB.Y);
    Out.Color=FVector4f(T.NB,T.UC.X);Out.MaterialRandom=T.UC.Y;return Out;
}
}
