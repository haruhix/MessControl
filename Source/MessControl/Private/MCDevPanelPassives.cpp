#include "MCDevPanelWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "MCUIDataLibrary.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCPlaytestSession.h"
#include "MCHubGameState.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"

namespace MCDevPassivesUI
{
UTextBlock* Label(UWidgetTree* Tree,FName Name,const UMCGameplayUITheme* Style,int32 Size,bool Bold=false)
{
    auto* Text=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Text->SetFont(Style->MakeGameplayFont(Size,Bold));Text->SetAutoWrapText(true);
    Text->SetColorAndOpacity(Style->Palette.Cream);return Text;
}
void ButtonStyle(UButton* Button,const UMCGameplayUITheme* Style,bool Primary=false)
{
    FButtonStyle Look;
    const FLinearColor Fill=Primary?Style->Palette.Mint:Style->Palette.SurfaceAlt;
    Look.SetNormal(FSlateRoundedBoxBrush(Fill,9.f,Style->Palette.Border,1.f));
    Look.SetHovered(FSlateRoundedBoxBrush(Style->Palette.ButtonHover,9.f,Style->Palette.Mint,1.5f));
    Look.SetPressed(FSlateRoundedBoxBrush(Style->Palette.ButtonPressed,9.f,Style->Palette.Mint,1.f));
    Look.SetDisabled(FSlateRoundedBoxBrush(Style->Palette.ButtonDisabled,9.f,Style->Palette.Border,.5f));
    Look.SetNormalPadding(FMargin(12,9));Look.SetPressedPadding(FMargin(12,10,12,8));
    Button->SetStyle(Look);
}
void Copy(UTextBlock* Label,const FText& Text)
{
    if(Label && !Label->GetText().EqualTo(Text)) Label->SetText(Text);
}
}

bool UMCDevPanelWidget::IsPassivesOpen() const
{
    return Pages && Pages->GetActiveWidgetIndex()==1;
}

void UMCDevPanelWidget::BuildPassivesPage()
{
    if(!Pages || PassiveList) return;
    PassiveTheme=UMCGameplayUIFactory::GetMasterTheme();
    const auto* Style=PassiveTheme?PassiveTheme.Get():GetDefault<UMCGameplayUITheme>();
    auto* Page=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PassivePage"));
    Pages->AddChild(Page);
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();
    Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,10));
    auto* Title=MCDevPassivesUI::Label(WidgetTree,TEXT("PassivePanelTitle"),Style,25,true);
    Title->SetText(FText::FromString(TEXT("ПАССИВКИ / ТЕКУЩИЙ РАН")));
    auto* TitleSlot=Header->AddChildToHorizontalBox(Title);TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TitleSlot->SetVerticalAlignment(VAlign_Center);
    PassivesBackButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("BackButton"));
    MCDevPassivesUI::ButtonStyle(PassivesBackButton,Style);
    auto* Back=MCDevPassivesUI::Label(WidgetTree,NAME_None,Style,16,true);
    Back->SetText(FText::FromString(TEXT("← НАЗАД")));Back->SetColorAndOpacity(Style->Palette.Ink);
    CastChecked<UButtonSlot>(PassivesBackButton->AddChild(Back))->SetPadding(FMargin(14,10));
    PassivesBackButton->OnClicked.AddDynamic(this,&UMCDevPanelWidget::PassivesBackClicked);
    Header->AddChildToHorizontalBox(PassivesBackButton)->SetPadding(FMargin(16,0,0,0));
    auto* Hint=MCDevPassivesUI::Label(WidgetTree,TEXT("PassivePanelHint"),Style,14);
    Hint->SetText(FText::FromString(TEXT("Выдать или снять пассивку своему игроку. Разные пассивки работают вместе; повтор одной не складывается. Esc — назад, F3 — в игру.")));
    Page->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0,0,0,8));
    PassiveSummary=MCDevPassivesUI::Label(WidgetTree,TEXT("PassiveSummary"),Style,14,true);
    PassiveSummary->SetColorAndOpacity(Style->Palette.Mint);
    Page->AddChildToVerticalBox(PassiveSummary)->SetPadding(FMargin(0,0,0,10));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("PassiveScroll"));
    Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    PassiveList=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PassiveList"));Scroll->AddChild(PassiveList);
    PassiveFeedback=MCDevPassivesUI::Label(WidgetTree,TEXT("PassiveFeedback"),Style,13);
    PassiveFeedback->SetText(FText::FromString(TEXT("Тестовые изменения действуют в текущем ране. Покупки в хабе и профиль не перезаписываются.")));
    Page->AddChildToVerticalBox(PassiveFeedback)->SetPadding(FMargin(0,10,0,4));
    auto* Close=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("PassivesCloseButton"));
    MCDevPassivesUI::ButtonStyle(Close,Style);
    auto* CloseText=MCDevPassivesUI::Label(WidgetTree,NAME_None,Style,14,true);
    CloseText->SetText(FText::FromString(TEXT("ВЕРНУТЬСЯ В ИГРУ  [F3]")));CloseText->SetColorAndOpacity(Style->Palette.Ink);
    CastChecked<UButtonSlot>(Close->AddChild(CloseText))->SetPadding(FMargin(12,8));
    Close->OnClicked.AddDynamic(this,&UMCDevPanelWidget::CloseClicked);
    Page->AddChildToVerticalBox(Close)->SetPadding(FMargin(0,6,0,0));
}

