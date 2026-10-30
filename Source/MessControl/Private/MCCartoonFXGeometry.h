#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

/** Small, closed, lit cartoon solids. Coordinates are component-local.
 * Right/Up/View must form an orthonormal frame. Angles are radians; Depth and
 * Arc Width are full thicknesses; Ball Scale contains the three radii.
 * Unit topology is retained across worlds and contains no UObject references.
 * Build/upload on the game thread, as for the owning procedural component.
 */
namespace MCCartoonFX
{
namespace Detail
{
struct FUnitVertex
{
    FVector Position,Normal;
    FVector2D UV;
};
struct FUnitMesh
{
    TArray<FUnitVertex> Vertices;
    // Mathematical outward winding. Append converts to UE procedural winding.
    TArray<int32> Indices;
};
inline double Cross2(FVector2D A,FVector2D B) {return A.X*B.Y-A.Y*B.X;}

inline TArray<FVector2D> RoundContour(const TArray<FVector2D>& Polygon)
{
    TArray<FVector2D> Result;Result.Reserve(Polygon.Num()*4);
    for(int32 I=0;I<Polygon.Num();++I)
    {
        const FVector2D P=Polygon[I],Previous=Polygon[(I+Polygon.Num()-1)%Polygon.Num()],Next=Polygon[(I+1)%Polygon.Num()];
        const FVector2D A=FMath::Lerp(P,Previous,.18),B=FMath::Lerp(P,Next,.18);
        for(int32 J=0;J<4;++J)
        {
            const double T=J/3.;
            Result.Add(A*((1-T)*(1-T))+P*(2*(1-T)*T)+B*(T*T));
        }
    }
    return Result;
}
// Concave plus/star caps cannot use a center fan. This runs only while a unit
// shape is first cached, not while an effect animates.
inline bool Triangulate(const TArray<FVector2D>& Outline,TArray<int32>& Out)
{
    Out.Reset();TArray<int32> Remaining;Remaining.Reserve(Outline.Num());
    for(int32 I=0;I<Outline.Num();++I) Remaining.Add(I);
    while(Remaining.Num()>3)
    {
        bool Found=false;
        for(int32 I=0;I<Remaining.Num();++I)
        {
            const int32 A=Remaining[(I+Remaining.Num()-1)%Remaining.Num()],B=Remaining[I],C=Remaining[(I+1)%Remaining.Num()];
            if(Cross2(Outline[B]-Outline[A],Outline[C]-Outline[B])<=1.e-10) continue;
            bool Contains=false;
            for(int32 P:Remaining)
            {
                if(P==A || P==B || P==C) continue;
                const FVector2D Q=Outline[P];
                if(Cross2(Outline[B]-Outline[A],Q-Outline[A])>=-1.e-10
                    && Cross2(Outline[C]-Outline[B],Q-Outline[B])>=-1.e-10
                    && Cross2(Outline[A]-Outline[C],Q-Outline[C])>=-1.e-10) {Contains=true;break;}
            }
            if(Contains) continue;
            Out.Append({A,B,C});Remaining.RemoveAt(I);Found=true;break;
        }
        if(!Found) {Out.Reset();return false;}
    }
    if(Remaining.Num()!=3 || Cross2(Outline[Remaining[1]]-Outline[Remaining[0]],Outline[Remaining[2]]-Outline[Remaining[0]])<=1.e-10)
    {Out.Reset();return false;}
    Out.Append({Remaining[0],Remaining[1],Remaining[2]});return true;
}
inline FUnitMesh BeveledContour(const TArray<FVector2D>& Polygon)
{
    FUnitMesh Mesh;const auto Outline=RoundContour(Polygon);const int32 Count=Outline.Num();
    // Homothetic bevel profiles stay simple even at narrow star tips. A fixed
    // distance polygon offset can cross itself there and erase the whole cap.
    TArray<FVector2D> Cap;Cap.Reserve(Count);for(const auto& P:Outline) Cap.Add(P*.88);
    TArray<int32> CapIndices;
    if(!Triangulate(Cap,CapIndices)) return Mesh;
    // Six rings form rounded front/back bevels and a genuinely extruded wall.
    constexpr double Z[]={-.5,-.44,-.32,.32,.44,.5};
    constexpr double ProfileScale[]={.88,.97,1,1,.97,.88};
    Mesh.Vertices.Reserve(Count*8);
    for(int32 Ring=0;Ring<6;++Ring)
    {
        for(int32 I=0;I<Count;++I)
        {
            const FVector2D Point=Outline[I]*ProfileScale[Ring];
            FVector Normal(0,0,Ring?1:-1);
            if(Ring>0 && Ring<5)
            {
                const FVector2D Along=Outline[(I+1)%Count]-Outline[(I+Count-1)%Count];
                const double Ds=ProfileScale[Ring+1]-ProfileScale[Ring-1];
                const FVector Across(Outline[I].X*Ds,Outline[I].Y*Ds,Z[Ring+1]-Z[Ring-1]);
                Normal=FVector::CrossProduct(FVector(Along.X,Along.Y,0),Across).GetSafeNormal();
            }
            Mesh.Vertices.Add({FVector(Point.X,Point.Y,Z[Ring]),
                Normal,FVector2D(I/double(Count),Ring/5.)});
        }
    }
    for(int32 Ring=0;Ring<5;++Ring) for(int32 I=0;I<Count;++I)
    {
        const int32 J=(I+1)%Count,A=Ring*Count+I,B=Ring*Count+J,C=(Ring+1)*Count+J,D=(Ring+1)*Count+I;
        Mesh.Indices.Append({A,B,C,A,C,D});
    }
    for(int32 Side=0;Side<2;++Side)
    {
        const int32 Offset=Mesh.Vertices.Num();const double Depth=Side?.5:-.5;
        for(const auto& P:Cap) Mesh.Vertices.Add({FVector(P.X,P.Y,Depth),FVector(0,0,Side?1:-1),P*.5+FVector2D(.5,.5)});
        for(int32 I=0;I<CapIndices.Num();I+=3)
        {
            const int32 A=Offset+CapIndices[I],B=Offset+CapIndices[I+1],C=Offset+CapIndices[I+2];
            if(Side) Mesh.Indices.Append({A,B,C});else Mesh.Indices.Append({A,C,B});
        }
    }
    return Mesh;
}
inline const FUnitMesh& Plus()
{
    static const FUnitMesh Mesh=BeveledContour({FVector2D(1,.34),FVector2D(.34,.34),FVector2D(.34,1),FVector2D(-.34,1),
        FVector2D(-.34,.34),FVector2D(-1,.34),FVector2D(-1,-.34),FVector2D(-.34,-.34),FVector2D(-.34,-1),
        FVector2D(.34,-1),FVector2D(.34,-.34),FVector2D(1,-.34)});
    return Mesh;
}
inline const FUnitMesh& Star(int32 Tips)
{
    static const TArray<FUnitMesh> Meshes=[]
    {
        TArray<FUnitMesh> Result;Result.Reserve(6);
        for(int32 N=3;N<=8;++N)
        {
            TArray<FVector2D> Outline;
            for(int32 I=0;I<N*2;++I)
            {
                const double A=I*PI/N,Radius=I%2?.48:1.;
                Outline.Add(FVector2D(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius));
            }
            Result.Add(BeveledContour(Outline));
        }
        return Result;
    }();
    return Meshes[FMath::Clamp(Tips,3,8)-3];
}
inline const FUnitMesh& Sphere(int32 Sides,int32 Rings)
{
    Sides=FMath::Clamp(Sides,6,16);Rings=FMath::Clamp(Rings,3,10);
    // Bounded, game-thread cache: one generation per requested resolution.
    static TMap<int32,FUnitMesh> Cache;const int32 Key=Sides*32+Rings;
    if(const auto* Existing=Cache.Find(Key)) return *Existing;
    FUnitMesh Mesh;const int32 Stride=Sides+1;
    for(int32 Ring=0;Ring<=Rings;++Ring) for(int32 Side=0;Side<=Sides;++Side)
    {
        const double Theta=PI*Ring/Rings,Phi=2*PI*Side/Sides;
        const FVector P(FMath::Sin(Theta)*FMath::Cos(Phi),FMath::Sin(Theta)*FMath::Sin(Phi),FMath::Cos(Theta));
        Mesh.Vertices.Add({P,P,FVector2D(Side/double(Sides),Ring/double(Rings))});
    }
    for(int32 Ring=0;Ring<Rings;++Ring) for(int32 Side=0;Side<Sides;++Side)
    {
        const int32 A=Ring*Stride+Side,B=(Ring+1)*Stride+Side,C=B+1,D=A+1;
        if(Ring<Rings-1) Mesh.Indices.Append({A,B,C});
        if(Ring>0) Mesh.Indices.Append({A,C,D});
    }
    return Cache.Add(Key,MoveTemp(Mesh));
}

struct FArcVertex
{
    double T,Radial,Depth,Tangent;
};
struct FArcUnit
{
    TArray<FArcVertex> Vertices;
    TArray<int32> Indices;
};
inline FArcUnit MakeArc(bool Closed)
{
    FArcUnit Mesh;constexpr int32 Sides=10,Steps=18,CapRings=4;
    auto AddRing=[&](double T,double Angle)
    {
        for(int32 I=0;I<Sides;++I)
        {
            const double A=2*PI*I/Sides;
            Mesh.Vertices.Add({T,FMath::Cos(Angle)*FMath::Cos(A),FMath::Cos(Angle)*FMath::Sin(A),FMath::Sin(Angle)});
        }
    };
    if(!Closed) for(int32 I=0;I<CapRings;++I) AddRing(0,-PI*.5+PI*.5*I/CapRings);
    for(int32 I=0;I<(Closed?Steps:Steps+1);++I) AddRing(I/double(Steps),0);
    if(!Closed) for(int32 I=1;I<=CapRings;++I) AddRing(1,PI*.5*I/CapRings);
    const int32 Rows=Mesh.Vertices.Num()/Sides;
    for(int32 Row=0;Row<(Closed?Rows:Rows-1);++Row) for(int32 I=0;I<Sides;++I)
    {
        const int32 Next=(Row+1)%Rows,J=(I+1)%Sides,A=Row*Sides+I,B=Next*Sides+I,C=Next*Sides+J,D=Row*Sides+J;
        // Cap poles have coincident vertices: omit their zero-area triangles.
        if(Closed || Row<Rows-2) Mesh.Indices.Append({A,B,C});
        if(Closed || Row>0) Mesh.Indices.Append({A,C,D});
    }
    return Mesh;
}
inline const FArcUnit& Arc(bool Closed)
{
    static const FArcUnit Open=MakeArc(false),Ring=MakeArc(true);
    return Closed?Ring:Open;
}
} // namespace Detail

struct FGeometry
{
    TArray<FVector> Points,Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    FVector Right=FVector::RightVector,Up=FVector::UpVector,View=FVector::ForwardVector,Origin=FVector::ZeroVector;

