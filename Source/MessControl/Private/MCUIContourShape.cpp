#include "MCUIContourShape.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace MCUIContourPrivate
{
    double Cross(FVector2D A,FVector2D B,FVector2D C)
    {return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X);}

    bool Triangulate(const TArray<FVector2D>& Points,TArray<SlateIndex>& Out)
    {
        Out.Reset();if(Points.Num()<3 || int64(Points.Num())>int64(TNumericLimits<SlateIndex>::Max())) return false;
        double Area=0;
        for(int32 I=0;I<Points.Num();++I) {
            const FVector2D A=Points[I],B=Points[(I+1)%Points.Num()];Area+=A.X*B.Y-B.X*A.Y;
        }
        if(FMath::Abs(Area)<.000001) return false;
        TArray<int32> Remaining;Remaining.Reserve(Points.Num());
        for(int32 I=0;I<Points.Num();++I) Remaining.Add(Area>0?I:Points.Num()-1-I);
        while(Remaining.Num()>3) {
            bool Removed=false;
            for(int32 I=0;I<Remaining.Num();++I) {
                const int32 A=Remaining[(I+Remaining.Num()-1)%Remaining.Num()],B=Remaining[I],C=Remaining[(I+1)%Remaining.Num()];
                if(Cross(Points[A],Points[B],Points[C])<=.000001) continue;
                bool Contains=false;
                for(int32 Candidate:Remaining) {
                    if(Candidate==A || Candidate==B || Candidate==C) continue;
                    if(Cross(Points[A],Points[B],Points[Candidate])>=-.000001
                        && Cross(Points[B],Points[C],Points[Candidate])>=-.000001
                        && Cross(Points[C],Points[A],Points[Candidate])>=-.000001) {Contains=true;break;}
                }
                if(Contains) continue;
                Out.Append({SlateIndex(A),SlateIndex(B),SlateIndex(C)});Remaining.RemoveAt(I);Removed=true;break;
            }
            if(!Removed) {Out.Reset();return false;}
        }
        Out.Append({SlateIndex(Remaining[0]),SlateIndex(Remaining[1]),SlateIndex(Remaining[2])});return true;
    }

    TArray<FVector2D> MakePath(const TArray<FVector2D>& Normalized,FVector2D Size,float Outline,float Radius)
    {
        // Bounded editable contours keep both ear clipping and SlateIndex safe.
        TArray<FVector2D> Raw;if(Normalized.Num()<3 || Normalized.Num()>64 || !FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y)
            || !FMath::IsFinite(Outline) || !FMath::IsFinite(Radius) || Size.X<=0 || Size.Y<=0) return Raw;
        const double Inset=FMath::Min(double(FMath::Max(0.f,Outline))*.5,FMath::Min(Size.X,Size.Y)*.5);
        for(FVector2D Value:Normalized) {
            if(!FMath::IsFinite(Value.X) || !FMath::IsFinite(Value.Y)) return {};
            const FVector2D Point(FMath::Clamp(Value.X,0.,1.)*Size.X,FMath::Clamp(Value.Y,0.,1.)*Size.Y);
            if(Raw.IsEmpty() || !Point.Equals(Raw.Last(),.0001)) Raw.Add(Point);
        }
        if(Raw.Num()>1 && Raw[0].Equals(Raw.Last(),.0001)) Raw.Pop();
        // Remove straight intermediate points before rounding and ear clipping.
        bool Changed=true;
        while(Changed && Raw.Num()>3) {
            Changed=false;
            for(int32 I=0;I<Raw.Num();++I)
                if(FMath::Abs(Cross(Raw[(I+Raw.Num()-1)%Raw.Num()],Raw[I],Raw[(I+1)%Raw.Num()]))<.000001) {
                    Raw.RemoveAt(I);Changed=true;break;
                }
        }
        if(Raw.Num()<3) return {};
        double SignedArea=0;for(int32 I=0;I<Raw.Num();++I) SignedArea+=Cross(FVector2D::ZeroVector,Raw[I],Raw[(I+1)%Raw.Num()]);
        if(FMath::Abs(SignedArea)<.000001) return {};
        if(Inset>0) {
            TArray<FVector2D> Offset;Offset.Reserve(Raw.Num());const double Winding=SignedArea>0?1.:-1.;
            for(int32 I=0;I<Raw.Num();++I) {
                const FVector2D P=Raw[I],Before=(P-Raw[(I+Raw.Num()-1)%Raw.Num()]).GetSafeNormal(),After=(Raw[(I+1)%Raw.Num()]-P).GetSafeNormal();
                const FVector2D N0(-Before.Y*Winding,Before.X*Winding),N1(-After.Y*Winding,After.X*Winding);
                const FVector2D A=P+N0*Inset,B=P+N1*Inset;const double Denominator=Cross(FVector2D::ZeroVector,Before,After);
                FVector2D Result=A;
                if(FMath::Abs(Denominator)>1.e-8) Result=A+Before*(Cross(FVector2D::ZeroVector,B-A,After)/Denominator);
                else if(FVector2D::DotProduct(Before,After)<0) return {};
                if(!FMath::IsFinite(Result.X) || !FMath::IsFinite(Result.Y) || (Result-P).Size()>Inset*16.+.00001) return {};
                Offset.Add(Result);
            }
            double OffsetArea=0;for(int32 I=0;I<Offset.Num();++I) OffsetArea+=Cross(FVector2D::ZeroVector,Offset[I],Offset[(I+1)%Offset.Num()]);
            if(OffsetArea*SignedArea<=.000001) return {};
            Raw=MoveTemp(Offset);
        }
        const double RoundedRadius=FMath::Max(0.,double(Radius)-Inset);
        if(RoundedRadius<=.001) return Raw;
        TArray<FVector2D> Rounded;Rounded.Reserve(Raw.Num()*9);
        for(int32 I=0;I<Raw.Num();++I) {
            const FVector2D Point=Raw[I],Incoming=Point-Raw[(I+Raw.Num()-1)%Raw.Num()],Outgoing=Raw[(I+1)%Raw.Num()]-Point;
            const double Reach=FMath::Min(RoundedRadius,FMath::Min(Incoming.Size(),Outgoing.Size())*.45);
            const FVector2D Start=Point-Incoming.GetSafeNormal()*Reach,End=Point+Outgoing.GetSafeNormal()*Reach;
            for(int32 Step=0;Step<=8;++Step) {
                const double T=Step/8.;Rounded.Add(Start*((1-T)*(1-T))+Point*(2*(1-T)*T)+End*(T*T));
            }
        }
        return Rounded;
    }

    using FConvexPiece=TArray<FVector2D>;
    constexpr double ClipEpsilon=.00001;
    bool HasArea(const FConvexPiece& Polygon)
    {
        if(Polygon.Num()<3) return false;
        double Area=0;for(int32 I=1;I+1<Polygon.Num();++I) Area+=Cross(Polygon[0],Polygon[I],Polygon[I+1]);
        return FMath::Abs(Area)>.000001;
    }
    float BoundaryCoverage(FVector2D Point,const TArray<FVector2D>& Outer,float Width)
    {
        double DistanceSquared=TNumericLimits<double>::Max();
        for(int32 I=0;I<Outer.Num();++I) {
            const FVector2D A=Outer[I],Edge=Outer[(I+1)%Outer.Num()]-A;const double LengthSquared=Edge.SizeSquared();
            if(LengthSquared<=1.e-12) continue;
            const double T=FMath::Clamp(FVector2D::DotProduct(Point-A,Edge)/LengthSquared,0.,1.);
            DistanceSquared=FMath::Min(DistanceSquared,(Point-(A+Edge*T)).SizeSquared());
        }
        return FMath::Clamp(float(FMath::Sqrt(DistanceSquared)/FMath::Max(.00001f,Width)),0.f,1.f);
    }
    FConvexPiece ClipHalfPlane(const FConvexPiece& Polygon,FVector2D A,FVector2D B,bool Inside,double Bias=ClipEpsilon)
    {
        FConvexPiece Result;if(Polygon.IsEmpty()) return Result;
        const double Length=(B-A).Size();if(Length<=ClipEpsilon) return Result;
        auto Distance=[&](FVector2D P){return Cross(A,B,P)/Length+Bias;};
        auto Keep=[Inside](double D){return Inside?D>=0:D<=0;};
        auto Add=[&](FVector2D P){if(Result.IsEmpty() || !P.Equals(Result.Last(),ClipEpsilon)) Result.Add(P);};
        FVector2D Previous=Polygon.Last();double Before=Distance(Previous);bool KeptBefore=Keep(Before);
        for(FVector2D Current:Polygon) {
            const double After=Distance(Current);const bool KeptAfter=Keep(After);
            if(KeptBefore!=KeptAfter) {
                const double Divisor=Before-After;
                if(FMath::Abs(Divisor)>1.e-12) Add(Previous+(Current-Previous)*FMath::Clamp(Before/Divisor,0.,1.));
            }
            if(KeptAfter) Add(Current);
            Previous=Current;Before=After;KeptBefore=KeptAfter;
        }
        if(Result.Num()>1 && Result[0].Equals(Result.Last(),ClipEpsilon)) Result.Pop();
        return Result;
    }
    void SubtractTriangle(const FConvexPiece& Subject,const FVector2D (&Clip)[3],TArray<FConvexPiece>& Out,double Bias)
    {
        // No intersection: retain the original piece rather than splitting along unrelated internal diagonals.
        FConvexPiece Intersection=Subject;
        for(int32 Edge=0;Edge<3 && !Intersection.IsEmpty();++Edge) Intersection=ClipHalfPlane(Intersection,Clip[Edge],Clip[(Edge+1)%3],true,Bias);
        if(!HasArea(Intersection)) {Out.Add(Subject);return;}
        FConvexPiece Remaining=Subject;
        for(int32 Edge=0;Edge<3 && HasArea(Remaining);++Edge) {
            FConvexPiece Outside=ClipHalfPlane(Remaining,Clip[Edge],Clip[(Edge+1)%3],false,Bias);
            if(HasArea(Outside)) Out.Add(MoveTemp(Outside));
            Remaining=ClipHalfPlane(Remaining,Clip[Edge],Clip[(Edge+1)%3],true,Bias);
        }
    }
    bool AppendPiece(const FConvexPiece& Piece,TArray<FVector2D>& Vertices,TArray<SlateIndex>& Indices)
    {
        if(!HasArea(Piece)) return true;
        const int32 MaxVertices=int32(FMath::Min<int64>(32760,int64(TNumericLimits<SlateIndex>::Max())));
        if(Vertices.Num()+Piece.Num()>MaxVertices || Indices.Num()+3*(Piece.Num()-2)>98304) return false;
        const int32 Base=Vertices.Num();Vertices.Append(Piece);
        for(int32 I=1;I+1<Piece.Num();++I)
            if(FMath::Abs(Cross(Piece[0],Piece[I],Piece[I+1]))>.000001)
                Indices.Append({SlateIndex(Base),SlateIndex(Base+I),SlateIndex(Base+I+1)});
        return true;
    }
    bool MakeDifferenceMesh(const TArray<FVector2D>& Path,const TArray<SlateIndex>& Triangles,const TArray<FVector2D>& Outer,
        const TArray<SlateIndex>& OuterTriangles,FVector2D Offset,TArray<FVector2D>& Vertices,TArray<SlateIndex>& Indices,double Bias)
    {
        Vertices.Reset();Indices.Reset();
        auto Fail=[&](){Vertices.Reset();Indices.Reset();return false;};
        if(!FMath::IsFinite(Offset.X) || !FMath::IsFinite(Offset.Y) || OuterTriangles.IsEmpty()) return Fail();
        int32 ClipTests=0;
        for(int32 Triangle=0;Triangle+2<Triangles.Num();Triangle+=3) {
            TArray<FConvexPiece> Pieces;
            FConvexPiece First={Path[Triangles[Triangle]]+Offset,Path[Triangles[Triangle+1]]+Offset,Path[Triangles[Triangle+2]]+Offset};
            Pieces.Add(MoveTemp(First));
            for(int32 Cut=0;Cut+2<OuterTriangles.Num() && !Pieces.IsEmpty();Cut+=3) {
                const FVector2D Clip[]={Outer[OuterTriangles[Cut]],Outer[OuterTriangles[Cut+1]],Outer[OuterTriangles[Cut+2]]};
                FBox2D CutBounds(ForceInit);for(FVector2D P:Clip) CutBounds+=P;
                TArray<FConvexPiece> Next;
                for(const auto& Piece:Pieces) {
                    if(++ClipTests>1000000) return Fail();
                    FBox2D Bounds(ForceInit);for(FVector2D P:Piece) Bounds+=P;
                    if(!Bounds.Intersect(CutBounds)) Next.Add(Piece);else SubtractTriangle(Piece,Clip,Next,Bias);
                    if(Next.Num()>4096) return Fail();
                }
                Pieces=MoveTemp(Next);
            }
            for(const auto& Piece:Pieces) {
                if(!AppendPiece(Piece,Vertices,Indices)) return Fail();
            }
        }
        return true;
    }
    bool MakeIntersectionMesh(const TArray<FVector2D>& Path,const TArray<SlateIndex>& Triangles,const TArray<FVector2D>& Outer,
        const TArray<SlateIndex>& OuterTriangles,TArray<FVector2D>& Vertices,TArray<SlateIndex>& Indices)
    {
        Vertices.Reset();Indices.Reset();int32 Tests=0;
        auto Fail=[&](){Vertices.Reset();Indices.Reset();return false;};
        if(Triangles.IsEmpty() || OuterTriangles.IsEmpty()) return Fail();
        for(int32 T=0;T+2<Triangles.Num();T+=3) {
            const FConvexPiece Subject={Path[Triangles[T]],Path[Triangles[T+1]],Path[Triangles[T+2]]};
            FBox2D Bounds(ForceInit);for(FVector2D P:Subject) Bounds+=P;
            for(int32 C=0;C+2<OuterTriangles.Num();C+=3) {
                if(++Tests>1000000) return Fail();
                const FVector2D Clip[]={Outer[OuterTriangles[C]],Outer[OuterTriangles[C+1]],Outer[OuterTriangles[C+2]]};
                FBox2D CutBounds(ForceInit);for(FVector2D P:Clip) CutBounds+=P;
                if(!Bounds.Intersect(CutBounds)) continue;
                FConvexPiece Piece=Subject;
                // Zero bias keeps adjacent clip triangles disjoint; no doubled alpha along their shared diagonal.
                for(int32 Edge=0;Edge<3 && !Piece.IsEmpty();++Edge) Piece=ClipHalfPlane(Piece,Clip[Edge],Clip[(Edge+1)%3],true,0.);
                if(!AppendPiece(Piece,Vertices,Indices)) return Fail();
            }
        }
        return true;
    }
    void MakeShadowMesh(const TArray<FVector2D>& Path,const TArray<SlateIndex>& Triangles,const TArray<FVector2D>& Outer,
        FVector2D Offset,TArray<FVector2D>& Vertices,TArray<SlateIndex>& Indices)
    {
        Vertices.Reset();Indices.Reset();if(Offset.IsNearlyZero(ClipEpsilon)) return;
        TArray<SlateIndex> OuterTriangles;if(!Triangulate(Outer,OuterTriangles)) return;
        MakeDifferenceMesh(Path,Triangles,Outer,OuterTriangles,Offset,Vertices,Indices,ClipEpsilon);
    }
}

