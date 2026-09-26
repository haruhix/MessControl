#include "MCEmoteWidget.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCExpressionComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Styling/CoreStyle.h"

void UMCEmoteButton::Configure(AMCPlayerController* Player,FName Emote)
{
    Controller=Player; Id=Emote; OnClicked.AddDynamic(this,&UMCEmoteButton::Clicked);
}
void UMCEmoteButton::Clicked()
{
    if (!Controller) return;
    if (auto* Hero=Cast<AMCToothCharacter>(Controller->GetPawn())) Hero->Expression->ServerPlayEmote(Id);
    Controller->ToggleEmotes();
}
void UMCEmoteWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized(); SetIsFocusable(true);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=Root;
    auto* Border=WidgetTree->ConstructWidget<UBorder>(); Border->SetBrushColor(FLinearColor(.025f,.045f,.05f,.97f)); Border->SetPadding(FMargin(24));
    auto* PanelSlot=Root->AddChildToCanvas(Border); PanelSlot->SetAnchors(FAnchors(.5f,.5f)); PanelSlot->SetAlignment(FVector2D(.5f,.5f)); PanelSlot->SetSize(FVector2D(500,540));
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Box);
    auto* Title=WidgetTree->ConstructWidget<UTextBlock>(); Title->SetText(FText::FromString(TEXT("ЭМОЦИИ И ЖЕСТЫ")));
    Title->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),23)); Box->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,0,0,10));
    Hint=WidgetTree->ConstructWidget<UTextBlock>(); Hint->SetAutoWrapText(true); Hint->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));
    Box->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0,0,0,14));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Items=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Items);
    auto* Footer=WidgetTree->ConstructWidget<UTextBlock>(); Footer->SetText(FText::FromString(TEXT("ЛКМ — выбрать     T / Esc — закрыть")));
    Footer->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)); Box->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,14,0,0));
}
void UMCEmoteWidget::Refresh()
{
    if (!Items) return;
    Items->ClearChildren(); Buttons.Reset();
    auto* Hero=GetOwningPlayerPawn<AMCToothCharacter>(); if (!Hero || !Hero->Expression->Library) return;
    for (const auto& Entry:Hero->Expression->Library->Entries)
    {
        auto* Button=WidgetTree->ConstructWidget<UMCEmoteButton>(); Button->Configure(Cast<AMCPlayerController>(GetOwningPlayer()),Entry.Id);
        Button->SetBackgroundColor(FLinearColor(.08f,.25f,.23f));
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(Entry.Label); Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),19));
        Text->SetJustification(ETextJustify::Center); Button->SetContent(Text);
        Items->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,5)); Buttons.Add(Button);
    }
}
void UMCEmoteWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt); if (!IsVisible()) return;
    auto* Hero=GetOwningPlayerPawn<AMCToothCharacter>();
    for (const auto& Button:Buttons)
    {
        const auto* Lib=Hero?Hero->Expression->Library.Get():nullptr;
        const auto* E=Lib?Lib->Entries.FindByPredicate([&](const FMCEmoteEntry& Entry){return Entry.Id==Button->Id;}):nullptr;
        Button->SetIsEnabled(E && Hero->Expression->CanPlay(*E) && Hero->Expression->EmoteAlpha()<.01f);
    }
    if (Hint) Hint->SetText(FText::FromString(TEXT("Жесты — стоя на земле. Движение, работа и боль прерывают жест. Мимика доступна отдельно.")));
}
FReply UMCEmoteWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if (Event.GetKey()==EKeys::T || Event.GetKey()==EKeys::Escape)
    {
        if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->ToggleEmotes();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(Geometry,Event);
}
