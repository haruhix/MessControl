#include "MCWorldStatusWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
FLinearColor MCStatusShade(FLinearColor Color,float Factor)
{const float Alpha=Color.A;Color*=Factor;Color.A=Alpha;return Color;}
/** Shared vector skin: a one-pixel coverage fringe on every silhouette edge.
 * No texture, new UI material, runtime shader variant, blur pass or glow. */
struct FMCStatusVectorPaint
{
    const FGeometry& Geometry;
    FSlateWindowElementList& Elements;
    int32 Layer;
    FLinearColor Tint;
    TArray<FSlateVertex> Vertices;
    TArray<SlateIndex> Indices;

    void Polygon(const TArray<FVector2D>& Outline,FVector2D Interior,FLinearColor Top,FLinearColor Bottom,
        float TopY,float Height,float Softness=1.f,bool bOutwardOnly=false)
    {
        if(Outline.Num()<3 || (Top.A<=0.f && Bottom.A<=0.f)) return;
        // Quarter arcs share endpoints when a pill becomes a circle. Keep one
        // copy: a zero-length edge has no normal and produces a cardinal spike
        // in the coverage fringe. The closing pair needs the same treatment.
        constexpr double DuplicateDistanceSquared=.000001;
        TArray<FVector2D> Points;Points.Reserve(Outline.Num());
        for(const FVector2D& Point:Outline)
            if(Points.IsEmpty() || FVector2D::DistSquared(Point,Points.Last())>DuplicateDistanceSquared) Points.Add(Point);
        if(Points.Num()>1 && FVector2D::DistSquared(Points[0],Points.Last())<=DuplicateDistanceSquared) Points.Pop();
        if(Points.Num()<3) return;
        const auto Transform=Geometry.GetAccumulatedRenderTransform();
        const int32 First=Vertices.Num();
        auto Vertex=[&](FVector2D Position,float Coverage) {
            FLinearColor Color=FMath::Lerp(Top,Bottom,FMath::Clamp(float((Position.Y-TopY)/FMath::Max(.01f,Height)),0.f,1.f));
            Color.A*=Coverage;
            Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(Position),FVector2f(.5f,.5f),(Color*Tint).ToFColorSRGB()));
        };
        if(bOutwardOnly) {
            // A near-full pie has a concave hub. One shared miter strip folds
            // over itself when the remaining notch is narrower than its AA.
            // Independent edge quads and convex bevel joins stay well wound;
            // the two bounded coverage fringes can meet in the tiny notch.
            Vertex(Interior,1.f);
            for(const FVector2D& Point:Points) Vertex(Point,1.f);
            for(int32 I=0;I<Points.Num();++I) {
                const int32 Next=(I+1)%Points.Num();
                Indices.Append({SlateIndex(First),SlateIndex(First+1+I),SlateIndex(First+1+Next)});
                const FVector2D Direction=(Points[Next]-Points[I]).GetSafeNormal();
                const FVector2D Outward(Direction.Y,-Direction.X);
                const int32 EdgeFirst=Vertices.Num();
                Vertex(Points[I],1.f);Vertex(Points[I]+Outward*Softness,0.f);
                Vertex(Points[Next],1.f);Vertex(Points[Next]+Outward*Softness,0.f);
                Indices.Append({SlateIndex(EdgeFirst),SlateIndex(EdgeFirst+1),SlateIndex(EdgeFirst+2),
                    SlateIndex(EdgeFirst+1),SlateIndex(EdgeFirst+3),SlateIndex(EdgeFirst+2)});
                const FVector2D Before=(Points[I]-Points[(I+Points.Num()-1)%Points.Num()]).GetSafeNormal();
                if(Before.X*Direction.Y-Before.Y*Direction.X>0.) {
                    const int32 JoinFirst=Vertices.Num();
                    Vertex(Points[I],1.f);
                    Vertex(Points[I]+FVector2D(Before.Y,-Before.X)*Softness,0.f);
                    Vertex(Points[I]+Outward*Softness,0.f);
                    Indices.Append({SlateIndex(JoinFirst),SlateIndex(JoinFirst+1),SlateIndex(JoinFirst+2)});
                }
            }
            return;
        }
        Vertex(Interior,1.f);
        for(int32 I=0;I<Points.Num();++I) {
            const FVector2D Before=(Points[I]-Points[(I+Points.Num()-1)%Points.Num()]).GetSafeNormal();
            const FVector2D After=(Points[(I+1)%Points.Num()]-Points[I]).GetSafeNormal();
            const FVector2D NormalBefore(Before.Y,-Before.X),NormalAfter(After.Y,-After.X);
            FVector2D Normal=(NormalBefore+NormalAfter).GetSafeNormal();
            if(Normal.IsNearlyZero()) Normal=NormalAfter;
            const float Denominator=FMath::Max(.5f,float(FVector2D::DotProduct(Normal,NormalBefore)));
            const FVector2D Feather=Normal*(Softness*.5f/Denominator);
            Vertex(Points[I]-Feather,1.f);
            Vertex(Points[I]+Feather,0.f);
        }
        for(int32 I=0;I<Points.Num();++I) {
            const int32 A=First+1+I*2,B=First+1+(I+1)%Points.Num()*2;
            Indices.Append({SlateIndex(First),SlateIndex(A),SlateIndex(B),SlateIndex(A),SlateIndex(A+1),SlateIndex(B),
                SlateIndex(A+1),SlateIndex(B+1),SlateIndex(B)});
        }
    }

    void Pill(FVector2D Min,FVector2D Max,FLinearColor Top,FLinearColor Bottom,float Softness=1.f)
    {
        const float Width=float(Max.X-Min.X),Height=float(Max.Y-Min.Y);
        if(Width<=.05f || Height<=.05f) return;
        const float Radius=.5f*FMath::Min(Width,Height);
        const FVector2D Centers[]={Min+FVector2D(Radius,Radius),FVector2D(Max.X-Radius,Min.Y+Radius),
            Max-FVector2D(Radius,Radius),FVector2D(Min.X+Radius,Max.Y-Radius)};
        TArray<FVector2D> Outline;Outline.Reserve(68);
        for(int32 Corner=0;Corner<4;++Corner) for(int32 I=0;I<=16;++I) {
            const float Angle=UE_PI+(Corner+I/16.f)*UE_HALF_PI;
            Outline.Add(Centers[Corner]+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);
        }
        Polygon(Outline,(Min+Max)*.5,Top,Bottom,float(Min.Y),Height,Softness);
    }

    void Pie(FVector2D Center,float Radius,float Fraction,FLinearColor Top,FLinearColor Bottom)
    {
        if(Fraction<=.0001f) return;
        if(Fraction>=.9999f) {Pill(Center-FVector2D(Radius),Center+FVector2D(Radius),Top,Bottom);return;}
        const int32 Steps=FMath::Max(3,FMath::CeilToInt(Fraction*128.f));
        TArray<FVector2D> Outline;Outline.Reserve(Steps+2);Outline.Add(Center);
        for(int32 I=0;I<=Steps;++I) {
            const float Angle=-UE_HALF_PI+UE_TWO_PI*Fraction*I/Steps;
            Outline.Add(Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);
        }
        const float Middle=-UE_HALF_PI+UE_PI*Fraction;
        Polygon(Outline,Center+FVector2D(FMath::Cos(Middle),FMath::Sin(Middle))*.7,
            Top,Bottom,float(Center.Y-Radius),Radius*2.f,1.f,true);
    }

    void Flush()
    {
        if(Vertices.IsEmpty()) return;
        const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        FSlateDrawElement::MakeCustomVerts(Elements,Layer,Resource,Vertices,Indices,nullptr,0,0);
    }
};
}

