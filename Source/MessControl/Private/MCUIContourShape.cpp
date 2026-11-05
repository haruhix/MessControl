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

    TArray<FVector2D> MakePath(const TArray<FVector2D>& Normalized,FVector2D Size,float Outline,float Radius)
    {
        // Editable contours have a bounded number of scanline edges.
        TArray<FVector2D> Raw;if(Normalized.Num()<3 || Normalized.Num()>64 || !FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y)
            || !FMath::IsFinite(Outline) || !FMath::IsFinite(Radius) || Size.X<=0 || Size.Y<=0) return Raw;
        const double Inset=FMath::Min(double(FMath::Max(0.f,Outline))*.5,FMath::Min(Size.X,Size.Y)*.5);
        for(FVector2D Value:Normalized) {
            if(!FMath::IsFinite(Value.X) || !FMath::IsFinite(Value.Y)) return {};
            const FVector2D Point(FMath::Clamp(Value.X,0.,1.)*Size.X,FMath::Clamp(Value.Y,0.,1.)*Size.Y);
            if(Raw.IsEmpty() || !Point.Equals(Raw.Last(),.0001)) Raw.Add(Point);
        }
        if(Raw.Num()>1 && Raw[0].Equals(Raw.Last(),.0001)) Raw.Pop();
        // Remove straight intermediate points before rounding.
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
        if(Radius<=.001 && Inset<=.001) return Raw;
        TArray<FVector2D> Rounded;Rounded.Reserve(Raw.Num()*9);
        for(int32 I=0;I<Raw.Num();++I) {
            const FVector2D Point=Raw[I],Incoming=Point-Raw[(I+Raw.Num()-1)%Raw.Num()],Outgoing=Raw[(I+1)%Raw.Num()]-Point;
            // An inset shrinks convex radii and expands concave radii. Using
            // the convex rule at a tab's notch pinches its interior outline.
            const bool Concave=Cross(FVector2D::ZeroVector,Incoming,Outgoing)*SignedArea<0;
            const double RoundedRadius=FMath::Max(0.,double(Radius)+(Concave?Inset:-Inset));
            const double Reach=FMath::Min(RoundedRadius,FMath::Min(Incoming.Size(),Outgoing.Size())*.45);
            if(Reach<=.001) {Rounded.Add(Point);continue;}
            const FVector2D Start=Point-Incoming.GetSafeNormal()*Reach,End=Point+Outgoing.GetSafeNormal()*Reach;
            for(int32 Step=0;Step<=8;++Step) {
                const double T=Step/8.;Rounded.Add(Start*((1-T)*(1-T))+Point*(2*(1-T)*T)+End*(T*T));
            }
        }
        return Rounded;
    }

    constexpr double ClipEpsilon=.00001;

    struct FRasterBatch
    {
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
    };
    using FInterval=FVector2D;
    constexpr int32 RasterSamples=8;
    constexpr int32 MaxRasterSide=8192;
    constexpr int64 MaxRasterPixels=16*1024*1024;
    constexpr int64 MaxScanTests=32*1024*1024;
    constexpr int32 MaxRasterRuns=262144;

    TArray<FVector2D> ScreenPath(const TArray<FVector2D>& Local,const FSlateRenderTransform& Transform,FVector2D Offset=FVector2D::ZeroVector)
    {
        TArray<FVector2D> Result;Result.Reserve(Local.Num());
        for(FVector2D Point:Local) {
            const FVector2f P=TransformPoint(Transform,FVector2f(Point+Offset));
            if(!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y)) return {};
            Result.Add(FVector2D(P));
        }
        return Result;
    }
    void ScanIntervals(const TArray<FVector2D>& Path,double Y,TArray<FInterval>& Out,TArray<double>& Crossings)
    {
        Out.Reset();Crossings.Reset();
        for(int32 I=0;I<Path.Num();++I) {
            const FVector2D A=Path[I],B=Path[(I+1)%Path.Num()];
            // Half-open edges count a shared vertex once, for either winding.
            if((A.Y<=Y && B.Y>Y) || (B.Y<=Y && A.Y>Y))
                Crossings.Add(A.X+(Y-A.Y)*(B.X-A.X)/(B.Y-A.Y));
        }
        Crossings.Sort();
        for(int32 I=0;I+1<Crossings.Num();I+=2)
            if(Crossings[I+1]>Crossings[I]) Out.Add({Crossings[I],Crossings[I+1]});
    }
    FVector4d Premultiplied(FColor Color)
    {
        const double Alpha=Color.A/255.;
        // Slate's ordinary Windows SDR path gamma-encodes before source-over.
        // Keep existing flat FColor bytes and average in that blend domain.
        return FVector4d(Color.R/255.*Alpha,Color.G/255.*Alpha,Color.B/255.*Alpha,Alpha);
    }
    FColor StraightColor(FVector4d Value)
    {
        const double Alpha=FMath::Clamp(Value.W,0.,1.);
        const auto Byte=[](double V){return uint8(FMath::Clamp(FMath::RoundToInt(V*255.),0,255));};
        if(Alpha<.5/255.) return FColor(0,0,0,0);
        return FColor(Byte(Value.X/Alpha),Byte(Value.Y/Alpha),Byte(Value.Z/Alpha),Byte(Alpha));
    }
    void AddSpan(double Left,double Right,int32 MinX,int32 Width,const FVector4d& Color,TArray<FVector4d>& Differences)
    {
        Left=FMath::Clamp(Left-MinX,0.,double(Width));Right=FMath::Clamp(Right-MinX,0.,double(Width));
        if(Right<=Left || Color.W<=0) return;
        const int32 First=FMath::FloorToInt(Left),Last=FMath::CeilToInt(Right)-1;
        auto AddRange=[&](int32 Begin,int32 End,double Coverage) {
            if(Begin>=End || Coverage<=0) return;
            const FVector4d Value=Color*(Coverage/RasterSamples);
            Differences[Begin]+=Value;Differences[End]-=Value;
        };
        if(First==Last) AddRange(First,First+1,Right-Left);
        else {
            AddRange(First,First+1,First+1.-Left);
            AddRange(First+1,Last,1.);
            AddRange(Last,Last+1,Right-Last);
        }
    }
    void AddPartition(const TArray<FInterval>& Subject,const TArray<FInterval>& Cut,bool Intersection,
        int32 MinX,int32 Width,const FVector4d& Color,TArray<FVector4d>& Differences)
    {
        if(Color.W<=0) return;
        for(FInterval S:Subject) {
            double Cursor=S.X;
            for(FInterval C:Cut) {
                if(C.Y<=Cursor) continue;if(C.X>=S.Y) break;
                if(Intersection) AddSpan(FMath::Max(S.X,C.X),FMath::Min(S.Y,C.Y),MinX,Width,Color,Differences);
                else if(C.X>Cursor) AddSpan(Cursor,FMath::Min(S.Y,C.X),MinX,Width,Color,Differences);
                Cursor=FMath::Max(Cursor,C.Y);if(Cursor>=S.Y) break;
            }
            if(!Intersection && Cursor<S.Y) AddSpan(Cursor,S.Y,MinX,Width,Color,Differences);
        }
    }
    bool AddRun(int32 Left,int32 Right,int32 Y,FColor Color,int32& Runs,TArray<FRasterBatch>& Batches)
    {
        if(Color.A==0 || Right<=Left) return true;
        if(++Runs>MaxRasterRuns) return false;
        constexpr int32 MaxVertices=32760;
        if(Batches.IsEmpty() || Batches.Last().Vertices.Num()+4>MaxVertices) Batches.AddDefaulted();
        auto& Batch=Batches.Last();const int32 Base=Batch.Vertices.Num();
        const FSlateRenderTransform Identity;
        for(FVector2f P:{FVector2f(float(Left),float(Y)),FVector2f(float(Right),float(Y)),
            FVector2f(float(Right),float(Y+1)),FVector2f(float(Left),float(Y+1))})
            Batch.Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Identity,P,FVector2f(.5f,.5f),Color));
        Batch.Indices.Append({SlateIndex(Base),SlateIndex(Base+1),SlateIndex(Base+2),SlateIndex(Base),SlateIndex(Base+2),SlateIndex(Base+3)});
        return true;
    }
    bool Rasterize(const TArray<FVector2D>& Outer,const TArray<FVector2D>& Inner,const TArray<FVector2D>& Shadow,
        bool HasOutline,const FSlateRect& Clip,FColor FillColor,FColor OutlineColor,FColor ShadowColor,TArray<FRasterBatch>& Batches)
    {
        Batches.Reset();if(Outer.Num()<3) return false;
        FBox2D Bounds(ForceInit);for(FVector2D P:Outer) Bounds+=P;for(FVector2D P:Shadow) Bounds+=P;
        const double Left=FMath::Max(Bounds.Min.X,double(Clip.Left)),Right=FMath::Min(Bounds.Max.X,double(Clip.Right));
        const double Top=FMath::Max(Bounds.Min.Y,double(Clip.Top)),Bottom=FMath::Min(Bounds.Max.Y,double(Clip.Bottom));
        if(!FMath::IsFinite(Left) || !FMath::IsFinite(Right) || !FMath::IsFinite(Top) || !FMath::IsFinite(Bottom)
            || FMath::Abs(Left)>1.e7 || FMath::Abs(Right)>1.e7 || FMath::Abs(Top)>1.e7 || FMath::Abs(Bottom)>1.e7) return false;
        if(Right<=Left || Bottom<=Top) return true;
        const int32 MinX=FMath::FloorToInt(Left),MaxX=FMath::CeilToInt(Right),MinY=FMath::FloorToInt(Top),MaxY=FMath::CeilToInt(Bottom);
        const int32 Width=MaxX-MinX,Height=MaxY-MinY;
        if(Width>MaxRasterSide || Height>MaxRasterSide || int64(Width)*Height>MaxRasterPixels
            || int64(Height)*RasterSamples*(Outer.Num()+Inner.Num()+Shadow.Num())>MaxScanTests) return false;
        const FVector4d Fill=Premultiplied(FillColor),Outline=Premultiplied(OutlineColor),ShadowTint=Premultiplied(ShadowColor);
        TArray<FVector4d> Differences;
        TArray<FInterval> BodySpans,InnerSpans,ShadowSpans;
        TArray<double> Crossings;int32 Runs=0;int64 PartitionTests=0;
        for(int32 Y=MinY;Y<MaxY;++Y) {
            Differences.Init(FVector4d(0,0,0,0),Width+1);
            for(int32 Sample=0;Sample<RasterSamples;++Sample) {
                const double ScanY=Y+(Sample+.5)/RasterSamples;
                ScanIntervals(Outer,ScanY,BodySpans,Crossings);ScanIntervals(Inner,ScanY,InnerSpans,Crossings);ScanIntervals(Shadow,ScanY,ShadowSpans,Crossings);
                PartitionTests+=(HasOutline?2ll*BodySpans.Num()*InnerSpans.Num():0ll)+int64(ShadowSpans.Num())*BodySpans.Num();
                if(PartitionTests>MaxScanTests) {Batches.Reset();return false;}
                // Samples belong to exactly one region. Fractional boundary
                // pixels retain both sides' coverage without layering alpha.
                if(HasOutline) {
                    AddPartition(BodySpans,InnerSpans,true,MinX,Width,Fill,Differences);
                    AddPartition(BodySpans,InnerSpans,false,MinX,Width,Outline,Differences);
                } else for(FInterval Span:BodySpans) AddSpan(Span.X,Span.Y,MinX,Width,Fill,Differences);
                AddPartition(ShadowSpans,BodySpans,false,MinX,Width,ShadowTint,Differences);
            }
            FVector4d Sum(0,0,0,0);FColor RunColor(0,0,0,0);int32 Start=0;
            for(int32 X=0;X<=Width;++X) {
                FColor Color(0,0,0,0);if(X<Width) {Sum+=Differences[X];Color=StraightColor(Sum);}
                if(Color==RunColor) continue;
                if(!AddRun(MinX+Start,MinX+X,Y,RunColor,Runs,Batches)) {Batches.Reset();return false;}
                Start=X;RunColor=Color;
            }
        }
        return true;
    }
}

