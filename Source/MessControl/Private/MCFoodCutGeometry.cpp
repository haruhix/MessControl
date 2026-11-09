#include "MCFoodCutGeometry.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "GeomTools.h"
#include "Algo/Reverse.h"
#include "Chaos/Convex.h"
#include "Materials/MaterialInterface.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
DEFINE_LOG_CATEGORY(LogMCFoodCuts);

MCFoodCutGeometry::FPreparationStats& MCFoodCutGeometry::PreparationStats()
{static FPreparationStats Stats;return Stats;}
MCFoodCutGeometry::FPreparationStats MCFoodCutGeometry::GetPreparationStats(){return PreparationStats();}
void MCFoodCutGeometry::ResetPreparationStats(){PreparationStats()={};}
bool MCFoodCutGeometry::FPreparedMesh::MaterialsValid() const
{for(const auto& Material:Materials)if(!Material.IsExplicitlyNull() && !Material.IsValid())return false;return true;}

bool MCFoodCutGeometry::FPreparedCollision::IsValid() const
{
    if(!Convex || !Convex->IsValidGeometry() || !Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()
        || HullVertices.Num()<3 || HullIndices.IsEmpty() || HullIndices.Num()%3)return false;
    for(const auto& V:HullVertices)if(V.ContainsNaN())return false;
    for(int32 I:HullIndices)if(!HullVertices.IsValidIndex(I))return false;
    return true;
}

namespace
{
// This function owns one plain-data result. No UObject, shared-pointer control
// block, weak index or global diagnostic counter is accessed by a worker.
TUniquePtr<MCFoodCutGeometry::FPreparedCollision> BuildFoodConvex(const MCFoodCutGeometry::FConvexRequest& Request,uint32& Attempts)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodBuildPreparedConvex);
    using namespace MCFoodCutGeometry;
    for(const auto& V:Request.Vertices)if(V.ContainsNaN())return {};
    auto Build=[&](const TArray<FVector>& Input,bool Proxy)->TUniquePtr<FPreparedCollision> {
        if(Input.Num()<4)return {};
        TArray<Chaos::FConvex::FVec3Type> Points;Points.Reserve(Input.Num());FBox Bounds(ForceInit);
        for(const auto& V:Input){const Chaos::FConvex::FVec3Type Point(V);if(Point.ContainsNaN())return {};Points.Add(Point);Bounds+=V;}
        ++Attempts;
        auto Result=MakeUnique<FPreparedCollision>();Result->Convex=new Chaos::FConvex(Points,0);
        if(!Result->Convex->IsValidGeometry())return {};
        Result->Bounds=Bounds;Result->bPlanarProxy=Proxy;
        for(int32 V=0;V<Result->Convex->NumVertices();++V)Result->HullVertices.Add(FVector(Result->Convex->GetVertex(V)));
        // Reuse the hull's own merged faces, rather than calling BuildIndices
        // (which would construct a second hull on the game thread at install).
        for(int32 Face=0;Face<Result->Convex->NumPlanes();++Face) {
            const int32 Count=Result->Convex->NumPlaneVertices(Face);if(Count<3)continue;
            const FVector Normal=FVector(Result->Convex->GetPlane(Face).Normal());
            for(int32 T=1;T<Count-1;++T) {
                const int32 A=Result->Convex->GetPlaneVertex(Face,0);
                int32 B=Result->Convex->GetPlaneVertex(Face,T),C=Result->Convex->GetPlaneVertex(Face,T+1);
                if(!Result->HullVertices.IsValidIndex(A) || !Result->HullVertices.IsValidIndex(B) || !Result->HullVertices.IsValidIndex(C))return {};
                const FVector Cross=FVector::CrossProduct(Result->HullVertices[B]-Result->HullVertices[A],Result->HullVertices[C]-Result->HullVertices[A]);
                const double Winding=FVector::DotProduct(Cross,Normal);
                if(Cross.SizeSquared()<=1.e-20 || FMath::Abs(Winding)<=1.e-20)continue;
                if(Winding<0)Swap(B,C);
                Result->HullIndices.Append({A,B,C});
            }
        }
        return Result->IsValid()?MoveTemp(Result):TUniquePtr<FPreparedCollision>();
    };
    if(auto Result=Build(Request.Vertices,false))return Result;
    if(!Request.bAllowPlanarProxy || Request.Vertices.Num()<3)return {};
    FVector Normal=FVector::ZeroVector,Edge=FVector::ZeroVector;
    for(int32 I=1;I<Request.Vertices.Num();++I){if(Edge.IsNearlyZero()){Edge=Request.Vertices[I]-Request.Vertices[0];continue;}Normal=FVector::CrossProduct(Edge,Request.Vertices[I]-Request.Vertices[0]).GetSafeNormal();if(!Normal.IsNearlyZero())break;}
    if(Normal.IsNearlyZero())return {};
    TArray<FVector> Proxy;Proxy.Reserve(Request.Vertices.Num()*2);
    for(const auto& P:Request.Vertices){Proxy.Add(P+Normal*.05);Proxy.Add(P-Normal*.05);}
    return Build(Proxy,true);
}
}