    void RoundedPlus(FVector Center,float HalfSpan,float Depth,FLinearColor Color,float Roll=0,float Stretch=1)
    {Append(Detail::Plus(),Center,FVector(HalfSpan,HalfSpan*Stretch,Depth),Color,Roll);}
    void Star(FVector Center,float Radius,float Depth,FLinearColor Color,int32 Tips=4,float Roll=0)
    {Append(Detail::Star(Tips),Center,FVector(Radius,Radius,Depth),Color,Roll);}
    void Ball(FVector Center,FVector Scale,FLinearColor Color,int32 Sides=10,int32 Rings=6)
    {Append(Detail::Sphere(Sides,Rings),Center,Scale,Color,0);}

    void Arc(FVector Center,float Radius,float Start,float Sweep,float Width,float Depth,FLinearColor Color)
    {
        if(!Finite(Center,FVector(Radius,Width,Depth)) || !FMath::IsFinite(Start) || !FMath::IsFinite(Sweep) || FMath::Abs(Sweep)<.001f) return;
        // Keep the inner edge away from the center, where a fat torus folds.
        const double HalfWidth=FMath::Min(double(Width)*.5,double(Radius)*.85),HalfDepth=Depth*.5;
        const double SignedSweep=FMath::Clamp(double(Sweep),-2*PI,2*PI),Sign=SignedSweep<0?-1.:1.;
        const bool Closed=FMath::Abs(SignedSweep)>=2*PI-.001;
        const auto& Unit=Detail::Arc(Closed);const int32 First=Points.Num();
        const FVector R=Right.GetSafeNormal(),U=Up.GetSafeNormal(),V=View.GetSafeNormal();
        for(const auto& P:Unit.Vertices)
        {
            const double A=Start+SignedSweep*P.T;
            const FVector Radial=R*FMath::Cos(A)+U*FMath::Sin(A),Along=(-R*FMath::Sin(A)+U*FMath::Cos(A))*Sign;
            const FVector Normal=(Radial*(P.Radial/HalfWidth)+V*(P.Depth/HalfDepth)+Along*(P.Tangent/HalfWidth)).GetSafeNormal();
            const FVector Position=Center+Radial*(Radius+P.Radial*HalfWidth)+V*(P.Depth*HalfDepth)+Along*(P.Tangent*HalfWidth);
            Add(Position,Normal,FVector2D(P.T,(P.Depth+1)*.5),Color,Along,V);
        }
        AppendIndices(Unit.Indices,First,SignedSweep<0);
    }