class SMCUIContourShape final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMCUIContourShape) {} SLATE_ARGUMENT(TWeakObjectPtr<UMCUIContourShape>,Owner) SLATE_END_ARGS()
    void Construct(const FArguments& Args) {Owner=Args._Owner;}
    virtual FVector2D ComputeDesiredSize(float) const override
    {return Owner.IsValid()?Owner->DesiredSize:FVector2D(100,100);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,
        int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override
    {
        const auto* Data=Owner.Get();if(!Data || Style.GetColorAndOpacityTint().A<=0) return Layer;
        const FVector2D Size=Geometry.GetLocalSize();
        const auto RenderScale=Geometry.GetAccumulatedRenderTransform().GetMatrix().GetScale().GetVector();
        const float PixelsPerUnit=FMath::Min(FMath::Abs(RenderScale.X),FMath::Abs(RenderScale.Y));
        const float AAWidth=1.f/(FMath::IsFinite(PixelsPerUnit)?FMath::Max(.001f,PixelsPerUnit):1.f);
        if(CachedNormalized!=Data->Points || !CachedSize.Equals(Size,.001) || CachedOutline!=Data->OutlineWidth || CachedRadius!=Data->CornerRadius || !CachedShadowOffset.Equals(Data->ShadowOffset,.00001) || !FMath::IsNearlyEqual(CachedAAWidth,AAWidth,.00001f)) {
            CachedNormalized=Data->Points;CachedSize=Size;CachedOutline=Data->OutlineWidth;CachedRadius=Data->CornerRadius;CachedShadowOffset=Data->ShadowOffset;CachedAAWidth=AAWidth;
            CachedPath=MCUIContourPrivate::MakePath(Data->Points,Size,Data->OutlineWidth,Data->CornerRadius);
            MCUIContourPrivate::Triangulate(CachedPath,CachedTriangles);
            const auto Outer=MCUIContourPrivate::MakePath(Data->Points,Size,0,Data->CornerRadius);
            TArray<SlateIndex> OuterTriangles;MCUIContourPrivate::Triangulate(Outer,OuterTriangles);
            const auto AABoundary=MCUIContourPrivate::MakePath(Data->Points,Size,AAWidth*2.f,Data->CornerRadius);
            TArray<SlateIndex> AATriangles;MCUIContourPrivate::Triangulate(AABoundary,AATriangles);
            CachedOutlinePath.Reset();CachedOutlineTriangles.Reset();CachedAACoverage.Reset();
            CachedFillPath=AABoundary;CachedFillTriangles=AATriangles;
            bool PartitionReady=!AATriangles.IsEmpty() && MCUIContourPrivate::MakeDifferenceMesh(Outer,OuterTriangles,AABoundary,AATriangles,FVector2D::ZeroVector,CachedAAPath,CachedAATriangles,0.);
            if(PartitionReady && Data->OutlineWidth>0) {
                const auto Inner=MCUIContourPrivate::MakePath(Data->Points,Size,Data->OutlineWidth*2.f,Data->CornerRadius);
                TArray<SlateIndex> InnerTriangles;MCUIContourPrivate::Triangulate(Inner,InnerTriangles);
                // Partition one outer silhouette: the fill cannot protrude into a
                // concave notch, and the outline never overlaps itself or the fill.
                const bool FillReady=MCUIContourPrivate::MakeIntersectionMesh(Inner,InnerTriangles,AABoundary,AATriangles,CachedFillPath,CachedFillTriangles);
                PartitionReady=FillReady && MCUIContourPrivate::MakeDifferenceMesh(AABoundary,AATriangles,Inner,InnerTriangles,FVector2D::ZeroVector,CachedOutlinePath,CachedOutlineTriangles,0.);
            }
            if(!PartitionReady) { // Bounded failure keeps a single-coverage body, never an overlapping fallback stroke.
                CachedFillPath=Outer;CachedFillTriangles=OuterTriangles;CachedOutlinePath.Reset();CachedOutlineTriangles.Reset();CachedAAPath.Reset();CachedAATriangles.Reset();
            }
            CachedAACoverage.Reserve(CachedAAPath.Num());
            for(FVector2D Point:CachedAAPath) CachedAACoverage.Add(MCUIContourPrivate::BoundaryCoverage(Point,Outer,AAWidth));
            // Remove the outer silhouette, including the outline, before alpha blending the offset shadow.
            MCUIContourPrivate::MakeShadowMesh(CachedPath,CachedTriangles,Outer,Data->ShadowOffset,CachedShadowPath,CachedShadowTriangles);
        }
        if(CachedTriangles.IsEmpty()) return Layer;
        const ESlateDrawEffect Effects=ShouldBeEnabled(bParentEnabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
        const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        const auto Transform=Geometry.GetAccumulatedRenderTransform();
        auto Polygon=[&](const TArray<FVector2D>& Path,const TArray<SlateIndex>& Triangles,FLinearColor Color,int32 PaintLayer,const TArray<float>* Coverage=nullptr)
        {
            const FColor Tint=(Color*Style.GetColorAndOpacityTint()).ToFColorSRGB();if(Tint.A==0 || Triangles.IsEmpty()) return;
            TArray<FSlateVertex> Vertices;Vertices.Reserve(Path.Num());
            for(int32 I=0;I<Path.Num();++I) {
                FColor VertexTint=Tint;
                if(Coverage && Coverage->IsValidIndex(I)) VertexTint.A=uint8(FMath::Clamp(FMath::RoundToInt(Tint.A*(*Coverage)[I]),0,255));
                Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(Path[I]),FVector2f(.5f,.5f),VertexTint));
            }
            // Uses the active Slate clipping stack, including inherited clips.
            FSlateDrawElement::MakeCustomVerts(Elements,PaintLayer,Resource,Vertices,Triangles,nullptr,0,0,Effects);
        };
        Polygon(CachedShadowPath,CachedShadowTriangles,Data->ShadowColor,Layer);
        Polygon(CachedFillPath,CachedFillTriangles,Data->FillColor,Layer+1);
        if(Data->OutlineWidth>0) Polygon(CachedOutlinePath,CachedOutlineTriangles,Data->OutlineColor,Layer+2);
        Polygon(CachedAAPath,CachedAATriangles,Data->OutlineWidth>0?Data->OutlineColor:Data->FillColor,Layer+2,&CachedAACoverage);
        return Layer+2;
    }
private:
    TWeakObjectPtr<UMCUIContourShape> Owner;
    mutable TArray<FVector2D> CachedNormalized,CachedPath,CachedFillPath,CachedOutlinePath,CachedShadowPath,CachedAAPath;
    mutable TArray<SlateIndex> CachedTriangles,CachedFillTriangles,CachedOutlineTriangles,CachedShadowTriangles,CachedAATriangles;
    mutable TArray<float> CachedAACoverage;
    mutable FVector2D CachedSize=FVector2D(-1,-1);
    mutable FVector2D CachedShadowOffset=FVector2D(MAX_dbl,MAX_dbl);
    mutable float CachedOutline=-1,CachedRadius=-1,CachedAAWidth=-1;
};

TSharedRef<SWidget> UMCUIContourShape::RebuildWidget()
{ContourSlate=SNew(SMCUIContourShape).Owner(this);return ContourSlate.ToSharedRef();}
void UMCUIContourShape::SynchronizeProperties()
{Super::SynchronizeProperties();if(ContourSlate) ContourSlate->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);}
void UMCUIContourShape::ReleaseSlateResources(bool bReleaseChildren)
{Super::ReleaseSlateResources(bReleaseChildren);ContourSlate.Reset();}
