#include "MCFoodCutGeometry.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "GeomTools.h"
#include "Algo/Reverse.h"
DEFINE_LOG_CATEGORY(LogMCFoodCuts);

namespace
{
constexpr double WeldTolerance=1.e-3;

struct FContourPoints
{
    TArray<FVector> Positions;
    TMap<FIntVector,TArray<int32>> Buckets;

    int32 FindOrAdd(FVector P)
    {
        const FVector Grid=P/WeldTolerance;
        if(Grid.ContainsNaN() || Grid.GetAbs().GetMax()>MAX_int32-2.) return INDEX_NONE;
        const FIntVector Cell(FMath::FloorToInt(Grid.X),FMath::FloorToInt(Grid.Y),FMath::FloorToInt(Grid.Z));
        for(int32 X=-1;X<=1;++X) for(int32 Y=-1;Y<=1;++Y) for(int32 Z=-1;Z<=1;++Z)
            if(const auto* Candidates=Buckets.Find(Cell+FIntVector(X,Y,Z)))
                for(int32 I:*Candidates)
                    if(FVector::DistSquared(P,Positions[I])<=FMath::Square(WeldTolerance)) return I;
        const int32 Index=Positions.Add(P);
        Buckets.FindOrAdd(Cell).Add(Index);
        return Index;
    }
};

double TwiceArea(const TArray<FVector2D>& Polygon)
{
    double Area=0;
    for(int32 I=0;I<Polygon.Num();++I) Area+=Polygon[I]^Polygon[(I+1)%Polygon.Num()];
    return Area;
}
}

