#include "MCMainMenuWidget.h"
#include "MCVoiceSettingsWidget.h"
#include "MCGameplayUIFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"

void UMCMainMenuWidget::EnsureVoiceSettingsPresentation()
{
    if(!WidgetTree || !SettingsSections) return;
    VoiceSettings=WidgetTree->FindWidget<UMCVoiceSettingsWidget>(PageWidgetName(TEXT("VoicePanel")));
    if(VoiceSettings) return;
    auto* Tabs=WidgetTree->FindWidget<UHorizontalBox>(PageWidgetName(TEXT("Tabs")));
    if(!Tabs || SettingsSections->GetChildrenCount()!=4) return;
    auto* Tab=WidgetTree->ConstructWidget<UMCMainMenuActionButton>(UMCMainMenuActionButton::StaticClass(),PageWidgetName(TEXT("TabVoice")));
    Tab->Configure(this,EMCMainMenuAction::SettingsVoice);
    auto* Caption=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),PageWidgetName(TEXT("TabVoiceLabel")));
    Caption->SetText(Localized(TEXT("UI.Settings.Tab.Voice"),NSLOCTEXT("MCMainMenu","VoiceTab","Голос")));Caption->SetJustification(ETextJustify::Center);Tab->SetContent(Caption);
    auto* TabSlot=Tabs->AddChildToHorizontalBox(Tab);TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TabSlot->SetPadding(FMargin(0,0,4,0));
    TextBindings.Add(Caption->GetFName(),TEXT("UI.Settings.Tab.Voice"));
    BindStyle(Tab->GetFName(),EMCUIColorToken::Button,EMCUIStyleTarget::Button);BindStyle(Caption->GetFName(),EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,14,true);
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),PageWidgetName(TEXT("VoiceSection")));
    SettingsSections->AddChild(Scroll);
    VoiceSettings=WidgetTree->ConstructWidget<UMCVoiceSettingsWidget>(UMCGameplayUIFactory::ResolveClass(TEXT("VoiceSettings"),UMCVoiceSettingsWidget::StaticClass()),PageWidgetName(TEXT("VoicePanel")));
    if(VoiceSettings) {VoiceSettings->Theme=GetResolvedTheme();Scroll->AddChild(VoiceSettings);VoiceSettings->BuildPresentationTree();VoiceSettings->ApplyTheme();}
}
