#include "MCHubInteractionPromptWidget.h"
#include "MCWorldUIShapes.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

namespace MCHubPromptKeycap
{
int32 FontSizeFor(const FText& Label) {return Label.ToString().Len()>1?16:36;}
void ApplyLabel(UTextBlock* Key,const FText& Label)
{
    if(!Key) return;
    // A mouse-button label is one keycap, never a vertical per-character caption.
    Key->SetAutoWrapText(false);Key->SetWrapTextAt(0.f);
    Key->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
    Key->SetText(Label);
    auto Font=Key->GetFont();Font.Size=FontSizeFor(Label);Key->SetFont(Font);
}
}

UMCHubInteractionPromptWidget::UMCHubInteractionPromptWidget() {bUseVersionedWorldPresentation=true;}

void UMCHubInteractionPromptWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMCHubInteractionPromptWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!WidgetTree->RootWidget) {
        auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HubInteractionPromptRoot"));
        Size->SetMinDesiredWidth(60.f);Size->SetMinDesiredHeight(62.f);WidgetTree->RootWidget=Size;
        auto* Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HubInteractionPromptPanel"));
        Panel->SetPadding(FMargin(14.f,6.f));Panel->SetHorizontalAlignment(HAlign_Center);Panel->SetVerticalAlignment(VAlign_Center);
        Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,12.f));Size->SetContent(Panel);
        auto* Key=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HubInteractionPromptKey"));
        Key->SetJustification(ETextJustify::Center);Panel->SetContent(Key);
    }
    HubInteractionPromptKey=WidgetTree->FindWidget<UTextBlock>(TEXT("HubInteractionPromptKey"));
    MCHubPromptKeycap::ApplyLabel(HubInteractionPromptKey,KeyLabel);
    BindStyle(TEXT("HubInteractionPromptKey"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,MCHubPromptKeycap::FontSizeFor(KeyLabel),true);
    BuildWorldPresentationTree();
    MCHubPromptKeycap::ApplyLabel(NewPromptKey,KeyLabel);
}

void UMCHubInteractionPromptWidget::BuildWorldPresentationTree()
{
    if(!bUseVersionedWorldPresentation || !WidgetTree) return;
    auto* NewRoot=WidgetTree->FindWidget<USizeBox>(TEXT("WorldPromptV2Root"));
    if(!NewRoot) {
        NewRoot=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WorldPromptV2Root"));
        NewRoot->SetMinDesiredWidth(60);NewRoot->SetMaxDesiredWidth(220);NewRoot->SetMinDesiredHeight(62);
        auto* Plate=MCWorldUIShapes::Panel(WidgetTree,TEXT("WorldPromptV2Plate"),GetNewWorldPresentationTheme(),EMCUIColorToken::Cream,FMargin(14,6));NewRoot->SetContent(Plate);
        auto* Caption=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldPromptV2Key"),188);Plate->SetContentForSlot(TEXT("Content"),Caption);
    }
    InstallWorldPresentation(NewRoot);
    NewPromptKey=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldPromptV2Key"));
}

void UMCHubInteractionPromptWidget::ApplyTheme()
{
    BuildPresentationTree();
    BindStyle(TEXT("HubInteractionPromptPanel"),EMCUIColorToken::Accent,EMCUIStyleTarget::Background);
    BindStyle(TEXT("HubInteractionPromptKey"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,MCHubPromptKeycap::FontSizeFor(KeyLabel),true);
    Super::ApplyTheme();
    MCWorldUIShapes::TextStyle(NewPromptKey,GetNewWorldPresentationTheme(),MCHubPromptKeycap::FontSizeFor(KeyLabel),EMCUIColorToken::Marine);
    // Both mouse labels are known before pickup. Prepare their smaller glyphs
    // with the ordinary prompt, behind the location's existing UI readiness.
    const TArray<FText> MouseKeys={FText::AsCultureInvariant(TEXT("RMB")),FText::AsCultureInvariant(TEXT("ПКМ"))};
    const float Scale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    if(const auto* Current=GetResolvedTheme()) MCWorldUIShapes::PrimeText(Current->MakeGameplayFont(16,true),MouseKeys,Scale);
    if(const auto* New=GetNewWorldPresentationTheme()) MCWorldUIShapes::PrimeText(New->MakeFont(16),MouseKeys,Scale);
}

void UMCHubInteractionPromptWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    SetVisibility(PreviewScenario==EMCGameplayUIPreviewScenario::Empty
        ?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
