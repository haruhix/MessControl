#include "MCUnifiedBossWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCUIDataLibrary.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"

namespace MCUnifiedBossPrivate
{
    FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    FName Part(const TCHAR* Name,int32 Index) {return FName(*FString::Printf(TEXT("BossV2%s%d"),Name,Index));}
}
UMCUnifiedBossWidget::UMCUnifiedBossWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;PreviewUIVersion=EMCUIVersion::New;
    SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);
}
UMCUniversalUIWidget* UMCUnifiedBossWidget::MakeShape(FName Name)
{
    auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Shape->Label=FText::GetEmpty();Shape->LabelKey=NAME_None;Shape->bUseNativeLayout=false;Shape->bUseThemeShape=false;Shape->bCustomColors=true;
    Shape->CustomFill=MCUnifiedBossPrivate::Color(TEXT("30333F"));Shape->CustomText=MCUnifiedBossPrivate::Color(TEXT("FFF9F8"));
    Shape->CustomOutline=MCUnifiedBossPrivate::Color(TEXT("718080"));Shape->CustomShadow=MCUnifiedBossPrivate::Color(TEXT("879094"));
    Shape->ContentPadding=FMargin(14,10);Shape->CornerRadius=16;Shape->OutlineWidth=4;Shape->ShadowOffset=FVector2D(8,9);
    Shape->SetVisibility(ESlateVisibility::HitTestInvisible);Shape->Initialize();Shape->BuildPresentationTree();Shape->ApplyTheme();return Shape;
}
UTextBlock* UMCUnifiedBossWidget::MakeText(FName Name,int32 Size)
{
    auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Text->SetAutoWrapText(false);Text->SetWrapTextAt(600);Text->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
    Text->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);Text->SetColorAndOpacity(MCUnifiedBossPrivate::Color(TEXT("FFF9F8")));
    Text->SetVisibility(ESlateVisibility::HitTestInvisible);if(const auto* T=GetResolvedTheme()) Text->SetFont(T->MakeFont(Size,true));return Text;
}
void UMCUnifiedBossWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) return;
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("BossV2Root"));WidgetTree->RootWidget=Root;
    auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("BossV2Stack"));
    auto* Anchor=Root->AddChildToCanvas(Stack);Anchor->SetAnchors(FAnchors(.5f,0));Anchor->SetAlignment(FVector2D(.5f,0));Anchor->SetPosition(FVector2D(0,18));Anchor->SetAutoSize(true);
    for(int32 I=0;I<3;++I) {
        auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedBossPrivate::Part(TEXT("Bounds"),I));Bounds->SetWidthOverride(680);
        auto* Frame=MakeShape(MCUnifiedBossPrivate::Part(TEXT("Frame"),I));Bounds->SetContent(Frame);
        Stack->AddChildToVerticalBox(Bounds)->SetPadding(FMargin(0,0,0,12));Bounds->SetVisibility(ESlateVisibility::Collapsed);
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Frame->SetContentForSlot(TEXT("Content"),Row);
        auto* PortraitBounds=WidgetTree->ConstructWidget<USizeBox>();PortraitBounds->SetWidthOverride(60);PortraitBounds->SetHeightOverride(60);
        Row->AddChildToHorizontalBox(PortraitBounds)->SetVerticalAlignment(VAlign_Center);
        auto* PortraitLayers=WidgetTree->ConstructWidget<UOverlay>();PortraitBounds->SetContent(PortraitLayers);
        auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);
        auto* Portrait=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),MCUnifiedBossPrivate::Part(TEXT("Portrait"),I));Fit->SetContent(Portrait);
        auto* FitSlot=PortraitLayers->AddChildToOverlay(Fit);FitSlot->SetHorizontalAlignment(HAlign_Fill);FitSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Fallback=MakeText(MCUnifiedBossPrivate::Part(TEXT("Fallback"),I),30);Fallback->SetText(FText::AsCultureInvariant(TEXT("!")));Fallback->SetJustification(ETextJustify::Center);
        auto* FallbackSlot=PortraitLayers->AddChildToOverlay(Fallback);FallbackSlot->SetHorizontalAlignment(HAlign_Center);FallbackSlot->SetVerticalAlignment(VAlign_Center);
        auto* Copy=WidgetTree->ConstructWidget<UVerticalBox>();auto* CopySlot=Row->AddChildToHorizontalBox(Copy);CopySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));CopySlot->SetPadding(FMargin(12,0,0,0));
        auto* Name=MakeText(MCUnifiedBossPrivate::Part(TEXT("Name"),I),22);Copy->AddChildToVerticalBox(Name)->SetPadding(FMargin(0,0,0,4));
        auto AddBar=[&](bool Shield) {
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),MCUnifiedBossPrivate::Part(Shield?TEXT("ShieldLine"):TEXT("HealthLine"),I));
            auto* LineSlot=Copy->AddChildToVerticalBox(Line);if(Shield) LineSlot->SetPadding(FMargin(0,5,0,0));
            auto* BarBounds=WidgetTree->ConstructWidget<USizeBox>();BarBounds->SetHeightOverride(Shield?16:24);
            auto* Shape=MakeShape(MCUnifiedBossPrivate::Part(Shield?TEXT("ShieldFrame"):TEXT("HealthFrame"),I));
            Shape->CustomFill=MCUnifiedBossPrivate::Color(TEXT("30333F"));Shape->CustomOutline=MCUnifiedBossPrivate::Color(TEXT("FFF9F8"));
            Shape->OutlineWidth=2;Shape->ContentPadding=FMargin(3);Shape->CornerRadius=5;Shape->ShadowOffset=FVector2D::ZeroVector;Shape->ApplyTheme();BarBounds->SetContent(Shape);
            auto* BarSlot=Line->AddChildToHorizontalBox(BarBounds);BarSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));BarSlot->SetVerticalAlignment(VAlign_Center);
            auto* Layers=WidgetTree->ConstructWidget<UOverlay>();Shape->SetContentForSlot(TEXT("Content"),Layers);
            if(!Shield) {
                auto* Loss=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),MCUnifiedBossPrivate::Part(TEXT("Loss"),I));
                auto* Cell=Layers->AddChildToOverlay(Loss);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
            }
            auto* Bar=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),MCUnifiedBossPrivate::Part(Shield?TEXT("Shield"):TEXT("Health"),I));
            auto* Cell=Layers->AddChildToOverlay(Bar);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
            auto* Value=MakeText(MCUnifiedBossPrivate::Part(Shield?TEXT("ShieldValue"):TEXT("Value"),I),Shield?15:20);
            Value->SetWrapTextAt(0);auto* ValueSlot=Line->AddChildToHorizontalBox(Value);ValueSlot->SetPadding(FMargin(8,0,0,0));ValueSlot->SetVerticalAlignment(VAlign_Center);
        };
        AddBar(false);AddBar(true);
    }
    ApplyTheme();
}
void UMCUnifiedBossWidget::ApplyTheme()
{
    bPresentationPrepared=false;UMCGameplayUIWidget::ApplyTheme();if(!WidgetTree) return;CachedWidth=-1;CachedCopy.Reset();
    FProgressBarStyle Style;Style.SetBackgroundImage(FSlateNoResource());Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,3));Style.SetMarqueeImage(FSlateNoResource());
    for(int32 I=0;I<3;++I) {
        for(const TCHAR* Part:{TEXT("Health"),TEXT("Loss"),TEXT("Shield")}) if(auto* Bar=WidgetTree->FindWidget<UProgressBar>(MCUnifiedBossPrivate::Part(Part,I))) {
            Bar->SetWidgetStyle(Style);Bar->SetBarFillStyle(EProgressBarFillStyle::Scale);
            Bar->SetFillColorAndOpacity(MCUnifiedBossPrivate::Color(FCString::Strcmp(Part,TEXT("Health"))==0?TEXT("CC4848"):FCString::Strcmp(Part,TEXT("Loss"))==0?TEXT("FFE000"):TEXT("718080")));
        }
        for(const TCHAR* Part:{TEXT("Name"),TEXT("Value"),TEXT("ShieldValue")}) if(auto* Text=WidgetTree->FindWidget<UTextBlock>(MCUnifiedBossPrivate::Part(Part,I))) {
            const int32 Size=FCString::Strcmp(Part,TEXT("Name"))==0?22:FCString::Strcmp(Part,TEXT("Value"))==0?20:15;
            Text->SetFont(GetResolvedTheme()->MakeFont(Size,true));Text->SetColorAndOpacity(MCUnifiedBossPrivate::Color(TEXT("FFF9F8")));
        }
    }
    UpdateLayout();
}
void UMCUnifiedBossWidget::SetBackend(UMCBossHealthWidget* Source)
{
    Backend=Source;RefreshFromBackend();
}
void UMCUnifiedBossWidget::RefreshFromBackend()
{
    if(Backend.IsValid()) Present(Backend->GetPresentationSnapshot());
    else if(!IsDesignTime()) {FMCBossHealthPresentationSnapshot Empty;Present(Empty);}
}
void UMCUnifiedBossWidget::Present(const FMCBossHealthPresentationSnapshot& Snapshot)
{
    BuildPresentationTree();Latest=Snapshot;DisplayedCount=Snapshot.bVisible?FMath::Min(3,Snapshot.Bosses.Num()):0;
    for(int32 I=0;I<3;++I) {
        auto* Bounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedBossPrivate::Part(TEXT("Bounds"),I));
        const bool Visible=I<DisplayedCount;if(Bounds) Bounds->SetVisibility(Visible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(!Visible) continue;const auto& Row=Snapshot.Bosses[I];
        for(const auto& Pair:TArray<TPair<FName,FText>>{{MCUnifiedBossPrivate::Part(TEXT("Name"),I),Row.Name},{MCUnifiedBossPrivate::Part(TEXT("Value"),I),Row.Value},{MCUnifiedBossPrivate::Part(TEXT("ShieldValue"),I),Row.ShieldValue}})
            if(auto* Text=WidgetTree->FindWidget<UTextBlock>(Pair.Key);Text && !Text->GetText().EqualTo(Pair.Value)) Text->SetText(Pair.Value);
        if(auto* Bar=WidgetTree->FindWidget<UProgressBar>(MCUnifiedBossPrivate::Part(TEXT("Health"),I))) Bar->SetPercent(Row.HealthFraction);
        if(auto* Bar=WidgetTree->FindWidget<UProgressBar>(MCUnifiedBossPrivate::Part(TEXT("Loss"),I))) Bar->SetPercent(Row.LossFraction);
        if(auto* Bar=WidgetTree->FindWidget<UProgressBar>(MCUnifiedBossPrivate::Part(TEXT("Shield"),I))) Bar->SetPercent(Row.ShieldFraction);
        if(auto* Line=WidgetTree->FindWidget(MCUnifiedBossPrivate::Part(TEXT("ShieldLine"),I))) Line->SetVisibility(Row.bShieldVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        // Only already-resolved source references are read at runtime. The source
        // encounter/theme preload remains the owner of loading and portrait identity.
        UTexture2D* Texture=Row.ResolvedPortrait.Get();if(!Texture) Texture=Row.Source.Portrait.Get();
        if(auto* Image=WidgetTree->FindWidget<UImage>(MCUnifiedBossPrivate::Part(TEXT("Portrait"),I))) {
            if(Image->GetBrush().GetResourceObject()!=Texture) Image->SetBrushFromTexture(Texture,true);
            Image->SetRenderScale(Row.PortraitScale);Image->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        }
        if(auto* Fallback=WidgetTree->FindWidget(MCUnifiedBossPrivate::Part(TEXT("Fallback"),I))) Fallback->SetVisibility(Texture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    UpdateLayout();
}
void UMCUnifiedBossWidget::UpdateLayout()
{
    if(!WidgetTree) return;
    const float Painted=GetPaintSpaceGeometry().GetLocalSize().X;
    const float Available=Painted>1.f?Painted:float(GetCachedGeometry().GetLocalSize().X);
    const float Width=FMath::Min(840.f,FMath::Max(160.f,(Available>1.f?Available:1920.f)-32.f));
    FString Copy=FString::FromInt(DisplayedCount);
    for(int32 I=0;I<DisplayedCount;++I) Copy+=Latest.Bosses[I].Name.ToString()+TEXT("\x1f")+Latest.Bosses[I].Value.ToString()+Latest.Bosses[I].ShieldValue.ToString()+(Latest.Bosses[I].bShieldVisible?TEXT("+"):TEXT("-"));
    if(FMath::IsNearlyEqual(Width,CachedWidth,.1f) && Copy==CachedCopy) return;CachedWidth=Width;CachedCopy=Copy;DisplayedHeight=0;
    for(int32 I=0;I<DisplayedCount;++I) {
        if(auto* Name=WidgetTree->FindWidget<UTextBlock>(MCUnifiedBossPrivate::Part(TEXT("Name"),I))) Name->SetWrapTextAt(FMath::Max(24.f,Width-112.f));
        auto* Bounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedBossPrivate::Part(TEXT("Bounds"),I));auto* Surface=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedBossPrivate::Part(TEXT("Frame"),I));
        if(Bounds) {Bounds->SetWidthOverride(Width);Bounds->ClearHeightOverride();}
        if(Surface) Surface->TakeWidget()->SlatePrepass();
        const float Height=FMath::Max(82.f,Surface?float(Surface->GetDesiredSize().Y):82.f);
        if(Bounds) Bounds->SetHeightOverride(Height);
        DisplayedHeight+=Height+12.f;
    }
}
bool UMCUnifiedBossWidget::PreparePresentation()
{
    BuildPresentationTree();if(!WidgetTree || !WidgetTree->FindWidget(TEXT("BossV2Stack")) || !FSlateApplication::IsInitialized()) return false;
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const auto* Style=GetResolvedTheme();if(!Style) return false;
    UDataTable* Table=Backend.IsValid()?Backend->GetResolvedTranslationTable():GetResolvedTranslationTable();
    const TPair<FName,FText> Copy[]={
        {TEXT("UI.Boss.Zombie.Name"),FText::AsCultureInvariant(TEXT("Гнилой страж"))},
        {TEXT("UI.Boss.NutTank.Name"),FText::AsCultureInvariant(TEXT("Ореховый рыцарь"))},
        {TEXT("UI.Boss.NutMage.Name"),FText::AsCultureInvariant(TEXT("Ореховый колдун"))},
        {TEXT("UI.Boss.LongName"),FText::AsCultureInvariant(TEXT("Очень длинное локализованное имя опасного босса"))},
        {TEXT("UI.Boss.Percent"),FText::AsCultureInvariant(TEXT("{Value}%"))},
        {TEXT("UI.Boss.Shield"),FText::AsCultureInvariant(TEXT("Щит {Value}%"))}};
    for(const TCHAR* Culture:{TEXT("ru"),TEXT("en"),TEXT("de"),TEXT("fr"),TEXT("es"),TEXT("pt-BR"),TEXT("zh-Hans"),TEXT("zh-Hant"),TEXT("ja"),TEXT("ko")})
        for(const auto& Item:Copy) {
            const FText Text=UMCUIDataLibrary::ResolveText(Table,Item.Key,Item.Value,Culture);
            for(int32 Size:{22,20,15}) Measure->Measure(Text,Style->MakeFont(Size,true));
        }
    for(int32 Size:{22,20,15}) Measure->Measure(FText::AsCultureInvariant(TEXT("0123456789%+-−.,:/()[] ")),Style->MakeFont(Size,true));
    bPresentationPrepared=true;return true;
}
void UMCUnifiedBossWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);if(Backend.IsValid()) RefreshFromBackend();else UpdateLayout();
}
void UMCUnifiedBossWidget::ApplyEditorPreview(float Time)
{
    if(Backend.IsValid() || !IsDesignTime()) {RefreshFromBackend();return;}
#if WITH_EDITOR
    PreviewTime=FMath::Max(0.f,Time);BuildPresentationTree();
    if(!PreviewSource) {PreviewSource=WidgetTree->ConstructWidget<UMCBossHealthWidget>(UMCBossHealthWidget::StaticClass(),TEXT("BossV2PreviewSource"));PreviewSource->SetDesignerFlags(EWidgetDesignFlags::Designing);PreviewSource->Initialize();}
    PreviewSource->Theme=GetResolvedTheme();PreviewSource->TranslationTable=GetResolvedTranslationTable();
    PreviewSource->PreviewScenario=PreviewScenario;PreviewSource->PreviewState=PreviewState;PreviewSource->PreviewCulture=PreviewCulture;
    PreviewSource->ApplyEditorPreview(Time);Present(PreviewSource->GetPresentationSnapshot());
#endif
}
