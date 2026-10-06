#include "MCDeliveryZoneSurface.h"
#include "MCTongue.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "HAL/IConsoleManager.h"

namespace MCDeliveryGuide
{
namespace
{
int32 DepthGuardOwners=0,PreviousDepthMode=0;
bool bChangedDepthMode=false;
double Cross(FVector2D A,FVector2D B) {return A.X*B.Y-A.Y*B.X;}
FVector2D XY(FVector P) {return FVector2D(P.X,P.Y);}
bool CapDepth(FVector2D P,const TArray<FVector>& Outer,const TArray<FVector>& Inner,double& T)
{
    if(Outer.Num()<2 || Outer.Num()!=Inner.Num() || P.Y<Outer[0].Y || P.Y>Outer.Last().Y) return false;
    for(int32 I=1;I<Outer.Num();++I) if(P.Y<=Outer[I].Y)
    {
        const double L=(P.Y-Outer[I-1].Y)/(Outer[I].Y-Outer[I-1].Y);
        const double Edge=FMath::Lerp(Outer[I-1].X,Outer[I].X,L),Inside=FMath::Lerp(Inner[I-1].X,Inner[I].X,L);
        if(FMath::Abs(Inside-Edge)<.001) return false;
        T=(P.X-Edge)/(Inside-Edge);return true;
    }
    return false;
}
}

void FReceiverDepthGuard::Bind(UPrimitiveComponent* Component)
{
    if(bBound || !IsValid(Component)) return;
    if(DepthGuardOwners++==0)
    {
        if(auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))
        {
            PreviousDepthMode=Mode->GetInt();bChangedDepthMode=PreviousDepthMode!=3;
            if(bChangedDepthMode) Mode->Set(3,ECVF_SetByCode);
        }
    }
    Receiver=Component;PreviousStencil=Component->CustomDepthStencilValue;bPreviousDepth=Component->bRenderCustomDepth;bBound=true;
    Component->SetCustomDepthStencilValue(244);Component->SetRenderCustomDepth(true);
}

void FReceiverDepthGuard::Restore()
{
    if(!bBound) return;
    if(auto* Component=Receiver.Get();Component && Component->CustomDepthStencilValue==244)
    {
        Component->SetCustomDepthStencilValue(PreviousStencil);Component->SetRenderCustomDepth(bPreviousDepth);
    }
    bBound=false;Receiver.Reset();
    if(--DepthGuardOwners==0 && bChangedDepthMode)
    {
        if(auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));Mode && Mode->GetInt()==3)
            Mode->Set(PreviousDepthMode,ECVF_SetByCode);
        bChangedDepthMode=false;
    }
}