void UMCDevPanelWidget::OpenPassivesClicked()
{
    if(!Pages) return;
    Pages->SetActiveWidgetIndex(1);PassiveRefreshDelay=0.f;
    RefreshPassives();SetKeyboardFocus();
}

void UMCDevPanelWidget::PassivesBackClicked()
{
    if(Pages) Pages->SetActiveWidgetIndex(0);
    if(OpenPassivesButton) OpenPassivesButton->SetKeyboardFocus();
}

void UMCDevPanelWidget::RefreshPassives()
{
    if(!PassiveList) return;
    auto* PC=Cast<AMCPlayerController>(GetOwningPlayer());
    const auto* Player=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    const auto* Perks=Player?Player->Perks.Get():nullptr;
    const auto IDs=MCDevPassiveIDs(Perks);
    const auto* Style=PassiveTheme?PassiveTheme.Get():GetDefault<UMCGameplayUITheme>();
    const auto* Session=AMCPlaytestSession::Find(GetWorld());
    const bool BotsActive=Session && Session->IsActive();
    const bool CanControl=PC && PC->CanUseDevPanel() && !BotsActive;
    const bool InHub=GetWorld()->GetGameState<AMCHubGameState>()!=nullptr;
    const FString Culture=FInternationalization::Get().GetCurrentLanguage()->GetName();
    UDataTable* Localization=Style->LocalizationTable.LoadSynchronous();
    if(!Localization) Localization=UMCUIDataLibrary::GetDefaultLocalizationTable();

    // Rows are retained and reused by identity. Ownership updates never rebuild
    // the scroll tree or steal focus; catalog changes only reorder live rows.
    for(FName ID:IDs) if(!PassiveRows.Contains(ID)) {
        const FString Suffix=ID.ToString();
        auto* Row=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*(TEXT("PassiveRow_")+Suffix)));
        Row->SetBrush(FSlateRoundedBoxBrush(Style->Palette.Marine,12.f,Style->Palette.Border,1.f));Row->SetPadding(FMargin(16,13));
        auto* Layout=WidgetTree->ConstructWidget<UHorizontalBox>();Row->SetContent(Layout);
        auto* Copy=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* CopySlot=Layout->AddChildToHorizontalBox(Copy);CopySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));CopySlot->SetPadding(FMargin(0,0,18,0));
        auto* Name=MCDevPassivesUI::Label(WidgetTree,FName(*(TEXT("PassiveName_")+Suffix)),Style,18,true);
        Copy->AddChildToVerticalBox(Name)->SetPadding(FMargin(0,0,0,5));
        auto* Description=MCDevPassivesUI::Label(WidgetTree,FName(*(TEXT("PassiveDescription_")+Suffix)),Style,14);
        Copy->AddChildToVerticalBox(Description);
        auto* Controls=WidgetTree->ConstructWidget<UVerticalBox>();Layout->AddChildToHorizontalBox(Controls)->SetVerticalAlignment(VAlign_Center);
        auto* Owned=MCDevPassivesUI::Label(WidgetTree,FName(*(TEXT("PassiveOwned_")+Suffix)),Style,13,true);
        Owned->SetJustification(ETextJustify::Right);Controls->AddChildToVerticalBox(Owned)->SetPadding(FMargin(0,0,0,7));
        auto* Buttons=WidgetTree->ConstructWidget<UHorizontalBox>();Controls->AddChildToVerticalBox(Buttons);
        auto Action=[&](const TCHAR* Prefix,const TCHAR* Text,EMCDevAction Command,bool Primary) {
            auto* Button=WidgetTree->ConstructWidget<UMCDevActionButton>(UMCDevActionButton::StaticClass(),FName(*(FString(Prefix)+Suffix)));
            Button->Configure(PC,Command,MCDevPassiveToken(ID));MCDevPassivesUI::ButtonStyle(Button,Style,Primary);
            auto* Caption=MCDevPassivesUI::Label(WidgetTree,NAME_None,Style,14,true);
            Caption->SetText(FText::FromString(Text));Caption->SetColorAndOpacity(Style->Palette.Ink);
            CastChecked<UButtonSlot>(Button->AddChild(Caption))->SetPadding(FMargin(12,9));
            Buttons->AddChildToHorizontalBox(Button)->SetPadding(FMargin(Primary?0:8,0,0,0));return Button;
        };
        auto* Grant=Action(TEXT("PassiveGrant_"),TEXT("ВЫДАТЬ"),EMCDevAction::GrantPassive,true);
        auto* Remove=Action(TEXT("PassiveRemove_"),TEXT("СНЯТЬ"),EMCDevAction::RemovePassive,false);
        PassiveRows.Add(ID,Row);PassiveNames.Add(ID,Name);PassiveDescriptions.Add(ID,Description);
        PassiveOwnedLabels.Add(ID,Owned);PassiveGrantButtons.Add(ID,Grant);PassiveRemoveButtons.Add(ID,Remove);
    }
    if(DisplayedPassiveIDs!=IDs) {
        PassiveList->ClearChildren();
        for(FName ID:IDs) PassiveList->AddChildToVerticalBox(PassiveRows.FindChecked(ID))->SetPadding(FMargin(0,0,10,9));
        DisplayedPassiveIDs=IDs;
    }
    int32 OwnedCount=0;
    for(FName ID:IDs) if(const auto* Row=Perks->FindDefinition(ID)) {
        const bool Owned=Perks->OwnsPerk(ID);OwnedCount+=Owned?1:0;
        MCDevPassivesUI::Copy(PassiveNames.FindChecked(ID),UMCUIDataLibrary::ResolveText(Localization,Row->DisplayNameKey,Row->DisplayName,Culture));
        MCDevPassivesUI::Copy(PassiveDescriptions.FindChecked(ID),UMCUIDataLibrary::ResolveText(Localization,Row->DescriptionKey,Row->Description,Culture));
        auto* State=PassiveOwnedLabels.FindChecked(ID).Get();
        MCDevPassivesUI::Copy(State,FText::FromString(Owned?TEXT("Есть"):TEXT("Не получено")));
        State->SetColorAndOpacity(Owned?Style->Palette.Mint:Style->Palette.OnMarine.CopyWithNewOpacity(.86f));
        auto* Grant=PassiveGrantButtons.FindChecked(ID).Get();auto* Remove=PassiveRemoveButtons.FindChecked(ID).Get();
        Grant->SetIsEnabled(CanControl && !Owned && Perks->CanAcquirePerk(ID));Remove->SetIsEnabled(CanControl && Owned);
        const FText Hint=FText::FromString(CanControl?TEXT("Изменяет только пассивку текущего игрока на этот ран."):
            InHub?TEXT("В хабе доступен только просмотр. Покупки готовятся для следующего рана через магазин."):
            BotsActive?TEXT("Сначала останови тест ботов."):TEXT("Выдавать и снимать пассивки может только локальный хост на арене."));
        Grant->SetToolTipText(Hint);Remove->SetToolTipText(Hint);
    }
    const FString Access=CanControl?TEXT("ХОСТ — текущий ран"):
        InHub?TEXT("ХАБ — только просмотр; покупки через магазин"):
        BotsActive?TEXT("БОТЫ ИГРАЮТ — только просмотр"):TEXT("КЛИЕНТ — только просмотр");
    MCDevPassivesUI::Copy(PassiveSummary,FText::FromString(FString::Printf(TEXT("%s · Получено %d / %d · Слоты 1–4 не используются"),*Access,OwnedCount,IDs.Num())));
    if(IDs.IsEmpty()) MCDevPassivesUI::Copy(PassiveFeedback,FText::FromString(TEXT("Доступных пассивок нет. Дождись загрузки актуального каталога.")));
}