bool MCFoodCutGeometry::PrepareConvexBatch(const TArray<FConvexRequest>& Requests,TArray<TUniquePtr<FPreparedCollision>>& Out)
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodPrepareConvexBatch);
    Out.Reset();if(Requests.IsEmpty())return true;if(Requests.Num()>512)return false;
    struct FResult {TUniquePtr<FPreparedCollision> Collision;uint32 Attempts=0;};
    TArray<FResult> Results;Results.SetNum(Requests.Num());
    ++PreparationStats().ConvexBatches;
    if(Requests.Num()>1)PreparationStats().ConvexParallelItems+=Requests.Num();
    ParallelFor(Requests.Num(),[&](int32 I){Results[I].Collision=BuildFoodConvex(Requests[I],Results[I].Attempts);},
        Requests.Num()>1?EParallelForFlags::Unbalanced:EParallelForFlags::ForceSingleThread);
    bool Valid=true;
    for(const auto& Result:Results){PreparationStats().ConvexBuilds+=Result.Attempts;Valid&=bool(Result.Collision);}
    if(!Valid)return false;
    Out.Reserve(Results.Num());for(auto& Result:Results)Out.Add(MoveTemp(Result.Collision));
    return true;
}

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
    int32 Count=1,Direction=0;
};

// Ordinary welded edges must occur twice in opposite directions. At a cap's
// collinear T-junction, a long edge may instead be covered by several reverse
// edges. Verify that coverage without changing the render mesh or its indices.
bool ClosedFoodIsland(const TArray<FFoodIslandEdge>& Edges,const TArray<int32>& Indices,
    const TArray<FVector>& Positions)
{
    if(Indices.IsEmpty())return false;
    TArray<int32> Boundary;
    for(int32 Index:Indices) {
        const auto& Edge=Edges[Index];
        if(Edge.Count==2 && Edge.Direction==0)continue;
        if(Edge.Count!=1)return false;
        Boundary.Add(Index);
    }
    if(Boundary.IsEmpty())return true;
    // Large/open artwork takes the conservative hull path rather than turning
    // closure inspection into an unbounded quadratic repair operation.
    if(Boundary.Num()>1024)return false;
    for(int32 Index:Boundary) {
        const auto& Edge=Edges[Index];
        const FVector A=Positions[Edge.Direction>0?Edge.A:Edge.B];
        const FVector B=Positions[Edge.Direction>0?Edge.B:Edge.A];
        const FVector Along=B-A;const double Length=Along.Size();
        if(Length<=WeldTolerance)return false;
        const FVector Axis=Along/Length;
        TArray<FVector2D> Coverage;
        for(int32 OtherIndex:Boundary) {
            if(OtherIndex==Index)continue;
            const auto& Other=Edges[OtherIndex];
            const FVector C=Positions[Other.Direction>0?Other.A:Other.B];
            const FVector D=Positions[Other.Direction>0?Other.B:Other.A];
            if(FVector::DotProduct(D-C,Axis)>=0)continue;
            if(FVector::CrossProduct(C-A,Axis).SizeSquared()>FMath::Square(WeldTolerance)
                || FVector::CrossProduct(D-A,Axis).SizeSquared()>FMath::Square(WeldTolerance))continue;
            const double Low=FMath::Max(0.,FVector::DotProduct(D-A,Axis));
            const double High=FMath::Min(Length,FVector::DotProduct(C-A,Axis));
            if(High>Low)Coverage.Emplace(Low,High);
        }
        Coverage.Sort([](const FVector2D& A,const FVector2D& B){return A.X<B.X;});
        double Covered=0;
        for(const auto& Interval:Coverage) {
            if(FMath::Abs(Interval.X-Covered)>WeldTolerance)return false;
            Covered=Interval.Y;
        }
        if(FMath::Abs(Covered-Length)>WeldTolerance)return false;
    }
    return true;
}

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
    TArray<int32>& TriangleIslands,TArray<MCFoodCutGeometry::FIsland>& Islands,FString* Error,int32 MaxIslands,
    const FTransform* MeasurementTransform=nullptr)
{
    ++MCFoodCutGeometry::PreparationStats().TopologyPasses;
    Triangles.Reset();TriangleIslands.Reset();Islands.Reset();if(Error)Error->Reset();
    auto Fail=[&](const TCHAR* Message){if(Error)*Error=Message;Islands.Reset();return false;};
    if(!Mesh || MaxIslands<1)return Fail(TEXT("Island inspection requires a mesh and a positive component limit."));
    if(MeasurementTransform && (MeasurementTransform->ContainsNaN()
        || MeasurementTransform->GetScale3D().GetAbs().GetMin()<SMALL_NUMBER))
        return Fail(TEXT("Island volume requires a finite, non-zero measurement scale."));

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
            const int32 Direction=A==Key.X?1:-1;
            if(const int32* Existing=EdgeLookup.Find(Key)) {
                auto& Edge=Edges[*Existing];++Edge.Count;Edge.Direction+=Direction;
                Sets.Join(I,Edge.Triangle);
            }
            else {
                const int32 EdgeIndex=Edges.Num();
                FFoodIslandEdge Edge;Edge.A=Key.X;Edge.B=Key.Y;Edge.Triangle=I;Edge.Direction=Direction;
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
    if(!MeasurementTransform)return true;

    // Accumulate from referenced triangles, relative to each part's own centre.
    // Neither empty AABB space, an unused vertex nor a distant mesh origin is
    // food volume. Transform vectors keep translation out of these metrics.
    TArray<double> SignedSixVolume;SignedSixVolume.Init(0,Islands.Num());
    TArray<double> LargestFace;LargestFace.Init(0,Islands.Num());
    TBitArray<> CollapsedWeld(false,Islands.Num());
    TArray<FVector> PlaneNormals;PlaneNormals.Init(FVector::ZeroVector,Islands.Num());
    TArray<FVector> PlanePoints;PlanePoints.Init(FVector::ZeroVector,Islands.Num());
    TArray<TArray<int32>> IslandEdges;IslandEdges.SetNum(Islands.Num());
    for(int32 E=0;E<Edges.Num();++E)IslandEdges[TriangleIslands[Edges[E].Triangle]].Add(E);
    for(int32 I=0;I<Triangles.Num();++I) {
        const int32 Index=TriangleIslands[I];auto& Island=Islands[Index];
        const auto& Triangle=Triangles[I];const auto* Section=ReadFoodIslandSection(Mesh,Triangle.Section);
        CollapsedWeld[Index]=CollapsedWeld[Index] || Triangle.Welded.X==Triangle.Welded.Y
            || Triangle.Welded.Y==Triangle.Welded.Z || Triangle.Welded.Z==Triangle.Welded.X;
        const FVector Centre=Island.Bounds.GetCenter();FVector P[3];
        for(int32 K=0;K<3;++K)P[K]=MeasurementTransform->TransformVector(Section->ProcVertexBuffer[Triangle.Vertices[K]].Position-Centre);
        const FVector Cross=FVector::CrossProduct(P[1]-P[0],P[2]-P[0]);
        const double TwiceFaceArea=Cross.Size();
        Island.SurfaceArea+=TwiceFaceArea*.5;
        SignedSixVolume[Index]+=FVector::DotProduct(P[0],FVector::CrossProduct(P[1],P[2]));
        if(TwiceFaceArea>LargestFace[Index]) {
            LargestFace[Index]=TwiceFaceArea;PlaneNormals[Index]=Cross/TwiceFaceArea;PlanePoints[Index]=P[0];
        }
    }
    for(int32 Index=0;Index<Islands.Num();++Index) {
        auto& Island=Islands[Index];
        if(!FMath::IsFinite(Island.SurfaceArea) || Island.SurfaceArea<=0 || !FMath::IsFinite(SignedSixVolume[Index]))
            return Fail(TEXT("Island volume found a non-finite or empty surface."));
        const double SolidVolume=FMath::Abs(SignedSixVolume[Index])/6.;
        if(SolidVolume>0 && !CollapsedWeld[Index] && ClosedFoodIsland(Edges,IslandEdges[Index],Points.Positions)) {
            Island.Volume=SolidVolume;Island.VolumeMethod=MCFoodCutGeometry::EVolumeMethod::ClosedMesh;continue;
        }
        bool Planar=!PlaneNormals[Index].IsNearlyZero();
        TArray<Chaos::FConvex::FVec3Type> HullPoints;HullPoints.Reserve(Island.Vertices.Num());
        const FVector Centre=Island.Bounds.GetCenter();
        for(const FVector& Vertex:Island.Vertices) {
            const FVector P=MeasurementTransform->TransformVector(Vertex-Centre);
            if(P.ContainsNaN())return Fail(TEXT("Island volume found a non-finite transformed vertex."));
            Planar&=FMath::Abs(FVector::DotProduct(P-PlanePoints[Index],PlaneNormals[Index]))<=WeldTolerance;
            HullPoints.Add(Chaos::FConvex::FVec3Type(P));
        }
        if(Planar) {
            // A leaf/skin ornament has no enclosed solid. Give its real area
            // the same explicit 0.1 cm nominal thickness as the collision proxy.
            Island.Volume=Island.SurfaceArea*.1;
            Island.VolumeMethod=MCFoodCutGeometry::EVolumeMethod::PlanarSurface;
        } else {
            // Inconsistent winding/open imported artwork cannot safely use a
            // cancelling signed sum. Its hull is an upper bound, never zero.
            const Chaos::FConvex Hull(HullPoints,0);
            if(!Hull.IsValidGeometry() || !FMath::IsFinite(Hull.GetVolume()) || Hull.GetVolume()<=0)
                return Fail(TEXT("Island volume could not measure an open non-planar surface; original food must be retained."));
            Island.Volume=Hull.GetVolume();Island.VolumeMethod=MCFoodCutGeometry::EVolumeMethod::ConvexHull;
        }
        if(!FMath::IsFinite(Island.Volume) || Island.Volume<=0)
            return Fail(TEXT("Island volume must be finite and positive; original food must be retained."));
    }
    return true;
}
}

