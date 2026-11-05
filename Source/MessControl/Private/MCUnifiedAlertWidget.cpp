#include "MCUnifiedAlertWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCUIDataLibrary.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

namespace MCUnifiedAlertPrivate
{
    FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
}
UMCUnifiedAlertWidget::UMCUnifiedAlertWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;PreviewUIVersion=EMCUIVersion::New;
    SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UMCUnifiedAlertWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) return;
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("AlertV2Root"));WidgetTree->RootWidget=Root;
    auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("AlertV2Bounds"));Bounds->SetWidthOverride(800);
    auto* Anchor=Root->AddChildToCanvas(Bounds);Anchor->SetAnchors(FAnchors(.5f,0));Anchor->SetAlignment(FVector2D(.5f,0));Anchor->SetPosition(FVector2D(0,18));Anchor->SetAutoSize(true);
    auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),TEXT("AlertV2Frame"));
    Shape->Label=FText::GetEmpty();Shape->LabelKey=NAME_None;Shape->bUseNativeLayout=false;Shape->bUseThemeShape=false;Shape->bCustomColors=true;
    Shape->CustomFill=MCUnifiedAlertPrivate::Color(TEXT("30333F"));Shape->CustomText=MCUnifiedAlertPrivate::Color(TEXT("FFF9F8"));
    Shape->CustomOutline=MCUnifiedAlertPrivate::Color(TEXT("718080"));Shape->CustomShadow=MCUnifiedAlertPrivate::Color(TEXT("879094"));
    Shape->ContentPadding=FMargin(18,12);Shape->CornerRadius=16;Shape->OutlineWidth=4;Shape->ShadowOffset=FVector2D(8,9);
    Shape->SetVisibility(ESlateVisibility::HitTestInvisible);Shape->Initialize();Shape->BuildPresentationTree();Shape->ApplyTheme();Bounds->SetContent(Shape);
    auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("AlertV2Text"));
    Text->SetJustification(ETextJustify::Center);Text->SetAutoWrapText(false);Text->SetWrapTextAt(764);Text->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
    Text->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);Text->SetColorAndOpacity(MCUnifiedAlertPrivate::Color(TEXT("FFF9F8")));Text->SetFont(GetResolvedTheme()->MakeFont(24,true));
    Shape->SetContentForSlot(TEXT("Content"),Text);Bounds->SetRenderTransformPivot(FVector2D(.5f,.5f));Bounds->SetVisibility(ESlateVisibility::Collapsed);
}
void UMCUnifiedAlertWidget::ApplyTheme()
{
    bPresentationPrepared=false;UMCGameplayUIWidget::ApplyTheme();CachedViewportWidth=-1.f;CachedText.Reset();
    if(WidgetTree) if(auto* Text=WidgetTree->FindWidget<UTextBlock>(TEXT("AlertV2Text"))) {
        Text->SetFont(GetResolvedTheme()->MakeFont(24,true));Text->SetColorAndOpacity(MCUnifiedAlertPrivate::Color(TEXT("FFF9F8")));
    }
    if(Backend.IsValid()) RefreshFromBackend();else Present(Latest);
}
void UMCUnifiedAlertWidget::SetBackend(UMCGameplayAlertWidget* Source)
{
    Backend=Source;RefreshFromBackend();
}
void UMCUnifiedAlertWidget::SetBossStackPresentation(int32 Count,float Height)
{
    StackCount=Count<0?-1:FMath::Clamp(Count,0,3);StackHeight=FMath::Max(0.f,Height);Present(Latest);
}
void UMCUnifiedAlertWidget::RefreshFromBackend()
{
    if(Backend.IsValid()) Present(Backend->GetPresentationSnapshot());
    else if(!IsDesignTime()) {FMCGameplayAlertPresentationSnapshot Empty;Present(Empty);}
}
void UMCUnifiedAlertWidget::Present(const FMCGameplayAlertPresentationSnapshot& Snapshot)
{
    BuildPresentationTree();Latest=Snapshot;if(!WidgetTree) return;
    auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("AlertV2Bounds"));auto* Text=WidgetTree->FindWidget<UTextBlock>(TEXT("AlertV2Text"));if(!Bounds || !Text) return;
    Bounds->SetVisibility(Snapshot.bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(!Snapshot.bVisible) return;
    if(!Text->GetText().EqualTo(Snapshot.Text)) Text->SetText(Snapshot.Text);
    const float Painted=GetPaintSpaceGeometry().GetLocalSize().X;
    const float Available=Painted>1.f?Painted:float(GetCachedGeometry().GetLocalSize().X);
    const float Maximum=FMath::Min(880.f,FMath::Max(160.f,(Available>1.f?Available:1920.f)-32.f));
    const FString Copy=Snapshot.Text.ToString();
    if(!FMath::IsNearlyEqual(Maximum,CachedViewportWidth,.1f) || Copy!=CachedText) {
        CachedViewportWidth=Maximum;CachedText=Copy;
        Text->SetWrapTextAt(0);Text->TakeWidget()->SlatePrepass();
        const float Width=FMath::Clamp(float(Text->GetDesiredSize().X)+44.f,FMath::Min(360.f,Maximum),Maximum);
        Text->SetWrapTextAt(FMath::Max(24.f,Width-44.f));Text->TakeWidget()->SlatePrepass();
        Bounds->SetWidthOverride(Width);Bounds->SetMinDesiredHeight(FMath::Max(60.f,float(Text->GetDesiredSize().Y)+30.f));
    }
    const float Top=StackCount>=0?18.f+(StackCount>0?StackHeight+5.f:0.f):Snapshot.TopOffset;
    if(auto* Cell=Cast<UCanvasPanelSlot>(Bounds->Slot)) Cell->SetPosition(FVector2D(0,Top));
    Bounds->SetRenderOpacity(Snapshot.Reveal);Bounds->SetRenderScale(Snapshot.RenderScale);Bounds->SetRenderTranslation(Snapshot.RenderTranslation);
}
bool UMCUnifiedAlertWidget::PreparePresentation()
{
    BuildPresentationTree();if(!WidgetTree || !WidgetTree->FindWidget(TEXT("AlertV2Text")) || !FSlateApplication::IsInitialized()) return false;
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const auto* Style=GetResolvedTheme();if(!Style) return false;
    UDataTable* Table=Backend.IsValid()?Backend->GetResolvedTranslationTable():GetResolvedTranslationTable();
    const TPair<FName,FText> Copy[]={
        {TEXT("UI.Alert.OwnedMessage"),FText::AsCultureInvariant(TEXT("{Owner} · {Message}"))},
        {TEXT("UI.Alert.BossWarningPreview"),FText::AsCultureInvariant(TEXT("БЕРЕГИСЬ УДАРА!"))},
        {TEXT("UI.Alert.LongPreview"),FText::AsCultureInvariant(TEXT("Берегись удара! Держись рядом с опорой и помоги товарищу выбраться из опасной зоны."))}};
    for(const TCHAR* Culture:{TEXT("ru"),TEXT("en"),TEXT("de"),TEXT("fr"),TEXT("es"),TEXT("pt-BR"),TEXT("zh-Hans"),TEXT("zh-Hant"),TEXT("ja"),TEXT("ko")})
        for(const auto& Item:Copy) Measure->Measure(UMCUIDataLibrary::ResolveText(Table,Item.Key,Item.Value,Culture),Style->MakeFont(24,true));
    Measure->Measure(FText::AsCultureInvariant(TEXT("0123456789%+-−.,:/()[] ")),Style->MakeFont(24,true));
    if(!Latest.Text.IsEmpty()) Measure->Measure(Latest.Text,Style->MakeFont(24,true));
    bPresentationPrepared=true;return true;
}
void UMCUnifiedAlertWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);if(Backend.IsValid()) RefreshFromBackend();else if(IsDesignTime()) Present(Latest);
}
void UMCUnifiedAlertWidget::ApplyEditorPreview(float Time)
{
    if(Backend.IsValid() || !IsDesignTime()) {RefreshFromBackend();return;}
#if WITH_EDITOR
    PreviewTime=FMath::Max(0.f,Time);BuildPresentationTree();
    if(!PreviewSource) {PreviewSource=WidgetTree->ConstructWidget<UMCGameplayAlertWidget>(UMCGameplayAlertWidget::StaticClass(),TEXT("AlertV2PreviewSource"));PreviewSource->SetDesignerFlags(EWidgetDesignFlags::Designing);PreviewSource->Initialize();}
    PreviewSource->Theme=GetResolvedTheme();PreviewSource->TranslationTable=GetResolvedTranslationTable();
    PreviewSource->PreviewScenario=PreviewScenario;PreviewSource->PreviewState=PreviewState;PreviewSource->PreviewCulture=PreviewCulture;
    PreviewSource->ApplyEditorPreview(Time);Present(PreviewSource->GetPresentationSnapshot());
#endif
}