bool FSurfaceCache::Refresh(const AMCTongue* Tongue)
{
    if(!Tongue || !Tongue->Surface || Tongue->CurrentVertices().IsEmpty() || Tongue->TriangleIndices().IsEmpty())
    {bReady=false;return false;}
    const auto& Vertices=Tongue->CurrentVertices();const auto& Indices=Tongue->TriangleIndices();
    const FTransform Transform=Tongue->Surface->GetComponentTransform();
    const bool Changed=!bReady || SourceMesh.Get()!=Tongue->SourceMesh || !SourceTransform.Equals(Transform)
        || SourceVertexCount!=Vertices.Num() || SourceIndexCount!=Indices.Num();
    WorldVertices.SetNumUninitialized(Vertices.Num());
    for(int32 I=0;I<Vertices.Num();++I) WorldVertices[I]=Transform.TransformPosition(Vertices[I]);
    if(!Changed) return false;

    Faces.Reset();Cells.Reset();Boundary.Reset();Floor.Reset();Arrows.Reset();FloorOuter.Reset();FloorInner.Reset();
    SourceMesh=Tongue->SourceMesh;SourceTransform=Transform;SourceVertexCount=Vertices.Num();SourceIndexCount=Indices.Num();
    TMap<FIntVector,int32> Weld;
    TArray<int32> Welded;Welded.Reserve(Vertices.Num());
    for(const FVector P:WorldVertices)
    {
        const FIntVector Key(FMath::RoundToInt(P.X*10),FMath::RoundToInt(P.Y*10),FMath::RoundToInt(P.Z*10));
        int32* Existing=Weld.Find(Key);
        if(Existing) Welded.Add(*Existing);
        else {const int32 Index=Weld.Num();Weld.Add(Key,Index);Welded.Add(Index);}
    }
    TMap<FIntPoint,FBoundary> Edges;
    for(int32 I=0;I+2<Indices.Num();I+=3)
    {
        const FIntVector Source(Indices[I],Indices[I+1],Indices[I+2]);
        if(!WorldVertices.IsValidIndex(Source.X) || !WorldVertices.IsValidIndex(Source.Y) || !WorldVertices.IsValidIndex(Source.Z)) continue;
        FFace Face;Face.Source=Source;
        Face.A=XY(WorldVertices[Source.X]);Face.B=XY(WorldVertices[Source.Y]);Face.C=XY(WorldVertices[Source.Z]);
        Face.Det=Cross(Face.B-Face.A,Face.C-Face.A);
        // Unreal's native clockwise winding: Cross(C-A,B-A) points upwards.
        // Exclude underside and vertical faces from this floor guide.
        if(Face.Det>=-1.e-5) continue;
        Face.Bounds=FBox2D(ForceInit);Face.Bounds+=Face.A;Face.Bounds+=Face.B;Face.Bounds+=Face.C;
        const int32 FaceIndex=Faces.Add(Face);
        const FIntPoint First(FMath::FloorToInt(Face.Bounds.Min.X/CellSize),FMath::FloorToInt(Face.Bounds.Min.Y/CellSize));
        const FIntPoint Last(FMath::FloorToInt(Face.Bounds.Max.X/CellSize),FMath::FloorToInt(Face.Bounds.Max.Y/CellSize));
        for(int32 X=First.X;X<=Last.X;++X) for(int32 Y=First.Y;Y<=Last.Y;++Y) Cells.FindOrAdd(FIntPoint(X,Y)).Add(FaceIndex);
        const int32 Id[]={Source.X,Source.Y,Source.Z};
        for(int32 E=0;E<3;++E)
        {
            const int32 A=Id[E],B=Id[(E+1)%3],WA=Welded[A],WB=Welded[B];
            auto& Edge=Edges.FindOrAdd(FIntPoint(FMath::Min(WA,WB),FMath::Max(WA,WB)));
            if(Edge.Count++==0) {Edge.A=A;Edge.B=B;Edge.Face=FaceIndex;}
        }
    }
    for(const auto& Edge:Edges) if(Edge.Value.Count==1) Boundary.Add(Edge.Value);
    bReady=!Faces.IsEmpty();
    UE_LOG(LogTemp,Display,TEXT("MC_DELIVERY_SURFACE_CACHE vertices=%d native_triangles=%d upper_faces=%d boundary_edges=%d"),
        Vertices.Num(),Indices.Num()/3,Faces.Num(),Boundary.Num());
    return true;
}

