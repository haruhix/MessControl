#include "MCUniversalUIWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCUIContourShape.h"
#include "Brushes/SlateNoResource.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/NamedSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/SafeZone.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#define LOCTEXT_NAMESPACE "MCUniversalUI"

UMCUniversalUIWidget::UMCUniversalUIWidget()
{
    bUseNativeLayout=false;
    bOverridePreviewUIVersion=true;
    Label=LOCTEXT("DefaultLabel","Панель");
}

bool UMCUniversalUIWidget::Initialize()
{
    // Native WidgetTree is DuplicateTransient. UUserWidget initializes named
    // slots before this native class has reconstructed that tree, and may drop
    // their serialized bindings. Retain the parent-authored content across it.
    UWidget* Composed=GetContentForSlot(TEXT("Content"));
    const bool Initialized=Super::Initialize();
    if(Initialized && Composed) {BuildPresentationTree();SetContentForSlot(TEXT("Content"),Composed);}
    return Initialized;
}

void UMCUniversalUIWidget::GetSlotNames(TArray<FName>& Names) const
{
    Super::GetSlotNames(Names);
    Names.AddUnique(TEXT("Content"));
}

void UMCUniversalUIWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!WidgetTree->RootWidget)
    {
        auto* Root=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("UniversalRoot"));
        WidgetTree->RootWidget=Root;
        auto* Shadow=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("UniversalShadow"));
        Shadow->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* ShadowSlot=Root->AddChildToOverlay(Shadow);ShadowSlot->SetHorizontalAlignment(HAlign_Fill);ShadowSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Surface=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("UniversalSurface"));
        auto* SurfaceSlot=Root->AddChildToOverlay(Surface);SurfaceSlot->SetHorizontalAlignment(HAlign_Fill);SurfaceSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Content=WidgetTree->ConstructWidget<UNamedSlot>(UNamedSlot::StaticClass(),TEXT("Content"));
        auto* ContentSlot=CastChecked<UButtonSlot>(Surface->AddChild(Content));ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("UniversalDefaultContent"));Content->SetContent(Stack);
        auto* Art=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("UniversalIcon"));
        Stack->AddChildToVerticalBox(Art)->SetHorizontalAlignment(HAlign_Center);
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("UniversalLabel"));
        // Let the parent limit width and the button grow vertically. Label
        // fonts keep their authored size; translation is never shrunk to fit.
        Text->SetAutoWrapText(Kind!=EMCUniversalUIKind::Button);Text->SetWrapTextAt(Kind==EMCUniversalUIKind::Button?800.f:0.f);Text->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
        Stack->AddChildToVerticalBox(Text);
        auto* Bar=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("UniversalProgress"));
        Stack->AddChildToVerticalBox(Bar)->SetPadding(FMargin(0,10,0,0));
    }
    if(auto* Root=Cast<UOverlay>(WidgetTree->RootWidget);Root && !WidgetTree->FindWidget(TEXT("UniversalContour")))
    {
        auto* Contour=WidgetTree->ConstructWidget<UMCUIContourShape>(UMCUIContourShape::StaticClass(),TEXT("UniversalContour"));
        Contour->SetVisibility(ESlateVisibility::Collapsed);
        auto* ContourCell=CastChecked<UOverlaySlot>(Root->InsertChildAt(0,Contour));
        ContourCell->SetHorizontalAlignment(HAlign_Fill);ContourCell->SetVerticalAlignment(VAlign_Fill);
    }
    if(auto* Surface=WidgetTree->FindWidget<UButton>(TEXT("UniversalSurface")))
    {
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        Surface->IsFocusable=Kind==EMCUniversalUIKind::Button;
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
        Surface->OnClicked.AddUniqueDynamic(this,&ThisClass::Clicked);
    }
}

void UMCUniversalUIWidget::SetLabel(FName Key,const FText& Fallback) {LabelKey=Key;Label=Fallback;ApplyTheme();}
void UMCUniversalUIWidget::SetProgressValue(float Value) {Progress=FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):0.f;ApplyTheme();}
void UMCUniversalUIWidget::Clicked() {if(Kind==EMCUniversalUIKind::Button && GetIsEnabled()) OnClicked.Broadcast();}

