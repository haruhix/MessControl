#include "MCHubWardrobeWidget.h"

#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElementTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

#define LOCTEXT_NAMESPACE "MCHubWardrobe"

namespace MCHubWardrobeWidgetPrivate
{
    class SPalette final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SPalette) {} SLATE_ARGUMENT(UMCWardrobeColorPalette*, Owner) SLATE_END_ARGS()
        void Construct(const FArguments& Args) {Owner=Args._Owner;}
        virtual bool SupportsKeyboardFocus() const override {return true;}
        virtual FVector2D ComputeDesiredSize(float) const override
        {return Owner.IsValid()?Owner->DesiredSize:FVector2D(216,216);}
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
            FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override
        {
            if(!Owner.IsValid()) return Layer;
            const FVector2D Size=Geometry.GetLocalSize();if(Size.X<1 || Size.Y<1) return Layer;
            const FVector HSV=Owner->GetHSV();const bool Hue=Owner->Mode==EMCWardrobePaletteMode::Hue;
            const FLinearColor Tint=Style.GetColorAndOpacityTint();
            const ESlateDrawEffect Effects=ShouldBeEnabled(bParentEnabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
            if(Hue) {
                TArray<FSlateGradientStop> Stops;
                for(int32 Index=0;Index<=6;++Index)
                    Stops.Emplace(FVector2D(0,Size.Y*Index/6.),FLinearColor(Index*60.f,1,1).HSVToLinearRGB()*Tint);
                // Slate's orientation names describe the STOP line: horizontal
                // stops interpolate top-to-bottom (ElementBatcher.cpp:1783).
                FSlateDrawElement::MakeGradient(Elements,Layer,Geometry.ToPaintGeometry(),Stops,Orient_Horizontal,Effects);
            }
            else {
                // Exactly two gradient quads: saturation, then value. No grid,
                // procedural texture, material instance or per-cell draw calls.
                const FLinearColor HueColor=FLinearColor(float(HSV.X)*360.f,1,1).HSVToLinearRGB();
                FSlateDrawElement::MakeGradient(Elements,Layer,Geometry.ToPaintGeometry(),
                    {FSlateGradientStop(FVector2D(0,0),FLinearColor::White*Tint),
                     FSlateGradientStop(FVector2D(Size.X,0),HueColor*Tint)},Orient_Vertical,Effects);
                FSlateDrawElement::MakeGradient(Elements,Layer+1,Geometry.ToPaintGeometry(),
                    {FSlateGradientStop(FVector2D(0,0),FLinearColor(0,0,0,0)),
                     FSlateGradientStop(FVector2D(0,Size.Y),FLinearColor(0,0,0,Tint.A))},Orient_Horizontal,Effects);
            }
            const FVector2D Center=Hue?FVector2D(Size.X*.5,HSV.X*Size.Y):FVector2D(HSV.Y*Size.X,(1.-HSV.Z)*Size.Y);
            TArray<FVector2D> Points;
            if(Hue) {
                const double Y=FMath::Clamp(Center.Y,3.,FMath::Max(3.,Size.Y-3.));
                Points={FVector2D(1,Y),FVector2D(Size.X-1,Y)};
            }
            else for(int32 Index=0;Index<=24;++Index) {
                const double Angle=2.*PI*Index/24.;
                Points.Add(Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Owner->IndicatorRadius);
            }
            FSlateDrawElement::MakeLines(Elements,Layer+2,Geometry.ToPaintGeometry(),Points,Effects,FLinearColor(0,0,0,Tint.A),true,4.f);
            FSlateDrawElement::MakeLines(Elements,Layer+3,Geometry.ToPaintGeometry(),Points,Effects,FLinearColor(1,1,1,Tint.A),true,2.f);
            return Layer+3;
        }
        virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override
        {
            if(E.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
            return Begin(G,E);
        }
        virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E) override
        {if(!Captured(E)) return FReply::Unhandled();Select(G,E);return FReply::Handled().PreventThrottling();}
        virtual FReply OnMouseButtonUp(const FGeometry& G,const FPointerEvent& E) override
        {
            if(!Captured(E) || E.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
            Select(G,E);return FReply::Handled().ReleaseMouseCapture();
        }
        virtual FReply OnTouchStarted(const FGeometry& G,const FPointerEvent& E) override {return Begin(G,E);}
        virtual FReply OnTouchMoved(const FGeometry& G,const FPointerEvent& E) override
        {if(!Captured(E)) return FReply::Unhandled();Select(G,E);return FReply::Handled().PreventThrottling();}
        virtual FReply OnTouchEnded(const FGeometry& G,const FPointerEvent& E) override
        {if(!Captured(E)) return FReply::Unhandled();Select(G,E);return FReply::Handled().ReleaseMouseCapture();}
        virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& Event) override
        {
            if(!Owner.IsValid()) return FReply::Unhandled();
            FVector2D Delta=FVector2D::ZeroVector;const FKey Key=Event.GetKey();
            if(Key==EKeys::Left || Key==EKeys::Gamepad_DPad_Left) Delta.X=-1;
            else if(Key==EKeys::Right || Key==EKeys::Gamepad_DPad_Right) Delta.X=1;
            else if(Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Up) Delta.Y=-1;
            else if(Key==EKeys::Down || Key==EKeys::Gamepad_DPad_Down) Delta.Y=1;
            else return FReply::Unhandled();
            const FVector HSV=Owner->GetHSV();const float Step=FMath::Clamp(Owner->KeyboardStep,.002f,.1f);
            FVector2D Point(HSV.Y,1.-HSV.Z);
            if(Owner->Mode==EMCWardrobePaletteMode::Hue) Point=FVector2D(.5,HSV.X+(Delta.X+Delta.Y)*Step);
            else Point+=Delta*Step;
            Owner->SelectNormalizedPoint(Point);return FReply::Handled().PreventThrottling();
        }
    private:
        TWeakObjectPtr<UMCWardrobeColorPalette> Owner;
        bool Captured(const FPointerEvent& Event) const {return HasMouseCaptureByUser(Event.GetUserIndex(),Event.GetPointerIndex());}
        void Select(const FGeometry& G,const FPointerEvent& E)
        {
            if(!Owner.IsValid()) return;
            const FVector2D Size=G.GetLocalSize(),Point=G.AbsoluteToLocal(E.GetScreenSpacePosition());
            Owner->SelectNormalizedPoint(FVector2D(Point.X/FMath::Max(1.,Size.X),Point.Y/FMath::Max(1.,Size.Y)));
        }
        FReply Begin(const FGeometry& G,const FPointerEvent& E)
        {Select(G,E);return FReply::Handled().CaptureMouse(AsShared()).SetUserFocus(AsShared(),EFocusCause::Mouse).PreventThrottling();}
    };

    FLinearColor SafeColor(FLinearColor Color)
    {
        if(!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B))
            return FLinearColor(.24f,.65f,1.f);
        Color=Color.GetClamped(0.f,1.f);Color.A=1.f;return Color;
    }

    UTextBlock* AddLabel(UWidgetTree* Tree,FName Name,const FText& Text)
    {
        auto* Label=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Label->SetText(Text);Label->SetAutoWrapText(true);return Label;
    }
}

