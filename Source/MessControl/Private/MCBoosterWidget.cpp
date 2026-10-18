#include "MCBoosterWidget.h"

#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"

#define LOCTEXT_NAMESPACE "MCBoosterWidget"

int32 UMCBoosterFallbackIcon::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
    const int32 Top = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled);
    const FVector2D Size = Geometry.GetLocalSize();
    const float Scale = FMath::Min(Size.X, Size.Y) / 48.f;
    const FVector2D Origin = (Size - FVector2D(48,48)*Scale)*.5f;
    const FLinearColor Color = Tint * Style.GetColorAndOpacityTint();
    const FLinearColor Fill = FillTint * Style.GetColorAndOpacityTint();
    auto Box=[&](FVector2D At,FVector2D Extent,float Radius,FLinearColor InFill) {
        const FSlateRoundedBoxBrush Brush(InFill,Radius*Scale,Color,LineWidth);
        FSlateDrawElement::MakeBox(Elements,Top+1,Geometry.ToPaintGeometry(Extent*Scale,FSlateLayoutTransform(Origin+At*Scale)),&Brush);
    };
    auto Line=[&](TArray<FVector2D> Points) {
        for(auto& Point:Points) Point=Origin+Point*Scale;
        FSlateDrawElement::MakeLines(Elements,Top+2,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,LineWidth);
    };
    if(Glyph==EMCBoosterGlyph::Recovery) {
        // Energy/wave, deliberately no medical cross.
        Line({{4,28},{12,28},{18,12},{24,36},{31,19},{36,28},{44,28}});
        Line({{10,38},{38,38}});
    } else if(Glyph==EMCBoosterGlyph::Armed) {
        Box({10,16},{28,27},13,Fill);Box({18,11},{12,8},2,Fill);
        Line({{24,11},{24,7},{32,5},{35,9}});Line({{39,4},{43,1}});Line({{41,10},{46,11}});
        Line({{20,23},{20,34}});Line({{28,23},{28,34}});
    } else {
        Box({11,15},{26,28},7,Fill);Box({17,5},{14,11},3,Fill);
        Line({{14,31},{34,31}});Line({{24,20},{19,27},{28,27},{23,35}});
    }
    return Top + 2;
}

void UMCBoosterWidget::CacheDataTable()
{
    const auto* Style = GetResolvedTheme();
    CachedBoosterTable = BoosterTable;
    if (!CachedBoosterTable && Style && !Style->BoosterTable.IsNull()) CachedBoosterTable = Style->BoosterTable.LoadSynchronous();
    if (!CachedBoosterTable) CachedBoosterTable = UMCUIDataLibrary::GetDefaultBoosterTable();
    CachedLocalizationTable = Style && !Style->LocalizationTable.IsNull() ? Style->LocalizationTable.LoadSynchronous() : nullptr;
    if (!CachedLocalizationTable) CachedLocalizationTable = UMCUIDataLibrary::GetDefaultLocalizationTable();
}