bool MCFoodCutGeometry::SliceAndCap(UProceduralMeshComponent* Mesh,FVector WorldPoint,FVector WorldNormal,UMaterialInterface* CapMaterial)
{
    if(!Mesh || WorldPoint.ContainsNaN() || WorldNormal.ContainsNaN()) return false;
    const FTransform Pose=Mesh->GetComponentTransform();
    const FVector Point=Pose.InverseTransformPosition(WorldPoint);
    const FVector Normal=(Pose.GetScale3D()*Pose.InverseTransformVectorNoScale(WorldNormal)).GetSafeNormal();
    if(Point.ContainsNaN() || Normal.IsNearlyZero() || Normal.ContainsNaN()) return false;

    // Do not invoke the stock cap triangulator: its degenerate-loop path can
    // append indices from an unrelated earlier polygon. Keep its skin clipping.
    UProceduralMeshComponent* Unused=nullptr;
    UKismetProceduralMeshLibrary::SliceProceduralMesh(Mesh,WorldPoint,WorldNormal,false,Unused,
        EProcMeshSliceCapOption::NoCap,nullptr);

    FContourPoints Points;
    TMap<FIntPoint,int32> EdgeCounts;
    for(int32 Section=0;Section<Mesh->GetNumSections();++Section) {
        const FProcMeshSection* Data=Mesh->GetProcMeshSection(Section);
        if(!Data) continue;
        if(Data->ProcIndexBuffer.Num()%3) return false;
        for(int32 I=0;I<Data->ProcIndexBuffer.Num();I+=3) {
            FVector V[3];bool On[3];
            for(int32 K=0;K<3;++K) {
                const uint32 Index=Data->ProcIndexBuffer[I+K];
                if(!Data->ProcVertexBuffer.IsValidIndex(Index)) return false;
                V[K]=Data->ProcVertexBuffer[Index].Position;
                if(V[K].ContainsNaN()) return false;
                On[K]=FMath::Abs(FVector::DotProduct(V[K]-Point,Normal))<=WeldTolerance;
            }
            if(FVector::CrossProduct(V[1]-V[0],V[2]-V[0]).SizeSquared()<=1.e-20) continue;
            // A face already lying on the plane is not a newly exposed rim.
            if(On[0] && On[1] && On[2]) continue;
            for(int32 K=0;K<3;++K) if(On[K] && On[(K+1)%3]) {
                FVector A=V[K],B=V[(K+1)%3];
                A-=Normal*FVector::DotProduct(A-Point,Normal);
                B-=Normal*FVector::DotProduct(B-Point,Normal);
                const int32 IA=Points.FindOrAdd(A),IB=Points.FindOrAdd(B);
                if(IA==INDEX_NONE || IB==INDEX_NONE) return false;
                if(IA!=IB) ++EdgeCounts.FindOrAdd(FIntPoint(FMath::Min(IA,IB),FMath::Max(IA,IB)));
            }
        }
    }

    TArray<TArray<int32>> Neighbours;Neighbours.SetNum(Points.Positions.Num());
    for(const auto& Edge:EdgeCounts) {
        if(Edge.Value>2) {UE_LOG(LogMCFoodCuts,Verbose,TEXT("Cap refused: edge shared %d times"),Edge.Value);return false;}
        if(Edge.Value!=1) continue;
        Neighbours[Edge.Key.X].Add(Edge.Key.Y);
        Neighbours[Edge.Key.Y].Add(Edge.Key.X);
    }
    FVector AxisX,UnusedAxis;
    Normal.FindBestAxisVectors(AxisX,UnusedAxis);
    const FVector AxisY=FVector::CrossProduct(Normal,AxisX).GetSafeNormal();
    FProcMeshSection Cap;Cap.bEnableCollision=false;Cap.bSectionVisible=true;
    TBitArray<> Visited(false,Neighbours.Num());
    for(int32 Start=0;Start<Neighbours.Num();++Start) {
        if(Visited[Start] || Neighbours[Start].IsEmpty()) continue;
        TArray<int32> Component,Pending={Start};bool Closed=true;
        Visited[Start]=true;
        while(!Pending.IsEmpty()) {
            const int32 Current=Pending.Pop(EAllowShrinking::No);Component.Add(Current);
            if(Neighbours[Current].Num()>2) {UE_LOG(LogMCFoodCuts,Verbose,TEXT("Cap refused: ambiguous contour junction degree %d"),Neighbours[Current].Num());return false;}
            if(Neighbours[Current].Num()!=2) Closed=false;
            for(int32 Next:Neighbours[Current]) if(!Visited[Next]) {Visited[Next]=true;Pending.Add(Next);}
        }
        // Imported one-sided leaves can leave open ornament chains. They have
        // no closed cap to fill; retain their skin without inventing triangles.
        if(!Closed || Component.Num()<3) continue;
        TArray<FVector2D> Polygon;
        int32 Previous=INDEX_NONE,Current=Start;
        do {
            const FVector P=Points.Positions[Current]-Point;
            Polygon.Add(FVector2D(FVector::DotProduct(P,AxisX),FVector::DotProduct(P,AxisY)));
            const auto& Links=Neighbours[Current];
            const int32 Next=Links[0]!=Previous?Links[0]:Links[1];
            Previous=Current;Current=Next;
            if(Polygon.Num()>Component.Num()) return false;
        } while(Current!=Start);
        if(Polygon.Num()!=Component.Num()) return false;
        double Area=TwiceArea(Polygon);
        if(FMath::Abs(Area)<=1.e-10) continue;
        if(Area<0) {Algo::Reverse(Polygon);Area=-Area;}
        TArray<FVector2D> Triangles;
        if(!FGeomTools2D::TriangulatePoly(Triangles,Polygon) || Triangles.IsEmpty() || Triangles.Num()%3) {
            UE_LOG(LogMCFoodCuts,Verbose,TEXT("Cap refused: contour triangulation, %d vertices"),Polygon.Num());return false;
        }
        double FilledArea=0;
        for(int32 I=0;I<Triangles.Num();I+=3) {
            const double TriangleArea=(Triangles[I+1]-Triangles[I])^(Triangles[I+2]-Triangles[I]);
            if(!FMath::IsFinite(TriangleArea) || TriangleArea<=1.e-12) {
                UE_LOG(LogMCFoodCuts,Verbose,TEXT("Cap refused: degenerate triangulated area %g"),TriangleArea);return false;
            }
            FilledArea+=TriangleArea;
        }
        if(FMath::Abs(FilledArea-Area)>FMath::Max(1.e-8,Area*.001)) return false;
        for(const FVector2D& P:Triangles) {
            FProcMeshVertex Vertex;
            Vertex.Position=Point+AxisX*P.X+AxisY*P.Y;
            Vertex.Normal=-Normal;Vertex.Tangent=FProcMeshTangent(AxisX,false);Vertex.Color=FColor::White;
            Vertex.UV0=FVector2D(FVector::DotProduct(Vertex.Position,AxisX),FVector::DotProduct(Vertex.Position,AxisY))/64.;
            Cap.ProcIndexBuffer.Add(Cap.ProcVertexBuffer.Num());
            Cap.ProcVertexBuffer.Add(Vertex);Cap.SectionLocalBox+=Vertex.Position;
        }
    }
    // Independent loops match stock solid-food caps. Nested contour holes are
    // not a supported hollow-solid Boolean operation and are not claimed here.
    if(!Cap.ProcIndexBuffer.IsEmpty()) {
        const int32 CapSection=Mesh->GetNumSections();
        Mesh->SetProcMeshSection(CapSection,Cap);Mesh->SetMaterial(CapSection,CapMaterial);
    }
    return true;
}