void UMCUniversalUIWidget::ApplyTheme()
{
    Super::ApplyTheme();
    if(!WidgetTree) return;
    const auto* T=GetResolvedTheme();if(!T) return;
    const float Radius=FMath::Max(0.f,bUseThemeShape?T->CornerRadius:CornerRadius);
    const float Outline=FMath::Max(0.f,bUseThemeShape?T->OutlineWidth:OutlineWidth);
    const bool Button=Kind==EMCUniversalUIKind::Button;
    auto* Contour=WidgetTree->FindWidget<UMCUIContourShape>(TEXT("UniversalContour"));
    // Designer forces wrappers enabled. Use the authored flag for its specimen,
    // so disabled controls still display their real state in the existing preview tool.
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    const bool Disabled=!bIsEnabled || (IsDesignTime() && PreviewScenario==EMCGameplayUIPreviewScenario::Disabled);
    PRAGMA_ENABLE_DEPRECATION_WARNINGS
    const auto FillColor=bCustomColors?CustomFill:T->GetColor(Button?EMCUIColorToken::Button:Fill);
    // Semi-transparent PSD panels need an exterior-only shadow. The contour
    // renderer clips that shadow rather than stacking another fill behind it.
    const bool ExteriorShadow=Kind==EMCUniversalUIKind::Panel && bCustomColors
        && FillColor.A<1.f && CustomShadow.A>0.f;
    const bool Polygon=Kind==EMCUniversalUIKind::Panel && Contour && (ContourPoints.Num()>=3 || ExteriorShadow);
    const bool Selected=bSelected || (IsDesignTime() && PreviewScenario==EMCGameplayUIPreviewScenario::Selected);
    const auto Edge=bCustomColors?CustomOutline:T->GetColor(Selected?EMCUIColorToken::Focus:EMCUIColorToken::Border);
    if(auto* Surface=WidgetTree->FindWidget<UButton>(TEXT("UniversalSurface")))
    {
        FButtonStyle Style;
        Style.SetNormal(FSlateRoundedBoxBrush(Disabled?T->Palette.ButtonDisabled:FillColor,Radius,Edge,Outline));
        Style.SetHovered(FSlateRoundedBoxBrush(Button?(bCustomColors?FMath::Lerp(CustomFill,FLinearColor::White,.12f):T->Palette.ButtonHover):FillColor,Radius,bCustomColors?Edge:Button?T->Palette.Focus:Edge,Outline));
        Style.SetPressed(FSlateRoundedBoxBrush(Button?(bCustomColors?(CustomFill*.8f).CopyWithNewOpacity(CustomFill.A):T->Palette.ButtonPressed):FillColor,Radius,bCustomColors?Edge:Button?T->Palette.Focus:Edge,Outline));
        Style.SetDisabled(FSlateRoundedBoxBrush(T->Palette.ButtonDisabled,Radius,Edge,Outline));
        if(Polygon) {
            Style.SetNormal(FSlateNoResource());Style.SetHovered(FSlateNoResource());
            Style.SetPressed(FSlateNoResource());Style.SetDisabled(FSlateNoResource());
        }
        Style.SetNormalPadding(FMargin(0));Style.SetPressedPadding(FMargin(0));
        Surface->SetStyle(Style);Surface->SetBackgroundColor(FLinearColor::White);
        Surface->SetIsEnabled(!Disabled);
        // UE 5.8 exposes focusability only at construction; Kind is chosen before the tree is built.
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        Surface->IsFocusable=Button;
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
        Surface->SetVisibility(Button?ESlateVisibility::Visible:ESlateVisibility::SelfHitTestInvisible);
        if(auto* ContentSlot=Cast<UButtonSlot>(Surface->GetContentSlot())) {
            const float Inner=bFitProgressToOutline?Outline:Outline+.5f;
            const FMargin InnerPadding=Kind==EMCUniversalUIKind::Progress?
                (bFitProgressToOutline?FMargin(Inner):FMargin(FMath::Max(Inner,ContentPadding.Left),FMath::Max(Inner,ContentPadding.Top),
                    FMath::Max(Inner,ContentPadding.Right),FMath::Max(Inner,ContentPadding.Bottom))):ContentPadding;
            ContentSlot->SetPadding(InnerPadding);
            ContentSlot->SetVerticalAlignment(Button?VAlign_Center:VAlign_Fill);
        }
    }
    if(auto* Shadow=WidgetTree->FindWidget<UBorder>(TEXT("UniversalShadow")))
    {
        Shadow->SetBrush(FSlateRoundedBoxBrush(bCustomColors?CustomShadow:T->Palette.Border,Radius));Shadow->SetBrushColor(FLinearColor::White);
        Shadow->SetRenderTranslation(ShadowOffset);
        if(Polygon) {
            if(!PreContourShadowVisibility.IsSet()) PreContourShadowVisibility=Shadow->GetVisibility();
            Shadow->SetVisibility(ESlateVisibility::Collapsed);
        } else if(PreContourShadowVisibility.IsSet()) {
            Shadow->SetVisibility(PreContourShadowVisibility.GetValue());PreContourShadowVisibility.Reset();
        }
    }
    if(Contour) {
        Contour->Points=ContourPoints.Num()>=3?ContourPoints:TArray<FVector2D>{{0,0},{1,0},{1,1},{0,1}};
        Contour->FillColor=Disabled?T->Palette.ButtonDisabled:FillColor;
        Contour->OutlineColor=Edge;Contour->ShadowColor=bCustomColors?CustomShadow:T->Palette.Border;
        Contour->OutlineWidth=Outline;Contour->CornerRadius=Radius;Contour->ShadowOffset=ShadowOffset;
        Contour->SynchronizeProperties();
        // Designer synchronization restores its bound visibility. Apply the
        // actual shape visibility last so an empty contour contributes no size.
        Contour->SetVisibility(Polygon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(auto* Text=WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel")))
    {
        Text->SetText(Localized(LabelKey,Label));Text->SetFont(T->MakeFont(FontSize,true));
        // An intrinsic button width must not feed back into auto-wrap and
        // progressively split ordinary labels at smaller DPI. Grow the shape
        // to the text; wrap only beyond the bounded readable column.
        Text->SetAutoWrapText(!Button);Text->SetWrapTextAt(Button?800.f:0.f);Text->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
        const auto TextToken=Button?EMCUIColorToken::ButtonText:
            Foreground==EMCUIColorToken::OnMarine && FillColor.GetLuminance()>.35f?EMCUIColorToken::Ink:Foreground;
        Text->SetColorAndOpacity(bCustomColors?CustomText:T->GetColor(TextToken));
        Text->SetJustification(Button?ETextJustify::Center:ETextJustify::Left);
        Text->SetVisibility(Text->GetText().IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    if(auto* Art=WidgetTree->FindWidget<UImage>(TEXT("UniversalIcon")))
    {
        Art->SetBrushFromTexture(Icon);Art->SetDesiredSizeOverride(FVector2D(48,48));
        Art->SetVisibility(Icon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(auto* Bar=WidgetTree->FindWidget<UProgressBar>(TEXT("UniversalProgress")))
    {
        FProgressBarStyle Style;
        if(bFitProgressToOutline) Style.SetBackgroundImage(FSlateNoResource());
        else Style.SetBackgroundImage(FSlateRoundedBoxBrush(T->Palette.LCDBackground,Radius));
        const float FillRadius=bFitProgressToOutline?FMath::Max(0.f,Radius-Outline):Radius;
        Style.SetFillImage(FSlateRoundedBoxBrush(bUseCustomProgressFill?CustomProgressFillColor:T->GetColor(ProgressFill),FillRadius));
        Style.BackgroundImage.ImageSize=FVector2D(120,18);Style.FillImage.ImageSize=FVector2D(120,18);
        Bar->SetWidgetStyle(Style);Bar->SetFillColorAndOpacity(FLinearColor::White);
        // Fill the actual remaining interior, including small portrait bars.
        // Intrinsic brush size is only the unconstrained specimen's desired size.
        if(auto* BarCell=Cast<UVerticalBoxSlot>(Bar->Slot)) {
            BarCell->SetPadding(FMargin(0));BarCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            BarCell->SetVerticalAlignment(VAlign_Fill);
        }
        Bar->SetBorderPadding(FVector2D::ZeroVector);Bar->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        Bar->SetPercent(FMath::IsFinite(Progress)?FMath::Clamp(Progress,0.f,1.f):0.f);
        Bar->SetVisibility(Kind==EMCUniversalUIKind::Progress?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
}

void UMCUniversalUIWidget::ApplyEditorPreview(float Time)
{
    // Preview properties never overwrite the saved Label, Progress or enabled state.
    Super::ApplyEditorPreview(Time);
    if(!IsDesignTime() || !WidgetTree) return;
    if(auto* Text=WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel")))
        if(PreviewScenario==EMCGameplayUIPreviewScenario::LongText)
            Text->SetText(Localized(TEXT("UI.V2.Preview.Long"),LOCTEXT("LongLabel","Следующее событие: очень длинное локализованное название события")));
    if(auto* Bar=WidgetTree->FindWidget<UProgressBar>(TEXT("UniversalProgress")))
        if(PreviewScenario==EMCGameplayUIPreviewScenario::LowResources) Bar->SetPercent(.08f);
}

UMCUniversalUIPreviewWidget::UMCUniversalUIPreviewWidget() {bUseNativeLayout=false;bOverridePreviewUIVersion=true;}

void UMCUniversalUIPreviewWidget::BuildPresentationTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    auto* Safe=WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass(),TEXT("PreviewSafeArea"));WidgetTree->RootWidget=Safe;
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Safe->SetContent(Canvas);
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
    auto* CanvasSlot=Canvas->AddChildToCanvas(Scroll);CanvasSlot->SetAnchors(FAnchors(.04f,.04f,.96f,.96f));CanvasSlot->SetOffsets(FMargin(0));
    auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Stack);
    auto Add=[&](FName Name,EMCUniversalUIKind Kind,FName Key,const FText& Copy)
    {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("Universal"),UMCUniversalUIWidget::StaticClass(),GetResolvedTheme());
        auto* Sample=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(Class,Name);
        Sample->Kind=Kind;Sample->LabelKey=Key;Sample->Label=Copy;Sample->Initialize();Sample->BuildPresentationTree();
        Stack->AddChildToVerticalBox(Sample)->SetPadding(FMargin(6,6,12,14));return Sample;
    };
    Add(TEXT("PreviewPanel"),EMCUniversalUIKind::Panel,TEXT("UI.V2.Preview.Panel"),LOCTEXT("PanelSample","Универсальная панель"));
    Add(TEXT("PreviewAction"),EMCUniversalUIKind::Button,TEXT("UI.V2.Preview.Action"),LOCTEXT("ButtonSample","Продолжить"));
    Add(TEXT("PreviewProgress"),EMCUniversalUIKind::Progress,TEXT("UI.V2.Preview.Progress"),LOCTEXT("ProgressSample","Шкала прогресса"));
    Add(TEXT("PreviewLongText"),EMCUniversalUIKind::Panel,TEXT("UI.V2.Preview.Long"),LOCTEXT("LongSample","Следующее событие: очень длинное локализованное название события"))->Fill=EMCUIColorToken::Accent;
    Add(TEXT("PreviewDisabled"),EMCUniversalUIKind::Button,TEXT("UI.V2.Preview.Disabled"),LOCTEXT("DisabledSample","Недоступно"))->SetIsEnabled(false);
    Add(TEXT("PreviewSelected"),EMCUniversalUIKind::Panel,TEXT("UI.V2.Preview.Selected"),LOCTEXT("SelectedSample","Выбрано"))->bSelected=true;
}

void UMCUniversalUIPreviewWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    if(!WidgetTree) return;
    if(auto* Bar=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("PreviewProgress")))
    {
        // Designer-only animation. The F3 specimen keeps its explicit sample value.
        Bar->SetProgressValue(IsDesignTime() && Time>0.f?.5f+.45f*FMath::Sin(Time*2.f):.65f);
    }
}

#undef LOCTEXT_NAMESPACE
