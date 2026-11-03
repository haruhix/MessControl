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
        Out.Reset();if(Points.Num()<3) return false;
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
        TArray<FVector2D> Raw;if(Normalized.Num()<3 || Normalized.Num()>64 || Size.X<=0 || Size.Y<=0) return Raw;
        const double Inset=FMath::Min(double(FMath::Max(0.f,Outline))*.5,FMath::Min(Size.X,Size.Y)*.5);
        const FVector2D Extent(FMath::Max(0.,Size.X-Inset*2),FMath::Max(0.,Size.Y-Inset*2));
        for(FVector2D Value:Normalized) {
            if(!FMath::IsFinite(Value.X) || !FMath::IsFinite(Value.Y)) return {};
            const FVector2D Point=FVector2D(Inset)+FVector2D(FMath::Clamp(Value.X,0.,1.)*Extent.X,FMath::Clamp(Value.Y,0.,1.)*Extent.Y);
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
        if(Radius<=.001f || Raw.Num()<3) return Raw;
        TArray<FVector2D> Rounded;Rounded.Reserve(Raw.Num()*9);
        for(int32 I=0;I<Raw.Num();++I) {
            const FVector2D Point=Raw[I],Incoming=Point-Raw[(I+Raw.Num()-1)%Raw.Num()],Outgoing=Raw[(I+1)%Raw.Num()]-Point;
            const double Reach=FMath::Min(double(Radius),FMath::Min(Incoming.Size(),Outgoing.Size())*.45);
            const FVector2D Start=Point-Incoming.GetSafeNormal()*Reach,End=Point+Outgoing.GetSafeNormal()*Reach;
            for(int32 Step=0;Step<=8;++Step) {
                const double T=Step/8.;Rounded.Add(Start*((1-T)*(1-T))+Point*(2*(1-T)*T)+End*(T*T));
            }
        }
        return Rounded;
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
        if(CachedNormalized!=Data->Points || !CachedSize.Equals(Size,.001) || CachedOutline!=Data->OutlineWidth || CachedRadius!=Data->CornerRadius) {
            CachedNormalized=Data->Points;CachedSize=Size;CachedOutline=Data->OutlineWidth;CachedRadius=Data->CornerRadius;
            CachedPath=MCUIContourPrivate::MakePath(Data->Points,Size,Data->OutlineWidth,Data->CornerRadius);
            MCUIContourPrivate::Triangulate(CachedPath,CachedTriangles);
        }
        if(CachedTriangles.IsEmpty()) return Layer;
        const ESlateDrawEffect Effects=ShouldBeEnabled(bParentEnabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
        const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        const auto Transform=Geometry.GetAccumulatedRenderTransform();
        auto Polygon=[&](FVector2D Offset,FLinearColor Color,int32 PaintLayer)
        {
            const FColor Tint=(Color*Style.GetColorAndOpacityTint()).ToFColorSRGB();if(Tint.A==0) return;
            TArray<FSlateVertex> Vertices;Vertices.Reserve(CachedPath.Num());
            for(const FVector2D& Point:CachedPath)
                Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(Point+Offset),FVector2f(.5f,.5f),Tint));
            // Uses the active Slate clipping stack, including inherited clips.
            FSlateDrawElement::MakeCustomVerts(Elements,PaintLayer,Resource,Vertices,CachedTriangles,nullptr,0,0,Effects);
        };
        Polygon(Data->ShadowOffset,Data->ShadowColor,Layer);
        Polygon(FVector2D::ZeroVector,Data->FillColor,Layer+1);
        if(Data->OutlineWidth>0 && Data->OutlineColor.A>0) {
            TArray<FVector2D> Closed=CachedPath;Closed.Add(CachedPath[0]);
            FSlateDrawElement::MakeLines(Elements,Layer+2,Geometry.ToPaintGeometry(),Closed,Effects,
                Data->OutlineColor*Style.GetColorAndOpacityTint(),true,Data->OutlineWidth);
        }
        return Layer+2;
    }
private:
    TWeakObjectPtr<UMCUIContourShape> Owner;
    mutable TArray<FVector2D> CachedNormalized,CachedPath;
    mutable TArray<SlateIndex> CachedTriangles;
    mutable FVector2D CachedSize=FVector2D(-1,-1);
    mutable float CachedOutline=-1,CachedRadius=-1;
};

TSharedRef<SWidget> UMCUIContourShape::RebuildWidget()
{ContourSlate=SNew(SMCUIContourShape).Owner(this);return ContourSlate.ToSharedRef();}
void UMCUIContourShape::SynchronizeProperties()
{Super::SynchronizeProperties();if(ContourSlate) ContourSlate->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);}
void UMCUIContourShape::ReleaseSlateResources(bool bReleaseChildren)
{Super::ReleaseSlateResources(bReleaseChildren);ContourSlate.Reset();}