class SMCUIContourShape final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMCUIContourShape) {} SLATE_ARGUMENT(TWeakObjectPtr<UMCUIContourShape>,Owner) SLATE_END_ARGS()
    void Construct(const FArguments& Args) {Owner=Args._Owner;}
    virtual FVector2D ComputeDesiredSize(float) const override
    {return Owner.IsValid()?Owner->DesiredSize:FVector2D(100,100);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,
        int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override
    {
        const auto* Data=Owner.Get();if(!Data || Style.GetColorAndOpacityTint().A<=0) return Layer;
        const FVector2D Size=Geometry.GetLocalSize();
        const auto Transform=Geometry.GetAccumulatedRenderTransform();
        const FColor Fill=(Data->FillColor*Style.GetColorAndOpacityTint()).ToFColorSRGB();
        const FColor Outline=(Data->OutlineColor*Style.GetColorAndOpacityTint()).ToFColorSRGB();
        const FColor Shadow=(Data->ShadowColor*Style.GetColorAndOpacityTint()).ToFColorSRGB();
        if(!bCached || CachedNormalized!=Data->Points || CachedSize!=Size || CachedOutline!=Data->OutlineWidth
            || CachedRadius!=Data->CornerRadius || CachedShadowOffset!=Data->ShadowOffset || CachedTransform!=Transform
            || CachedClip!=CullingRect || CachedFill!=Fill || CachedEdge!=Outline || CachedShadow!=Shadow) {
            bCached=true;CachedNormalized=Data->Points;CachedSize=Size;CachedOutline=Data->OutlineWidth;CachedRadius=Data->CornerRadius;
            CachedShadowOffset=Data->ShadowOffset;CachedTransform=Transform;CachedClip=CullingRect;CachedFill=Fill;CachedEdge=Outline;CachedShadow=Shadow;
            const auto Outer=MCUIContourPrivate::ScreenPath(MCUIContourPrivate::MakePath(Data->Points,Size,0,Data->CornerRadius),Transform);
            const auto Inner=Data->OutlineWidth>0?
                MCUIContourPrivate::ScreenPath(MCUIContourPrivate::MakePath(Data->Points,Size,Data->OutlineWidth*2.f,Data->CornerRadius),Transform):TArray<FVector2D>();
            const auto ShadowPath=Shadow.A>0 && !Data->ShadowOffset.IsNearlyZero(MCUIContourPrivate::ClipEpsilon)?
                MCUIContourPrivate::ScreenPath(MCUIContourPrivate::MakePath(Data->Points,Size,Data->OutlineWidth,Data->CornerRadius),Transform,Data->ShadowOffset):TArray<FVector2D>();
            MCUIContourPrivate::Rasterize(Outer,Inner,ShadowPath,Data->OutlineWidth>0,CullingRect,Fill,Outline,Shadow,CachedBatches);
        }
        if(CachedBatches.IsEmpty()) return Layer;
        const ESlateDrawEffect Effects=ShouldBeEnabled(bParentEnabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
        const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        // Integer window-space quads tile without overlaps/T-junction cracks.
        // MakeCustomVerts retains the inherited Slate scissor/stencil stack.
        for(const auto& Batch:CachedBatches)
            FSlateDrawElement::MakeCustomVerts(Elements,Layer+2,Resource,Batch.Vertices,Batch.Indices,nullptr,0,0,Effects);
        return Layer+2;
    }
private:
    TWeakObjectPtr<UMCUIContourShape> Owner;
    mutable TArray<FVector2D> CachedNormalized;
    mutable TArray<MCUIContourPrivate::FRasterBatch> CachedBatches;
    mutable FSlateRenderTransform CachedTransform;
    mutable FSlateRect CachedClip;
    mutable FColor CachedFill,CachedEdge,CachedShadow;
    mutable FVector2D CachedSize=FVector2D(-1,-1);
    mutable FVector2D CachedShadowOffset=FVector2D(MAX_dbl,MAX_dbl);
    mutable float CachedOutline=-1,CachedRadius=-1;
    mutable bool bCached=false;
};

TSharedRef<SWidget> UMCUIContourShape::RebuildWidget()
{ContourSlate=SNew(SMCUIContourShape).Owner(this);return ContourSlate.ToSharedRef();}
void UMCUIContourShape::SynchronizeProperties()
{Super::SynchronizeProperties();if(ContourSlate) ContourSlate->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);}
void UMCUIContourShape::ReleaseSlateResources(bool bReleaseChildren)
{Super::ReleaseSlateResources(bReleaseChildren);ContourSlate.Reset();}
