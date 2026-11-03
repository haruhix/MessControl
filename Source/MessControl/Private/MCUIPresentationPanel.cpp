#include "MCUIPresentationPanel.h"

#include "MCGameplayUIFactory.h"
#include "MCUniversalUIWidget.h"
#include "MCUIPresentationSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SafeZone.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCUIPresentation"

UMCUIPresentationSelectorWidget::UMCUIPresentationSelectorWidget() {bUseNativeLayout=false;}

void UMCUIPresentationSelectorWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!WidgetTree->RootWidget)
    {
        auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>();WidgetTree->RootWidget=Stack;
        auto* Action=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),TEXT("SwitchUIVersion"));
        Action->Kind=EMCUniversalUIKind::Button;Action->FontSize=20;Action->Initialize();Action->BuildPresentationTree();
        Stack->AddChildToVerticalBox(Action)->SetPadding(FMargin(0,0,0,10));
        auto* Hint=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("UIVersionHint"));
        Hint->SetAutoWrapText(true);Stack->AddChildToVerticalBox(Hint);
        BindStyle(TEXT("UIVersionHint"),EMCUIColorToken::Cream,EMCUIStyleTarget::Text,14,false);
    }
    if(auto* Action=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("SwitchUIVersion")))
        Action->OnClicked.AddUniqueDynamic(this,&ThisClass::ToggleVersion);
}

void UMCUIPresentationSelectorWidget::ApplyTheme()
{
    Super::ApplyTheme();if(!WidgetTree) return;
    const bool New=UMCGameplayUIFactory::GetPresentationVersion()==EMCUIVersion::New;
    if(auto* Action=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("SwitchUIVersion")))
        Action->SetLabel(NAME_None,Localized(New?TEXT("UI.V2.Switch.ToCurrent"):TEXT("UI.V2.Switch.ToNew"),
            New?LOCTEXT("ToCurrent","UI: Новый → Текущий"):LOCTEXT("ToNew","UI: Текущий → Новый")));
    if(auto* Hint=WidgetTree->FindWidget<UTextBlock>(TEXT("UIVersionHint")))
        Hint->SetText(Localized(TEXT("UI.V2.Switch.Hint"),LOCTEXT("SwitchHint","Выбор только для этого устройства. Новый интерфейс переносится по этапам.")));
}

void UMCUIPresentationSelectorWidget::ToggleVersion()
{
    if(IsDesignTime()) return;
    const auto Current=UMCGameplayUIFactory::GetPresentationVersion();
    UMCGameplayUIFactory::SetPresentationVersion(Current==EMCUIVersion::Current?EMCUIVersion::New:EMCUIVersion::Current);
    ApplyTheme();
}

UMCUIPresentationPanelWidget::UMCUIPresentationPanelWidget() {bUseNativeLayout=false;SetIsFocusable(true);}

void UMCUIPresentationPanelWidget::BuildPresentationTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    auto* Back=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("F3Backdrop"));WidgetTree->RootWidget=Back;
    Back->SetPadding(FMargin(0));BindStyle(TEXT("F3Backdrop"),EMCUIColorToken::Backdrop,EMCUIStyleTarget::Background);
    auto* Safe=WidgetTree->ConstructWidget<USafeZone>();Back->SetContent(Safe);
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Safe->SetContent(Canvas);
    auto* Panel=WidgetTree->ConstructWidget<UMCUniversalUIWidget>();Panel->Fill=EMCUIColorToken::Marine;Panel->Label=FText::GetEmpty();Panel->Initialize();Panel->BuildPresentationTree();
    auto* CanvasSlot=Canvas->AddChildToCanvas(Panel);CanvasSlot->SetAnchors(FAnchors(.12f,.08f,.88f,.92f));CanvasSlot->SetOffsets(FMargin(0));
    auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>();Panel->SetContentForSlot(TEXT("Content"),Stack);
    auto* Header=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("F3Title"));
    Header->SetText(LOCTEXT("F3Title","ИНТЕРФЕЙС · F3 / Esc — закрыть"));Header->SetAutoWrapText(true);
    Stack->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,14));
    TextBindings.Add(TEXT("F3Title"),TEXT("UI.V2.F3.Title"));BindStyle(TEXT("F3Title"),EMCUIColorToken::Cream,EMCUIStyleTarget::Text,22,true);
    auto* Selector=WidgetTree->ConstructWidget<UMCUIPresentationSelectorWidget>();
    Stack->AddChildToVerticalBox(Selector)->SetPadding(FMargin(0,0,0,12));
    auto* Specimen=WidgetTree->ConstructWidget<UMCUniversalUIPreviewWidget>();
    Stack->AddChildToVerticalBox(Specimen)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

FReply UMCUIPresentationPanelWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::F3 || Event.GetKey()==EKeys::Escape)
    {
        if(auto* GI=GetGameInstance()) if(auto* Subsystem=GI->GetSubsystem<UMCUIPresentationSubsystem>()) Subsystem->ClosePanel();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(Geometry,Event);
}

#undef LOCTEXT_NAMESPACE
