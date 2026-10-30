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
#include "Components/ComboBoxString.h"
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

namespace MCDevActivesUI
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
    Look.SetNormal(FSlateRoundedBoxBrush(Primary?Style->Palette.Mint:Style->Palette.SurfaceAlt,9.f,Style->Palette.Border,1.f));
    Look.SetHovered(FSlateRoundedBoxBrush(Style->Palette.ButtonHover,9.f,Style->Palette.Mint,1.5f));
    Look.SetPressed(FSlateRoundedBoxBrush(Style->Palette.ButtonPressed,9.f,Style->Palette.Mint,1.f));
    Look.SetDisabled(FSlateRoundedBoxBrush(Style->Palette.ButtonDisabled,9.f,Style->Palette.Border,.5f));
    Look.SetNormalPadding(FMargin(12,9));Look.SetPressedPadding(FMargin(12,10,12,8));Button->SetStyle(Look);
}
void Copy(UTextBlock* Label,const FText& Text)
{
    if(Label && !Label->GetText().EqualTo(Text)) Label->SetText(Text);
}
}

bool UMCDevPanelWidget::IsActivesOpen() const
{
    return Pages && Pages->GetActiveWidgetIndex()==2;
}

void UMCDevPanelWidget::BuildActivesPage()
{
    if(!Pages || ActiveList) return;
    ActiveTheme=UMCGameplayUIFactory::GetMasterTheme();
    const auto* Style=ActiveTheme?ActiveTheme.Get():GetDefault<UMCGameplayUITheme>();
    auto* Page=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ActiveSkillsPage"));Pages->AddChild(Page);
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,8));
    auto* Title=MCDevActivesUI::Label(WidgetTree,TEXT("ActivePanelTitle"),Style,25,true);
    Title->SetText(FText::FromString(TEXT("АКТИВКИ / СЛОТЫ 1–4")));
    auto* TitleSlot=Header->AddChildToHorizontalBox(Title);TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TitleSlot->SetVerticalAlignment(VAlign_Center);
    auto* Back=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("ActivesBackButton"));MCDevActivesUI::ButtonStyle(Back,Style);
    auto* BackText=MCDevActivesUI::Label(WidgetTree,NAME_None,Style,16,true);BackText->SetText(FText::FromString(TEXT("← НАЗАД")));BackText->SetColorAndOpacity(Style->Palette.Ink);
    CastChecked<UButtonSlot>(Back->AddChild(BackText))->SetPadding(FMargin(14,10));Back->OnClicked.AddDynamic(this,&UMCDevPanelWidget::ActivesBackClicked);
    Header->AddChildToHorizontalBox(Back)->SetPadding(FMargin(16,0,0,0));
    auto* Hint=MCDevActivesUI::Label(WidgetTree,TEXT("ActivePanelHint"),Style,14);
    Hint->SetText(FText::FromString(TEXT("Выбери слот, затем активку. Занятый слот заменяется; пассивки и покупки не изменяются. Esc — назад, F3 — в игру.")));
    Page->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0,0,0,7));
    ActiveSummary=MCDevActivesUI::Label(WidgetTree,TEXT("ActiveSummary"),Style,13,true);ActiveSummary->SetColorAndOpacity(Style->Palette.Mint);
    Page->AddChildToVerticalBox(ActiveSummary)->SetPadding(FMargin(0,0,0,7));
    for(int32 SlotIndex=0;SlotIndex<UMCPerkComponent::ActiveSkillSlotCount;++SlotIndex) {
        auto* Label=MCDevActivesUI::Label(WidgetTree,FName(*FString::Printf(TEXT("ActiveSlotLabel_%d"),SlotIndex)),Style,14);
        ActiveSlotLabels.Add(Label);Page->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,0,0,3));
    }
    auto* Controls=WidgetTree->ConstructWidget<UHorizontalBox>();Page->AddChildToVerticalBox(Controls)->SetPadding(FMargin(0,7,0,10));
    ActiveSlotCombo=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("ActiveSlotCombo"));
    for(int32 SlotIndex=0;SlotIndex<UMCPerkComponent::ActiveSkillSlotCount;++SlotIndex) ActiveSlotCombo->AddOption(FString::Printf(TEXT("Слот %d  [%d]"),SlotIndex+1,SlotIndex+1));
    ActiveSlotCombo->SetSelectedIndex(0);
    ActiveSlotCombo->OnSelectionChanged.AddDynamic(this,&UMCDevPanelWidget::ActiveSlotChanged);
    Controls->AddChildToHorizontalBox(ActiveSlotCombo)->SetPadding(FMargin(0,0,10,0));
    auto Control=[&](const TCHAR* Name,const TCHAR* Caption,EMCDevAction Action) {
        auto* Button=WidgetTree->ConstructWidget<UMCDevActionButton>(UMCDevActionButton::StaticClass(),Name);
        Button->Configure(Cast<AMCPlayerController>(GetOwningPlayer()),Action,0);MCDevActivesUI::ButtonStyle(Button,Style);
        auto* Text=MCDevActivesUI::Label(WidgetTree,NAME_None,Style,13,true);Text->SetText(FText::FromString(Caption));Text->SetColorAndOpacity(Style->Palette.Ink);
        CastChecked<UButtonSlot>(Button->AddChild(Text))->SetPadding(FMargin(10,8));Controls->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0,0,8,0));return Button;
    };
    ActiveRemoveButton=Control(TEXT("ActiveRemoveButton"),TEXT("УБРАТЬ"),EMCDevAction::RemoveActiveSlot);
    ActiveRefillButton=Control(TEXT("ActiveRefillButton"),TEXT("ЗАРЯДЫ"),EMCDevAction::RefillActiveSlot);
    ActiveCooldownButton=Control(TEXT("ActiveCooldownButton"),TEXT("СБРОСИТЬ КД"),EMCDevAction::ResetActiveCooldown);
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("ActiveScroll"));Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    ActiveList=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ActiveList"));Scroll->AddChild(ActiveList);
    ActiveFeedback=MCDevActivesUI::Label(WidgetTree,TEXT("ActiveFeedback"),Style,13);
    ActiveFeedback->SetText(FText::FromString(TEXT("Только текущий игрок и ран. Закрой F3 и используй клавиши 1–4; колесо переключает инструменты.")));
    Page->AddChildToVerticalBox(ActiveFeedback)->SetPadding(FMargin(0,8,0,3));
    auto* Close=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("ActivesCloseButton"));MCDevActivesUI::ButtonStyle(Close,Style);
    auto* CloseText=MCDevActivesUI::Label(WidgetTree,NAME_None,Style,14,true);CloseText->SetText(FText::FromString(TEXT("ВЕРНУТЬСЯ В ИГРУ  [F3]")));CloseText->SetColorAndOpacity(Style->Palette.Ink);
    CastChecked<UButtonSlot>(Close->AddChild(CloseText))->SetPadding(FMargin(12,8));Close->OnClicked.AddDynamic(this,&UMCDevPanelWidget::CloseClicked);
    Page->AddChildToVerticalBox(Close)->SetPadding(FMargin(0,5,0,0));
}

