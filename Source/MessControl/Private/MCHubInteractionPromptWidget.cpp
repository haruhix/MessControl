#include "MCHubInteractionPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

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
}

void UMCHubInteractionPromptWidget::ApplyTheme()
{
    BuildPresentationTree();
    BindStyle(TEXT("HubInteractionPromptPanel"),EMCUIColorToken::Accent,EMCUIStyleTarget::Background);
    BindStyle(TEXT("HubInteractionPromptKey"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,36,true);
    Super::ApplyTheme();
}

void UMCHubInteractionPromptWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    SetVisibility(PreviewScenario==EMCGameplayUIPreviewScenario::Empty
        ?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
