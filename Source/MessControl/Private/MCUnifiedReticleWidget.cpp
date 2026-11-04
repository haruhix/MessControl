#include "MCUnifiedReticleWidget.h"

#include "MCGameplayHUD.h"
#include "MCGameplayUIFactory.h"
#include "MCUIContourShape.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "CoreGlobals.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "MCUnifiedReticle"

namespace MCUnifiedReticlePrivate
{
    constexpr int32 LabelCapacity=4,BoxCapacity=4,MaximumStripSegments=30;
    FName Indexed(const TCHAR* Prefix,int32 Index) {return FName(*FString::Printf(TEXT("%s%d"),Prefix,Index));}
    bool Finite(FVector2D Point) {return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y);}
    struct FContourPose {TArray<FVector2D> Points;FVector2D Position,Size;FLinearColor Color;int32 Layer=0;};
    struct FBoxPose {FVector2D Position,Size;float Radius=0;};
    struct FLabelPose {FVector2D Position;float Width=180;};
    FVector2D JoinOffset(const TArray<FVector2D>& Points,int32 Index,double HalfWidth)
    {
        const bool Closed=Points.Num()>2 && Points[0].Equals(Points.Last(),.0001);
        const int32 Before=Index>0?Index-1:Closed?Points.Num()-2:0;
        const int32 After=Index+1<Points.Num()?Index+1:Closed?1:Index;
        FVector2D Incoming=(Points[Index]-Points[Before]).GetSafeNormal(),Outgoing=(Points[After]-Points[Index]).GetSafeNormal();
        if(Incoming.IsNearlyZero()) Incoming=Outgoing;if(Outgoing.IsNearlyZero()) Outgoing=Incoming;
        const FVector2D FirstNormal(-Incoming.Y,Incoming.X),SecondNormal(-Outgoing.Y,Outgoing.X);
        FVector2D Bisector=(FirstNormal+SecondNormal).GetSafeNormal();if(Bisector.IsNearlyZero()) Bisector=SecondNormal;
        const double Denominator=FVector2D::DotProduct(Bisector,SecondNormal);
        return Bisector*(HalfWidth/FMath::Max(.25,FMath::Abs(Denominator)));
    }
    bool MakeStrip(const FMCUIReticleStroke& Stroke,int32 First,int32 SegmentCount,const FGeometry& SourcePaint,const FGeometry& DestinationPaint,bool Designer,FContourPose& Out)
    {
        TArray<FVector2D> Polygon;Polygon.Reserve((SegmentCount+1)*2);
        auto Convert=[&](FVector2D Point) {return Designer?Point:DestinationPaint.AbsoluteToLocal(SourcePaint.LocalToAbsolute(Point));};
        for(int32 Offset=0;Offset<=SegmentCount;++Offset) {
            const int32 Index=First+Offset;Polygon.Add(Convert(Stroke.Points[Index]+JoinOffset(Stroke.Points,Index,Stroke.Width*.5)));
        }
        for(int32 Offset=SegmentCount;Offset>=0;--Offset) {
            const int32 Index=First+Offset;Polygon.Add(Convert(Stroke.Points[Index]-JoinOffset(Stroke.Points,Index,Stroke.Width*.5)));
        }
        FVector2D Minimum(TNumericLimits<double>::Max()),Maximum(TNumericLimits<double>::Lowest());
        for(FVector2D Point:Polygon) {
            if(!Finite(Point)) return false;Minimum.X=FMath::Min(Minimum.X,Point.X);Minimum.Y=FMath::Min(Minimum.Y,Point.Y);
            Maximum.X=FMath::Max(Maximum.X,Point.X);Maximum.Y=FMath::Max(Maximum.Y,Point.Y);
        }
        const FVector2D Extent=Maximum-Minimum;if(Extent.X<=.001 || Extent.Y<=.001 || Polygon.Num()>64) return false;
        Out.Position=Minimum;Out.Size=Extent;Out.Color=Stroke.Color;Out.Layer=Stroke.Layer;
        for(FVector2D Point:Polygon) Out.Points.Add((Point-Minimum)/Extent);return true;
    }
}
UMCUnifiedReticleWidget::UMCUnifiedReticleWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;PreviewUIVersion=EMCUIVersion::New;
    SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UMCUnifiedReticleWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    auto* Canvas=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Canvas && WidgetTree->RootWidget) return;
    if(!Canvas) {Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("ReticleShapeCanvas"));WidgetTree->RootWidget=Canvas;Canvas->SetVisibility(ESlateVisibility::Collapsed);}
    ContourViews.Reset();BoxViews.Reset();LabelViews.Reset();
    for(int32 Index=0;Index<FMath::Clamp(PreparedContourCount,32,512);++Index) {
        const FName Name=MCUnifiedReticlePrivate::Indexed(TEXT("ReticleStrokeShape"),Index);
        auto* Shape=WidgetTree->FindWidget<UMCUIContourShape>(Name);
        if(!Shape) {
            if(WidgetTree->FindWidget(Name)) continue;
            Shape=WidgetTree->ConstructWidget<UMCUIContourShape>(UMCUIContourShape::StaticClass(),Name);
            Shape->OutlineWidth=0;Shape->CornerRadius=0;Shape->ShadowColor=FLinearColor::Transparent;Shape->ShadowOffset=FVector2D::ZeroVector;
            Canvas->AddChildToCanvas(Shape);Shape->SetVisibility(ESlateVisibility::Collapsed);
        }
        ContourViews.Add(Shape);
    }
    for(int32 Index=0;Index<MCUnifiedReticlePrivate::BoxCapacity;++Index) {
        const FName Name=MCUnifiedReticlePrivate::Indexed(TEXT("ReticleDotShape"),Index);auto* Box=WidgetTree->FindWidget<UBorder>(Name);
        if(!Box) {if(WidgetTree->FindWidget(Name)) continue;Box=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),Name);Box->SetPadding(FMargin(0));Canvas->AddChildToCanvas(Box);Box->SetVisibility(ESlateVisibility::Collapsed);}
        BoxViews.Add(Box);
    }
    for(int32 Index=0;Index<MCUnifiedReticlePrivate::LabelCapacity;++Index) {
        const FName Name=MCUnifiedReticlePrivate::Indexed(TEXT("ReticleSourceLabel"),Index);auto* Label=WidgetTree->FindWidget<UTextBlock>(Name);
        if(!Label) {
            if(WidgetTree->FindWidget(Name)) continue;Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
            Label->SetAutoWrapText(true);Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
            Canvas->AddChildToCanvas(Label);Label->SetVisibility(ESlateVisibility::Collapsed);
        }
        LabelViews.Add(Label);
    }
}
void UMCUnifiedReticleWidget::PreparePresentation()
{
    const FString Culture=IsDesignTime()?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();
    UDataTable* CopyTable=Backend.IsValid()?Backend->GetResolvedTranslationTable():GetResolvedTranslationTable();
    const float FontScale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    if(bPreparing) return;
    const FText FoodCopy=UMCUIDataLibrary::ResolveText(CopyTable,TEXT("UI.HUD.FoodThrow"),LOCTEXT("FoodThrow","Q · БРОСИТЬ"),IsDesignTime()?PreviewCulture:FString());
    if(bPrepared && PreparedCopyTable.Get()==CopyTable && PreparedCulture==Culture && PreparedFoodCopy.EqualTo(FoodCopy) && FMath::IsNearlyEqual(FontScale,PreparedFontScale,.001f)) return;
    bPrepared=false;TGuardValue<bool> Guard(bPreparing,true);BuildPresentationTree();
    if(!WidgetTree || !GetResolvedTheme() || !FSlateApplication::IsInitialized()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    const auto* FontTheme=Backend.IsValid()?Backend->GetResolvedTheme():GetResolvedTheme();
    const FSlateFontInfo SourceFont=FontTheme->MakeGameplayFont(12);
    bool Ready=SourceFont.HasValidFont() && ContourViews.Num()==FMath::Clamp(PreparedContourCount,32,512)
        && BoxViews.Num()==MCUnifiedReticlePrivate::BoxCapacity && LabelViews.Num()==MCUnifiedReticlePrivate::LabelCapacity;
    for(UTextBlock* Label:LabelViews) {Label->SetFont(SourceFont);Renderer->GetFontMeasureService()->Measure(FoodCopy,Label->GetFont(),FontScale);}
    // Existing CoreStyle white brush is the only contour/rounded-brush resource. No material/MID/texture is created.
    Renderer->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    for(UMCUIContourShape* Shape:ContourViews) Shape->TakeWidget();
    PreparedCopyTable=CopyTable;PreparedFoodCopy=FoodCopy;PreparedCulture=Culture;PreparedFontScale=FontScale;bPrepared=Ready;
}
bool UMCUnifiedReticleWidget::CanPresentInViewport() const
{
    if(IsDesignTime() || !GetWorld()) return false;
    const UWidget* Node=this;bool FoundViewport=false;
    for(int32 Guard=0;Node && Guard<64;++Guard) {
        if(!Node->IsVisible() || Node->GetRenderOpacity()<=.001f) return false;
        if(const auto* User=Cast<UUserWidget>(Node)) FoundViewport|=User->IsInViewport();
        if(const auto* Parent=Node->GetParent()) {Node=Parent;continue;}
        const auto* Tree=Node->GetTypedOuter<UWidgetTree>();const auto* Owner=Tree?Cast<UUserWidget>(Tree->GetOuter()):nullptr;
        if(Owner && Owner!=Node) {Node=Owner;continue;}
        return FoundViewport;
    }
    return false;
}
void UMCUnifiedReticleWidget::HideReticleCanvas()
{
    if(WidgetTree && WidgetTree->RootWidget) WidgetTree->RootWidget->SetVisibility(ESlateVisibility::Collapsed);
    PresentedSource.Reset();PresentedFrame=MAX_uint64;
}
void UMCUnifiedReticleWidget::ClearPresentationFor(const UUserWidget* Source)
{
    if(!Source || PresentedSource.Get()==Source) HideReticleCanvas();
}
bool UMCUnifiedReticleWidget::HasPresentedSourceFrame(const UUserWidget* Source,uint64 SourceFrame) const
{
    return bPrepared && Source && PresentedSource.Get()==Source && PresentedFrame==SourceFrame && CanPresentInViewport()
        && WidgetTree && WidgetTree->RootWidget && WidgetTree->RootWidget->IsVisible();
}
bool UMCUnifiedReticleWidget::ConsumePresentation(const FMCUIReticlePresentation& Snapshot,const FGeometry& SourcePaintGeometry)
{
    if(IsDesignTime() || !bPrepared || !Snapshot.Source.IsValid() || Snapshot.Source->GetWorld()!=GetWorld()
        || Snapshot.Source->GetOwningPlayer()!=GetOwningPlayer() || Snapshot.SourceFrame!=GFrameCounter || !Snapshot.bSourceVisible
        || !Snapshot.bPaintGeometryCompatible || !CanPresentInViewport()) {HideReticleCanvas();return false;}
    if(!PresentGeometry(Snapshot,SourcePaintGeometry,false)) {HideReticleCanvas();return false;}
    PresentedSource=Snapshot.Source;PresentedFrame=Snapshot.SourceFrame;return true;
}
bool UMCUnifiedReticleWidget::PresentGeometry(const FMCUIReticlePresentation& Snapshot,const FGeometry& SourcePaintGeometry,bool Designer)
{
    if(!bPrepared || !WidgetTree || Snapshot.Boxes.Num()>BoxViews.Num() || Snapshot.Labels.Num()>LabelViews.Num()) return false;
    const FGeometry DestinationPaint=GetPaintSpaceGeometry();
    if(!Designer && (DestinationPaint.GetLocalSize().X<=1 || DestinationPaint.GetLocalSize().Y<=1
        || !SourcePaintGeometry.GetLocalSize().Equals(Snapshot.LocalSize,.5))) return false;
    auto Convert=[&](FVector2D Point) {return Designer?Point:DestinationPaint.AbsoluteToLocal(SourcePaintGeometry.LocalToAbsolute(Point));};
    TArray<MCUnifiedReticlePrivate::FContourPose> Poses;
    for(const auto& Stroke:Snapshot.Strokes) {
        if(Stroke.Points.Num()<2 || Stroke.Width<=0 || !FMath::IsFinite(Stroke.Width)) return false;
        for(FVector2D Point:Stroke.Points) if(!MCUnifiedReticlePrivate::Finite(Point)) return false;
        for(int32 First=0;First<Stroke.Points.Num()-1;First+=MCUnifiedReticlePrivate::MaximumStripSegments) {
            if(Poses.Num()>=ContourViews.Num()) return false;
            MCUnifiedReticlePrivate::FContourPose Pose;
            const int32 Segments=FMath::Min(MCUnifiedReticlePrivate::MaximumStripSegments,Stroke.Points.Num()-1-First);
            if(!MCUnifiedReticlePrivate::MakeStrip(Stroke,First,Segments,SourcePaintGeometry,DestinationPaint,Designer,Pose)) return false;
            Poses.Add(MoveTemp(Pose));
        }
    }
    TArray<MCUnifiedReticlePrivate::FBoxPose> BoxPoses;
    for(const auto& Box:Snapshot.Boxes) {
        MCUnifiedReticlePrivate::FBoxPose Pose;Pose.Position=Convert(Box.Position);const FVector2D End=Convert(Box.Position+Box.Size);Pose.Size=End-Pose.Position;
        if(!MCUnifiedReticlePrivate::Finite(Pose.Position) || !MCUnifiedReticlePrivate::Finite(Pose.Size) || Pose.Size.X<=0 || Pose.Size.Y<=0) return false;
        Pose.Radius=Box.CornerRadius*float(Pose.Size.X/FMath::Max(.001,Box.Size.X));BoxPoses.Add(Pose);
    }
    TArray<MCUnifiedReticlePrivate::FLabelPose> LabelPoses;
    const FVector2D Area=Designer?Snapshot.LocalSize:DestinationPaint.GetLocalSize();
    for(const auto& Label:Snapshot.Labels) {
        MCUnifiedReticlePrivate::FLabelPose Pose;Pose.Position=Convert(Label.Position);
        if(!MCUnifiedReticlePrivate::Finite(Pose.Position) || !Label.Font.HasValidFont()) return false;
        // Fixed authored font; extend/wrap into the available column rather than crop, ellipsize or shrink.
        Pose.Width=FMath::Max(1.f,FMath::Min(MaximumLabelWidth,float(Area.X-Pose.Position.X-8)));LabelPoses.Add(Pose);
    }
    for(int32 Index=0;Index<ContourViews.Num();++Index) {
        auto* Shape=ContourViews[Index].Get();if(!Poses.IsValidIndex(Index)) {Shape->SetVisibility(ESlateVisibility::Collapsed);continue;}
        const auto& Pose=Poses[Index];auto* Cell=Cast<UCanvasPanelSlot>(Shape->Slot);if(!Cell) return false;
        Shape->Points=Pose.Points;Shape->FillColor=Pose.Color;Shape->DesiredSize=Pose.Size;Shape->SynchronizeProperties();
        Cell->SetAnchors(FAnchors(0,0));Cell->SetAlignment(FVector2D::ZeroVector);Cell->SetPosition(Pose.Position);Cell->SetSize(Pose.Size);Cell->SetAutoSize(false);Cell->SetZOrder(Pose.Layer);
        Shape->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    for(int32 Index=0;Index<BoxViews.Num();++Index) {
        auto* Box=BoxViews[Index].Get();if(!Snapshot.Boxes.IsValidIndex(Index)) {Box->SetVisibility(ESlateVisibility::Collapsed);continue;}
        const auto& Data=Snapshot.Boxes[Index];const auto& Pose=BoxPoses[Index];auto* Cell=Cast<UCanvasPanelSlot>(Box->Slot);if(!Cell) return false;
        Box->SetBrush(FSlateRoundedBoxBrush(Data.Color,Pose.Radius));Box->SetBrushColor(FLinearColor::White);
        Cell->SetPosition(Pose.Position);Cell->SetSize(Pose.Size);Cell->SetAutoSize(false);Cell->SetZOrder(Data.Layer);Box->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    for(int32 Index=0;Index<LabelViews.Num();++Index) {
        auto* Label=LabelViews[Index].Get();if(!Snapshot.Labels.IsValidIndex(Index)) {Label->SetVisibility(ESlateVisibility::Collapsed);continue;}
        const auto& Data=Snapshot.Labels[Index];const auto& Pose=LabelPoses[Index];auto* Cell=Cast<UCanvasPanelSlot>(Label->Slot);if(!Cell) return false;
        Label->SetFont(Data.Font);Label->SetText(Data.Text);Label->SetColorAndOpacity(Data.Color);Label->SetWrapTextAt(Pose.Width);
        Cell->SetPosition(Pose.Position);Cell->SetAutoSize(true);Cell->SetZOrder(Data.Layer);Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    WidgetTree->RootWidget->SetVisibility(ESlateVisibility::HitTestInvisible);return true;
}
void UMCUnifiedReticleWidget::BindSourceNotification()
{
    if(!SourceSnapshotHandle.IsValid()) if(auto* Source=Backend.Get()) SourceSnapshotHandle=Source->OnReticleSnapshotUpdated().AddUObject(this,&ThisClass::OnSourceSnapshot);
}
void UMCUnifiedReticleWidget::UnbindSourceNotification()
{
    if(auto* Source=Backend.Get()) {Source->OnReticleSnapshotUpdated().Remove(SourceSnapshotHandle);Source->ClearUnifiedReticleView(this);}
    SourceSnapshotHandle.Reset();
}
void UMCUnifiedReticleWidget::SetBackend(UMCGameplayHUD* Source)
{
    auto* Candidate=Source && Source->GetWorld()==GetWorld() && Source->GetOwningPlayer()==GetOwningPlayer()?Source:nullptr;
    if(Backend.Get()!=Candidate) {UnbindSourceNotification();HideReticleCanvas();Backend=Candidate;bPrepared=false;}
    BindSourceNotification();if(!IsDesignTime()) {PreparePresentation();RefreshFromBackend();}
}
void UMCUnifiedReticleWidget::OnSourceSnapshot(const FMCUIReticlePresentation& Snapshot)
{
    auto* Source=Backend.Get();if(!Source || Snapshot.Source.Get()!=Source) return;
    if(!ConsumePresentation(Snapshot,Source->GetUnifiedReticlePaintGeometry()) || !Source->CommitUnifiedReticleView(this,Snapshot.SourceFrame)) {
        Source->ClearUnifiedReticleView(this);HideReticleCanvas();
    }
}
void UMCUnifiedReticleWidget::RefreshFromBackend()
{
    if(IsDesignTime()) return;if(auto* Source=Backend.Get()) OnSourceSnapshot(Source->GetUnifiedReticleSnapshot());
    else if(!PresentedSource.IsValid() || PresentedFrame!=GFrameCounter) HideReticleCanvas(); // External providers may have consumed this current frame before our Tick.
}
void UMCUnifiedReticleWidget::ApplyTheme() {Super::ApplyTheme();bPrepared=false;PreparePresentation();if(!IsDesignTime()) RefreshFromBackend();}
void UMCUnifiedReticleWidget::NativeConstruct() {Super::NativeConstruct();BindSourceNotification();PreparePresentation();RefreshFromBackend();}
void UMCUnifiedReticleWidget::NativeDestruct() {UnbindSourceNotification();HideReticleCanvas();Backend.Reset();bPrepared=false;Super::NativeDestruct();}
void UMCUnifiedReticleWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);if(IsDesignTime()) return;PreparePresentation();RefreshFromBackend();
}
void UMCUnifiedReticleWidget::OnLocalizationChanged() {Super::OnLocalizationChanged();bPrepared=false;PreparePresentation();if(!IsDesignTime()) RefreshFromBackend();}
void UMCUnifiedReticleWidget::ApplyEditorPreview(float Time)
{
    if(!IsDesignTime()) {RefreshFromBackend();return;}BuildPresentationTree();PreparePresentation();
    const bool Long=PreviewScenario==EMCGameplayUIPreviewScenario::LongText || PreviewState==TEXT("LongText");
    const bool Food=Long || PreviewState==TEXT("FoodReticle") || PreviewState==TEXT("FoodReticleFade") || PreviewState==TEXT("ReticleCombined");
    const bool Spray=PreviewState==TEXT("SprayReticle") || PreviewState==TEXT("PressureReticle") || PreviewState==TEXT("AntisepticReload") || PreviewState==TEXT("ReticleCombined");
    const bool Fog=PreviewState==TEXT("FogNavigation") || PreviewState==TEXT("ReticleCombined");
    if(!Food && !Spray && !Fog) {HideReticleCanvas();return;}
    FMCUIReticlePresentation Sample;Sample.bSourceVisible=true;Sample.bPaintGeometryCompatible=true;
    Sample.LocalSize=GetPaintSpaceGeometry().GetLocalSize();if(Sample.LocalSize.X<=1 || Sample.LocalSize.Y<=1) Sample.LocalSize=FVector2D(1280,720);
    const auto* T=GetResolvedTheme();if(!T) return;const auto& P=T->Palette;const FVector2D Center=Sample.LocalSize*.5;
    auto Line=[&](const TArray<FVector2D>& Points,FLinearColor Color,float Width,int32 Depth) {FMCUIReticleStroke Shape;Shape.Points=Points;Shape.Color=Color;Shape.Width=Width;Shape.Layer=Depth;Sample.Strokes.Add(MoveTemp(Shape));};
    if(Fog) {const FVector2D Direction(.6,-.8),Side(.8,.6),Tip=Center+Direction*130;Line({Tip-Direction*20+Side*10,Tip,Tip-Direction*20-Side*10},P.Mint,4,1);}
    if(Food) {
        const float Alpha=PreviewState==TEXT("FoodReticleFade")?float(.5+.5*FMath::Sin(Time*2)):1.f;
        const float Radius=FMath::Lerp(5.f,25.f,Alpha),Arm=FMath::Lerp(3.f,10.f,Alpha);
        for(int32 Index=0;Index<4;++Index) {const FVector2D Sign(Index<2?-1:1,Index%2?-1:1),Corner=Center+Sign*Radius;const TArray<FVector2D> Points={Corner-FVector2D(Sign.X*Arm,0),Corner,Corner-FVector2D(0,Sign.Y*Arm)};Line(Points,P.Ink.CopyWithNewOpacity(P.Ink.A*Alpha*.85f),4,2);Line(Points,P.OnMarine.CopyWithNewOpacity(P.OnMarine.A*Alpha),2,3);}
        FMCUIReticleLabel Copy;Copy.Position=Center+FVector2D(-58,Radius+12);Copy.Font=T->MakeGameplayFont(12);Copy.Layer=3;Copy.Color=P.OnMarine.CopyWithNewOpacity(P.OnMarine.A*Alpha);
        Copy.Text=UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),TEXT("UI.HUD.FoodThrow"),LOCTEXT("FoodThrow","Q · БРОСИТЬ"),PreviewCulture);
        if(Long) Copy.Text=FText::Format(FText::AsCultureInvariant(TEXT("{0} · {0} · {0}")),Copy.Text);Sample.Labels.Add(MoveTemp(Copy));
    }
    if(Spray) {
        auto Stroke=[&](const TArray<FVector2D>& Points,FLinearColor Color) {Line(Points,P.Ink.CopyWithNewOpacity(P.Ink.A*.8f),4,4);Line(Points,Color,2,5);};
        auto Arc=[&](float Radius,float Fraction,FLinearColor Color) {TArray<FVector2D> Points;const int32 Steps=FMath::Max(2,FMath::CeilToInt(40*Fraction));for(int32 Index=0;Index<=Steps;++Index) {const float Angle=-PI/2+2*PI*Fraction*Index/Steps;Points.Add(Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);}Stroke(Points,Color);};
        const bool Pressure=PreviewState==TEXT("PressureReticle"),Reload=PreviewState==TEXT("AntisepticReload");const float Radius=Pressure?7.f:10.f;const auto Tint=Reload?P.Muted:Pressure?P.OnMarine:P.Accent;Arc(Radius,1,Tint);
        for(FVector2D Axis:{FVector2D(1,0),FVector2D(-1,0),FVector2D(0,1),FVector2D(0,-1)}) Stroke({Center+Axis*(Radius+4),Center+Axis*(Radius+9)},Tint);
        FMCUIReticleBox Dot;Dot.Position=Center-FVector2D(2,2);Dot.Color=Tint;Dot.Layer=5;Sample.Boxes.Add(Dot);
        if(Pressure || Reload) {Arc(18,1,P.SurfaceAlt.CopyWithNewOpacity(P.SurfaceAlt.A*.65f));Arc(18,.55f,P.Shield);}
    }
    if(!PresentGeometry(Sample,GetPaintSpaceGeometry(),true)) HideReticleCanvas();
}
#undef LOCTEXT_NAMESPACE