void FSurfaceCache::Clip(FSurfaceOverlay& Out,TConstArrayView<FFootprintVertex> Polygon,float Lift) const
{
    if(!bReady || Polygon.Num()<3) return;
    FBox2D Bounds(ForceInit);for(const auto& V:Polygon) Bounds+=V.XY;
    const FIntPoint First(FMath::FloorToInt(Bounds.Min.X/CellSize),FMath::FloorToInt(Bounds.Min.Y/CellSize));
    const FIntPoint Last(FMath::FloorToInt(Bounds.Max.X/CellSize),FMath::FloorToInt(Bounds.Max.Y/CellSize));
    TSet<int32> Candidates;
    for(int32 X=First.X;X<=Last.X;++X) for(int32 Y=First.Y;Y<=Last.Y;++Y)
        if(const auto* Found=Cells.Find(FIntPoint(X,Y))) for(int32 Face:*Found) Candidates.Add(Face);
    for(const int32 Index:Candidates)
    {
        const auto& Face=Faces[Index];if(!Bounds.Intersect(Face.Bounds)) continue;
        TArray<FFootprintVertex,TInlineAllocator<10>> Clipped,Scratch;
        Clipped.Append(Polygon.GetData(),Polygon.Num());
        const FVector2D Corners[]={Face.A,Face.B,Face.C};
        for(int32 Edge=0;Edge<3 && Clipped.Num()>=3;++Edge)
        {
            Scratch.Reset();const FVector2D A=Corners[Edge],Axis=Corners[(Edge+1)%3]-A;
            auto Distance=[&](FVector2D P) {return -Cross(Axis,P-A);};
            auto Previous=Clipped.Last();double DP=Distance(Previous.XY);
            for(const auto& Current:Clipped)
            {
                const double DC=Distance(Current.XY);const bool PreviousInside=DP>=-1.e-6,CurrentInside=DC>=-1.e-6;
                if(PreviousInside!=CurrentInside)
                {
                    const double T=FMath::Clamp(DP/(DP-DC),0.,1.);
                    Scratch.Add({FMath::Lerp(Previous.XY,Current.XY,T),FMath::Lerp(Previous.Alpha,Current.Alpha,float(T))});
                }
                if(CurrentInside) Scratch.Add(Current);
                Previous=Current;DP=DC;
            }
            Swap(Clipped,Scratch);
        }
        // Repeated endpoints in triangle-shaped quads must not create zero-area fans.
        for(int32 I=Clipped.Num()-1;I>=0 && Clipped.Num()>1;--I)
            if(FVector2D::DistSquared(Clipped[I].XY,Clipped[(I+1)%Clipped.Num()].XY)<1.e-10) Clipped.RemoveAt(I);
        if(Clipped.Num()<3) continue;
        const int32 Start=Out.Vertices.Num();
        for(const auto& V:Clipped)
        {
            const double WB=Cross(V.XY-Face.A,Face.C-Face.A)/Face.Det;
            const double WC=Cross(Face.B-Face.A,V.XY-Face.A)/Face.Det;
            Out.Vertices.Add({Face.Source,FVector(1-WB-WC,WB,WC),V.Alpha,Lift});
        }
        for(int32 I=1;I+1<Clipped.Num();++I)
        {
            if(FMath::Abs(Cross(Clipped[I].XY-Clipped[0].XY,Clipped[I+1].XY-Clipped[0].XY))<1.e-6) continue;
            Out.Indices.Append({Start,Start+I,Start+I+1});
        }
    }
}

void FSurfaceCache::ClipQuad(FSurfaceOverlay& Out,const FVector& A,const FVector& B,const FVector& C,const FVector& D,
    float Alpha,float OtherAlpha,float Lift) const
{
    const float Other=OtherAlpha>=0?OtherAlpha:Alpha;
    // Alpha is affine on each footprint triangle. Clipping a tapered quad directly
    // can give adjacent native faces different alpha at the same shared vertex.
    const FFootprintVertex First[]={{XY(A),Alpha},{XY(B),Other},{XY(C),Other}};
    const FFootprintVertex Second[]={{XY(A),Alpha},{XY(C),Other},{XY(D),Alpha}};
    Clip(Out,MakeArrayView(First),Lift);
    Clip(Out,MakeArrayView(Second),Lift);
}

void FSurfaceCache::AddRim(FSurfaceOverlay& Out,const TArray<FVector>& Outer,const TArray<FVector>& Inner) const
{
    // Follow the actual upper surface boundary rather than an independently projected strip.
    for(const auto& Edge:Boundary)
    {
        const FVector A=WorldVertices[Edge.A],B=WorldVertices[Edge.B];const FVector2D Mid=XY((A+B)*.5);
        double Depth;if(!CapDepth(Mid,Outer,Inner,Depth) || Depth<-.15 || Depth>.15) continue;
        const FVector2D Axis=(XY(B)-XY(A)).GetSafeNormal();
        FVector2D Inward(-Axis.Y,Axis.X);
        const auto& Face=Faces[Edge.Face];
        if(FVector2D::DotProduct(Inward,(Face.A+Face.B+Face.C)/3-Mid)<0) Inward=-Inward;
        const FVector Offset(Inward.X*8,Inward.Y*8,0);
        ClipQuad(Out,A,B,B+Offset,A+Offset,.85f,-1,3);
    }
}

void FSurfaceCache::Positions(const FSurfaceOverlay& Overlay,const FTransform& Destination,TArray<FVector>& Out) const
{
    Out.SetNumUninitialized(Overlay.Vertices.Num());
    for(int32 I=0;I<Overlay.Vertices.Num();++I)
    {
        const auto& V=Overlay.Vertices[I];
        const FVector P=WorldVertices[V.Source.X]*V.Weights.X+WorldVertices[V.Source.Y]*V.Weights.Y+WorldVertices[V.Source.Z]*V.Weights.Z;
        Out[I]=Destination.InverseTransformPosition(P+FVector(0,0,V.Lift));
    }
}
}