void UMCBoosterWidget::BindPresentationTree()
{
    MainPanel = Cast<UBorder>(WidgetTree->FindWidget(TEXT("BoosterMainPanel")));
    Keycap = Cast<UBorder>(WidgetTree->FindWidget(TEXT("BoosterKeycap")));
    Illustration = Cast<UImage>(WidgetTree->FindWidget(TEXT("BoosterImage")));
    FallbackIcon = Cast<UMCBoosterFallbackIcon>(WidgetTree->FindWidget(TEXT("BoosterFallbackIcon")));
    NameLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("BoosterName")));
    KeyLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("BoosterKey")));
    CountLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("BoosterQueueCount")));
    FuryPanel = Cast<UBorder>(WidgetTree->FindWidget(TEXT("BoosterFuryPanel")));
    FuryLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("BoosterFuryText")));
    ArmedPanel = Cast<UBorder>(WidgetTree->FindWidget(TEXT("BoosterArmedPanel")));
    ArmedLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("BoosterArmedText")));
    DetailsPanel = Cast<UBorder>(WidgetTree->FindWidget(TEXT("BoosterDetailsPanel")));
    DescriptionLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("BoosterDescription")));
    DetailsScroll = Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("BoosterDetailsScroll")));
    for(UTextBlock* Text:{NameLabel.Get(),CountLabel.Get(),FuryLabel.Get(),ArmedLabel.Get(),DescriptionLabel.Get()})
        if(Text) {Text->SetAutoWrapText(true);Text->SetWrapTextAt(0);Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
    BindStyle(TEXT("BoosterMainPanel"),EMCUIColorToken::Surface,EMCUIStyleTarget::Background);
    BindStyle(TEXT("BoosterQueuePanel"),EMCUIColorToken::LCDBackground,EMCUIStyleTarget::Background);
    BindStyle(TEXT("BoosterFuryPanel"),EMCUIColorToken::LCDBackground,EMCUIStyleTarget::Background);
    BindStyle(TEXT("BoosterArmedPanel"),EMCUIColorToken::Ink,EMCUIStyleTarget::Background);
    BindStyle(TEXT("BoosterDetailsPanel"),EMCUIColorToken::SurfaceAlt,EMCUIStyleTarget::Background);
    BindStyle(TEXT("BoosterName"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,15,true);
    BindStyle(TEXT("BoosterQueueCount"),EMCUIColorToken::LCDInk,EMCUIStyleTarget::Text,13,true);
    BindStyle(TEXT("BoosterFuryText"),EMCUIColorToken::LCDInk,EMCUIStyleTarget::Text,13,true);
    BindStyle(TEXT("BoosterArmedText"),EMCUIColorToken::Cost,EMCUIStyleTarget::Text,13,true);
    BindStyle(TEXT("BoosterDescription"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,14,false);
    NormalizePresentationLayout();
    BoundRoot = WidgetTree->RootWidget;
}

void UMCBoosterWidget::NormalizePresentationLayout()
{
    if(!WidgetTree) return;
    auto* Root=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("BoosterRoot")));
    auto* Stack=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("BoosterStack")));
    if(Root && Stack) {
        Root->SetWidthOverride(304);
        auto* Compact=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("BoosterCompactSize")));
        if(!Compact && Root->GetContent()==Stack) {
            // WidthOverride is only a desired size. The outer preview can allot
            // 640px; a left-aligned 304px inner box keeps the real HUD compact.
            Compact=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("BoosterCompactSize"));
            Root->SetContent(Compact);Compact->SetContent(Stack);
        }
        if(Compact) {
            Compact->SetWidthOverride(304);
            if(auto* LayoutSlot=Cast<USizeBoxSlot>(Compact->Slot)) {LayoutSlot->SetHorizontalAlignment(HAlign_Left);LayoutSlot->SetVerticalAlignment(VAlign_Top);}
        }
    }
    if(MainPanel) MainPanel->SetPadding(FMargin(9,7));
    if(auto* Size=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("BoosterIconSize")))) {Size->SetWidthOverride(46);Size->SetHeightOverride(46);}
    if(auto* Column=WidgetTree->FindWidget(TEXT("BoosterTextColumn"))) if(auto* LayoutSlot=Cast<UHorizontalBoxSlot>(Column->Slot)) LayoutSlot->SetPadding(FMargin(8,0,7,0));
    if(auto* Queue=Cast<UBorder>(WidgetTree->FindWidget(TEXT("BoosterQueuePanel")))) Queue->SetPadding(FMargin(6,3));
    if(Keycap) Keycap->SetPadding(FMargin(7,5));
    for(UBorder* Panel:{FuryPanel.Get(),ArmedPanel.Get()}) if(Panel) {
        Panel->SetPadding(FMargin(9,6));
        if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Panel->Slot)) LayoutSlot->SetPadding(FMargin(0,4,0,0));
    }
    for(const TCHAR* Name:{TEXT("BoosterFuryIconSize"),TEXT("BoosterArmedIconSize")})
        if(auto* Size=Cast<USizeBox>(WidgetTree->FindWidget(Name))) {Size->SetWidthOverride(26);Size->SetHeightOverride(26);}
    if(DetailsScroll) {DetailsScroll->SetAllowOverscroll(false);DetailsScroll->SetScrollBarVisibility(ESlateVisibility::Visible);DetailsScroll->SetAlwaysShowScrollbar(false);}
}