class SMCStatusTimer final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMCStatusTimer) {} SLATE_ARGUMENT(TWeakObjectPtr<UMCStatusTimerWidget>,Owner) SLATE_END_ARGS()
    void Construct(const FArguments& Args) {Owner=Args._Owner;}
    virtual FVector2D ComputeDesiredSize(float) const override {return Owner.IsValid()?Owner->DesiredSize:FVector2D(30,30);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,
        int32 Layer,const FWidgetStyle& Style,bool) const override
    {
        const auto* Data=Owner.Get();if(!Data) return Layer;
        const FVector2D Center=Geometry.GetLocalSize()*.5;
        const float Radius=FMath::Min(float(Center.X),float(Center.Y))-1.5f;
        const FVector2D R(Radius);const FLinearColor Cream(.92f,.98f,.94f);
        FMCStatusVectorPaint Paint{Geometry,Elements,Layer,Style.GetColorAndOpacityTint()};
        Paint.Pill(Center-R+FVector2D(0,1.4),Center+R+FVector2D(0,1.4),FLinearColor(0,0,0,.28f),FLinearColor(0,0,0,.12f),2.2f);
        Paint.Pill(Center-R,Center+R,FMath::Lerp(Data->Rim,Cream,.12f),MCStatusShade(Data->Rim,.48f));
        const FVector2D Face(Radius-1.6f);
        Paint.Pill(Center-Face,Center+Face,MCStatusShade(Data->Surface,.60f),MCStatusShade(Data->Surface,.25f));
        const FVector2D Recess(Radius-3.f);
        Paint.Pill(Center-Recess,Center+Recess,MCStatusShade(Data->Surface,.23f),MCStatusShade(Data->Surface,.44f));
        const float SpoilBlend=FMath::Clamp(Data->SpoiledAlpha,0.f,1.f);
        const float PieOpacity=1.f-SpoilBlend;
        const float Consumed=FMath::Clamp(Data->Fraction,0.f,1.f);
        Paint.Pie(Center,Radius-3.5f,Data->bDrainTimer?1.f-Consumed:Consumed,
            FMath::Lerp(Data->Fill,Cream,.03f).CopyWithNewOpacity(PieOpacity),
            (Data->Fill*.88f).CopyWithNewOpacity(PieOpacity));
        if(SpoilBlend>.001f) {
            const float S=Radius/13.5f;
            const FVector2D Min=Center+FVector2D(-5.f,2.f)*S,Max=Center+FVector2D(5.f,8.f)*S;
            Paint.Pill(Min,Max,(Data->Fill*.7f).CopyWithNewOpacity(SpoilBlend),
                (Data->Fill*.36f).CopyWithNewOpacity(SpoilBlend));
            Paint.Pill(Min+FVector2D(1.2f,.8f)*S,Max-FVector2D(1.2f,2.8f)*S,
                FMath::Lerp(Data->Fill,Cream,.30f).CopyWithNewOpacity(SpoilBlend*.8f),
                Data->Fill.CopyWithNewOpacity(SpoilBlend*.2f));
            Paint.Pill(Center+FVector2D(-2.f,3.3f)*S,Center+FVector2D(.2f,5.5f)*S,
                Data->SpoiledTint.CopyWithNewOpacity(SpoilBlend*.8f),Data->Surface.CopyWithNewOpacity(SpoilBlend*.8f));
        }
        Paint.Flush();
        if(SpoilBlend>.001f) for(int32 W=-1;W<=1;++W) {
            const float S=Radius/13.5f;const FVector2D Start=Center+FVector2D(W*4.f,1.f)*S;
            const FVector2D P1=Start+FVector2D(2.8f,-2.4f)*S,P2=Start+FVector2D(-2.2f,-5.2f)*S;
            const FVector2D End=Start+FVector2D(.2f,-8.5f)*S;
            // Slate's adaptive spline flattens an 8px wisp into a few visible
            // straight sections. Supply the short cubic explicitly and fade
            // its ends, keeping the stroke smooth at native size and true4x.
            TArray<FVector2f> Wisp;TArray<FLinearColor> WispColors;Wisp.Reserve(25);WispColors.Reserve(25);
            for(int32 I=0;I<=24;++I) {
                const float T=I/24.f,U=1.f-T;
                Wisp.Add(FVector2f(Start*(U*U*U)+P1*(3.f*U*U*T)+P2*(3.f*U*T*T)+End*(T*T*T)));
                const float EndFade=FMath::SmoothStep(0.f,.16f,T)*(1.f-FMath::SmoothStep(.72f,1.f,T));
                WispColors.Add(Data->SpoiledTint.CopyWithNewOpacity(SpoilBlend*.86f*EndFade));
            }
            FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),MoveTemp(Wisp),MoveTemp(WispColors),
                ESlateDrawEffect::None,Style.GetColorAndOpacityTint(),true,1.05f*S);
        }
        return Layer+3;
    }