namespace
{
struct FFoodIslandTriangle
{
    int32 Section=0;
    FIntVector Vertices=FIntVector::ZeroValue;
    FIntVector Welded=FIntVector::ZeroValue;
};

struct FFoodIslandEdge
{
    int32 A=0,B=0,Triangle=0;
};

struct FFoodIslandSets
{
    TArray<int32> Parent,Count;

    explicit FFoodIslandSets(int32 Num)
    {
        Parent.SetNumUninitialized(Num);Count.Init(1,Num);
        for(int32 I=0;I<Num;++I)Parent[I]=I;
    }
    int32 Find(int32 I)
    {
        int32 Root=I;
        while(Parent[Root]!=Root)Root=Parent[Root];
        while(Parent[I]!=I) {const int32 Next=Parent[I];Parent[I]=Root;I=Next;}
        return Root;
    }
    void Join(int32 A,int32 B)
    {
        A=Find(A);B=Find(B);if(A==B)return;
        if(Count[A]<Count[B] || (Count[A]==Count[B] && A>B))Swap(A,B);
        Parent[B]=A;Count[A]+=Count[B];
    }
};

const FProcMeshSection* ReadFoodIslandSection(const UProceduralMeshComponent* Mesh,int32 Index)
{
    // UE 5.8 exposes no const GetProcMeshSection overload. This path only reads.
    return const_cast<UProceduralMeshComponent*>(Mesh)->GetProcMeshSection(Index);
}

bool BuildFoodIslandTopology(const UProceduralMeshComponent* Mesh,TArray<FFoodIslandTriangle>& Triangles,
    TArray<int32>& TriangleIslands,TArray<MCFoodCutGeometry::FIsland>& Islands,FString* Error,int32 MaxIslands)
{
    Triangles.Reset();TriangleIslands.Reset();Islands.Reset();if(Error)Error->Reset();
    auto Fail=[&](const TCHAR* Message){if(Error)*Error=Message;Islands.Reset();return false;};
    if(!Mesh || MaxIslands<1)return Fail(TEXT("Island inspection requires a mesh and a positive component limit."));

    FContourPoints Points;
    for(int32 S=0;S<Mesh->GetNumSections();++S) {
        const auto* Section=ReadFoodIslandSection(Mesh,S);
        if(!Section || Section->ProcIndexBuffer.IsEmpty())continue;
        if(Section->ProcIndexBuffer.Num()%3)return Fail(TEXT("Island inspection found an incomplete triangle."));
        TArray<int32> VertexWeld;VertexWeld.Init(INDEX_NONE,Section->ProcVertexBuffer.Num());
        for(int32 I=0;I<Section->ProcIndexBuffer.Num();I+=3) {
            FFoodIslandTriangle Triangle;Triangle.Section=S;
            FVector P[3];
            for(int32 K=0;K<3;++K) {
                const uint32 Index=Section->ProcIndexBuffer[I+K];
                if(!Section->ProcVertexBuffer.IsValidIndex(Index))return Fail(TEXT("Island inspection found an invalid triangle index."));
                Triangle.Vertices[K]=int32(Index);P[K]=Section->ProcVertexBuffer[Index].Position;
                if(P[K].ContainsNaN())return Fail(TEXT("Island inspection found a non-finite vertex."));
            }
            // Match the existing slice validator: a zero-area face has no
            // visible area and must not join two otherwise disconnected parts.
            if(FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).SizeSquared()<=1.e-20)continue;
            if(Triangles.Num()>=500000)return Fail(TEXT("Island triangle budget exceeded; original food must be retained."));
            for(int32 K=0;K<3;++K) {
                int32& Welded=VertexWeld[Triangle.Vertices[K]];
                if(Welded==INDEX_NONE)Welded=Points.FindOrAdd(P[K]);
                if(Welded==INDEX_NONE)return Fail(TEXT("Island vertex exceeds the supported local coordinate range."));
                Triangle.Welded[K]=Welded;
            }
            Triangles.Add(Triangle);
        }
    }
    if(Triangles.IsEmpty())return Fail(TEXT("Island inspection found no non-degenerate triangles."));

