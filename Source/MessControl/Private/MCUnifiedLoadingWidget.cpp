#include "MCUnifiedLoadingWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCLoadingTips.h"
#include "MCUniversalUIWidget.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SafeZone.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Throbber.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

namespace MCUnifiedLoadingPrivate
{
    FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    const TCHAR* Labels[]={TEXT("LoadingTitle"),TEXT("LoadingStatus"),TEXT("LoadingDetail"),TEXT("LoadingAssetLabel"),
        TEXT("RetryLoadingLabel"),TEXT("LeaveLoadingLabel"),TEXT("LoadingTipHeading"),TEXT("LoadingTipCurrent"),TEXT("LoadingTipNext")};
}
UMCUnifiedLoadingWidget::UMCUnifiedLoadingWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;PreviewUIVersion=EMCUIVersion::New;SetIsFocusable(true);
}
UMCUniversalUIWidget* UMCUnifiedLoadingWidget::MakeShape(FName Name,FLinearColor Fill,FMargin ShapePadding)
{
    auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Shape->Label=FText::GetEmpty();Shape->LabelKey=NAME_None;Shape->bUseNativeLayout=false;Shape->bUseThemeShape=false;Shape->bCustomColors=true;Shape->CustomFill=Fill;
    Shape->CustomOutline=MCUnifiedLoadingPrivate::Color(TEXT("FFF9F8"));Shape->CustomShadow=MCUnifiedLoadingPrivate::Color(TEXT("1E266A"));
    Shape->ContentPadding=ShapePadding;Shape->CornerRadius=22;Shape->OutlineWidth=3;Shape->ShadowOffset=FVector2D(8,10);
    Shape->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Shape->Initialize();Shape->BuildPresentationTree();Shape->ApplyTheme();return Shape;
}
UTextBlock* UMCUnifiedLoadingWidget::MakeLabel(FName Name,int32 FontSize)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Label->SetFont(GetResolvedTheme()->MakeFont(FontSize,true));Label->SetColorAndOpacity(MCUnifiedLoadingPrivate::Color(TEXT("FFF9F8")));
    Label->SetAutoWrapText(true);Label->SetWrapTextAt(660);Label->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
    Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);Label->SetJustification(ETextJustify::Center);
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);return Label;
}
void UMCUnifiedLoadingWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {Super::BuildPresentationTree();UpdateLayout();return;}
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("LoadingRoot"));WidgetTree->RootWidget=Root;
    auto Full=[&](UWidget* Child,int32 Z) {auto* CanvasCell=Root->AddChildToCanvas(Child);CanvasCell->SetAnchors(FAnchors(0,0,1,1));CanvasCell->SetOffsets(FMargin(0));CanvasCell->SetZOrder(Z);};
    auto* Backdrop=MakeShape(TEXT("LoadingV2Backdrop"),MCUnifiedLoadingPrivate::Color(TEXT("355C70")),FMargin(0));
    Backdrop->CornerRadius=0;Backdrop->OutlineWidth=0;Backdrop->ShadowOffset=FVector2D::ZeroVector;Backdrop->ApplyTheme();Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);Full(Backdrop,0);
    auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("LoadingArtFit"));Fit->SetStretch(EStretch::ScaleToFit);Full(Fit,1);
    auto* ArtworkNode=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("LoadingArtwork"));Fit->SetContent(ArtworkNode);
    UTexture2D* Texture=BackgroundArtwork.Get();if(!Texture && IsDesignTime() && !BackgroundArtwork.IsNull()) Texture=BackgroundArtwork.LoadSynchronous();
    if(Texture) ArtworkNode->SetBrushFromTexture(Texture,true);ArtworkNode->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Safe=WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass(),TEXT("LoadingV2SafeZone"));Full(Safe,3);
    auto* Center=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("LoadingV2Center"));Safe->SetContent(Center);
    auto* Viewport=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("LoadingV2ControlsViewport"));
    auto* CenterSlot=Center->AddChildToOverlay(Viewport);CenterSlot->SetHorizontalAlignment(HAlign_Center);CenterSlot->SetVerticalAlignment(VAlign_Center);CenterSlot->SetPadding(FMargin(24,24,34,34));
    auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("LoadingV2ControlsStack"));Viewport->SetContent(Stack);
    auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("LoadingFooterWidth"));Width->SetWidthOverride(720);Stack->AddChildToVerticalBox(Width)->SetHorizontalAlignment(HAlign_Center);
    // Transparent compatibility wrappers own the existing fade and tip visibility.
    auto* FooterNode=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("LoadingFooterPanel"));FooterNode->SetBrush(FSlateNoResource());FooterNode->SetPadding(FMargin(0));Width->SetContent(FooterNode);
    auto* Panel=MakeShape(TEXT("LoadingV2Panel"),MCUnifiedLoadingPrivate::Color(TEXT("4C76AE")),FMargin(24,20));FooterNode->SetContent(Panel);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("LoadingV2PanelContent"));Panel->SetContentForSlot(TEXT("Content"),Column);
    auto Add=[&](const TCHAR* Name,int32 Size,float Bottom) {auto* Label=MakeLabel(FName(Name),Size);Column->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,0,0,Bottom));return Label;};
    Add(TEXT("LoadingTitle"),32,12);Add(TEXT("LoadingStatus"),23,7);Add(TEXT("LoadingDetail"),19,8);Add(TEXT("LoadingAssetLabel"),20,10);
    auto* Track=MakeShape(TEXT("LoadingV2ProgressFrame"),MCUnifiedLoadingPrivate::Color(TEXT("355C70")),FMargin(3));Track->CornerRadius=9;Track->OutlineWidth=2;Track->ShadowOffset=FVector2D::ZeroVector;Track->ApplyTheme();
    auto* Bar=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("LoadingAssetProgress"));
    FProgressBarStyle Progress;Progress.SetBackgroundImage(FSlateRoundedBoxBrush(MCUnifiedLoadingPrivate::Color(TEXT("355C70")),6.f));Progress.SetFillImage(FSlateRoundedBoxBrush(MCUnifiedLoadingPrivate::Color(TEXT("AE4D78")),6.f));
    Progress.BackgroundImage.ImageSize=FVector2D(120,14);Progress.FillImage.ImageSize=FVector2D(120,14);Bar->SetWidgetStyle(Progress);Bar->SetFillColorAndOpacity(FLinearColor::White);Bar->SetBorderPadding(FVector2D::ZeroVector);Track->SetContentForSlot(TEXT("Content"),Bar);
    auto* TrackHeight=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("LoadingV2ProgressHeight"));TrackHeight->SetHeightOverride(22);TrackHeight->SetContent(Track);Column->AddChildToVerticalBox(TrackHeight)->SetPadding(FMargin(0,0,0,8));
    auto* Spinner=WidgetTree->ConstructWidget<UThrobber>(UThrobber::StaticClass(),TEXT("LoadingSpinner"));Spinner->SetNumberOfPieces(3);
    FSlateBrush Piece=FSlateRoundedBoxBrush(MCUnifiedLoadingPrivate::Color(TEXT("FFF9F8")),4.f);Piece.ImageSize=FVector2D(10,10);Spinner->SetImage(Piece);Column->AddChildToVerticalBox(Spinner)->SetHorizontalAlignment(HAlign_Center);
    auto* Actions=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("LoadingV2Actions"));Actions->SetExplicitWrapSize(true);Actions->SetWrapSize(672);Actions->SetHorizontalAlignment(HAlign_Center);Actions->SetInnerSlotPadding(FVector2D(16,12));Column->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,14,0,0));
    for(const TCHAR* Name:{TEXT("RetryLoading"),TEXT("LeaveLoading")}) {
        const FName Key(Name);auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(Key.ToString()+TEXT("V2Bounds"))));Bounds->SetMaxDesiredWidth(320);
        auto* Shape=MakeShape(FName(*(Key.ToString()+TEXT("V2Shape"))),MCUnifiedLoadingPrivate::Color(TEXT("AE4D78")),FMargin(0));Shape->CornerRadius=16;Shape->ShadowOffset=FVector2D(5,6);Shape->ApplyTheme();Bounds->SetContent(Shape);
        auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Key);FButtonStyle Style;Style.SetNormal(FSlateNoResource());Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.12f),14.f));Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.12f),14.f));Style.SetNormalPadding(FMargin(16,10));Style.SetPressedPadding(FMargin(16,10));Button->SetStyle(Style);
        auto* Label=MakeLabel(FName(*(Key.ToString()+TEXT("Label"))),22);Label->SetWrapTextAt(288);Button->SetContent(Label);Shape->SetContentForSlot(TEXT("Content"),Button);Actions->AddChildToWrapBox(Bounds);
    }
    auto* TipExtent=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("LoadingTipWidth"));TipExtent->SetWidthOverride(760);Stack->AddChildToVerticalBox(TipExtent)->SetHorizontalAlignment(HAlign_Center);Cast<UVerticalBoxSlot>(TipExtent->Slot)->SetPadding(FMargin(0,24,0,0));
    auto* Tip=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("LoadingTipPanel"));Tip->SetBrush(FSlateNoResource());Tip->SetPadding(FMargin(0));TipExtent->SetContent(Tip);
    auto* TipShape=MakeShape(TEXT("LoadingV2TipShape"),MCUnifiedLoadingPrivate::Color(TEXT("355C70")),FMargin(24,16));Tip->SetContent(TipShape);
    auto* TipColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("LoadingV2TipContent"));TipShape->SetContentForSlot(TEXT("Content"),TipColumn);
    auto* Heading=MakeLabel(TEXT("LoadingTipHeading"),19);TipColumn->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0,0,0,10));
    auto* Layers=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("LoadingV2TipLayers"));TipColumn->AddChildToVerticalBox(Layers);
    for(const TCHAR* Name:{TEXT("LoadingTipCurrent"),TEXT("LoadingTipNext")}) {
        auto* Label=MakeLabel(FName(Name),23);Label->SetAutoWrapText(false);Label->SetWrapTextAt(712);
        auto* Layer=Layers->AddChildToOverlay(Label);Layer->SetHorizontalAlignment(HAlign_Fill);Layer->SetVerticalAlignment(VAlign_Center);
    }
    Super::BuildPresentationTree(); // Real buttons, source artwork, tips and progress bind once.
    RefreshPresentation();UpdateLayout();SyncFrameVisibility();
}
void UMCUnifiedLoadingWidget::UpdateLayout(FVector2D AvailableSize)
{
    if(!WidgetTree) return;
    auto* ControlsViewport=WidgetTree->FindWidget<USizeBox>(TEXT("LoadingV2ControlsViewport"));
    auto* ControlsStack=WidgetTree->FindWidget<UVerticalBox>(TEXT("LoadingV2ControlsStack"));
    if(!ControlsViewport || !ControlsStack) return;
    if(ControlsViewport->GetContent()!=ControlsStack) {
        auto* Previous=Cast<UScrollBox>(ControlsViewport->GetContent());
        if(!Previous || Previous->GetFName()!=TEXT("LoadingV2ControlsScroll") || Previous->GetChildrenCount()!=1 || Previous->GetChildAt(0)!=ControlsStack) return;
        ControlsStack->RemoveFromParent();ControlsViewport->SetContent(ControlsStack);
    }
    // Remove only the known saved scroll wrappers. The same detail label and
    // current/next fade overlay return to their original direct column slots.
    // This runs before any arranged-size guard, including saved-tree migration.
    auto UnwrapCopy=[&](FName NodeName,FName ColumnName,FName BoundsName,FName ScrollName) {
        auto* BodyNode=WidgetTree->FindWidget(NodeName);
        auto* OwnerColumn=WidgetTree->FindWidget<UVerticalBox>(ColumnName);
        if(!BodyNode || !OwnerColumn) return false;
        if(BodyNode->GetParent()==OwnerColumn) return true;
        auto* BodyBounds=WidgetTree->FindWidget<USizeBox>(BoundsName);
        auto* BodyScroll=WidgetTree->FindWidget<UScrollBox>(ScrollName);
        if(!BodyBounds || !BodyScroll || BodyBounds->GetParent()!=OwnerColumn || BodyBounds->GetContent()!=BodyScroll
            || BodyScroll->GetChildrenCount()!=1 || BodyScroll->GetChildAt(0)!=BodyNode || BodyNode->GetParent()!=BodyScroll) return false;
        const auto ExistingRows=OwnerColumn->GetAllChildren();
        struct FCopyRowLayout {FMargin Inset;FSlateChildSize Sizing;EHorizontalAlignment H=HAlign_Fill;EVerticalAlignment V=VAlign_Fill;};
        TArray<FCopyRowLayout> RowLayouts;
        for(UWidget* Child:ExistingRows) {FCopyRowLayout Layout;if(auto* Cell=Cast<UVerticalBoxSlot>(Child->Slot)) {Layout.Inset=Cell->GetPadding();Layout.Sizing=Cell->GetSize();Layout.H=Cell->GetHorizontalAlignment();Layout.V=Cell->GetVerticalAlignment();}RowLayouts.Add(Layout);}
        BodyNode->RemoveFromParent();OwnerColumn->ClearChildren();
        for(int32 Index=0;Index<ExistingRows.Num();++Index) {
            auto* Cell=OwnerColumn->AddChildToVerticalBox(ExistingRows[Index]==BodyBounds?BodyNode:ExistingRows[Index]);
            Cell->SetPadding(RowLayouts[Index].Inset);Cell->SetSize(RowLayouts[Index].Sizing);Cell->SetHorizontalAlignment(RowLayouts[Index].H);Cell->SetVerticalAlignment(RowLayouts[Index].V);
        }
        return true;
    };
    if(!UnwrapCopy(TEXT("LoadingDetail"),TEXT("LoadingV2PanelContent"),TEXT("LoadingV2DetailBounds"),TEXT("LoadingV2DetailScroll"))
        || !UnwrapCopy(TEXT("LoadingV2TipLayers"),TEXT("LoadingV2TipContent"),TEXT("LoadingV2TipBodyBounds"),TEXT("LoadingV2TipBodyScroll"))) return;
    ControlsViewport->ClearHeightOverride();ControlsViewport->ClearMinDesiredHeight();ControlsViewport->ClearMaxDesiredHeight();
    if(AvailableSize.X<=1 || AvailableSize.Y<=1) AvailableSize=GetPaintSpaceGeometry().GetLocalSize();
    if(AvailableSize.X<=1 || AvailableSize.Y<=1) return;
    LastViewport=AvailableSize;
    const float AvailableWidth=FMath::Max(1.f,float(AvailableSize.X)-58.f);
    const float MainWidth=FMath::Max(1.f,FMath::Min(MaximumPanelWidth,AvailableWidth));
    const float TipPanelWidth=FMath::Max(1.f,FMath::Min(FMath::Clamp(float(AvailableSize.X)*.64f,260.f,760.f),AvailableWidth));
    if(auto* MainBounds=WidgetTree->FindWidget<USizeBox>(TEXT("LoadingFooterWidth"))) MainBounds->SetWidthOverride(MainWidth);
    if(auto* TipBoundsNode=WidgetTree->FindWidget<USizeBox>(TEXT("LoadingTipWidth"))) TipBoundsNode->SetWidthOverride(TipPanelWidth);
    ControlsViewport->SetWidthOverride(FMath::Min(FMath::Max(MainWidth,TipPanelWidth)+10.f,AvailableWidth));
    const float MainCopyWidth=FMath::Max(1.f,MainWidth-48.f),TipCopyWidth=FMath::Max(1.f,TipPanelWidth-48.f);
    for(const TCHAR* Name:{TEXT("LoadingTitle"),TEXT("LoadingStatus"),TEXT("LoadingDetail"),TEXT("LoadingAssetLabel")})
        if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(FName(Name))) {Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(MainCopyWidth);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);}
    for(const TCHAR* Name:{TEXT("LoadingTipHeading"),TEXT("LoadingTipCurrent"),TEXT("LoadingTipNext")})
        if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(FName(Name))) {Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(TipCopyWidth);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);}
    if(auto* Actions=WidgetTree->FindWidget<UWrapBox>(TEXT("LoadingV2Actions"))) Actions->SetWrapSize(MainCopyWidth);
    for(const TCHAR* Name:{TEXT("RetryLoading"),TEXT("LeaveLoading")}) {
        const FString Prefix(Name);const float ActionWidth=FMath::Min(320.f,MainCopyWidth);
        if(auto* ActionBounds=WidgetTree->FindWidget<USizeBox>(FName(*(Prefix+TEXT("V2Bounds"))))) ActionBounds->SetMaxDesiredWidth(ActionWidth);
        if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(FName(*(Prefix+TEXT("Label"))))) {Caption->SetWrapTextAt(FMath::Max(1.f,ActionWidth-32.f));Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);}
    }
}
void UMCUnifiedLoadingWidget::SyncFrameVisibility()
{
    if(!WidgetTree) return;
    auto Mirror=[&](FName Frame,FName Source) {auto* View=WidgetTree->FindWidget(Frame);const auto* State=WidgetTree->FindWidget(Source);
        if(View) View->SetVisibility(State && State->IsVisible()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    };
    Mirror(TEXT("LoadingV2ProgressHeight"),TEXT("LoadingAssetProgress"));
    Mirror(TEXT("LoadingTipWidth"),TEXT("LoadingTipPanel"));
    Mirror(TEXT("RetryLoadingV2Bounds"),TEXT("RetryLoading"));Mirror(TEXT("LeaveLoadingV2Bounds"),TEXT("LeaveLoading"));
}
void UMCUnifiedLoadingWidget::ApplyTheme()
{
    Super::ApplyTheme();if(!WidgetTree || !GetResolvedTheme()) return;
    for(const TCHAR* Name:MCUnifiedLoadingPrivate::Labels) if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) {
        Label->SetFont(GetResolvedTheme()->MakeFont(FMath::Max(8,FMath::RoundToInt(Label->GetFont().Size)),true));
        Label->SetColorAndOpacity(MCUnifiedLoadingPrivate::Color(TEXT("FFF9F8")));
    }
    bPrepared=false;UpdateLayout();SyncFrameVisibility();if(!bPreparing) PreparePresentation();
}
void UMCUnifiedLoadingWidget::PreparePresentation()
{
    if(bPrepared || bPreparing) return;TGuardValue<bool> Guard(bPreparing,true);BuildPresentationTree();
    if(!WidgetTree || !FSlateApplication::IsInitialized() || !GetResolvedTheme()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    bool Ready=true;const auto Measure=Renderer->GetFontMeasureService();
    PreparedFontScale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    TArray<FText> Warm=MCLoadingTips::Resolve(GetResolvedTranslationTable(),IsDesignTime()?PreviewCulture:FString());
    Warm.Add(MCLoadingTips::Heading(GetResolvedTranslationTable(),IsDesignTime()?PreviewCulture:FString()));
    const FName Keys[]={TEXT("loading.failed"),TEXT("loading.waiting"),TEXT("loading.title"),TEXT("loading.starting"),TEXT("loading.phase.assets"),TEXT("loading.phase.graphics"),TEXT("loading.phase.streaming"),TEXT("loading.localready"),TEXT("loading.assets"),TEXT("loading.streaming.percent"),TEXT("loading.retry"),TEXT("loading.return")};
    for(FName Key:Keys) Warm.Add(Localized(Key,FText::GetEmpty()));
    const FText AllCopy=FText::Join(FText::AsCultureInvariant(TEXT(" ")),Warm);
    for(const TCHAR* Name:MCUnifiedLoadingPrivate::Labels) {
        auto* Label=WidgetTree->FindWidget<UTextBlock>(Name);Ready&=Label && Label->GetFont().HasValidFont();
        if(Label) {Measure->Measure(AllCopy,Label->GetFont(),PreparedFontScale);Measure->Measure(FText::AsCultureInvariant(TEXT("0123456789 %.,/")),Label->GetFont(),PreparedFontScale);}
    }
    if(!BackgroundArtwork.IsNull()) {
        UTexture2D* Texture=IsDesignTime()?BackgroundArtwork.LoadSynchronous():BackgroundArtwork.Get();Ready&=Texture!=nullptr;
        if(Texture) if(auto* Art=WidgetTree->FindWidget<UImage>(TEXT("LoadingArtwork"))) if(Art->GetBrush().GetResourceObject()!=Texture) Art->SetBrushFromTexture(Texture,true);
    }
    Ready&=WidgetTree->FindWidget<UButton>(TEXT("RetryLoading")) && WidgetTree->FindWidget<UButton>(TEXT("LeaveLoading"))
        && WidgetTree->FindWidget<UProgressBar>(TEXT("LoadingAssetProgress")) && WidgetTree->FindWidget<UBorder>(TEXT("LoadingFooterPanel")) && WidgetTree->FindWidget<UBorder>(TEXT("LoadingTipPanel"));
    bPrepared=Ready;
}
void UMCUnifiedLoadingWidget::NativeConstruct() {Super::NativeConstruct();PreparePresentation();SyncFrameVisibility();}
void UMCUnifiedLoadingWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds); // Sole source tip clock, measured progress and easing.
    UpdateLayout(Geometry.GetLocalSize());SyncFrameVisibility();
    if(!FMath::IsNearlyEqual(PreparedFontScale,Geometry.GetAccumulatedLayoutTransform().GetScale(),.001f)) bPrepared=false;
    PreparePresentation();
}
void UMCUnifiedLoadingWidget::OnLocalizationChanged() {Super::OnLocalizationChanged();bPrepared=false;PreparePresentation();}
void UMCUnifiedLoadingWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);UpdateLayout();SyncFrameVisibility();
}