private:TWeakObjectPtr<UMCStatusTimerWidget> Owner;
};

TSharedRef<SWidget> UMCStatusTimerWidget::RebuildWidget() {return SNew(SMCStatusTimer).Owner(this);}

class SMCStatusHealth final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMCStatusHealth) {} SLATE_ARGUMENT(TWeakObjectPtr<UMCStatusHealthWidget>,Owner) SLATE_END_ARGS()
    void Construct(const FArguments& Args) {Owner=Args._Owner;}
    virtual FVector2D ComputeDesiredSize(float) const override {return FVector2D(90,11);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,
        int32 Layer,const FWidgetStyle& Style,bool) const override
    {
        const auto* Data=Owner.Get();if(!Data) return Layer;
        const FVector2D Size=Geometry.GetLocalSize(),Min(-Data->FramePadding.Left,-Data->FramePadding.Top);
        const FVector2D Max=Size+FVector2D(Data->FramePadding.Right,Data->FramePadding.Bottom);
        FMCStatusVectorPaint Paint{Geometry,Elements,Layer,Style.GetColorAndOpacityTint()};
        Paint.Pill(Min+FVector2D(0,1.5f),Max+FVector2D(0,1.5f),FLinearColor(0,0,0,.24f),FLinearColor(0,0,0,.12f),2.4f);
        Paint.Pill(Min,Max,FMath::Lerp(Data->Rim,Data->Highlight,.10f),MCStatusShade(Data->Rim,.46f));
        Paint.Pill(Min+FVector2D(1.6f),Max-FVector2D(1.6f),MCStatusShade(Data->Surface,.64f),MCStatusShade(Data->Surface,.24f));
        Paint.Pill(FVector2D(-.75f),Size+FVector2D(.75f),MCStatusShade(Data->Surface,.17f),MCStatusShade(Data->Surface,.38f));
        auto Fill=[&](float Fraction,FLinearColor Color,float Opacity) {
            const float Width=float(Size.X)*FMath::Clamp(Fraction,0.f,1.f);
            if(Width<.05f) return;
            Paint.Pill(FVector2D::ZeroVector,FVector2D(Width,Size.Y),
                FMath::Lerp(Color,Data->Highlight,.22f).CopyWithNewOpacity(Opacity),
                (Color*.76f).CopyWithNewOpacity(Opacity));
            Paint.Pill(FVector2D(1.f,.5f),FVector2D(FMath::Max(1.01f,Width-1.f),float(Size.Y)*.29f),
                Data->Highlight.CopyWithNewOpacity(Opacity*.23f),Data->Highlight.CopyWithNewOpacity(0.f),.8f);
        };
        Fill(FMath::Max(Data->HealthFraction,Data->TrailFraction),Data->Loss,.48f);
        Fill(Data->HealthFraction,Data->Fill,1.f);Paint.Flush();return Layer;
    }