void UMCWardrobeColorPalette::SetHSV(FVector HSV)
{
    if(HSV.ContainsNaN()) return;
    SelectedHSV=FVector(FMath::Clamp(HSV.X,0.,1.),FMath::Clamp(HSV.Y,0.,1.),FMath::Clamp(HSV.Z,0.,1.));
    if(PaletteSlate) PaletteSlate->Invalidate(EInvalidateWidgetReason::Paint);
}
void UMCWardrobeColorPalette::SelectNormalizedPoint(FVector2D Point)
{
    if(!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y) || !GetIsEnabled()) return;
    FVector HSV=SelectedHSV;
    if(Mode==EMCWardrobePaletteMode::Hue) HSV.X=FMath::Clamp(Point.Y,0.,1.);
    else {HSV.Y=FMath::Clamp(Point.X,0.,1.);HSV.Z=1.-FMath::Clamp(Point.Y,0.,1.);}
    if(HSV.Equals(SelectedHSV,1.e-7)) return;
    SetHSV(HSV);OnHSVChanged.Broadcast(SelectedHSV);
}
TSharedRef<SWidget> UMCWardrobeColorPalette::RebuildWidget()
{
    PaletteSlate=SNew(MCHubWardrobeWidgetPrivate::SPalette).Owner(this);return PaletteSlate.ToSharedRef();
}
void UMCWardrobeColorPalette::SynchronizeProperties()
{Super::SynchronizeProperties();if(PaletteSlate) PaletteSlate->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);}
void UMCWardrobeColorPalette::ReleaseSlateResources(bool bReleaseChildren)
{Super::ReleaseSlateResources(bReleaseChildren);PaletteSlate.Reset();}

