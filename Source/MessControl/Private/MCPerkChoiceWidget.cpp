#include "MCPerkChoiceWidget.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

namespace MCPerkCardPrivate
{
    const FLinearColor Cream(.98f, .97f, .90f);
    const FLinearColor Muted(.58f, .68f, .73f);
    const FLinearColor Mint(.32f, .91f, .72f);
    const FLinearColor Coral(1.f, .38f, .48f);

    UTextBlock* Label(UWidgetTree* Tree, const FText& Text, int32 Size, FLinearColor Color = Cream)
    {
        auto* Result = Tree->ConstructWidget<UTextBlock>();
        Result->SetText(Text);
        Result->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size));
        Result->SetColorAndOpacity(FSlateColor(Color));
        Result->SetJustification(ETextJustify::Center);
        Result->SetAutoWrapText(true);
        return Result;
    }
}

UMCPerkCardButton::UMCPerkCardButton() { InitIsFocusable(false); }
void UMCPerkCardButton::Configure(AMCPlayerController* Player, int32 Index)
{
    Controller = Player;
    ChoiceIndex = Index;
    OnClicked.AddUniqueDynamic(this, &UMCPerkCardButton::Choose);
}
void UMCPerkCardButton::Choose()
{
    if (Controller.IsValid()) Controller->ChooseRewardPerk(ChoiceIndex);
}

void UMCPerkChoiceWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);

    // Scale one compact panel to fit small windows; the world remains visible behind it.
    auto* Backdrop = WidgetTree->ConstructWidget<UBorder>();
    WidgetTree->RootWidget = Backdrop;
    Backdrop->SetBrushColor(FLinearColor(.004f, .010f, .016f, .84f));
    Backdrop->SetPadding(FMargin(38));
    Backdrop->SetHorizontalAlignment(HAlign_Fill);
    Backdrop->SetVerticalAlignment(VAlign_Fill);
    auto* Scale = WidgetTree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);
    Scale->SetStretchDirection(EStretchDirection::DownOnly);
    Backdrop->SetContent(Scale);
    auto* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(1000);
    Size->SetHeightOverride(540);
    Scale->SetContent(Size);
    auto* Main = WidgetTree->ConstructWidget<UVerticalBox>();
    Size->SetContent(Main);

    Title = MCPerkCardPrivate::Label(WidgetTree, FText::GetEmpty(), 30);
    Main->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 8));
    Subtitle = MCPerkCardPrivate::Label(WidgetTree, FText::GetEmpty(), 15, MCPerkCardPrivate::Muted);
    Main->AddChildToVerticalBox(Subtitle)->SetPadding(FMargin(0, 0, 0, 24));
    Cards = WidgetTree->ConstructWidget<UHorizontalBox>();
    Main->AddChildToVerticalBox(Cards)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    Opening = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* OpeningSlot = Main->AddChildToVerticalBox(Opening);
    OpeningSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    OpeningSlot->SetHorizontalAlignment(HAlign_Center);
    OpeningSlot->SetVerticalAlignment(VAlign_Center);
    auto* OpeningSymbol = MCPerkCardPrivate::Label(WidgetTree, FText::FromString(TEXT("◇")), 86, MCPerkCardPrivate::Mint);
    Opening->AddChildToVerticalBox(OpeningSymbol)->SetPadding(FMargin(0, 0, 0, 24));
    auto* ProgressSize = WidgetTree->ConstructWidget<USizeBox>();
    ProgressSize->SetWidthOverride(520);
    ProgressSize->SetHeightOverride(14);
    OpeningProgress = WidgetTree->ConstructWidget<UProgressBar>();
    OpeningProgress->SetFillColorAndOpacity(MCPerkCardPrivate::Mint);
    ProgressSize->SetContent(OpeningProgress);
    Opening->AddChildToVerticalBox(ProgressSize);
    OpeningTime = MCPerkCardPrivate::Label(WidgetTree, FText::GetEmpty(), 16, MCPerkCardPrivate::Muted);
    Opening->AddChildToVerticalBox(OpeningTime)->SetPadding(FMargin(0, 16, 0, 0));
    Hint = MCPerkCardPrivate::Label(WidgetTree, FText::GetEmpty(), 14, MCPerkCardPrivate::Muted);
    Main->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0, 22, 0, 0));
}

void UMCPerkChoiceWidget::ShowOpening(double ServerEndsAt)
{
    bShowingChoices = false;
    bSelectionPending = false;
    OpeningEndsAt = ServerEndsAt;
    Cards->SetVisibility(ESlateVisibility::Collapsed);
    Opening->SetVisibility(ESlateVisibility::Visible);
    Title->SetText(FText::FromString(TEXT("Вскрытие щёткой…")));
    Subtitle->SetText(FText::FromString(TEXT("Открываем замок")));
    Hint->SetText(FText::GetEmpty());
    OpeningProgress->SetPercent(0);
    OpeningTime->SetText(FText::GetEmpty());
}

void UMCPerkChoiceWidget::UpdateOpeningProgress(double ServerNow)
{
    if (bShowingChoices) return;
    const float Remaining = FMath::Max(0., OpeningEndsAt - ServerNow);
    OpeningProgress->SetPercent(FMath::Clamp(1.f - Remaining / 5.f, 0.f, 1.f));
    OpeningTime->SetText(Remaining > 0 ? FText::FromString(FString::Printf(TEXT("Осталось %.1f с"), Remaining))
        : FText::FromString(TEXT("Замок открыт…")));
}

