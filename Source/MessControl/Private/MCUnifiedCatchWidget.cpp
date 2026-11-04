#include "MCUnifiedCatchWidget.h"

#include "MCFishingRewardArt.h"
#include "MCGameplayUIFactory.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/BreakIterator.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SlateRenderer.h"

#define LOCTEXT_NAMESPACE "MCUnifiedCatch"

namespace MCUnifiedCatchPrivate
{
    FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    FName Part(const TCHAR* Prefix,int32 Index) {return FName(*FString::Printf(TEXT("Catch%s%d"),Prefix,Index));}
    struct Metrics {float Width=0,Word=0,Height=0;};
    Metrics Measure(const FText& Text,const FSlateFontInfo& Font,float Wrap=0)
    {
        Metrics Out;if(Text.IsEmpty() || !FSlateApplication::IsInitialized()) return Out;
        const auto Service=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const float LineHeight=Service->Measure(FText::FromString(TEXT("Ag")),Font).Y;
        auto Breaks=FBreakIterator::CreateLineBreakIterator();TArray<FString> Paragraphs;Text.ToString().ParseIntoArray(Paragraphs,TEXT("\n"),false);
        int32 Lines=0;
        for(const FString& Paragraph:Paragraphs) {
            Out.Width=FMath::Max(Out.Width,float(Service->Measure(FText::FromString(Paragraph),Font).X));
            ++Lines;float LineWidth=0;Breaks->SetStringRef(Paragraph);int32 Start=Breaks->ResetToBeginning();
            for(int32 End=Breaks->MoveToNext();End!=INDEX_NONE;Start=End,End=Breaks->MoveToNext()) {
                const FString Span=Paragraph.Mid(Start,End-Start);
                Out.Word=FMath::Max(Out.Word,float(Service->Measure(FText::FromString(Span.TrimStartAndEnd()),Font).X));
                if(Wrap>0) {
                    const float Width=Service->Measure(FText::FromString(Span),Font).X;
                    if(LineWidth>0 && LineWidth+Width>Wrap) {++Lines;LineWidth=0;}
                    Lines+=FMath::Max(0,FMath::CeilToInt(Width/Wrap)-1);
                    LineWidth=Width>Wrap?FMath::Max(1.f,FMath::Fmod(Width,Wrap)):LineWidth+Width;
                }
            }
        }
        Out.Height=Lines*LineHeight;return Out;
    }
    void Place(UCanvasPanel* Canvas,UWidget* Child,FVector2D Size,int32 Z)
    {
        auto* CardCell=Canvas->AddChildToCanvas(Child);CardCell->SetAnchors(FAnchors(0,0));CardCell->SetAlignment(FVector2D(.5,.5));
        CardCell->SetPosition(FVector2D::ZeroVector);CardCell->SetSize(Size);CardCell->SetZOrder(Z);
        Child->SetRenderTransformPivot(FVector2D(.5,.5));Child->SetVisibility(ESlateVisibility::Collapsed);
    }
}