private:TWeakObjectPtr<UMCStatusHealthWidget> Owner;
};

TSharedRef<SWidget> UMCStatusHealthWidget::RebuildWidget() {return SNew(SMCStatusHealth).Owner(this);}

UMCWorldStatusWidget::UMCWorldStatusWidget() {SetIsFocusable(false);}

void UMCWorldStatusWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!WidgetTree->RootWidget) {
        auto* Root=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("StatusRoot"));WidgetTree->RootWidget=Root;
        auto* Pie=WidgetTree->ConstructWidget<UMCStatusTimerWidget>(UMCStatusTimerWidget::StaticClass(),TEXT("StatusTimer"));
        auto* PieSlot=Root->AddChildToHorizontalBox(Pie);PieSlot->SetVerticalAlignment(VAlign_Center);PieSlot->SetPadding(FMargin(0,0,4,0));
        auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("StatusHealthSize"));
        Size->SetWidthOverride(98);Size->SetHeightOverride(19);Root->AddChildToHorizontalBox(Size)->SetVerticalAlignment(VAlign_Center);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("StatusHealthFrame"));Frame->SetPadding(FMargin(4));Size->SetContent(Frame);
        auto* Stack=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("StatusHealthLayers"));Frame->SetContent(Stack);
        for(FName Name:{FName(TEXT("StatusLossTrail")),FName(TEXT("StatusHealth"))}) {
            auto* Bar=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),Name);
            auto* HealthLayerSlot=Stack->AddChildToOverlay(Bar);HealthLayerSlot->SetHorizontalAlignment(HAlign_Fill);HealthLayerSlot->SetVerticalAlignment(VAlign_Fill);
        }
    }
    StatusTimer=WidgetTree->FindWidget<UMCStatusTimerWidget>(TEXT("StatusTimer"));
    StatusHealth=WidgetTree->FindWidget<UProgressBar>(TEXT("StatusHealth"));
    StatusLossTrail=WidgetTree->FindWidget<UProgressBar>(TEXT("StatusLossTrail"));
    StatusHealthPresentation=WidgetTree->FindWidget<UMCStatusHealthWidget>(TEXT("StatusHealthPresentation"));
    // Add one optional skin layer to the known native host. Existing artist
    // sizes/padding, data controls and their names are preserved.
    if(!StatusHealthPresentation && !WidgetTree->FindWidget(TEXT("StatusHealthPresentation")))
        if(auto* Layers=WidgetTree->FindWidget<UOverlay>(TEXT("StatusHealthLayers"))) {
            StatusHealthPresentation=WidgetTree->ConstructWidget<UMCStatusHealthWidget>(UMCStatusHealthWidget::StaticClass(),TEXT("StatusHealthPresentation"));
            auto* PresentationSlot=Layers->AddChildToOverlay(StatusHealthPresentation);
            PresentationSlot->SetHorizontalAlignment(HAlign_Fill);PresentationSlot->SetVerticalAlignment(VAlign_Fill);
        }
}