void UMCHubWardrobeWidget::Configure(FLinearColor Color)
{
    DraftColor=MCHubWardrobeWidgetPrivate::SafeColor(Color);
    const FLinearColor HSV=DraftColor.LinearRGBToHSV();DraftHSV=FVector(HSV.R/360.f,HSV.G,HSV.B);
    BuildPresentationTree();RefreshValues();
}

void UMCHubWardrobeWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {ResolveControls();return;}
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("WardrobeScale"));
    Scale->SetStretch(EStretch::ScaleToFit);WidgetTree->RootWidget=Scale;
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeDesignSize"));
    Size->SetWidthOverride(1280);Size->SetHeightOverride(720);Scale->SetContent(Size);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("WardrobeRoot"));Size->SetContent(Root);
    WardrobePanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("WardrobePanel"));
    WardrobePanel->SetPadding(FMargin(24,20));
    auto* PanelSlot=Root->AddChildToCanvas(WardrobePanel);
    PanelSlot->SetAnchors(FAnchors(1.f,.5f));PanelSlot->SetAlignment(FVector2D(1.f,.5f));
    PanelSlot->SetPosition(FVector2D(-36,0));PanelSlot->SetSize(FVector2D(460,648));
    auto* Content=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WardrobeContent"));
    WardrobePanel->SetContent(Content);
    auto AddText=[this,Content](FName Name,FName Key,const FText& Copy,FMargin RowPadding=FMargin(0)) {
        auto* Label=MCHubWardrobeWidgetPrivate::AddLabel(WidgetTree,Name,Copy);
        Content->AddChildToVerticalBox(Label)->SetPadding(RowPadding);TextBindings.Add(Name,Key);return Label;
    };
    AddText(TEXT("WardrobeTitle"),TEXT("UI.Wardrobe.Title"),LOCTEXT("Title","ГАРДЕРОБ"));
    AddText(TEXT("WardrobeSubtitle"),TEXT("UI.Wardrobe.Subtitle"),LOCTEXT("Subtitle","Внешний вид"),FMargin(0,0,0,10));
    AddText(TEXT("WardrobeSkinHeading"),TEXT("UI.Wardrobe.Skin"),LOCTEXT("Skin","Скин"),FMargin(0,0,0,3));
    auto* Skins=WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(),TEXT("WardrobeSkinSlots"));
    Skins->SetMinDesiredSlotWidth(130);Skins->SetMinDesiredSlotHeight(40);Skins->SetSlotPadding(FMargin(3));
    Content->AddChildToVerticalBox(Skins)->SetPadding(FMargin(-3,0,-3,12));
    for(int32 Index=0;Index<6;++Index) {
        const FName CardName=Index==0?FName(TEXT("WardrobeDefaultSlot")):FName(*FString::Printf(TEXT("WardrobeEmptySlot%d"),Index));
        auto* Card=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),CardName);Card->SetPadding(FMargin(8,5));
        Card->SetIsEnabled(Index==0);
        const FName LabelName=Index==0?FName(TEXT("WardrobeDefaultLabel")):FName(*FString::Printf(TEXT("WardrobeEmptyLabel%d"),Index));
        auto* Label=MCHubWardrobeWidgetPrivate::AddLabel(WidgetTree,LabelName,
            Index==0?LOCTEXT("DefaultSkin","Стандартный"):LOCTEXT("EmptySlot","Пусто"));
        Label->SetJustification(ETextJustify::Center);Card->SetContent(Label);
        Card->SetVerticalAlignment(VAlign_Center);
        auto* GridSlot=Skins->AddChildToUniformGrid(Card,Index/3,Index%3);
        GridSlot->SetHorizontalAlignment(HAlign_Fill);GridSlot->SetVerticalAlignment(VAlign_Fill);
        TextBindings.Add(LabelName,Index==0?FName(TEXT("UI.Wardrobe.DefaultSkin")):FName(TEXT("UI.Wardrobe.EmptySlot")));
    }
    auto* ColorControls=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WardrobeColorControls"));
    Content->AddChildToVerticalBox(ColorControls);
    auto* ColorHeading=MCHubWardrobeWidgetPrivate::AddLabel(WidgetTree,TEXT("WardrobeColorHeading"),LOCTEXT("BagAndBelt","Цвет сумки и пояса"));
    TextBindings.Add(TEXT("WardrobeColorHeading"),TEXT("UI.Wardrobe.BagAndBelt"));
    ColorControls->AddChildToVerticalBox(ColorHeading)->SetPadding(FMargin(0,0,0,6));
    WardrobeColorSwatch=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("WardrobeColorSwatch"));
    auto* SwatchSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeSwatchSize"));
    SwatchSize->SetHeightOverride(14);SwatchSize->SetContent(WardrobeColorSwatch);
    ColorControls->AddChildToVerticalBox(SwatchSize)->SetPadding(FMargin(0,0,0,8));
    auto* PaletteRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("WardrobePaletteRow"));
    auto* PaletteRowSlot=ColorControls->AddChildToVerticalBox(PaletteRow);
    PaletteRowSlot->SetHorizontalAlignment(HAlign_Center);PaletteRowSlot->SetPadding(FMargin(0,0,0,8));
    WardrobeColorPalette=WidgetTree->ConstructWidget<UMCWardrobeColorPalette>(UMCWardrobeColorPalette::StaticClass(),TEXT("WardrobeColorPalette"));
    WardrobeColorPalette->DesiredSize=FVector2D(200,200);
    auto* PaletteSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobePaletteSize"));
    PaletteSize->SetWidthOverride(200);PaletteSize->SetHeightOverride(200);PaletteSize->SetContent(WardrobeColorPalette);
    PaletteRow->AddChildToHorizontalBox(PaletteSize)->SetPadding(FMargin(0,0,12,0));
    WardrobeHueStrip=WidgetTree->ConstructWidget<UMCWardrobeColorPalette>(UMCWardrobeColorPalette::StaticClass(),TEXT("WardrobeHueStrip"));
    WardrobeHueStrip->Mode=EMCWardrobePaletteMode::Hue;WardrobeHueStrip->DesiredSize=FVector2D(28,200);
    auto* HueSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeHueSize"));
    HueSize->SetWidthOverride(28);HueSize->SetHeightOverride(200);HueSize->SetContent(WardrobeHueStrip);PaletteRow->AddChildToHorizontalBox(HueSize);
    auto* Hint=MCHubWardrobeWidgetPrivate::AddLabel(WidgetTree,TEXT("WardrobeHint"),
        LOCTEXT("PaletteHint","Выбери оттенок на полоске и цвет на палитре. Изменяется только внешний вид."));
    TextBindings.Add(TEXT("WardrobeHint"),TEXT("UI.Wardrobe.PaletteHint"));
    ColorControls->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0,0,0,8));
    auto* FooterSpace=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeFooterSpace"));
    FooterSpace->SetMinDesiredHeight(8);
    Content->AddChildToVerticalBox(FooterSpace)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto AddButton=[this,Content](FName Name,FName LabelName,FName Key,const FText& Copy,float Height,FMargin RowPadding) {
        auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
        auto* Label=MCHubWardrobeWidgetPrivate::AddLabel(WidgetTree,LabelName,Copy);Label->SetJustification(ETextJustify::Center);
        Button->SetContent(Label);TextBindings.Add(LabelName,Key);
        if(auto* Slot=Cast<UButtonSlot>(Button->GetContentSlot())) {Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Center);}
        auto* HeightBox=WidgetTree->ConstructWidget<USizeBox>();HeightBox->SetHeightOverride(Height);HeightBox->SetContent(Button);
        Content->AddChildToVerticalBox(HeightBox)->SetPadding(RowPadding);return Button;
    };
    WardrobeApply=AddButton(TEXT("WardrobeApply"),TEXT("WardrobeApplyLabel"),TEXT("UI.Wardrobe.Apply"),LOCTEXT("Apply","ПРИМЕНИТЬ"),44,FMargin(0,0,0,8));
    WardrobeCancel=AddButton(TEXT("WardrobeCancel"),TEXT("WardrobeCancelLabel"),TEXT("UI.Wardrobe.Cancel"),LOCTEXT("Cancel","Назад"),38,FMargin(0));
    ResolveControls();ApplyTheme();
}

