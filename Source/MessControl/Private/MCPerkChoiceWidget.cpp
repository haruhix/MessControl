#include "MCPerkChoiceWidget.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
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
    Main->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 6));
    Subtitle = MCPerkCardPrivate::Label(WidgetTree, FText::GetEmpty(), 15, MCPerkCardPrivate::Muted);
    Main->AddChildToVerticalBox(Subtitle)->SetPadding(FMargin(0, 0, 0, 18));
    Cards = WidgetTree->ConstructWidget<UHorizontalBox>();
    Main->AddChildToVerticalBox(Cards)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    Hint = MCPerkCardPrivate::Label(WidgetTree, FText::GetEmpty(), 14, MCPerkCardPrivate::Muted);
    Main->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0, 18, 0, 0));
}

void UMCPerkChoiceWidget::ShowChoices(const TArray<FName>& IDs, EMCPerkPolarity Polarity)
{
    BuildChoices(IDs,Polarity,false);
}

void UMCPerkChoiceWidget::ShowLevelChoices(const TArray<FName>& IDs,int32 TeamLevel,int32 PendingChoices)
{
    BuildChoices(IDs,EMCPerkPolarity::Positive,true);
    UpdateLevelChoiceHeader(TeamLevel,PendingChoices);
}

void UMCPerkChoiceWidget::UpdateLevelChoiceHeader(int32 TeamLevel,int32 PendingChoices)
{
    if (!Title || !Subtitle) return;
    Title->SetText(FText::FromString(FString::Printf(TEXT("КОМАНДА · УРОВЕНЬ %d"),TeamLevel)));
    Subtitle->SetText(FText::FromString(PendingChoices>1
        ? FString::Printf(TEXT("Выберите личный перк · осталось выборов: %d"),PendingChoices)
        : FString(TEXT("Общие усилия открыли уровень. Выберите личный перк."))));
}