void UMCPerkChoiceWidget::ShowChoices(const TArray<FName>& IDs, EMCPerkPolarity Polarity)
{
    bShowingChoices = true;
    bSelectionPending = false;
    Opening->SetVisibility(ESlateVisibility::Collapsed);
    Cards->SetVisibility(ESlateVisibility::Visible);
    Cards->ClearChildren();
    Buttons.Reset();
    Title->SetText(FText::FromString(TEXT("Выберите один перк")));
    Subtitle->SetText(FText::FromString(TEXT("Два других исчезнут после выбора")));
    Hint->SetText(FText::FromString(TEXT("Нажмите на карточку или клавишу 1 / 2 / 3")));
    const bool Positive = Polarity == EMCPerkPolarity::Positive;
    const FLinearColor Accent = Positive ? MCPerkCardPrivate::Mint : MCPerkCardPrivate::Coral;
    auto* PC = GetOwningPlayer<AMCPlayerController>();
    const auto* State = PC ? PC->GetPlayerState<AMCPlayerState>() : nullptr;

    for (int32 Index = 0; Index < IDs.Num() && Index < 3; ++Index)
    {
        FMCPerkDefinition Definition;
        if (State && State->Perks) State->Perks->GetPerkDefinition(IDs[Index], Definition);
        auto* Button = WidgetTree->ConstructWidget<UMCPerkCardButton>();
        Button->Configure(PC, Index);
        FButtonStyle Style = Button->GetStyle();
        Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.023f, .047f, .060f), 16.f, Accent.CopyWithNewOpacity(.60f), 2.f));
        Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.044f, .085f, .093f), 16.f, Accent, 3.f));
        Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.025f, .060f, .071f), 16.f, Accent, 3.f));
        Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.023f, .047f, .060f), 16.f, Accent.CopyWithNewOpacity(.25f), 2.f));
        Style.SetNormalPadding(FMargin(22, 20));
        Style.SetPressedPadding(FMargin(22, 20));
        Button->SetStyle(Style);
        auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
        Button->SetContent(Column);
        auto* Badge = MCPerkCardPrivate::Label(WidgetTree,
            FText::FromString(Positive ? TEXT("ПОЛОЖИТЕЛЬНЫЙ") : TEXT("НЕГАТИВНЫЙ")), 12, Accent);
        Column->AddChildToVerticalBox(Badge)->SetPadding(FMargin(0, 0, 0, 24));

        auto* IconSize = WidgetTree->ConstructWidget<USizeBox>();
        IconSize->SetWidthOverride(106);
        IconSize->SetHeightOverride(106);
        auto* IconBorder = WidgetTree->ConstructWidget<UBorder>();
        IconBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.055f, .095f, .108f), 20.f));
        IconBorder->SetPadding(FMargin(14));
        IconSize->SetContent(IconBorder);
        if (!Definition.Icon.IsNull())
        {
            auto* Icon = WidgetTree->ConstructWidget<UImage>();
            Icon->SetBrushFromSoftTexture(Definition.Icon);
            IconBorder->SetContent(Icon);
        }
        else IconBorder->SetContent(MCPerkCardPrivate::Label(WidgetTree, FText::FromString(TEXT("?")), 44, Accent));
        auto* IconSlot = Column->AddChildToVerticalBox(IconSize);
        IconSlot->SetHorizontalAlignment(HAlign_Center);
        IconSlot->SetPadding(FMargin(0, 0, 0, 24));
        const FText Name = Definition.DisplayName.IsEmpty() ? FText::FromName(IDs[Index]) : Definition.DisplayName;
        auto* NameLabel = MCPerkCardPrivate::Label(WidgetTree, Name, 21);
        Column->AddChildToVerticalBox(NameLabel)->SetPadding(FMargin(0, 0, 0, 12));
        auto* Description = MCPerkCardPrivate::Label(WidgetTree,
            FText::FromString(Definition.Description.ToString().Replace(TEXT("<br>"), TEXT("\n"))), 14, MCPerkCardPrivate::Muted);
        auto* DescriptionSlot = Column->AddChildToVerticalBox(Description);
        DescriptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        DescriptionSlot->SetVerticalAlignment(VAlign_Top);
        Column->AddChildToVerticalBox(MCPerkCardPrivate::Label(WidgetTree,
            FText::FromString(FString::Printf(TEXT("ВЫБРАТЬ   [%d]"), Index + 1)), 15, Accent));
        auto* CardSlot = Cards->AddChildToHorizontalBox(Button);
        CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CardSlot->SetPadding(FMargin(10, 0));
        Buttons.Add(Button);
    }
}

void UMCPerkChoiceWidget::SetSelectionPending(bool bPending)
{
    bSelectionPending = bPending;
    for (UMCPerkCardButton* Button : Buttons) if (Button) Button->SetIsEnabled(!bPending);
    Hint->SetText(FText::FromString(bPending ? TEXT("Получаем перк…") : TEXT("Нажмите на карточку или клавишу 1 / 2 / 3")));
}

FReply UMCPerkChoiceWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (bShowingChoices && !bSelectionPending && !Event.IsRepeat())
    {
        int32 Index = INDEX_NONE;
        if (Event.GetKey() == EKeys::One || Event.GetKey() == EKeys::NumPadOne) Index = 0;
        else if (Event.GetKey() == EKeys::Two || Event.GetKey() == EKeys::NumPadTwo) Index = 1;
        else if (Event.GetKey() == EKeys::Three || Event.GetKey() == EKeys::NumPadThree) Index = 2;
        if (Index != INDEX_NONE)
        {
            if (auto* PC = GetOwningPlayer<AMCPlayerController>()) PC->ChooseRewardPerk(Index);
            return FReply::Handled();
        }
    }
    // A choice cannot be dismissed accidentally; death and invalid rewards close it at the controller.
    if (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::F3 || Event.GetKey() == EKeys::T)
        return FReply::Handled();
    return Super::NativeOnKeyDown(Geometry, Event);
}
