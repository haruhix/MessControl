#include "MCHubInteractionPromptWidget.h"
#include "MCWorldUIShapes.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

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
    if(HubInteractionPromptKey) HubInteractionPromptKey->SetText(KeyLabel);
    BuildWorldPresentationTree();
    if(NewPromptKey) NewPromptKey->SetText(KeyLabel);
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
    BindStyle(TEXT("HubInteractionPromptKey"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,36,true);
    Super::ApplyTheme();
    MCWorldUIShapes::TextStyle(NewPromptKey,GetNewWorldPresentationTheme(),36,EMCUIColorToken::Marine);
}

void UMCHubInteractionPromptWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    SetVisibility(PreviewScenario==EMCGameplayUIPreviewScenario::Empty
        ?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