void UMCPerkChoiceWidget::BuildChoices(const TArray<FName>& IDs,EMCPerkPolarity Polarity,bool bPersonal)
{
    bShowingChoices = true;
    bSelectionPending = false;
    Cards->SetVisibility(ESlateVisibility::Visible);
    Cards->ClearChildren();
    Buttons.Reset();
    Title->SetText(FText::FromString(TEXT("Выберите один перк")));
    Subtitle->SetText(FText::FromString(TEXT("Два других исчезнут после выбора")));
    Hint->SetText(FText::FromString(TEXT("Нажмите на карточку или клавишу 1 / 2 / 3")));
    const bool Positive = Polarity == EMCPerkPolarity::Positive;
    auto* PC = GetOwningPlayer<AMCPlayerController>();
    const auto* State = PC ? PC->GetPlayerState<AMCPlayerState>() : nullptr;

    for (int32 Index = 0; Index < IDs.Num() && Index < 3; ++Index)
    {
        FMCPerkDefinition Definition;
        if (State && State->Perks) State->Perks->GetPerkDefinition(IDs[Index], Definition);
        const bool Legendary=Definition.Rarity==EMCPerkRarity::Legendary;
        const bool Rare=Definition.Rarity==EMCPerkRarity::Rare;
        const FLinearColor Accent=Legendary?FLinearColor(1.f,.70f,.14f):Rare?FLinearColor(.30f,.58f,1.f):Positive?MCPerkCardPrivate::Mint:MCPerkCardPrivate::Coral;
        auto* Button = WidgetTree->ConstructWidget<UMCPerkCardButton>();
        Button->Configure(PC, Index);
        FButtonStyle Style = Button->GetStyle();
        Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.023f, .047f, .060f), 16.f, Accent.CopyWithNewOpacity(.60f), 2.f));
        Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.044f, .085f, .093f), 16.f, Accent, 3.f));
        Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.025f, .060f, .071f), 16.f, Accent, 3.f));
        Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.023f, .047f, .060f), 16.f, Accent.CopyWithNewOpacity(.25f), 2.f));
        Style.SetNormalPadding(FMargin(0));
        Style.SetPressedPadding(FMargin(0));
        Button->SetStyle(Style);
        auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* ContentSlot=Cast<UButtonSlot>(Button->SetContent(Column));
        ContentSlot->SetPadding(FMargin(16,14));
        ContentSlot->SetHorizontalAlignment(HAlign_Fill);
        ContentSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Badge = MCPerkCardPrivate::Label(WidgetTree,
            FText::FromString(Legendary?(bPersonal?TEXT("ЛЕГЕНДАРНЫЙ · ТОЛЬКО ВАМ"):TEXT("ЛЕГЕНДАРНЫЙ · ВСЯ КОМАНДА")):Rare?TEXT("РЕДКИЙ · ТОЛЬКО ВАМ"):bPersonal?TEXT("ЛИЧНЫЙ ПЕРК"):Positive ? TEXT("ПОЛОЖИТЕЛЬНЫЙ") : TEXT("НЕГАТИВНЫЙ")), 12, Accent);
        Column->AddChildToVerticalBox(Badge)->SetPadding(FMargin(0, 0, 0, 10));

        auto* IconSize = WidgetTree->ConstructWidget<USizeBox>();
        IconSize->SetWidthOverride(80);
        IconSize->SetHeightOverride(80);
        auto* IconBorder = WidgetTree->ConstructWidget<UBorder>();
        IconBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.055f, .095f, .108f), 20.f));
        IconBorder->SetPadding(FMargin(10));
        IconSize->SetContent(IconBorder);
        if (!Definition.Icon.IsNull())
        {
            auto* Icon = WidgetTree->ConstructWidget<UImage>();
            Icon->SetBrushFromSoftTexture(Definition.Icon);
            IconBorder->SetContent(Icon);
        }
        else IconBorder->SetContent(MCPerkCardPrivate::Label(WidgetTree, FText::FromString(Definition.ToolUpgrade==EMCToolUpgrade::None?TEXT("?"):Definition.ToolUpgrade==EMCToolUpgrade::MeshaBrush?TEXT("1"):Definition.ToolUpgrade==EMCToolUpgrade::Buffer?TEXT("2"):Definition.ToolUpgrade==EMCToolUpgrade::Chainsaw?TEXT("3"):TEXT("4")), 44, Accent));
        auto* IconSlot = Column->AddChildToVerticalBox(IconSize);
        IconSlot->SetHorizontalAlignment(HAlign_Center);
        IconSlot->SetPadding(FMargin(0, 0, 0, 12));
        const FText Name = Definition.DisplayName.IsEmpty() ? FText::FromName(IDs[Index]) : Definition.DisplayName;
        auto* NameLabel = MCPerkCardPrivate::Label(WidgetTree, Name, 20);
        Column->AddChildToVerticalBox(NameLabel)->SetPadding(FMargin(0, 0, 0, 8));
        auto* Description = MCPerkCardPrivate::Label(WidgetTree,
            FText::FromString(Definition.Description.ToString().Replace(TEXT("<br>"), TEXT("\n"))), 14, MCPerkCardPrivate::Muted);
        // Give wrapping a stable prepass width, then fit the complete description
        // into its own flexible area. It cannot paint over the fixed choice footer.
        Description->SetAutoWrapText(false);
        Description->SetWrapTextAt(264);
        auto* DescriptionSize=WidgetTree->ConstructWidget<USizeBox>();
        DescriptionSize->SetWidthOverride(264); DescriptionSize->SetContent(Description);
        auto* DescriptionFit=WidgetTree->ConstructWidget<UScaleBox>();
        DescriptionFit->SetStretch(EStretch::ScaleToFit);
        DescriptionFit->SetStretchDirection(EStretchDirection::DownOnly);
        auto* FitSlot=Cast<UScaleBoxSlot>(DescriptionFit->SetContent(DescriptionSize));
        FitSlot->SetVerticalAlignment(VAlign_Top);
        auto* DescriptionSlot = Column->AddChildToVerticalBox(DescriptionFit);
        DescriptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        DescriptionSlot->SetVerticalAlignment(VAlign_Fill);
        Column->AddChildToVerticalBox(MCPerkCardPrivate::Label(WidgetTree,
            FText::FromString(FString::Printf(TEXT("ВЫБРАТЬ   [%d]"), Index + 1)), 15, Accent))->SetPadding(FMargin(0,8,0,0));
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