void UMCDevPanelWidget::OpenActivesClicked()
{
    if(!Pages) return;
    Pages->SetActiveWidgetIndex(2);ActiveRefreshDelay=0.f;RefreshActives();SetKeyboardFocus();
}

void UMCDevPanelWidget::ActivesBackClicked()
{
    if(Pages) Pages->SetActiveWidgetIndex(0);
    if(OpenActivesButton) OpenActivesButton->SetKeyboardFocus();
}

void UMCDevPanelWidget::ActiveSlotChanged(FString,ESelectInfo::Type)
{
    RefreshActives();
}

void UMCDevPanelWidget::RefreshActives()
{
    if(!ActiveList || !ActiveSlotCombo) return;
    auto* PC=Cast<AMCPlayerController>(GetOwningPlayer());const auto* Player=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    const auto* Perks=Player?Player->Perks.Get():nullptr;const auto IDs=MCDevActiveIDs(Perks);
    const auto* Style=ActiveTheme?ActiveTheme.Get():GetDefault<UMCGameplayUITheme>();
    const auto* Session=AMCPlaytestSession::Find(GetWorld());const bool BotsActive=Session && Session->IsActive();
    const bool CanControl=PC && PC->CanUseDevPanel() && !BotsActive;
    const bool InHub=GetWorld()->GetGameState<AMCHubGameState>()!=nullptr;
    const int32 Selected=ActiveSlotCombo->GetSelectedIndex();
    const auto* Destination=Perks && Perks->ActiveSkillSlots.IsValidIndex(Selected)?&Perks->ActiveSkillSlots[Selected]:nullptr;
    const bool Filled=Destination && !Destination->PerkID.IsNone();
    const FString Culture=FInternationalization::Get().GetCurrentLanguage()->GetName();
    UDataTable* Localization=Style->LocalizationTable.LoadSynchronous();if(!Localization) Localization=UMCUIDataLibrary::GetDefaultLocalizationTable();
    auto NameFor=[&](FName ID) {const auto* Row=Perks?Perks->FindDefinition(ID):nullptr;
        return Row?UMCUIDataLibrary::ResolveText(Localization,Row->DisplayNameKey,Row->DisplayName,Culture):FText::FromName(ID);};
    for(int32 Index=0;Index<ActiveSlotLabels.Num();++Index) {
        const auto* SlotState=Perks && Perks->ActiveSkillSlots.IsValidIndex(Index)?&Perks->ActiveSkillSlots[Index]:nullptr;
        FString State=TEXT("Пусто");
        if(SlotState && !SlotState->PerkID.IsNone()) {
            State=NameFor(SlotState->PerkID).ToString();
            State+=SlotState->Charges<0?TEXT(" · многоразовая"):FString::Printf(TEXT(" · заряды: %d"),SlotState->Charges);
            const float Cooldown=Perks->GetSkillCooldownRemaining(Index);
            State+=Cooldown>0.f?FString::Printf(TEXT(" · КД %.1f с"),Cooldown):TEXT(" · готово");
        }
        MCDevActivesUI::Copy(ActiveSlotLabels[Index],FText::FromString(FString::Printf(TEXT("%s [%d]  %s"),Index==Selected?TEXT("→"):TEXT("  "),Index+1,*State)));
        ActiveSlotLabels[Index]->SetColorAndOpacity(Index==Selected?Style->Palette.Mint:Style->Palette.Cream);
    }
    auto SetupControl=[&](UMCDevActionButton* Button,EMCDevAction Action,bool Enabled,const TCHAR* Hint) {
        Button->Configure(PC,Action,Selected);Button->SetIsEnabled(CanControl && Filled && Enabled);Button->SetToolTipText(FText::FromString(Hint));
    };
    SetupControl(ActiveRemoveButton,EMCDevAction::RemoveActiveSlot,true,TEXT("Убирает только навык выбранного слота и завершает его текущее действие."));
    const auto* Current=Filled?Perks->FindDefinition(Destination->PerkID):nullptr;
    SetupControl(ActiveRefillButton,EMCDevAction::RefillActiveSlot,Current && UMCPerkComponent::ResolvePerkKind(*Current,Destination->PerkID)==EMCPerkKind::Consumable,TEXT("Возвращает исходное число зарядов выбранного навыка. КД сохраняется; текущее действие завершается."));
    SetupControl(ActiveCooldownButton,EMCDevAction::ResetActiveCooldown,true,TEXT("Обнуляет КД выбранного навыка. Заряды сохраняются; текущее действие завершается."));
    for(FName ID:IDs) if(!ActiveRows.Contains(ID)) {
        const FString Suffix=ID.ToString();auto* Row=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*(TEXT("ActiveRow_")+Suffix)));
        Row->SetBrush(FSlateRoundedBoxBrush(Style->Palette.Marine,12.f,Style->Palette.Border,1.f));Row->SetPadding(FMargin(14,11));
        auto* Layout=WidgetTree->ConstructWidget<UHorizontalBox>();Row->SetContent(Layout);
        auto* Copy=WidgetTree->ConstructWidget<UVerticalBox>();auto* CopySlot=Layout->AddChildToHorizontalBox(Copy);CopySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));CopySlot->SetPadding(FMargin(0,0,16,0));
        auto* Name=MCDevActivesUI::Label(WidgetTree,FName(*(TEXT("ActiveName_")+Suffix)),Style,18,true);Copy->AddChildToVerticalBox(Name)->SetPadding(FMargin(0,0,0,3));
        auto* Description=MCDevActivesUI::Label(WidgetTree,FName(*(TEXT("ActiveDescription_")+Suffix)),Style,13);Copy->AddChildToVerticalBox(Description);
        auto* Controls=WidgetTree->ConstructWidget<UVerticalBox>();Layout->AddChildToHorizontalBox(Controls)->SetVerticalAlignment(VAlign_Center);
        auto* Owned=MCDevActivesUI::Label(WidgetTree,FName(*(TEXT("ActiveOwned_")+Suffix)),Style,13,true);Owned->SetJustification(ETextJustify::Right);Controls->AddChildToVerticalBox(Owned)->SetPadding(FMargin(0,0,0,5));
        auto* Grant=WidgetTree->ConstructWidget<UMCDevActionButton>(UMCDevActionButton::StaticClass(),FName(*(TEXT("ActiveGrant_")+Suffix)));MCDevActivesUI::ButtonStyle(Grant,Style,true);
        auto* Caption=MCDevActivesUI::Label(WidgetTree,NAME_None,Style,13,true);Caption->SetColorAndOpacity(Style->Palette.Ink);CastChecked<UButtonSlot>(Grant->AddChild(Caption))->SetPadding(FMargin(12,9));Controls->AddChildToVerticalBox(Grant);
        ActiveRows.Add(ID,Row);ActiveNames.Add(ID,Name);ActiveDescriptions.Add(ID,Description);ActiveOwnedLabels.Add(ID,Owned);ActiveGrantButtons.Add(ID,Grant);ActiveGrantLabels.Add(ID,Caption);
    }
    if(DisplayedActiveIDs!=IDs) {
        ActiveList->ClearChildren();for(FName ID:IDs) ActiveList->AddChildToVerticalBox(ActiveRows.FindChecked(ID))->SetPadding(FMargin(0,0,10,8));DisplayedActiveIDs=IDs;
    }
    for(FName ID:IDs) if(const auto* Row=Perks->FindDefinition(ID)) {
        MCDevActivesUI::Copy(ActiveNames.FindChecked(ID),NameFor(ID));
        MCDevActivesUI::Copy(ActiveDescriptions.FindChecked(ID),UMCUIDataLibrary::ResolveText(Localization,Row->DescriptionKey,Row->Description,Culture));
        const int32 OwnedSlot=Perks->ActiveSkillSlots.IndexOfByPredicate([&](const auto& SlotState){return SlotState.PerkID==ID;});
        const bool Consumable=UMCPerkComponent::ResolvePerkKind(*Row,ID)==EMCPerkKind::Consumable;
        const FString State=OwnedSlot!=INDEX_NONE?FString::Printf(TEXT("Уже в слоте %d"),OwnedSlot+1):Consumable?FString::Printf(TEXT("%d заряда · КД %.1f с"),Row->InitialCharges,Row->CooldownSeconds):FString::Printf(TEXT("Многоразовая · КД %.1f с"),Row->CooldownSeconds);
        MCDevActivesUI::Copy(ActiveOwnedLabels.FindChecked(ID),FText::FromString(State));
        ActiveOwnedLabels.FindChecked(ID)->SetColorAndOpacity(OwnedSlot!=INDEX_NONE?Style->Palette.Mint:Style->Palette.Cream);
        auto* Grant=ActiveGrantButtons.FindChecked(ID).Get();Grant->Configure(PC,MCDevActiveGrantAction(Selected),MCDevActiveToken(ID));
        Grant->SetIsEnabled(CanControl && Selected>=0 && Selected<UMCPerkComponent::ActiveSkillSlotCount && Perks->CanAcquirePerk(ID,Selected));
        Grant->SetToolTipText(FText::FromString(OwnedSlot!=INDEX_NONE?TEXT("Сначала убери этот навык из его слота. Повторы не выдаются."):Filled?TEXT("Заменяет только выбранный слот. Предыдущее действие завершается."):TEXT("Выдаёт навык в выбранный слот только на текущий ран.")));
        MCDevActivesUI::Copy(ActiveGrantLabels.FindChecked(ID),FText::FromString(Filled?FString::Printf(TEXT("ЗАМЕНИТЬ СЛОТ %d"),Selected+1):FString::Printf(TEXT("ВЫДАТЬ В СЛОТ %d"),Selected+1)));
    }
    const FString Access=CanControl?TEXT("ХОСТ — текущий ран"):InHub?TEXT("ХАБ — только просмотр"):BotsActive?TEXT("БОТЫ ИГРАЮТ — только просмотр"):TEXT("КЛИЕНТ — только просмотр");
    MCDevActivesUI::Copy(ActiveSummary,FText::FromString(FString::Printf(TEXT("%s · Активок в каталоге: %d"),*Access,IDs.Num())));
    if(IDs.IsEmpty()) MCDevActivesUI::Copy(ActiveFeedback,FText::FromString(TEXT("Доступных активок нет. Дождись загрузки актуального каталога.")));
}