    FFoodIslandSets Sets(Triangles.Num());
    TMap<FIntPoint,int32> EdgeLookup;
    TArray<FFoodIslandEdge> Edges;
    TArray<TArray<int32>> IncidentEdges;IncidentEdges.SetNum(Points.Positions.Num());
    for(int32 I=0;I<Triangles.Num();++I) {
        const auto& Triangle=Triangles[I];
        for(int32 K=0;K<3;++K) {
            const int32 A=Triangle.Welded[K],B=Triangle.Welded[(K+1)%3];
            if(A==B)continue;
            const FIntPoint Key(FMath::Min(A,B),FMath::Max(A,B));
            if(const int32* Existing=EdgeLookup.Find(Key))Sets.Join(I,Edges[*Existing].Triangle);
            else {
                const int32 EdgeIndex=Edges.Num();
                FFoodIslandEdge Edge;Edge.A=Key.X;Edge.B=Key.Y;Edge.Triangle=I;
                Edges.Add(Edge);EdgeLookup.Add(Key,EdgeIndex);
                IncidentEdges[Edge.A].Add(EdgeIndex);IncidentEdges[Edge.B].Add(EdgeIndex);
            }
        }
    }

    // Cap triangulation may omit collinear rim vertices. Its long edge AB and
    // a skin edge AC then overlap but do not share the same pair of endpoints.
    // Join positive collinear overlap, rather than joining every point contact.
    // In particular two solids touching at only a corner stay separate.
    const double ToleranceSquared=FMath::Square(WeldTolerance);
    for(int32 Point=0;Point<IncidentEdges.Num();++Point) {
        const auto& Incident=IncidentEdges[Point];
        if(Incident.Num()<2)continue;
        const int32 FirstRoot=Sets.Find(Edges[Incident[0]].Triangle);
        bool Different=false;
        for(int32 EdgeIndex:Incident)if(Sets.Find(Edges[EdgeIndex].Triangle)!=FirstRoot){Different=true;break;}
        if(!Different)continue;
        if(Incident.Num()>4096)return Fail(TEXT("Island junction budget exceeded; original food must be retained."));
        for(int32 A=0;A<Incident.Num();++A) {
            const auto& EdgeA=Edges[Incident[A]];
            const FVector AlongA=Points.Positions[EdgeA.A==Point?EdgeA.B:EdgeA.A]-Points.Positions[Point];
            const double LengthSquared=AlongA.SizeSquared();
            if(LengthSquared<=ToleranceSquared)continue;
            for(int32 B=A+1;B<Incident.Num();++B) {
                const auto& EdgeB=Edges[Incident[B]];
                if(Sets.Find(EdgeA.Triangle)==Sets.Find(EdgeB.Triangle))continue;
                const FVector AlongB=Points.Positions[EdgeB.A==Point?EdgeB.B:EdgeB.A]-Points.Positions[Point];
                const double Dot=FVector::DotProduct(AlongA,AlongB);
                if(Dot<=ToleranceSquared)continue; // Opposite rays only share their endpoint.
                // Measure the short endpoint against the longer edge's line;
                // tiny rim edges must not amplify imported seam round-off.
                const double PerpendicularSquared=FVector::CrossProduct(AlongA,AlongB).SizeSquared()
                    /FMath::Max(LengthSquared,AlongB.SizeSquared());
                if(PerpendicularSquared<=ToleranceSquared)Sets.Join(EdgeA.Triangle,EdgeB.Triangle);
            }
        }
    }