void UMCHubWardrobeWidget::ResolveControls()
{
    if(!WidgetTree) return;
    WardrobePanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("WardrobePanel")));
    WardrobeColorSwatch=Cast<UBorder>(WidgetTree->FindWidget(TEXT("WardrobeColorSwatch")));
    WardrobeColorPalette=Cast<UMCWardrobeColorPalette>(WidgetTree->FindWidget(TEXT("WardrobeColorPalette")));
    WardrobeHueStrip=Cast<UMCWardrobeColorPalette>(WidgetTree->FindWidget(TEXT("WardrobeHueStrip")));
    WardrobeApply=Cast<UButton>(WidgetTree->FindWidget(TEXT("WardrobeApply")));
    WardrobeCancel=Cast<UButton>(WidgetTree->FindWidget(TEXT("WardrobeCancel")));
}

void UMCHubWardrobeWidget::BindControls()
{
    if(WardrobeColorPalette) WardrobeColorPalette->OnHSVChanged.AddUniqueDynamic(this,&ThisClass::PaletteChanged);
    if(WardrobeHueStrip) WardrobeHueStrip->OnHSVChanged.AddUniqueDynamic(this,&ThisClass::PaletteChanged);
    if(WardrobeApply) WardrobeApply->OnClicked.AddUniqueDynamic(this,&ThisClass::ApplyClicked);
    if(WardrobeCancel) WardrobeCancel->OnClicked.AddUniqueDynamic(this,&ThisClass::CancelClicked);
}