void UMCWorldStatusWidget::ApplyTheme()
{
    BuildPresentationTree();Super::ApplyTheme();const auto* ThemeData=GetResolvedTheme();if(!ThemeData) return;
    const auto& P=ThemeData->Palette;
    if(auto* Frame=WidgetTree->FindWidget<UBorder>(TEXT("StatusHealthFrame"))) {
        if(StatusHealthPresentation) {Frame->SetBrush(FSlateNoResource());StatusHealthPresentation->FramePadding=Frame->GetPadding();}
        else Frame->SetBrush(FSlateRoundedBoxBrush(P.Marine,.5f*19.f,P.Accent,1.f));
        Frame->SetBrushColor(FLinearColor::White);
    }
    for(UProgressBar* Bar:{StatusLossTrail.Get(),StatusHealth.Get()}) if(Bar) {
        FProgressBarStyle Style;Style.SetBackgroundImage(FSlateNoResource());Style.SetMarqueeImage(FSlateNoResource());
        if(StatusHealthPresentation) Style.SetFillImage(FSlateNoResource());
        else Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,5.f));
        Bar->SetWidgetStyle(Style);Bar->SetBarFillStyle(EProgressBarFillStyle::Scale);
        Bar->SetFillColorAndOpacity(Bar==StatusHealth.Get()?P.Mint:P.Warning.CopyWithNewOpacity(.65f));
    }
    const float MintMix=FMath::IsFinite(StatusMintAccentMix)?FMath::Clamp(StatusMintAccentMix,0.f,1.f):.42f;
    const FLinearColor ThemeMint=FMath::Lerp(P.Mint,P.Accent,MintMix);
    // Preserve the shared palette's hue/alpha and artist mix, with a local
    // LDR saturation/value lift for the compact status treatment.
    FLinearColor MintHSV=ThemeMint.LinearRGBToHSV();
    MintHSV.G=FMath::Clamp(MintHSV.G*1.35f,0.f,1.f);MintHSV.B=FMath::Clamp(MintHSV.B*1.20f,0.f,1.f);
    FLinearColor Mint=MintHSV.HSVToLinearRGB();Mint.A=ThemeMint.A;
    if(StatusHealthPresentation) {
        StatusHealthPresentation->Surface=P.Marine;StatusHealthPresentation->Rim=P.Accent;
        StatusHealthPresentation->Fill=Mint;StatusHealthPresentation->Loss=P.Warning;
        StatusHealthPresentation->Highlight=P.Cream;StatusHealthPresentation->InvalidateLayoutAndVolatility();
    }
    if(StatusTimer) {
        FLinearColor AmberHSV=P.Warning.LinearRGBToHSV();
        AmberHSV.R=FMath::Fmod(AmberHSV.R+352.f,360.f);
        AmberHSV.G=FMath::Clamp(AmberHSV.G*1.40f,0.f,1.f);AmberHSV.B=FMath::Clamp(AmberHSV.B*1.40f,0.f,1.f);
        FLinearColor Amber=AmberHSV.HSVToLinearRGB();Amber.A=P.Warning.A;
        StatusTimer->Surface=P.Marine;StatusTimer->Rim=P.Accent;StatusTimer->Fill=Amber;
        StatusTimer->SpoiledTint=Mint;StatusTimer->InvalidateLayoutAndVolatility();
    }
}