    TriangleIslands.SetNumUninitialized(Triangles.Num());
    TMap<int32,int32> RootToIsland;
    TArray<TSet<FIntPoint>> UsedVertices;
    for(int32 I=0;I<Triangles.Num();++I) {
        const int32 Root=Sets.Find(I);
        int32 Index;
        if(const int32* Existing=RootToIsland.Find(Root))Index=*Existing;
        else {
            if(Islands.Num()>=MaxIslands)return Fail(TEXT("Island component budget exceeded; original food must be retained."));
            Index=Islands.Num();RootToIsland.Add(Root,Index);
            auto& Island=Islands.AddDefaulted_GetRef();Island.Index=Index;
            UsedVertices.AddDefaulted();
        }
        TriangleIslands[I]=Index;
        auto& Island=Islands[Index];++Island.TriangleCount;
        const auto& Triangle=Triangles[I];
        const auto* Section=ReadFoodIslandSection(Mesh,Triangle.Section);
        for(int32 K=0;K<3;++K) {
            const int32 VertexIndex=Triangle.Vertices[K];
            const FIntPoint Key(Triangle.Section,VertexIndex);
            if(UsedVertices[Index].Contains(Key))continue;
            UsedVertices[Index].Add(Key);
            const FVector Position=Section->ProcVertexBuffer[VertexIndex].Position;
            Island.Vertices.Add(Position);Island.Bounds+=Position;
        }
    }
    return true;
}
}

bool MCFoodCutGeometry::FindIslands(const UProceduralMeshComponent* Mesh,TArray<FIsland>& Out,FString* Error,int32 MaxIslands)
{
    TArray<FFoodIslandTriangle> Triangles;TArray<int32> TriangleIslands;
    return BuildFoodIslandTopology(Mesh,Triangles,TriangleIslands,Out,Error,MaxIslands);
}

bool MCFoodCutGeometry::FilterIsland(UProceduralMeshComponent* Mesh,int32 IslandIndex,FString* Error)
{
    TArray<FFoodIslandTriangle> Triangles;TArray<int32> TriangleIslands;TArray<FIsland> Islands;
    if(!BuildFoodIslandTopology(Mesh,Triangles,TriangleIslands,Islands,Error,256))return false;
    if(!Islands.IsValidIndex(IslandIndex)) {if(Error)*Error=TEXT("Requested food island is absent from the reconstructed mesh.");return false;}

    // Build every section before mutating the component. Empty slots stay in
    // place so skin/cap material indices survive selection and future slicing.
    TArray<FProcMeshSection> Sections;Sections.SetNum(Mesh->GetNumSections());
    TArray<TArray<int32>> VertexMaps;VertexMaps.SetNum(Sections.Num());
    for(int32 S=0;S<Sections.Num();++S) {
        if(const auto* Source=ReadFoodIslandSection(Mesh,S)) {
            Sections[S]=*Source;
            Sections[S].ProcVertexBuffer.Reset();Sections[S].ProcIndexBuffer.Reset();
            Sections[S].SectionLocalBox=FBox(ForceInit);
            VertexMaps[S].Init(INDEX_NONE,Source->ProcVertexBuffer.Num());
        }
    }
    for(int32 I=0;I<Triangles.Num();++I)if(TriangleIslands[I]==IslandIndex) {
        const auto& Triangle=Triangles[I];
        const auto* Source=ReadFoodIslandSection(Mesh,Triangle.Section);
        auto& Target=Sections[Triangle.Section];auto& Remap=VertexMaps[Triangle.Section];
        for(int32 K=0;K<3;++K) {
            const int32 Original=Triangle.Vertices[K];int32& NewIndex=Remap[Original];
            if(NewIndex==INDEX_NONE) {
                NewIndex=Target.ProcVertexBuffer.Add(Source->ProcVertexBuffer[Original]);
                Target.SectionLocalBox+=Source->ProcVertexBuffer[Original].Position;
            }
            Target.ProcIndexBuffer.Add(uint32(NewIndex));
        }
    }
    for(int32 S=0;S<Sections.Num();++S)Mesh->SetProcMeshSection(S,Sections[S]);
    return true;
}