void UMCHubWardrobeWidget::UnbindControls()
{
    if(WardrobeColorPalette) WardrobeColorPalette->OnHSVChanged.RemoveAll(this);
    if(WardrobeHueStrip) WardrobeHueStrip->OnHSVChanged.RemoveAll(this);
    if(WardrobeApply) WardrobeApply->OnClicked.RemoveAll(this);
    if(WardrobeCancel) WardrobeCancel->OnClicked.RemoveAll(this);
}

void UMCHubWardrobeWidget::RefreshValues()
{
    TGuardValue<bool> Guard(bSynchronizing,true);
    if(WardrobeColorPalette) WardrobeColorPalette->SetHSV(DraftHSV);
    if(WardrobeHueStrip) WardrobeHueStrip->SetHSV(DraftHSV);
    if(const auto* Style=GetResolvedTheme()) MCUIVisualStyle::Panel(WardrobeColorSwatch,Style,DraftColor);
}

void UMCHubWardrobeWidget::PaletteChanged(FVector HSV)
{
    if(bSynchronizing || HSV.ContainsNaN()) return;
    DraftHSV=HSV;DraftColor=FLinearColor(float(HSV.X)*360.f,float(HSV.Y),float(HSV.Z),1).HSVToLinearRGB();
    RefreshValues();
    if(!IsPreviewing()) OnDraftChanged.Broadcast(DraftColor);
}