void UMCBoosterWidget::BuildPresentationTree()
{
    if (!WidgetTree || (BoundRoot.IsValid() && BoundRoot.Get() == WidgetTree->RootWidget)) return;
    if (WidgetTree->RootWidget)
    {
        // An artist-authored tree is retained; only its stable named controls are bound.
        BindPresentationTree();
        CacheDataTable();
        return;
    }

    auto* Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BoosterRoot"));
    Root->SetWidthOverride(304);
    WidgetTree->RootWidget = Root;
    auto* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BoosterStack"));
    Root->SetContent(Stack);
    MainPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BoosterMainPanel"));
    MainPanel->SetPadding(FMargin(12,9));
    Stack->AddChildToVerticalBox(MainPanel);
    auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BoosterContent"));
    MainPanel->SetContent(Row);

    auto* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BoosterIconSize"));
    IconSize->SetWidthOverride(54); IconSize->SetHeightOverride(54);
    Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
    auto* Layers = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("BoosterIconLayers"));
    IconSize->SetContent(Layers);
    FallbackIcon = WidgetTree->ConstructWidget<UMCBoosterFallbackIcon>(UMCBoosterFallbackIcon::StaticClass(), TEXT("BoosterFallbackIcon"));
    if (FallbackIcon) Layers->AddChildToOverlay(FallbackIcon);
    auto* ImageScale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("BoosterImageScale"));
    ImageScale->SetStretch(EStretch::ScaleToFit);
    Layers->AddChildToOverlay(ImageScale);
    Illustration = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BoosterImage"));
    ImageScale->SetContent(Illustration);

    auto* TextColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BoosterTextColumn"));
    auto* TextSlot = Row->AddChildToHorizontalBox(TextColumn);
    TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TextSlot->SetPadding(FMargin(10,0,8,0));
    TextSlot->SetVerticalAlignment(VAlign_Center);
    NameLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BoosterName"));
    NameLabel->SetAutoWrapText(true);
    TextColumn->AddChildToVerticalBox(NameLabel);
    CountLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BoosterQueueCount"));
    CountLabel->SetAutoWrapText(true);
    auto* QueuePanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("BoosterQueuePanel"));QueuePanel->SetPadding(FMargin(6,3));QueuePanel->SetContent(CountLabel);
    TextColumn->AddChildToVerticalBox(QueuePanel)->SetPadding(FMargin(0,5,0,0));

    auto* KeySize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BoosterKeySize"));
    KeySize->SetMinDesiredWidth(30); KeySize->SetMinDesiredHeight(30);
    Row->AddChildToHorizontalBox(KeySize)->SetVerticalAlignment(VAlign_Center);
    Keycap = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BoosterKeycap"));
    Keycap->SetPadding(FMargin(7,4)); KeySize->SetContent(Keycap);
    KeyLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BoosterKey"));
    KeyLabel->SetJustification(ETextJustify::Center); Keycap->SetContent(KeyLabel);

    auto AddStrip = [&](const TCHAR* PanelName, const TCHAR* LabelName, const TCHAR* GlyphName, EMCBoosterGlyph Glyph, TObjectPtr<UBorder>& Panel, TObjectPtr<UTextBlock>& Label)
    {
        Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), PanelName);
        Panel->SetPadding(FMargin(12,7));
        Stack->AddChildToVerticalBox(Panel)->SetPadding(FMargin(0,5,0,0));
        auto* StripRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),FName(*(FString(PanelName)+TEXT("Content"))));Panel->SetContent(StripRow);
        auto* GlyphSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(FString(GlyphName)+TEXT("Size"))));GlyphSize->SetWidthOverride(30);GlyphSize->SetHeightOverride(30);
        auto* Icon=WidgetTree->ConstructWidget<UMCBoosterFallbackIcon>(UMCBoosterFallbackIcon::StaticClass(),GlyphName);Icon->Glyph=Glyph;GlyphSize->SetContent(Icon);
        auto* GlyphSlot=StripRow->AddChildToHorizontalBox(GlyphSize);GlyphSlot->SetVerticalAlignment(VAlign_Center);GlyphSlot->SetPadding(FMargin(0,0,9,0));
        Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
        Label->SetAutoWrapText(true);auto* LabelSlot=StripRow->AddChildToHorizontalBox(Label);LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LabelSlot->SetVerticalAlignment(VAlign_Center);
    };
    AddStrip(TEXT("BoosterFuryPanel"), TEXT("BoosterFuryText"),TEXT("BoosterFuryIcon"),EMCBoosterGlyph::Recovery,FuryPanel,FuryLabel);
    AddStrip(TEXT("BoosterArmedPanel"), TEXT("BoosterArmedText"),TEXT("BoosterArmedIcon"),EMCBoosterGlyph::Armed,ArmedPanel,ArmedLabel);
    DetailsPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("BoosterDetailsPanel"));DetailsPanel->SetPadding(FMargin(12,9));
    Stack->AddChildToVerticalBox(DetailsPanel)->SetPadding(FMargin(0,5,0,0));
    auto* DetailsSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("BoosterDetailsSize"));DetailsSize->SetHeightOverride(176);DetailsPanel->SetContent(DetailsSize);
    DetailsScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("BoosterDetailsScroll"));DetailsScroll->SetAllowOverscroll(false);DetailsScroll->SetScrollbarThickness(FVector2D(5,5));DetailsSize->SetContent(DetailsScroll);
    DescriptionLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("BoosterDescription"));DescriptionLabel->SetAutoWrapText(true);DetailsScroll->AddChild(DescriptionLabel);
    BindPresentationTree();
    CacheDataTable();
    ShowLoadedImage(nullptr);
    ApplyTheme();
}