    /** Update in place when BOTH vertex and index topology are unchanged. */
    void Upload(UProceduralMeshComponent* Target,int32 Section) const
    {
        if(!IsValid(Target) || Section<0) return;
        if(Points.IsEmpty()) {
            const auto* Existing=Target->GetProcMeshSection(Section);
            if(Existing && !Existing->ProcVertexBuffer.IsEmpty()) Target->ClearMeshSection(Section);
            return;
        }
        if(Normals.Num()!=Points.Num() || UV.Num()!=Points.Num() || Colors.Num()!=Points.Num() || Tangents.Num()!=Points.Num()) return;
        const auto* Previous=Target->GetProcMeshSection(Section);
        bool Stable=Previous && Previous->ProcVertexBuffer.Num()==Points.Num() && Previous->ProcIndexBuffer.Num()==Indices.Num();
        if(Stable) for(int32 I=0;I<Indices.Num();++I) if(Previous->ProcIndexBuffer[I]!=uint32(Indices[I])) {Stable=false;break;}
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);Target->SetCastShadow(false);
        if(Stable) Target->UpdateMeshSection_LinearColor(Section,Points,Normals,UV,Colors,Tangents,false);
        else Target->CreateMeshSection_LinearColor(Section,Points,Indices,Normals,UV,Colors,Tangents,false,false);
        // Create and Update use the SAME linear-color conversion flag. Their
        // engine defaults differ and would otherwise change the palette on tick2.
    }

private:
    static bool Finite(FVector Center,FVector Scale)
    {
        return !Center.ContainsNaN() && !Scale.ContainsNaN() && Scale.X>UE_SMALL_NUMBER && Scale.Y>UE_SMALL_NUMBER && Scale.Z>UE_SMALL_NUMBER;
    }
    void Add(FVector Position,FVector Normal,FVector2D Tex,FLinearColor Color,FVector Tangent,FVector FrameView)
    {
        const float Shade=.6f+.4f*FMath::Abs(float(FVector::DotProduct(Normal,FrameView)));
        Color.R*=Shade;Color.G*=Shade;Color.B*=Shade;Color.A=1.f;
        Tangent=(Tangent-Normal*FVector::DotProduct(Tangent,Normal)).GetSafeNormal();
        if(Tangent.IsNearlyZero()) Tangent=FVector::CrossProduct(Normal,Up).GetSafeNormal(UE_SMALL_NUMBER,Right);
        Points.Add(Position+Origin);Normals.Add(Normal);UV.Add(Tex);Colors.Add(Color);Tangents.Add(FProcMeshTangent(Tangent,false));
    }
    void AppendIndices(const TArray<int32>& Source,int32 First,bool ExtraFlip=false)
    {
        // Match GenerateBoxMesh in UE5.8 KismetProceduralMeshLibrary: supplied
        // outward normals oppose the mathematical cross of rendered triangles.
        const bool Reverse=(FVector::DotProduct(FVector::CrossProduct(Right,Up),View)>=0)!=ExtraFlip;
        for(int32 I=0;I<Source.Num();I+=3)
        {
            Indices.Add(First+Source[I]);Indices.Add(First+Source[I+(Reverse?2:1)]);Indices.Add(First+Source[I+(Reverse?1:2)]);
        }
    }
    void Append(const Detail::FUnitMesh& Unit,FVector Center,FVector Scale,FLinearColor Color,float Roll)
    {
        if(!Finite(Center,Scale) || !FMath::IsFinite(Roll) || Unit.Vertices.IsEmpty()) return;
        const int32 First=Points.Num();const double C=FMath::Cos(Roll),S=FMath::Sin(Roll);
        const FVector R=Right.GetSafeNormal(),U=Up.GetSafeNormal(),V=View.GetSafeNormal();
        for(const auto& Vertex:Unit.Vertices)
        {
            const FVector P=Vertex.Position*Scale;
            // Inverse transpose for anisotropic plus stretch/ellipsoid scale.
            const FVector N(Vertex.Normal.X/Scale.X,Vertex.Normal.Y/Scale.Y,Vertex.Normal.Z/Scale.Z);
            const FVector Normal=(R*(N.X*C-N.Y*S)+U*(N.X*S+N.Y*C)+V*N.Z).GetSafeNormal();
            const FVector Position=Center+R*(P.X*C-P.Y*S)+U*(P.X*S+P.Y*C)+V*P.Z;
            Add(Position,Normal,Vertex.UV,Color,R*C+U*S,V);
        }
        AppendIndices(Unit.Indices,First);
    }
};
} // namespace MCCartoonFX