void UMCWorldStatusWidget::SetSnapshot(const FMCWorldStatusSnapshot& Snapshot)
{
    BuildPresentationTree();const float NewHealth=FMath::IsFinite(Snapshot.HealthFraction)?FMath::Clamp(Snapshot.HealthFraction,0.f,1.f):0.f;
    const float NewTimer=FMath::IsFinite(Snapshot.TimerFraction)?FMath::Clamp(Snapshot.TimerFraction,0.f,1.f):0.f;
    if(!bHasSnapshot) {
        TrailValue=TrailFrom=NewHealth;LossAge=LossSeconds;
        TimerValue=TimerFrom=NewTimer;TimerAge=TimerFollowSeconds;
        SpoilValue=SpoilFrom=Snapshot.bSpoiled?1.f:0.f;SpoilAge=SpoilTransitionSeconds;
    }
    else if(NewHealth<Current.HealthFraction-.0001f) {TrailFrom=TrailValue;LossAge=0;}
    else if(NewHealth>Current.HealthFraction) {TrailValue=TrailFrom=NewHealth;LossAge=LossSeconds;}
    if(bHasSnapshot && !FMath::IsNearlyEqual(NewTimer,Current.TimerFraction,.00001f)) {TimerFrom=TimerValue;TimerAge=0.f;}
    if(bHasSnapshot && Snapshot.bSpoiled!=Current.bSpoiled) {SpoilFrom=SpoilValue;SpoilAge=0.f;}
    Current=Snapshot;Current.HealthFraction=NewHealth;Current.TimerFraction=NewTimer;
    bHasSnapshot=true;RefreshVisuals();
}

void UMCWorldStatusWidget::SetDisplayActive(bool bActive)
{
    if(bActive==bTargetVisible) return;
    bTargetVisible=bActive;VisibilityFrom=VisibilityValue;VisibilityAge=0;
    if(bActive) SetVisibility(ESlateVisibility::HitTestInvisible);
    RefreshVisuals();
}

void UMCWorldStatusWidget::PresentAtTime(float VisualSeconds)
{
    const float T=FMath::Max(0.f,VisualSeconds);
    LossAge=T;VisibilityAge=T;TimerAge=T;SpoilAge=T;RefreshVisuals();
}