void UMCHubWardrobeWidget::ApplyClicked() {if(!IsPreviewing()) OnApply.Broadcast(DraftColor);}
void UMCHubWardrobeWidget::CancelClicked() {if(!IsPreviewing()) OnCancel.Broadcast();}

void UMCHubWardrobeWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(true);
}
void UMCHubWardrobeWidget::NativeConstruct()
{
    Super::NativeConstruct();ResolveControls();BindControls();RefreshValues();
}
void UMCHubWardrobeWidget::NativeDestruct()
{
    UnbindControls();Super::NativeDestruct();
}
FReply UMCHubWardrobeWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if((Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) && !Event.IsRepeat()) {
        CancelClicked();return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

void UMCHubWardrobeWidget::ApplyTheme()
{
    ResolveControls();Super::ApplyTheme();
    const auto* Style=GetResolvedTheme();if(!Style || !WidgetTree) return;
    const auto& Palette=Style->Palette;
    MCUIVisualStyle::Panel(WardrobePanel,Style,Palette.Backdrop.CopyWithNewOpacity(.94f));
    auto Text=[this,Style](FName Name,int32 Size,FLinearColor Color,bool Bold=true) {
        if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(Name))) {
            Label->SetFont(Style->MakeGameplayFont(Size,Bold));Label->SetColorAndOpacity(Color);
        }
    };
    Text(TEXT("WardrobeTitle"),40,Palette.Cream);
    Text(TEXT("WardrobeSubtitle"),16,Palette.OnMarine,false);
    Text(TEXT("WardrobeSkinHeading"),18,Palette.Cream);
    Text(TEXT("WardrobeColorHeading"),18,Palette.Cream);
    Text(TEXT("WardrobeHint"),12,Palette.OnMarine.CopyWithNewOpacity(.75f),false);
    for(int32 Index=0;Index<6;++Index) {
        const FName Name=Index==0?FName(TEXT("WardrobeDefaultSlot")):FName(*FString::Printf(TEXT("WardrobeEmptySlot%d"),Index));
        if(auto* Card=Cast<UBorder>(WidgetTree->FindWidget(Name))) {
            MCUIVisualStyle::Panel(Card,Style,Index==0?Palette.Mint:Palette.Marine.CopyWithNewOpacity(.5f));
            Card->SetIsEnabled(Index==0);
        }
        Text(Index==0?FName(TEXT("WardrobeDefaultLabel")):FName(*FString::Printf(TEXT("WardrobeEmptyLabel%d"),Index)),
            14,Index==0?Palette.ButtonText:Palette.OnMarine,false);
    }
    MCUIVisualStyle::Button(WardrobeApply,Style,Palette.Mint);
    MCUIVisualStyle::Button(WardrobeCancel,Style,Palette.Marine);
    Text(TEXT("WardrobeApplyLabel"),19,Palette.ButtonText);
    Text(TEXT("WardrobeCancelLabel"),17,Palette.OnMarine);
    RefreshValues();
}

void UMCHubWardrobeWidget::ApplyEditorPreview(float Time)
{
    bEditorFixture=true;Super::ApplyEditorPreview(Time);
    Configure(PreviewBagColor);ApplyTheme();
}
void UMCHubWardrobeWidget::ResetEditorPreview()
{
    Super::ResetEditorPreview();
}

#undef LOCTEXT_NAMESPACE