UMCUnifiedCatchGlyph::UMCUnifiedCatchGlyph() {bUseNativeLayout=false;SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);}
void UMCUnifiedCatchGlyph::BuildPresentationTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("CatchGlyphFit"));Fit->SetStretch(EStretch::ScaleToFit);WidgetTree->RootWidget=Fit;
    auto* Image=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("CatchGlyphImage"));Fit->SetContent(Image);Image->SetVisibility(ESlateVisibility::Collapsed);
}
void UMCUnifiedCatchGlyph::SetPresentationIcon(UTexture2D* InTexture,UMaterialInterface* InMaterial,FLinearColor Hue,FLinearColor Ink,FLinearColor Cream)
{
    BuildPresentationTree();FallbackHue=Hue;FallbackInk=Ink;FallbackCream=Cream;Texture=InTexture;Material=InMaterial;
    if(auto* Image=WidgetTree?WidgetTree->FindWidget<UImage>(TEXT("CatchGlyphImage")):nullptr) {
        UObject* Resource=Material?static_cast<UObject*>(Material.Get()):static_cast<UObject*>(Texture.Get());
        if(Image->GetBrush().GetResourceObject()!=Resource) {
            if(Material) {
                Image->SetBrushFromMaterial(Material);

            }
            else Image->SetBrushFromTexture(Texture,true);
        }
        // A desired-size override only reaches an existing SImage. Persist the
        // authored aspect in the brush as this also runs before Slate construction.
        if(Material && Texture) {
            const FVector2D ImageSize(Texture->GetSizeX(),Texture->GetSizeY());
            if(Image->GetBrush().ImageSize!=ImageSize) {auto Brush=Image->GetBrush();Brush.ImageSize=ImageSize;Image->SetBrush(Brush);}
        }
        Image->SetVisibility(Resource?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
}
int32 UMCUnifiedCatchGlyph::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const
{
    const int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,bParentEnabled);
    if(Texture || Material || !bAllowAnalyticFallback) return Top;
    const FVector2D Size=Geometry.GetLocalSize();const float Scale=FMath::Min(float(Size.X),float(Size.Y))/100.f;
    const FGeometry Icon=Geometry.MakeChild(FVector2D(100,100),FSlateLayoutTransform(Scale,(Size-FVector2D(100*Scale))*.5));
    return MCFishingRewardArt::DrawToken(Icon,Elements,Top+1,FallbackHue,FallbackInk,FallbackCream,Style.GetColorAndOpacityTint().A);
}

UMCUnifiedCatchWidget::UMCUnifiedCatchWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;PreviewUIVersion=EMCUIVersion::New;
    SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);PreviewDuration=2.4f;
    CommonFill=MCUnifiedCatchPrivate::Color(TEXT("4C76AE"));RareFill=MCUnifiedCatchPrivate::Color(TEXT("A156F7"));
    EpicFill=MCUnifiedCatchPrivate::Color(TEXT("A7496C"));LegendaryFill=MCUnifiedCatchPrivate::Color(TEXT("FFDD00"));
}
UMCUniversalUIWidget* UMCUnifiedCatchWidget::MakeShape(FName Name,FLinearColor Fill)
{
    auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Shape->Label=FText::GetEmpty();Shape->LabelKey=NAME_None;Shape->bUseNativeLayout=false;Shape->bUseThemeShape=false;Shape->bCustomColors=true;Shape->Kind=EMCUniversalUIKind::Panel;
    Shape->CustomFill=Fill;Shape->CustomOutline=MCUnifiedCatchPrivate::Color(TEXT("FFFDFD"));Shape->CustomShadow=Shape->CustomOutline;
    Shape->ContentPadding=FMargin(14);Shape->CornerRadius=20;Shape->OutlineWidth=4;Shape->ShadowOffset=FVector2D(10,12);
    Shape->SetVisibility(ESlateVisibility::HitTestInvisible);Shape->Initialize();Shape->BuildPresentationTree();Shape->ApplyTheme();return Shape;
}
UTextBlock* UMCUnifiedCatchWidget::MakeText(FName Name,int32 FontSize,FLinearColor Color)
{
    auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Text->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);Text->SetAutoWrapText(false);Text->SetWrapTextAt(600);
    Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Text->SetJustification(ETextJustify::Center);
    Text->SetColorAndOpacity(Color);Text->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(auto* T=GetResolvedTheme()) Text->SetFont(T->MakeFont(FontSize,true));return Text;
}
void UMCUnifiedCatchWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {ResolvePresentationTree();return;}
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("CatchRoot"));Root->SetVisibility(ESlateVisibility::HitTestInvisible);WidgetTree->RootWidget=Root;
    auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CatchCardBounds"));
    auto* Surface=MakeShape(TEXT("CatchCard"),CommonFill);Bounds->SetContent(Surface);MCUnifiedCatchPrivate::Place(Root,Bounds,FVector2D(348,288),0);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CatchCardColumn"));Surface->SetContentForSlot(TEXT("Content"),Column);
    auto* HeaderBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CatchTitleBounds"));
    auto* Header=MakeText(TEXT("CatchTitle"),24,MCUnifiedCatchPrivate::Color(TEXT("FFFDFD")));Header->SetText(Localized(TEXT("UI.V2.Catch.Title"),LOCTEXT("YourCatch","YOUR CATCH")));HeaderBounds->SetContent(Header);
    Column->AddChildToVerticalBox(HeaderBounds)->SetPadding(FMargin(0,0,0,8));
    auto* PictureBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CatchIllustrationBounds"));PictureBounds->SetHeightOverride(IllustrationHeight);PictureBounds->SetWidthOverride(MinimumCardWidth*.69f);
    auto* Picture=MakeShape(TEXT("CatchIllustration"),MCUnifiedCatchPrivate::Color(TEXT("404243")));Picture->OutlineWidth=0;Picture->ShadowOffset=FVector2D::ZeroVector;Picture->ContentPadding=FMargin(0);Picture->ApplyTheme();PictureBounds->SetContent(Picture);
    auto* PictureCell=Column->AddChildToVerticalBox(PictureBounds);PictureCell->SetPadding(FMargin(14,0,14,8));PictureCell->SetHorizontalAlignment(HAlign_Center);
    auto* Footer=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("CatchFooter"));Footer->SetExplicitWrapSize(true);Footer->SetWrapSize(292);Footer->SetInnerSlotPadding(FVector2D(12,6));Footer->SetHorizontalAlignment(HAlign_Center);
    Column->AddChildToVerticalBox(Footer)->SetHorizontalAlignment(HAlign_Center);
    for(bool bAmount:{true,false}) {
        auto* LabelBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),bAmount?TEXT("CatchAmountBounds"):TEXT("CatchRarityBounds"));
        auto* Label=MakeText(bAmount?TEXT("CatchAmount"):TEXT("CatchRarity"),bAmount?24:18,MCUnifiedCatchPrivate::Color(TEXT("472630")));
        LabelBounds->SetContent(Label);Footer->AddChildToWrapBox(LabelBounds)->SetVerticalAlignment(VAlign_Center);
    }
    for(int32 I=0;I<3;++I) {
        auto* Token=WidgetTree->ConstructWidget<UMCUnifiedCatchGlyph>(UMCUnifiedCatchGlyph::StaticClass(),MCUnifiedCatchPrivate::Part(TEXT("Token"),I));Token->Initialize();Token->BuildPresentationTree();
        MCUnifiedCatchPrivate::Place(Root,Token,FVector2D(100,100),20+I);
    }
    for(int32 I=0;I<15;++I) {
        auto* Dot=MakeShape(MCUnifiedCatchPrivate::Part(TEXT("Trail"),I),FLinearColor::White);
        Dot->ContentPadding=FMargin(0);Dot->CornerRadius=100;Dot->OutlineWidth=0;Dot->ShadowOffset=FVector2D::ZeroVector;Dot->ApplyTheme();
        MCUnifiedCatchPrivate::Place(Root,Dot,FVector2D(100,100),10);
    }
    for(int32 I=0;I<8;++I) for(const TCHAR* Axis:{TEXT("SparkX"),TEXT("SparkY")}) {
        auto* Bar=MakeShape(MCUnifiedCatchPrivate::Part(Axis,I),FLinearColor::White);
        Bar->ContentPadding=FMargin(0);Bar->CornerRadius=20;Bar->OutlineWidth=0;Bar->ShadowOffset=FVector2D::ZeroVector;Bar->ApplyTheme();
        MCUnifiedCatchPrivate::Place(Root,Bar,FVector2D(100,100),40);
    }
    auto* Ring=MakeShape(TEXT("CatchArrivalRing"),FLinearColor::Transparent);Ring->ContentPadding=FMargin(0);Ring->CornerRadius=100;Ring->OutlineWidth=2;Ring->ShadowOffset=FVector2D::ZeroVector;Ring->ApplyTheme();
    MCUnifiedCatchPrivate::Place(Root,Ring,FVector2D(100,100),39);
    ResolvePresentationTree();
}
void UMCUnifiedCatchWidget::ResolvePresentationTree()
{
    if(!WidgetTree) return;
    Canvas=Cast<UCanvasPanel>(WidgetTree->RootWidget);CardBounds=WidgetTree->FindWidget<USizeBox>(TEXT("CatchCardBounds"));
    Card=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("CatchCard"));Title=WidgetTree->FindWidget<UTextBlock>(TEXT("CatchTitle"));
    Amount=WidgetTree->FindWidget<UTextBlock>(TEXT("CatchAmount"));Rarity=WidgetTree->FindWidget<UTextBlock>(TEXT("CatchRarity"));
    Illustration=WidgetTree->FindWidget<USizeBox>(TEXT("CatchIllustrationBounds"));
}
FLinearColor UMCUnifiedCatchWidget::RarityColor(EMCHubItemRarity Value) const
{
    switch(Value) {case EMCHubItemRarity::Rare:return RareFill;case EMCHubItemRarity::Epic:return EpicFill;case EMCHubItemRarity::Legendary:return LegendaryFill;default:return CommonFill;}
}
FText UMCUnifiedCatchWidget::RarityLabel(EMCHubItemRarity Value,const FText& Fallback) const
{
    FName Key=TEXT("UI.V2.Catch.Rarity.Common");FText Copy=Fallback.IsEmpty()?LOCTEXT("Common","COMMON"):Fallback;
    switch(Value) {
    case EMCHubItemRarity::Rare:Key=TEXT("UI.V2.Catch.Rarity.Rare");if(Fallback.IsEmpty()) Copy=LOCTEXT("Rare","RARE");break;
    case EMCHubItemRarity::Epic:Key=TEXT("UI.V2.Catch.Rarity.Epic");if(Fallback.IsEmpty()) Copy=LOCTEXT("Epic","EPIC");break;
    case EMCHubItemRarity::Legendary:Key=TEXT("UI.V2.Catch.Rarity.Legendary");if(Fallback.IsEmpty()) Copy=LOCTEXT("Legendary","LEGENDARY");break;
    default:break;
    }
    return IsDesignTime()?UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Copy,PreviewCulture):Localized(Key,Copy);
}
UTexture2D* UMCUnifiedCatchWidget::ResolveIcon(EMCHubItemRarity Value) const
{
    if(const auto* Prepared=PreparedIcons.Find(Value)) return Prepared->Get();
    const auto* Icon=CatchIcons.Find(Value);if(!Icon || Icon->IsNull()) return nullptr;
    return IsDesignTime()?Icon->LoadSynchronous():Icon->Get();
}
UMaterialInterface* UMCUnifiedCatchWidget::ResolveIconMaterial(EMCHubItemRarity Value) const
{
    if(const auto* Prepared=PreparedIconMaterials.Find(Value)) return Prepared->Get();
    const auto* Icon=CatchIconMaterials.Find(Value);if(!Icon || Icon->IsNull()) return nullptr;
    return IsDesignTime()?Icon->LoadSynchronous():Icon->Get();
}