FString UMCBoosterWidget::DisplayCulture() const
{
    return bApplyingPreview || IsDesignTime() ? PreviewCulture : FString();
}

FText UMCBoosterWidget::Copy(FName Key, const FText& Fallback) const
{
    return UMCUIDataLibrary::ResolveText(CachedLocalizationTable, Key, Fallback, DisplayCulture());
}

void UMCBoosterWidget::ApplyPresentation(const FMCUIBoosterViewData& Data)
{
    BuildPresentationTree();
    if (BoosterTable && CachedBoosterTable != BoosterTable) CachedBoosterTable = BoosterTable;
    Presentation = Data;
    Presentation.QueueCount = FMath::Max(0, Presentation.QueueCount);
    const auto Content = UMCUIDataLibrary::FindBoosterForGameplayID(CachedBoosterTable, Data.GameplayPerkID, DisplayCulture());
    RefreshPresentation(Content, false);
    ApplyTheme();
}

void UMCBoosterWidget::RefreshPresentation(const FMCUIResolvedContent& Content, bool bShowDetails)
{
    const bool HasQueue = Presentation.QueueCount > 0;
    const bool HasAnything = HasQueue || Presentation.bFuryActive || Presentation.bGrenadeArmed;
    SetVisibility(HasAnything ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (MainPanel)
    {
        MainPanel->SetVisibility(HasQueue ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        MainPanel->SetToolTipText(Content.Description);
    }
    if (NameLabel) NameLabel->SetText(Content.DisplayName.IsEmpty()
        ? Copy(TEXT("UI.Booster.GenericName"), LOCTEXT("GenericName", "Бустер")) : Content.DisplayName);
    if (KeyLabel) KeyLabel->SetText(Presentation.ActivationInput);
    const bool Blocked = Presentation.bActivationBlocked || !HasQueue;
    if (Keycap)
    {
        // A key indicator, deliberately not a mouse button or a client-side activation command.
        Keycap->SetIsEnabled(!Blocked);
        Keycap->SetToolTipText(Blocked ? Copy(TEXT("UI.Booster.ActivationBlocked"), LOCTEXT("ActivationBlocked", "Сейчас недоступно"))
            : UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.Booster.Activate"), LOCTEXT("Activate", "{BoosterAction} · Использовать")),
                FFormatNamedArguments{{TEXT("BoosterAction"), Presentation.ActivationInput}}, DisplayCulture()));
    }
    if (CountLabel) CountLabel->SetText(UMCUIDataLibrary::FormatTextForCulture(
        Copy(TEXT("UI.Booster.Charges"), LOCTEXT("Charges", "Зарядов: {Count}")),
        FFormatNamedArguments{{TEXT("Count"), Presentation.QueueCount}}, DisplayCulture()));
    if (FuryPanel) FuryPanel->SetVisibility(Presentation.bFuryActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (FuryLabel) FuryLabel->SetText(Copy(TEXT("UI.Booster.FuryActive"), LOCTEXT("FuryActive", "Щёточный вихрь · Несёт вперёд")));
    if (ArmedPanel) ArmedPanel->SetVisibility(Presentation.bGrenadeArmed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (ArmedLabel) ArmedLabel->SetText(Copy(TEXT("UI.Booster.GrenadeArmed"), LOCTEXT("GrenadeArmed", "Граната взорвётся при падении")));
    if (DetailsPanel) DetailsPanel->SetVisibility(bShowDetails && HasQueue ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (DescriptionLabel) DescriptionLabel->SetText(Content.Description);
    UpdateImage(Content.Image);
}

void UMCBoosterWidget::ShowLoadedImage(UTexture2D* Texture)
{
    if (Illustration)
    {
        if (Illustration->GetBrush().GetResourceObject() != Texture) Illustration->SetBrushFromTexture(Texture, true);
        Illustration->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (FallbackIcon) FallbackIcon->SetVisibility(Texture ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UMCBoosterWidget::UpdateImage(const TSoftObjectPtr<UTexture2D>& Image)
{
    if(bApplyingPreview || IsDesignTime())
    {
        if(ImageLoadHandle.IsValid()) {ImageLoadHandle->CancelHandle();ImageLoadHandle.Reset();}
        RequestedImage=Image;bImageRequestFinished=true;
        ShowLoadedImage(Image.IsNull()?nullptr:Image.LoadSynchronous());
        return;
    }
    if (RequestedImage == Image)
    {
        if (UTexture2D* Loaded = Image.Get()) { ShowLoadedImage(Loaded); return; }
        if (ImageLoadHandle.IsValid() || bImageRequestFinished) return;
    }
    if (ImageLoadHandle.IsValid()) { ImageLoadHandle->CancelHandle(); ImageLoadHandle.Reset(); }
    RequestedImage = Image;
    bImageRequestFinished = Image.IsNull();
    ShowLoadedImage(Image.Get());
    if (Image.IsNull() || Image.Get()) return;
    ImageLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Image.ToSoftObjectPath(),
        FStreamableDelegate::CreateWeakLambda(this, [this, Image]()
        {
            if (RequestedImage != Image) return;
            bImageRequestFinished = true;
            ShowLoadedImage(Image.Get());
            ImageLoadHandle.Reset();
        }));
    if (!ImageLoadHandle.IsValid()) bImageRequestFinished = true;
}

void UMCBoosterWidget::ApplyTheme()
{
    const auto* Style = GetResolvedTheme();
    if (!Style) { Super::ApplyTheme(); return; }
    const auto& P = Style->Palette;
    const bool Blocked = Presentation.bActivationBlocked || Presentation.QueueCount <= 0;
    NormalizePresentationLayout();
    Super::ApplyTheme();
    MCUIVisualStyle::Panel(Keycap,Style,Blocked?P.SurfaceAlt:P.Cream);
    if(KeyLabel) {KeyLabel->SetFont(Style->MakeGameplayFont(14));KeyLabel->SetColorAndOpacity(Blocked?P.Muted:P.Ink);}
    if (FallbackIcon) { FallbackIcon->Tint = P.Marine;FallbackIcon->FillTint=P.Cream; FallbackIcon->InvalidateLayoutAndVolatility(); }
    if(WidgetTree) {
        if(auto* Icon=Cast<UMCBoosterFallbackIcon>(WidgetTree->FindWidget(TEXT("BoosterFuryIcon")))) {Icon->Tint=P.LCDInk;Icon->FillTint=P.Mint;Icon->InvalidateLayoutAndVolatility();}
        if(auto* Icon=Cast<UMCBoosterFallbackIcon>(WidgetTree->FindWidget(TEXT("BoosterArmedIcon")))) {Icon->Tint=P.Cost;Icon->FillTint=P.Ink;Icon->InvalidateLayoutAndVolatility();}
    }
    if (Illustration) Illustration->SetColorAndOpacity(FLinearColor::White);
}

void UMCBoosterWidget::ApplyEditorPreview(float Time)
{
    BuildPresentationTree();
    CacheDataTable();
    TGuardValue<bool> PreviewGuard(bApplyingPreview,true);
    if(bHasPreviewFixture && LastPreviewState==PreviewState && LastPreviewCulture==PreviewCulture
        && LastPreviewScenario==PreviewScenario && LastPreviewRow==PreviewBoosterRow && LastPreviewQueueCount==PreviewQueueCount
        && LastPreviewTable.Get()==CachedBoosterTable && LastPreviewLocalizationTable.Get()==CachedLocalizationTable) {
        SetRenderOpacity(Time<=0.f?1.f:FMath::Clamp(Time*6.f,0.f,1.f));return;
    }
    FMCUIBoosterViewData Data;
    Data.QueueCount = FMath::Max(0, PreviewQueueCount);
    FMCUIResolvedContent Content;
    if (PreviewBoosterRow.DataTable || !PreviewBoosterRow.RowName.IsNone())
    {
        Content = UMCUIDataLibrary::ResolveBooster(PreviewBoosterRow, PreviewCulture);
        if (PreviewBoosterRow.DataTable && PreviewBoosterRow.DataTable->GetRowStruct() == FMCUIBoosterRow::StaticStruct())
            if (const auto* Row = PreviewBoosterRow.DataTable->FindRow<FMCUIBoosterRow>(PreviewBoosterRow.RowName, TEXT("Booster editor preview"), false)) Data.GameplayPerkID = Row->GameplayPerkID;
    }
    else
    {
        Data.GameplayPerkID = TEXT("Fallback_Mouthwash");
        Content = UMCUIDataLibrary::FindBoosterForGameplayID(CachedBoosterTable, Data.GameplayPerkID, PreviewCulture);
    }
    if (PreviewScenario == EMCGameplayUIPreviewScenario::ActiveEffect || PreviewState == TEXT("ActiveEffect"))
    {
        Data.QueueCount = 0; Data.bFuryActive = true; Data.bActivationBlocked = true;
    }
    if (PreviewScenario == EMCGameplayUIPreviewScenario::Armed || PreviewState == TEXT("Armed"))
    {
        Data.QueueCount = 0; Data.bGrenadeArmed = true; Data.bActivationBlocked = true;
    }
    if(PreviewState==TEXT("AllStates")) {Data.QueueCount=FMath::Max(1,Data.QueueCount);Data.bFuryActive=true;Data.bGrenadeArmed=true;}
    if (PreviewScenario == EMCGameplayUIPreviewScenario::Disabled || PreviewState == TEXT("Disabled")) Data.bActivationBlocked = true;
    if (PreviewScenario == EMCGameplayUIPreviewScenario::Empty || PreviewState == TEXT("Empty")) Data = FMCUIBoosterViewData();
    const bool ShowDetails = PreviewState == TEXT("Details") || PreviewState == TEXT("LongText") || PreviewScenario == EMCGameplayUIPreviewScenario::LongText;
    Presentation = Data;
    bPreviewRowFound = Content.bFound;
    bPreviewUsedFallback = Content.bUsedFallback;
    RefreshPresentation(Content, ShowDetails);
    ApplyTheme();
    LastPreviewState=PreviewState;LastPreviewCulture=PreviewCulture;LastPreviewScenario=PreviewScenario;
    LastPreviewRow=PreviewBoosterRow;LastPreviewQueueCount=PreviewQueueCount;LastPreviewTable=CachedBoosterTable;LastPreviewLocalizationTable=CachedLocalizationTable;bHasPreviewFixture=true;
    SetRenderOpacity(Time <= 0.f ? 1.f : FMath::Clamp(Time * 6.f, 0.f, 1.f));
}

void UMCBoosterWidget::ResetEditorPreview()
{
    bHasPreviewFixture=false;
    SetRenderOpacity(1.f);
}

void UMCBoosterWidget::NativeDestruct()
{
    if (ImageLoadHandle.IsValid()) { ImageLoadHandle->CancelHandle(); ImageLoadHandle.Reset(); }
    RequestedImage.Reset();
    bImageRequestFinished = false;
    Super::NativeDestruct();
}

#undef LOCTEXT_NAMESPACE
