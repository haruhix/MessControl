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