void UMCUnifiedCatchWidget::SetBackend(UMCFishingRewardWidget* Source)
{
    if(Backend.Get()!=Source) {
        if(auto* Previous=Backend.Get()) Previous->ClearUnifiedView(this);
        Backend=Source;bHasCopy=false;
    }
    if(IsDesignTime()) return;
    PreparePresentation();RefreshFromBackend();
}
void UMCUnifiedCatchWidget::UpdateCopy(const FMCFishingRewardPresentationSnapshot& Snapshot)
{
    ResolvePresentationTree();if(!Title.IsValid() || !Amount.IsValid() || !Rarity.IsValid()) return;
    FText Heading=Localized(TEXT("UI.V2.Catch.Title"),LOCTEXT("YourCatch","YOUR CATCH"));
    FText Value=Snapshot.AmountText.IsEmpty()?FText::AsCultureInvariant(TEXT("+1")):Snapshot.AmountText;
    FText Detail=(Snapshot.bWalletFull || Snapshot.bMerged) && !Snapshot.DetailText.IsEmpty()?Snapshot.DetailText:RarityLabel(Snapshot.Rarity,Snapshot.RarityText);
    if(IsDesignTime() && (PreviewState==TEXT("LongText") || PreviewScenario==EMCGameplayUIPreviewScenario::LongText)) {
        Heading=FText::Format(FText::AsCultureInvariant(TEXT("{0} · {0} · {0}")),Heading);
        Detail=FText::Format(FText::AsCultureInvariant(TEXT("{0} · {0} · {0}")),Detail);
    }
    const bool Changed=!bHasCopy || LastRarity!=Snapshot.Rarity || !LastAmount.EqualTo(Value)
        || !LastRarityText.EqualTo(Detail) || !LastTitle.EqualTo(Heading);
    if(!Changed) return;
    Title->SetText(Heading);Amount->SetText(Value);Rarity->SetText(Detail);
    LastTitle=Heading;LastAmount=Value;LastRarityText=Detail;LastRarity=Snapshot.Rarity;bHasCopy=true;
    if(Card.IsValid()) {Card->CustomFill=RarityColor(Snapshot.Rarity);Card->ApplyTheme();}
    MeasureLayout();
}
void UMCUnifiedCatchWidget::MeasureLayout()
{
    if(bMeasuring || !CardBounds.IsValid() || !Card.IsValid() || !Title.IsValid() || !Amount.IsValid() || !Rarity.IsValid()) return;
    TGuardValue<bool> Guard(bMeasuring,true);
    FVector2D Available=GetPaintSpaceGeometry().GetLocalSize();
    if(Available.X<1 && GetWorld() && !IsDesignTime()) Available=UWidgetLayoutLibrary::GetViewportSize(this)/FMath::Max(.001f,UWidgetLayoutLibrary::GetViewportScale(this));
    const FMargin CardPadding=Card->ContentPadding;
    const float XPadding=CardPadding.Left+CardPadding.Right,YPadding=CardPadding.Top+CardPadding.Bottom;
    float Maximum=FMath::Max(180.f,MaximumCardWidth);
    if(Available.X>1) Maximum=FMath::Min(Maximum,FMath::Max(180.f,float(Available.X)-32.f));
    const float Minimum=FMath::Min(FMath::Max(180.f,MinimumCardWidth),Maximum);
    const auto TitleMetrics=MCUnifiedCatchPrivate::Measure(Title->GetText(),Title->GetFont());
    const auto AmountMetrics=MCUnifiedCatchPrivate::Measure(Amount->GetText(),Amount->GetFont());
    const auto RarityMetrics=MCUnifiedCatchPrivate::Measure(Rarity->GetText(),Rarity->GetFont());
    float Width=FMath::Clamp(FMath::Max3(TitleMetrics.Width,AmountMetrics.Width+RarityMetrics.Width+12.f,
        FMath::Max3(TitleMetrics.Word,AmountMetrics.Word,RarityMetrics.Word))+XPadding,Minimum,Maximum);
    if(!bAdaptTextLayout) if(auto* CardCell=Cast<UCanvasPanelSlot>(CardBounds->Slot)) Width=FMath::Max(180.f,float(CardCell->GetSize().X));
    const float Inner=FMath::Max(100.f,Width-XPadding);
    const float TitleHeight=MCUnifiedCatchPrivate::Measure(Title->GetText(),Title->GetFont(),Inner).Height;
    const float AmountWidth=FMath::Min(Inner,FMath::Max(1.f,FMath::CeilToFloat(AmountMetrics.Width)+8.f));
    const float RarityWidth=FMath::Min(Inner,FMath::Max(1.f,FMath::CeilToFloat(RarityMetrics.Width)+8.f));
    const float AmountHeight=MCUnifiedCatchPrivate::Measure(Amount->GetText(),Amount->GetFont(),AmountWidth).Height;
    const float RarityHeight=MCUnifiedCatchPrivate::Measure(Rarity->GetText(),Rarity->GetFont(),RarityWidth).Height;
    const bool TwoRows=AmountWidth+RarityWidth+12.f>Inner+.5f;
    const float FooterHeight=TwoRows?AmountHeight+RarityHeight+6.f:FMath::Max(AmountHeight,RarityHeight);
    const float PictureHeight=FMath::Max(48.f,IllustrationHeight);
    float Height=YPadding+TitleHeight+8.f+PictureHeight+8.f+FooterHeight;
    if(!bAdaptTextLayout) if(auto* CardCell=Cast<UCanvasPanelSlot>(CardBounds->Slot)) Height=FMath::Max(100.f,float(CardCell->GetSize().Y));
    if(bAdaptTextLayout) {
        CardBounds->SetWidthOverride(Width);CardBounds->SetHeightOverride(Height);
        if(auto* CardCell=Cast<UCanvasPanelSlot>(CardBounds->Slot)) if(!CardCell->GetSize().Equals(FVector2D(Width,Height),.5f)) CardCell->SetSize(FVector2D(Width,Height));
        if(Illustration.IsValid()) {Illustration->SetHeightOverride(PictureHeight);Illustration->SetWidthOverride(FMath::Min(Inner-28.f,Width*.69f));}
        if(auto* Footer=WidgetTree->FindWidget<UWrapBox>(TEXT("CatchFooter"))) Footer->SetWrapSize(Inner);
        const FName Names[]={TEXT("CatchTitleBounds"),TEXT("CatchAmountBounds"),TEXT("CatchRarityBounds")};
        const float Widths[]={Inner,AmountWidth,RarityWidth};UTextBlock* Labels[]={Title.Get(),Amount.Get(),Rarity.Get()};
        for(int32 I=0;I<3;++I) {
            if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(Names[I])) {Bounds->SetWidthOverride(Widths[I]);Bounds->SetMaxDesiredWidth(Widths[I]);}
            Labels[I]->SetAutoWrapText(false);Labels[I]->SetWrapTextAt(Widths[I]);
        }
    }
    float MeasuredTitleHeight=TitleHeight;
    if(bAdaptTextLayout) {
        // Text shaping and WrapBox line allocation own the real height. Font
        // advance estimates alone can miss a wrapped footer or fallback-font leading.
        if(auto* Footer=WidgetTree->FindWidget<UWrapBox>(TEXT("CatchFooter"))) Footer->SetExplicitWrapSize(true);
        if(auto* Column=WidgetTree->FindWidget<UVerticalBox>(TEXT("CatchCardColumn"))) {
            const auto SlateColumn=Column->TakeWidget();SlateColumn->MarkPrepassAsDirty();
            SlateColumn->SlatePrepass(FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale()));
            Height=FMath::CeilToFloat(Column->GetDesiredSize().Y+YPadding+2.f);
            if(auto* HeadingBounds=WidgetTree->FindWidget<USizeBox>(TEXT("CatchTitleBounds"))) MeasuredTitleHeight=HeadingBounds->GetDesiredSize().Y;
            CardBounds->SetHeightOverride(Height);
            if(auto* CardCell=Cast<UCanvasPanelSlot>(CardBounds->Slot)) CardCell->SetSize(FVector2D(Width,Height));
        }
    }
    Layout.CardSize=FVector2D(FMath::CeilToFloat(Width),FMath::CeilToFloat(Height));
    Layout.IconOffset=FVector2D(0,CardPadding.Top+MeasuredTitleHeight+8.f+PictureHeight*.5f-Height*.5f);
    const float Glyph=FMath::Max(24.f,FMath::Min(IconSize,FMath::Min(PictureHeight-8.f,Inner-36.f)));
    Layout.IconSize=FVector2D(Glyph,Glyph);
    PublishLayout();
}
void UMCUnifiedCatchWidget::PublishLayout()
{
    const auto& Geometry=GetPaintSpaceGeometry();
    const float Scale=Geometry.GetLocalSize().X>0?Geometry.GetAccumulatedLayoutTransform().GetScale():
        GetWorld() && !IsDesignTime()?UWidgetLayoutLibrary::GetViewportScale(this):1.f;
    Layout.AbsoluteScale=FMath::Max(.001f,Scale);LastAbsoluteScale=Layout.AbsoluteScale;
    if(!IsDesignTime()) if(auto* Source=Backend.Get()) Source->SetUnifiedView(this,Layout,bPrepared);
}
void UMCUnifiedCatchWidget::PreparePresentation()
{
    if(bPreparing) return;
    if(bPrepared) {PublishLayout();return;}
    TGuardValue<bool> Guard(bPreparing,true);BuildPresentationTree();ResolvePresentationTree();
    bPrepared=false;
    if(!GetResolvedTheme() || !CardBounds.IsValid() || !Card.IsValid() || !Title.IsValid()
        || !Amount.IsValid() || !Rarity.IsValid() || !FSlateApplication::IsInitialized()) {PublishLayout();return;}
    if(!bHasCopy) {
        FMCFishingRewardPresentationSnapshot Initial=Backend.IsValid()?Backend->GetPresentationSnapshot():FMCFishingRewardPresentationSnapshot();
        if(Initial.AmountText.IsEmpty()) {Initial.Amount=1;Initial.AmountText=FText::AsCultureInvariant(TEXT("+1"));Initial.RarityText=RarityLabel(EMCHubItemRarity::Common);}
        UpdateCopy(Initial);
    } else MeasureLayout();
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    bool FontsReady=true;
    for(UTextBlock* Label:{Title.Get(),Amount.Get(),Rarity.Get()}) {
        FontsReady&=Label->GetFont().HasValidFont();Measure->Measure(Label->GetText(),Label->GetFont(),Layout.AbsoluteScale);
    }
    Measure->Measure(FText::AsCultureInvariant(TEXT("+0123456789 ,.")),Amount->GetFont(),Layout.AbsoluteScale);
    const FString Culture=IsDesignTime()?PreviewCulture:FString();
    const FText NumberExample=UMCUIDataLibrary::NumberForCulture(1234567890,Culture);
    const FText AmountPattern=IsDesignTime()?UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),TEXT("hub.fishing.reward.amount"),LOCTEXT("Amount","+{Amount}"),PreviewCulture):
        Localized(TEXT("hub.fishing.reward.amount"),LOCTEXT("Amount","+{Amount}"));
    Measure->Measure(UMCUIDataLibrary::FormatTextForCulture(AmountPattern,{{TEXT("Amount"),NumberExample}},Culture),Amount->GetFont(),Layout.AbsoluteScale);
    const FText Full=IsDesignTime()?UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),TEXT("hub.fishing.reward.full"),LOCTEXT("Full","Wallet full"),PreviewCulture):
        Localized(TEXT("hub.fishing.reward.full"),LOCTEXT("Full","Wallet full"));
    Measure->Measure(Full,Amount->GetFont(),Layout.AbsoluteScale);
    for(EMCHubItemRarity Value:{EMCHubItemRarity::Common,EMCHubItemRarity::Rare,EMCHubItemRarity::Epic,EMCHubItemRarity::Legendary})
        Measure->Measure(RarityLabel(Value),Rarity->GetFont(),Layout.AbsoluteScale);
    MCFishingRewardArt::Prime();
    bool IconsReady=true;PreparedIcons.Reset();PreparedIconMaterials.Reset();
    // Retain every configured variant during loading. Runtime readiness never initiates a load.
    for(const auto& Pair:CatchIcons) if(!Pair.Value.IsNull()) {
        UTexture2D* Asset=IsDesignTime()?Pair.Value.LoadSynchronous():Pair.Value.Get();
        IconsReady&=Asset!=nullptr;if(Asset) PreparedIcons.Add(Pair.Key,Asset);
    }
    for(const auto& Pair:CatchIconMaterials) if(!Pair.Value.IsNull()) {
        UMaterialInterface* Asset=IsDesignTime()?Pair.Value.LoadSynchronous():Pair.Value.Get();
        IconsReady&=Asset!=nullptr;if(Asset) PreparedIconMaterials.Add(Pair.Key,Asset);
    }
    for(EMCHubItemRarity Value:{EMCHubItemRarity::Common,EMCHubItemRarity::Rare,EMCHubItemRarity::Epic,EMCHubItemRarity::Legendary})
        IconsReady&=ResolveIconMaterial(Value) || ResolveIcon(Value) || bAllowAnalyticIconFallback;
    bool TreeReady=WidgetTree->FindWidget(TEXT("CatchArrivalRing"))!=nullptr;
    for(int32 I=0;I<3;++I) TreeReady&=WidgetTree->FindWidget<UMCUnifiedCatchGlyph>(MCUnifiedCatchPrivate::Part(TEXT("Token"),I))!=nullptr;
    for(int32 I=0;I<15;++I) TreeReady&=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedCatchPrivate::Part(TEXT("Trail"),I))!=nullptr;
    for(int32 I=0;I<8;++I) for(const TCHAR* Axis:{TEXT("SparkX"),TEXT("SparkY")})
        TreeReady&=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedCatchPrivate::Part(Axis,I))!=nullptr;
    // Bind an idle brush before readiness rather than at the first accepted catch.
    if(IconsReady && TreeReady) for(int32 I=0;I<3;++I)
        if(auto* Token=WidgetTree->FindWidget<UMCUnifiedCatchGlyph>(MCUnifiedCatchPrivate::Part(TEXT("Token"),I))) {
            Token->bAllowAnalyticFallback=bAllowAnalyticIconFallback;
            Token->SetPresentationIcon(ResolveIcon(EMCHubItemRarity::Common),ResolveIconMaterial(EMCHubItemRarity::Common),CommonFill,
                MCUnifiedCatchPrivate::Color(TEXT("472630")),MCUnifiedCatchPrivate::Color(TEXT("FFFDFD")));
        }
    bPrepared=FontsReady && IconsReady && TreeReady && Layout.IsValid();PublishLayout();
}
void UMCUnifiedCatchWidget::HideMotion()
{
    if(!WidgetTree) return;
    if(CardBounds.IsValid()) CardBounds->SetVisibility(ESlateVisibility::Collapsed);
    for(int32 I=0;I<3;++I) if(auto* Node=WidgetTree->FindWidget(MCUnifiedCatchPrivate::Part(TEXT("Token"),I))) Node->SetVisibility(ESlateVisibility::Collapsed);
    for(int32 I=0;I<15;++I) if(auto* Node=WidgetTree->FindWidget(MCUnifiedCatchPrivate::Part(TEXT("Trail"),I))) Node->SetVisibility(ESlateVisibility::Collapsed);
    for(int32 I=0;I<8;++I) for(const TCHAR* Axis:{TEXT("SparkX"),TEXT("SparkY")})
        if(auto* Node=WidgetTree->FindWidget(MCUnifiedCatchPrivate::Part(Axis,I))) Node->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* Node=WidgetTree->FindWidget(TEXT("CatchArrivalRing"))) Node->SetVisibility(ESlateVisibility::Collapsed);
}
void UMCUnifiedCatchWidget::ApplyPose(UWidget* Node,const FMCFishingRewardMotionPose& Pose,FVector2D BaseSize)
{
    if(!Node) return;
    Node->SetVisibility(Pose.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(!Pose.bVisible) return;
    const auto& Geometry=GetPaintSpaceGeometry();
    if(Geometry.GetLocalSize().X<=0 || Geometry.GetLocalSize().Y<=0) {Node->SetVisibility(ESlateVisibility::Collapsed);return;}
    const FVector2D Zero=Geometry.AbsoluteToLocal(FVector2D::ZeroVector);
    const FVector2D Size=Geometry.AbsoluteToLocal(Pose.SizeAbsolute)-Zero;
    Node->SetRenderTranslation(Geometry.AbsoluteToLocal(Pose.CenterAbsolute));
    Node->SetRenderScale(FVector2D(Size.X/FMath::Max(1.,BaseSize.X),Size.Y/FMath::Max(1.,BaseSize.Y))*Pose.RenderScale);
    Node->SetRenderTransformAngle(Pose.AngleDegrees);Node->SetRenderOpacity(Pose.Opacity);
}
void UMCUnifiedCatchWidget::Present(const FMCFishingRewardPresentationSnapshot& Snapshot)
{
    if(!WidgetTree || !bPrepared) {HideMotion();return;}
    UpdateCopy(Snapshot);
    if(!Snapshot.bVisible) {HideMotion();return;}
    ApplyPose(CardBounds.Get(),Snapshot.Card,Layout.CardSize);
    for(int32 I=0;I<3;++I) if(auto* Token=WidgetTree->FindWidget<UMCUnifiedCatchGlyph>(MCUnifiedCatchPrivate::Part(TEXT("Token"),I))) {
        Token->bAllowAnalyticFallback=bAllowAnalyticIconFallback;
        Token->SetPresentationIcon(ResolveIcon(Snapshot.Rarity),ResolveIconMaterial(Snapshot.Rarity),Snapshot.FishColor,Snapshot.Ink,Snapshot.Cream);
        ApplyPose(Token,Snapshot.Tokens[I],FVector2D(100,100));
    }
    auto Tint=[&](UMCUniversalUIWidget* Shape,FLinearColor Fill,bool bRing=false) {
        if(!Shape) return;
        const FLinearColor Desired=bRing?FLinearColor::Transparent:Fill;
        if(Shape->CustomFill!=Desired || Shape->CustomOutline!=Fill) {
            Shape->CustomFill=Desired;Shape->CustomOutline=Fill;Shape->ApplyTheme();
        }
    };
    for(int32 I=0;I<15;++I) if(auto* Dot=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedCatchPrivate::Part(TEXT("Trail"),I))) {
        Tint(Dot,Snapshot.Trails[I].Color);ApplyPose(Dot,Snapshot.Trails[I],FVector2D(100,100));
    }
    for(int32 I=0;I<8;++I) for(int32 Axis=0;Axis<2;++Axis) {
        auto* Spark=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedCatchPrivate::Part(Axis==0?TEXT("SparkX"):TEXT("SparkY"),I));
        auto Pose=Snapshot.Sparks[I];
        if(Axis==0) Pose.SizeAbsolute.X=FMath::Min(Pose.SizeAbsolute.X,3.5*Layout.AbsoluteScale);
        else Pose.SizeAbsolute.Y=FMath::Min(Pose.SizeAbsolute.Y,3.5*Layout.AbsoluteScale);
        Tint(Spark,Pose.Color);ApplyPose(Spark,Pose,FVector2D(100,100));
    }
    auto* Ring=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("CatchArrivalRing"));Tint(Ring,Snapshot.Ring.Color,true);ApplyPose(Ring,Snapshot.Ring,FVector2D(100,100));
}
void UMCUnifiedCatchWidget::RefreshFromBackend()
{
    if(IsDesignTime()) return;
    auto* Source=Backend.Get();if(!Source || Source->GetWorld()!=GetWorld()) {HideMotion();return;}
    if(!bPrepared) PreparePresentation();
    const auto Snapshot=Source->GetPresentationSnapshot();Present(Snapshot);
}
void UMCUnifiedCatchWidget::ApplyTheme()
{
    Super::ApplyTheme();ResolvePresentationTree();
    if(auto* T=GetResolvedTheme()) for(UTextBlock* Label:{Title.Get(),Amount.Get(),Rarity.Get()}) if(Label) {
        const int32 Size=FMath::Max(8,FMath::RoundToInt(Label->GetFont().Size));Label->SetFont(T->MakeFont(Size,true));
    }
    bHasCopy=false;bPrepared=false;
    if(!bPreparing) PreparePresentation();
}
void UMCUnifiedCatchWidget::NativeConstruct() {Super::NativeConstruct();PreparePresentation();if(!IsDesignTime()) RefreshFromBackend();}
void UMCUnifiedCatchWidget::NativeDestruct()
{
    if(auto* Source=Backend.Get()) Source->ClearUnifiedView(this);
    bPrepared=false;Super::NativeDestruct();
}
void UMCUnifiedCatchWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);if(IsDesignTime()) return;
    if(!bPrepared) PreparePresentation();
    if(!LastViewportSize.Equals(Geometry.GetLocalSize(),.5f)
        || !FMath::IsNearlyEqual(LastAbsoluteScale,Geometry.GetAccumulatedLayoutTransform().GetScale(),.001f)) {
        LastViewportSize=Geometry.GetLocalSize();MeasureLayout();PublishLayout();
    }
    RefreshFromBackend();
}
void UMCUnifiedCatchWidget::OnLocalizationChanged()
{
    bHasCopy=false;bPrepared=false;PreparePresentation();
    if(IsDesignTime()) PresentPreview(PreviewTime);else RefreshFromBackend();
}
void UMCUnifiedCatchWidget::PresentPreview(float Time)
{
    if(!IsDesignTime()) return;
    PreparePresentation();FMCFishingRewardPresentationSnapshot Snapshot;
    Snapshot.Rarity=PreviewRarity;
    if(PreviewState==TEXT("Common")) Snapshot.Rarity=EMCHubItemRarity::Common;
    else if(PreviewState==TEXT("Rare")) Snapshot.Rarity=EMCHubItemRarity::Rare;
    else if(PreviewState==TEXT("Epic")) Snapshot.Rarity=EMCHubItemRarity::Epic;
    else if(PreviewState==TEXT("Legendary")) Snapshot.Rarity=EMCHubItemRarity::Legendary;
    Snapshot.bWalletFull=bPreviewWalletFull || PreviewState==TEXT("WalletFull");Snapshot.bMerged=bPreviewMerged || PreviewState==TEXT("Merged");
    Snapshot.Amount=Snapshot.bWalletFull?0:FMath::Max(1,PreviewAmount);Snapshot.WalletAfter=FMath::Max(Snapshot.Amount,PreviewWalletAfter);
    auto Copy=[this](FName Key,const FText& Fallback) {return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,PreviewCulture);};
    Snapshot.AmountText=Snapshot.bWalletFull?Copy(TEXT("hub.fishing.reward.full"),LOCTEXT("Full","Wallet full")):
        UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("hub.fishing.reward.amount"),LOCTEXT("Amount","+{Amount}")),{{TEXT("Amount"),Snapshot.Amount}},PreviewCulture);
    Snapshot.RarityText=RarityLabel(Snapshot.Rarity);Snapshot.DetailText=Copy(TEXT("hub.fishing.reward.currency"),LOCTEXT("Currency","Fish coins"));
    Snapshot.FishColor=RarityColor(Snapshot.Rarity);Snapshot.Ink=MCUnifiedCatchPrivate::Color(TEXT("472630"));Snapshot.Cream=MCUnifiedCatchPrivate::Color(TEXT("FFFDFD"));
    UpdateCopy(Snapshot);PublishLayout();
    const FGeometry& Geometry=GetPaintSpaceGeometry();FVector2D Size=Geometry.GetLocalSize();
    if(Size.X<1 || Size.Y<1) Size=FVector2D(1920,1080);
    const float End=Snapshot.bWalletFull?1.5f:2.f;
    Snapshot.Age=Time>0?FMath::Fmod(Time,End+.35f):.55f;Snapshot.bVisible=Snapshot.Age<End;
    const FVector2D Center(Size.X*.5,Size.Y*.48),Endpoint(Size.X*.59,54);
    const FVector2D Launch=Center+Layout.IconOffset-(Endpoint-(Center+Layout.IconOffset)).GetSafeNormal()*14.f;
    const FVector2D Normalized(Launch.X/Size.X,Launch.Y/Size.Y);
    UMCFishingRewardWidget::EvaluateSnapshotMotion(Snapshot,Geometry,Center,Normalized,Endpoint,true,Layout.CardSize,Layout.IconOffset,Layout.IconSize,1.f);
    Present(Snapshot);
}
void UMCUnifiedCatchWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);BuildPresentationTree();
    if(IsDesignTime()) PresentPreview(Time);else RefreshFromBackend();
}

#undef LOCTEXT_NAMESPACE