void UMCWorldStatusWidget::RefreshStatusLayout()
{
    UWidget* HealthHost=WidgetTree?WidgetTree->FindWidget(TEXT("StatusHealthSize")):nullptr;
    auto* TimerLayout=StatusTimer?Cast<UHorizontalBoxSlot>(StatusTimer->Slot):nullptr;
    if(LayoutHealthHost.Get()!=HealthHost || LayoutTimerSlot.Get()!=TimerLayout) {
        LayoutHealthHost=HealthHost;LayoutTimerSlot=TimerLayout;bTimerOnlyLayout=false;
    }
    const bool bTimerOnly=!Current.bShowHealth;
    if(bTimerOnly==bTimerOnlyLayout) return;
    if(bTimerOnly) {
        // Preserve the actual Designer tree. Collapsing the size host removes
        // its HP frame, bars, skin and width from Slate's desired-size layout.
        if(HealthHost) {HealthHostVisibility=HealthHost->GetVisibility();HealthHost->SetVisibility(ESlateVisibility::Collapsed);}
        if(TimerLayout) {
            TimerPaddingWithHealth=TimerLayout->GetPadding();
            TimerLayout->SetPadding(FMargin(0,TimerPaddingWithHealth.Top,0,TimerPaddingWithHealth.Bottom));
        }
    } else {
        // Restore the authored gap/visibility when reusing a Designer preview;
        // whole food and enemy snapshots otherwise never change their layout.
        if(HealthHost) HealthHost->SetVisibility(HealthHostVisibility);
        if(TimerLayout) TimerLayout->SetPadding(TimerPaddingWithHealth);
    }
    bTimerOnlyLayout=bTimerOnly;
}

void UMCWorldStatusWidget::RefreshVisuals()
{
    RefreshStatusLayout();
    const float LossT=FMath::Clamp((LossAge-.04f)/FMath::Max(.05f,LossSeconds-.04f),0.f,1.f);
    TrailValue=FMath::Lerp(TrailFrom,Current.HealthFraction,FMath::SmoothStep(0.f,1.f,LossT));
    if(StatusHealth && !FMath::IsNearlyEqual(StatusHealth->GetPercent(),Current.HealthFraction)) StatusHealth->SetPercent(Current.HealthFraction);
    const float TrailPercent=FMath::Max(Current.HealthFraction,TrailValue);
    if(StatusLossTrail && !FMath::IsNearlyEqual(StatusLossTrail->GetPercent(),TrailPercent)) StatusLossTrail->SetPercent(TrailPercent);
    if(StatusHealthPresentation) {
        const bool Changed=!FMath::IsNearlyEqual(StatusHealthPresentation->HealthFraction,Current.HealthFraction)
            || !FMath::IsNearlyEqual(StatusHealthPresentation->TrailFraction,TrailPercent);
        StatusHealthPresentation->HealthFraction=Current.HealthFraction;StatusHealthPresentation->TrailFraction=TrailPercent;
        if(Changed) StatusHealthPresentation->InvalidateLayoutAndVolatility();
    }
    const float TimerT=FMath::SmoothStep(0.f,1.f,FMath::Clamp(TimerAge/FMath::Max(.01f,TimerFollowSeconds),0.f,1.f));
    TimerValue=FMath::Lerp(TimerFrom,Current.TimerFraction,TimerT);
    const float SpoilT=FMath::SmoothStep(0.f,1.f,FMath::Clamp(SpoilAge/FMath::Max(.01f,SpoilTransitionSeconds),0.f,1.f));
    SpoilValue=FMath::Lerp(SpoilFrom,Current.bSpoiled?1.f:0.f,SpoilT);
    if(StatusTimer) {
        const bool Changed=!FMath::IsNearlyEqual(StatusTimer->Fraction,TimerValue) || StatusTimer->bSpoiled!=Current.bSpoiled
            || StatusTimer->bDrainTimer!=Current.bDrainTimer || !FMath::IsNearlyEqual(StatusTimer->SpoiledAlpha,SpoilValue);
        StatusTimer->Fraction=TimerValue;StatusTimer->bSpoiled=Current.bSpoiled;StatusTimer->SpoiledAlpha=SpoilValue;
        StatusTimer->bDrainTimer=Current.bDrainTimer;
        StatusTimer->SetVisibility(Current.bHasTimer?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(Changed) StatusTimer->InvalidateLayoutAndVolatility();
    }
    const float Duration=bTargetVisible?AppearSeconds:FadeSeconds;
    const float Alpha=FMath::SmoothStep(0.f,1.f,FMath::Clamp(VisibilityAge/FMath::Max(.01f,Duration),0.f,1.f));
    VisibilityValue=FMath::Lerp(VisibilityFrom,bTargetVisible?1.f:0.f,Alpha);
    SetRenderOpacity(VisibilityValue);SetRenderTransformPivot(FVector2D(.5f,.5f));SetRenderScale(FVector2D(FMath::Lerp(.94f,1.f,VisibilityValue)));
    if(!bTargetVisible && Alpha>=1.f) SetVisibility(ESlateVisibility::Collapsed);
}

void UMCWorldStatusWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt);
    const bool Animating=LossAge<LossSeconds || VisibilityAge<(bTargetVisible?AppearSeconds:FadeSeconds)
        || TimerAge<TimerFollowSeconds || SpoilAge<SpoilTransitionSeconds;
    LossAge+=Dt;VisibilityAge+=Dt;TimerAge+=Dt;SpoilAge+=Dt;if(Animating) RefreshVisuals();
}