bool MCFoodCutGeometry::FindIslands(const UProceduralMeshComponent* Mesh,TArray<FIsland>& Out,FString* Error,int32 MaxIslands,
    const FTransform& MeasurementTransform)
{
    TArray<FFoodIslandTriangle> Triangles;TArray<int32> TriangleIslands;
    return BuildFoodIslandTopology(Mesh,Triangles,TriangleIslands,Out,Error,MaxIslands,&MeasurementTransform);
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

namespace
{
bool FillPreparedSections(const UProceduralMeshComponent* Mesh,const FTransform& Transform,
    const TArray<FFoodIslandTriangle>& Triangles,const TArray<int32>* TriangleIslands,
    TArray<TSharedPtr<MCFoodCutGeometry::FPreparedMesh>>& Parts)
{
    using namespace MCFoodCutGeometry;
    TArray<TArray<TMap<int32,int32>>> Maps;Maps.SetNum(Parts.Num());
    for(int32 P=0;P<Parts.Num();++P) {
        auto& Out=*Parts[P];Out.Sections.SetNum(Mesh->GetNumSections());Out.Materials.SetNum(Mesh->GetNumSections());Maps[P].SetNum(Mesh->GetNumSections());
        for(int32 S=0;S<Mesh->GetNumSections();++S) {
            Out.Sections[S].bEnableCollision=false;
            if(const auto* Source=ReadFoodIslandSection(Mesh,S))Out.Sections[S].bSectionVisible=Source->bSectionVisible;
            Out.Materials[S]=Mesh->GetMaterial(S);
        }
    }
    for(int32 I=0;I<Triangles.Num();++I) {
        const int32 P=TriangleIslands?(*TriangleIslands)[I]:0;
        if(!Parts.IsValidIndex(P))return false;
        auto& Out=*Parts[P];const auto& Triangle=Triangles[I];const auto* Source=ReadFoodIslandSection(Mesh,Triangle.Section);
        auto& Target=Out.Sections[Triangle.Section];auto& Map=Maps[P][Triangle.Section];
        for(int32 K=0;K<3;++K) {
            const int32 Old=Triangle.Vertices[K];int32 New;
            if(const int32* Existing=Map.Find(Old))New=*Existing;
            else {
                const auto& Vertex=Source->ProcVertexBuffer[Old];New=Target.ProcVertexBuffer.Add(Vertex);Map.Add(Old,New);
                Target.SectionLocalBox+=Vertex.Position;const FVector V=Transform.TransformPosition(Vertex.Position);
                if(V.ContainsNaN())return false;
                Out.Vertices.Add(V);Out.Bounds+=V;
            }
            Target.ProcIndexBuffer.Add(uint32(New));
        }
    }
    for(const auto& Part:Parts)if(!Part->Bounds.IsValid || Part->Vertices.Num()<3)return false;
    return true;
}
}

bool MCFoodCutGeometry::CapturePreparedMesh(const UProceduralMeshComponent* Mesh,const FTransform& Transform,FPreparedMesh& Out)
{
    if(!Mesh || Transform.ContainsNaN())return false;
    TArray<FFoodIslandTriangle> Triangles;
    for(int32 S=0;S<Mesh->GetNumSections();++S)if(const auto* Section=ReadFoodIslandSection(Mesh,S)) {
        if(Section->ProcIndexBuffer.Num()%3)return false;
        for(int32 I=0;I<Section->ProcIndexBuffer.Num();I+=3) {
            FFoodIslandTriangle T;T.Section=S;
            for(int32 K=0;K<3;++K){const uint32 V=Section->ProcIndexBuffer[I+K];if(!Section->ProcVertexBuffer.IsValidIndex(V))return false;T.Vertices[K]=int32(V);}
            Triangles.Add(T);
        }
    }
    if(Triangles.IsEmpty())return false;
    TArray<TSharedPtr<FPreparedMesh>> Parts;Parts.Add(MakeShared<FPreparedMesh>());
    if(!FillPreparedSections(Mesh,Transform,Triangles,nullptr,Parts))return false;
    Out=MoveTemp(*Parts[0]);return true;
}

bool MCFoodCutGeometry::MaterializePreparedMesh(UProceduralMeshComponent* Mesh,const FPreparedMesh& Prepared)
{
    if(!Mesh || !Prepared.MaterialsValid() || !Prepared.Bounds.IsValid)return false;
    ++PreparationStats().Materializations;
    Mesh->ClearAllMeshSections();
    for(int32 S=0;S<Prepared.Sections.Num();++S){Mesh->SetProcMeshSection(S,Prepared.Sections[S]);Mesh->SetMaterial(S,Prepared.Materials.IsValidIndex(S)?Prepared.Materials[S].Get():nullptr);}
    return true;
}

bool MCFoodCutGeometry::ExtractPreparedIslands(const UProceduralMeshComponent* Mesh,const FTransform& Transform,
    TArray<TSharedPtr<const FPreparedMesh>>& Out,FString* Error)
{
    Out.Reset();TArray<FFoodIslandTriangle> Triangles;TArray<int32> TriangleIslands;TArray<FIsland> Islands;
    if(!BuildFoodIslandTopology(Mesh,Triangles,TriangleIslands,Islands,Error,256,&Transform))return false;
    ++PreparationStats().IslandExtractions;
    TArray<TSharedPtr<FPreparedMesh>> Parts;
    for(const auto& Island:Islands){auto Part=MakeShared<FPreparedMesh>();Part->LocalVolume=Island.Volume;Parts.Add(Part);}
    if(!FillPreparedSections(Mesh,Transform,Triangles,&TriangleIslands,Parts))return false;
    for(auto& Part:Parts)Out.Add(MoveTemp(Part));
    return !Out.IsEmpty();
}