void UMCWorldStatusWidget::ApplyEditorPreview(float Time)
{
    if(!IsDesignTime()) return;
    PreviewTime=FMath::Max(0.f,Time);BuildPresentationTree();ApplyTheme();
    bTargetVisible=false;VisibilityFrom=VisibilityValue=VisibilityAge=0;
    FMCWorldStatusSnapshot Value;Value.HealthFraction=.72f;Value.TimerFraction=.32f;
    Value.bDrainTimer=PreviewState!=TEXT("Enemy") && PreviewState!=TEXT("EnemyDamaged");
    Value.bShowHealth=PreviewState!=TEXT("FragmentFresh") && PreviewState!=TEXT("FragmentSpoiled");
    if(PreviewState==TEXT("FoodFill0") || PreviewState==TEXT("FragmentFresh")) Value.TimerFraction=0.f;
    else if(PreviewState==TEXT("FoodFill25")) Value.TimerFraction=.25f;
    else if(PreviewState==TEXT("FoodFill50")) Value.TimerFraction=.5f;
    else if(PreviewState==TEXT("FoodFill75")) Value.TimerFraction=.75f;
    else if(PreviewState==TEXT("FoodFill100") || PreviewState==TEXT("FragmentSpoiled")) Value.TimerFraction=1.f;
    else if(PreviewState==TEXT("TimerFill")) Value.TimerFraction=FMath::Clamp(PreviewTime/2.f,0.f,1.f);
    else if(PreviewState==TEXT("SpoilTransition")) Value.TimerFraction=1.f;
    Value.bHasTimer=PreviewState!=TEXT("Enemy") && PreviewState!=TEXT("EnemyDamaged");
    Value.bSpoiled=PreviewState==TEXT("FoodSpoiled") || PreviewState==TEXT("FragmentSpoiled");
    bHasSnapshot=false;SetSnapshot(Value);SetDisplayActive(true);
    if(PreviewState==TEXT("SpoilTransition")) {Value.bSpoiled=true;SetSnapshot(Value);}
    if(PreviewState==TEXT("Loss") || PreviewState==TEXT("EnemyDamaged")) {Value.HealthFraction=.39f;SetSnapshot(Value);}
    if(PreviewState==TEXT("Fade")) {VisibilityValue=1.f;SetDisplayActive(false);}
    else if(PreviewState!=TEXT("Appear")) {VisibilityFrom=1.f;VisibilityValue=1.f;}
    PresentAtTime(PreviewTime);
}
