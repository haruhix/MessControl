#include "MCPerkChoiceWidget.h"
#include "MCPerkCardWidget.h"
#include "MCGameplayUITheme.h"
#include "MCGameplayHUD.h"
#include "MCUIVisualStyle.h"
#include "MCUIDataLibrary.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodCollectionComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCPerkChoice"

namespace MCPerkCardPrivate
{
    UTextBlock* Label(UWidgetTree* Tree,const FText& Text)
    {
        auto* Result=Tree->ConstructWidget<UTextBlock>();Result->SetText(Text);
        Result->SetJustification(ETextJustify::Center);Result->SetAutoWrapText(true);return Result;
    }
    FButtonStyle ActionStyle(UMCPerkCardButton* Button,const UMCGameplayUITheme* T,int32 Index,const FButtonStyle* PreviousBase=nullptr)
    {
        const auto& P=T->Palette;const FLinearColor Fill=Index==-3?P.Mint:Index==-2?P.Cost:P.Button;
        // The visible Normal may temporarily be a copy of Hovered for keyboard
        // selection. Reuse the independent base MIDs, never that aliased brush.
        FButtonStyle Style=PreviousBase?*PreviousBase:Button->GetStyle();const auto Size=MCUIVisualStyle::SizeFor(Button,FVector2D(310,52));
        Style.SetNormal(MCUIVisualStyle::Surface(Button,Style.Normal,T,Fill,Size,9));
        Style.SetHovered(MCUIVisualStyle::Surface(Button,Style.Hovered,T,P.ButtonHover,Size,9,false,0,false,true));
        Style.SetPressed(MCUIVisualStyle::Surface(Button,Style.Pressed,T,P.ButtonPressed,Size,9,false,0,false,true));
        Style.SetDisabled(MCUIVisualStyle::Surface(Button,Style.Disabled,T,P.ButtonDisabled,Size,9));
        Style.SetNormalPadding(FMargin(20,14));Style.SetPressedPadding(FMargin(20,14));return Style;
    }
}

UMCPerkCardButton::UMCPerkCardButton() { InitIsFocusable(true); }
void UMCPerkCardButton::Configure(AMCPlayerController* Player,int32 Index)
{
    Controller=Player;ChoiceIndex=Index;OnClicked.AddUniqueDynamic(this,&UMCPerkCardButton::Choose);
}
void UMCPerkCardButton::Choose()
{
    if(IsDesignTime() || !bInteractionAllowed || !GetIsEnabled() || !Controller.IsValid()) return;
    if(ChoiceIndex==-1) Controller->RerollLevelPerks();
    else if(ChoiceIndex==-2 || ChoiceIndex==-3) Controller->ConfirmRogueReward(ChoiceIndex==-3);
    else Controller->ChooseRewardPerk(ChoiceIndex);
}
void UMCPerkCardButton::SetInteractionAllowed(bool bAllowed)
{
    bInteractionAllowed=bAllowed;
    InitIsFocusable(bAllowed);
    // Read-only contract cards retain their child scroll region for long descriptions.
    SetVisibility(bAllowed?ESlateVisibility::Visible:ESlateVisibility::SelfHitTestInvisible);
}

FString UMCPerkChoiceWidget::DisplayCulture() const
{
    return IsDesignTime() || bApplyingPreview?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();
}
FText UMCPerkChoiceWidget::Copy(FName Key,const FText& Fallback) const
{
    return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,DisplayCulture());
}
UDataTable* UMCPerkChoiceWidget::GetResolvedTranslationTable() const
{
    for (const auto* Owner=static_cast<const UMCGameplayUIWidget*>(this);Owner;Owner=Owner->GetTypedOuter<UMCGameplayUIWidget>())
        if (IsValid(Owner->TranslationTable)) return Owner->TranslationTable;
    return IsValid(LocalizationTable)?LocalizationTable.Get():Super::GetResolvedTranslationTable();
}
void UMCPerkChoiceWidget::CacheDataTables()
{
    const auto* Style=GetResolvedTheme();
    CachedCardTable=CardTable;
    if (!CachedCardTable && Style) CachedCardTable=Style->CardTable.LoadSynchronous();
    if (!CachedCardTable) CachedCardTable=UMCUIDataLibrary::GetDefaultCardTable();
    if (ObservedCardTable.Get()!=CachedCardTable || (CachedCardTable && !CardTableHandle.IsValid()))
    {
        UnobserveCardTable();
        ObservedCardTable=CachedCardTable;
        if (CachedCardTable) CardTableHandle=CachedCardTable->OnDataTableChanged().AddUObject(this,&ThisClass::RefreshLocalization);
    }
}
void UMCPerkChoiceWidget::UnobserveCardTable()
{
    if (auto* Table=ObservedCardTable.Get()) Table->OnDataTableChanged().Remove(CardTableHandle);
    ObservedCardTable.Reset();CardTableHandle.Reset();
}
FText UMCPerkChoiceWidget::Format(const FText& Pattern,const FFormatNamedArguments& Arguments) const
{
    return UMCUIDataLibrary::FormatTextForCulture(Pattern,Arguments,DisplayCulture());
}
void UMCPerkChoiceWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(true);BuildPresentationTree();
}
void UMCPerkChoiceWidget::BuildPresentationTree()
{
    CacheDataTables();
    if(!WidgetTree || Title) return;
    if(WidgetTree->RootWidget) {
        Title=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("RewardTitle")));
        Subtitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("RewardSubtitle")));
        Hint=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("RewardHint")));
        Cards=Cast<UHorizontalBox>(WidgetTree->FindWidget(TEXT("RewardCards")));
        Actions=Cast<UHorizontalBox>(WidgetTree->FindWidget(TEXT("RewardActions")));
        PanelBorder=Cast<UBorder>(WidgetTree->FindWidget(TEXT("RewardPanel")));
        HeaderBorder=Cast<UBorder>(WidgetTree->FindWidget(TEXT("RewardHeader")));
        if(auto* Size=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("RewardPanelSize")))) {Size->SetWidthOverride(1040);Size->SetHeightOverride(680);}
        if(Title) Title->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        if(Subtitle) Subtitle->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        NormalizePresentationLayout();
        return;
    }
    auto* Backdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("RewardBackdrop"));WidgetTree->RootWidget=Backdrop;
    Backdrop->SetPadding(FMargin(28));Backdrop->SetHorizontalAlignment(HAlign_Center);Backdrop->SetVerticalAlignment(VAlign_Center);
    Backdrop->SetBrushColor(GetResolvedTheme()->Palette.Backdrop);
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("RewardViewportFit"));Scale->SetStretch(EStretch::ScaleToFit);Scale->SetStretchDirection(EStretchDirection::DownOnly);Backdrop->SetContent(Scale);
    // Stable card viewport: long copy scrolls inside the card instead of shrinking the modal.
    auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardPanelSize"));Width->SetWidthOverride(1040);Width->SetHeightOverride(680);Scale->SetContent(Width);
    PanelBorder=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("RewardPanel"));PanelBorder->SetPadding(FMargin(18));Width->SetContent(PanelBorder);
    auto* Main=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("RewardColumn"));PanelBorder->SetContent(Main);
    HeaderBorder=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("RewardHeader"));HeaderBorder->SetPadding(FMargin(14,10));
    auto* HeaderRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("RewardHeaderRow"));HeaderBorder->SetContent(HeaderRow);
    auto* HeaderIconSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardHeaderIconSize"));HeaderIconSize->SetWidthOverride(44);HeaderIconSize->SetHeightOverride(50);
    auto* HeaderIcon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("RewardHeaderIcon"));HeaderIcon->bTooth=true;HeaderIcon->bHappy=true;HeaderIconSize->SetContent(HeaderIcon);
    auto* HeaderIconSlot=HeaderRow->AddChildToHorizontalBox(HeaderIconSize);HeaderIconSlot->SetPadding(FMargin(0,0,12,0));HeaderIconSlot->SetVerticalAlignment(VAlign_Center);
    Title=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("RewardTitle"));Title->SetAutoWrapText(true);Title->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Title->SetJustification(ETextJustify::Center);
    auto* TitleSlot=HeaderRow->AddChildToHorizontalBox(Title);TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TitleSlot->SetVerticalAlignment(VAlign_Center);
    Main->AddChildToVerticalBox(HeaderBorder)->SetPadding(FMargin(0,0,0,9));
    Subtitle=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("RewardSubtitle"));Subtitle->SetAutoWrapText(true);Subtitle->SetJustification(ETextJustify::Center);
    Main->AddChildToVerticalBox(Subtitle)->SetPadding(FMargin(8,0,8,14));
    Cards=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("RewardCards"));Main->AddChildToVerticalBox(Cards)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Actions=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("RewardActions"));
    auto* ActionSlot=Main->AddChildToVerticalBox(Actions);ActionSlot->SetPadding(FMargin(0,12,0,0));ActionSlot->SetHorizontalAlignment(HAlign_Center);
    Hint=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("RewardHint"));Hint->SetAutoWrapText(true);Hint->SetJustification(ETextJustify::Center);
    Main->AddChildToVerticalBox(Hint)->SetPadding(FMargin(6,9,6,0));ApplyTheme();
}

void UMCPerkChoiceWidget::NormalizePresentationLayout()
{
    if(!bUseNativeLayout || !WidgetTree) return;
    if(auto* Size=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("RewardPanelSize")))) {Size->SetWidthOverride(bWaitingForTeam?760:1040);Size->SetHeightOverride(bWaitingForTeam?280:680);}
    if(PanelBorder) PanelBorder->SetPadding(FMargin(18));
    if(HeaderBorder) {HeaderBorder->SetPadding(FMargin(14,8));if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(HeaderBorder->Slot)) LayoutSlot->SetPadding(FMargin(0,0,0,8));}
    if(Subtitle) if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Subtitle->Slot)) LayoutSlot->SetPadding(FMargin(8,0,8,12));
    if(Cards) if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Cards->Slot)) LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    if(Actions) {
        const bool Multiple=Actions->GetChildrenCount()>1;
        if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Actions->Slot)) {LayoutSlot->SetHorizontalAlignment(Multiple?HAlign_Fill:HAlign_Center);LayoutSlot->SetPadding(FMargin(6,12,6,0));}
        for(int32 Index=0;Index<Actions->GetChildrenCount();++Index) {
            auto* Button=Cast<UMCPerkCardButton>(Actions->GetChildAt(Index));if(!Button) continue;
            if(auto* LayoutSlot=Cast<UHorizontalBoxSlot>(Button->Slot)) {LayoutSlot->SetSize(FSlateChildSize(Multiple?ESlateSizeRule::Fill:ESlateSizeRule::Automatic));LayoutSlot->SetPadding(FMargin(6,0));LayoutSlot->SetVerticalAlignment(VAlign_Fill);}
            if(auto* Label=Cast<UTextBlock>(Button->GetContent())) {Label->SetWrapTextAt(Multiple?410.f:320.f);Label->SetMinDesiredWidth(Multiple?0.f:260.f);}
        }
    }
    if(Hint) if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Hint->Slot)) LayoutSlot->SetPadding(FMargin(6,8,6,0));
}

void UMCPerkChoiceWidget::ApplyTheme()
{
    const auto* T=GetResolvedTheme();if(!T) return;const auto& P=T->Palette;
    NormalizePresentationLayout();
    BindStyle(TEXT("RewardPanel"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    BindStyle(TEXT("RewardHeader"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    BindStyle(TEXT("RewardTitle"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,28,true);
    BindStyle(TEXT("RewardSubtitle"),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,16,false);
    BindStyle(TEXT("RewardHint"),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,13,false);
    Super::ApplyTheme();
    if(WidgetTree) if(auto* Backdrop=Cast<UBorder>(WidgetTree->FindWidget(TEXT("RewardBackdrop")))) Backdrop->SetBrushColor(P.Backdrop);
    if(WidgetTree) if(auto* Icon=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("RewardHeaderIcon")))) {Icon->Tint=P.Surface;Icon->InkTint=P.Ink;Icon->InvalidateLayoutAndVolatility();}
    for(int32 Index=0;Index<Buttons.Num();++Index) if(Buttons[Index] && ButtonStyles.IsValidIndex(Index)) {
        if(CardWidgets.IsValidIndex(Index) && CardWidgets[Index]) {CardWidgets[Index]->ApplyTheme();ButtonStyles[Index]=CardWidgets[Index]->GetBaseButtonStyle();}
        else {
            ButtonStyles[Index]=MCPerkCardPrivate::ActionStyle(Buttons[Index],T,Buttons[Index]->GetChoiceIndex(),&ButtonStyles[Index]);
            if(auto* TextLabel=Cast<UTextBlock>(Buttons[Index]->GetContent())) {TextLabel->SetFont(T->MakeGameplayFont(17));TextLabel->SetColorAndOpacity(P.Ink);}
        }
    }
    RefreshGamepadSelection();
}
void UMCPerkChoiceWidget::SetOrdinaryHeader()
{
    if(!Title || !Subtitle || !Hint) return;
    Title->SetText(Copy(TEXT("UI.Reward.ChoosePerk"),LOCTEXT("ChoosePerk","Выберите один перк")));
    Subtitle->SetText(Copy(TEXT("UI.Reward.OtherChoicesDisappear"),LOCTEXT("OtherChoicesDisappear","Два других исчезнут после выбора")));
    Hint->SetText(Copy(TEXT("UI.Reward.ChoiceHint"),LOCTEXT("ChoiceHint","Выбор: мышь / 1 / 2 / 3 · геймпад: стик / D-pad + A")));
}
void UMCPerkChoiceWidget::ShowChoices(const TArray<FName>& IDs,EMCPerkPolarity Polarity) {BuildChoices(IDs,Polarity,false);}
void UMCPerkChoiceWidget::SetTeamBoosterHeader()
{
    bTeamBooster=true;
    if(Title) Title->SetText(Copy(TEXT("UI.Reward.TeamBoosterTitle"),LOCTEXT("TeamBoosterTitle","КОМАНДНЫЙ БУСТЕР")));
    if(Subtitle) Subtitle->SetText(Copy(TEXT("UI.Reward.TeamBoosterSubtitle"),LOCTEXT("TeamBoosterSubtitle","Один выбор — эффект сразу для всей команды")));
    auto* PC=GetOwningPlayer<AMCPlayerController>();
    for(int32 Index=0;Index<CardWidgets.Num();++Index)
        if(CardWidgets[Index] && CurrentChoiceIDs.IsValidIndex(Index) && CurrentDefinitions.IsValidIndex(Index))
            CardWidgets[Index]->Configure(MakePresentation(CurrentChoiceIDs[Index],CurrentDefinitions[Index],false),PC,Index,true,true);
}
void UMCPerkChoiceWidget::ShowLevelChoices(const TArray<FName>& IDs,int32 TeamLevel,int32 PendingChoices)
{
    BuildChoices(IDs,EMCPerkPolarity::Positive,true);UpdateLevelChoiceHeader(TeamLevel,PendingChoices);
}
void UMCPerkChoiceWidget::AddAction(const FText& Text,int32 Index)
{
    if(!Actions) return;auto* Button=WidgetTree->ConstructWidget<UMCPerkCardButton>();Button->Configure(bApplyingPreview?nullptr:GetOwningPlayer<AMCPlayerController>(),Index);
    const auto* T=GetResolvedTheme();const auto& P=T->Palette;
    const FButtonStyle Style=MCPerkCardPrivate::ActionStyle(Button,T,Index);Button->SetStyle(Style);
    auto* TextLabel=MCPerkCardPrivate::Label(WidgetTree,Text);TextLabel->SetWrapTextAt(320);TextLabel->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);TextLabel->SetFont(T->MakeGameplayFont(17));TextLabel->SetColorAndOpacity(P.Ink);Button->SetContent(TextLabel);
    Actions->AddChildToHorizontalBox(Button)->SetPadding(FMargin(6,0));Buttons.Add(Button);ButtonStyles.Add(Style);NormalizePresentationLayout();RefreshGamepadSelection();
}
FMCPerkCardPresentation UMCPerkChoiceWidget::MakePresentation(FName ID,const FMCPerkDefinition& Definition,bool bPersonal,int32 PreviewStacks) const
{
    FMCPerkCardPresentation Data;Data.GameplayID=ID;Data.Polarity=Definition.Polarity;
    const auto Content=UMCUIDataLibrary::FindCardForGameplayID(CachedCardTable,ID,DisplayCulture(),GetResolvedTranslationTable());
    Data.DisplayName=Content.bFound?Content.DisplayName:Definition.DisplayName;
    Data.Description=Content.bFound?Content.Description:Definition.Description;
    Data.Image=Content.bFound?Content.Image:Definition.Icon;
    if(Data.DisplayName.IsEmpty()) Data.DisplayName=FText::FromName(ID);
    const auto* PC=bApplyingPreview?nullptr:GetOwningPlayer<AMCPlayerController>();const auto* State=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;const auto* Perks=State?State->Perks.Get():nullptr;
    const int32 Before=PreviewStacks!=INDEX_NONE?PreviewStacks:Perks?Perks->GetStacks(ID):0;
    const FCulturePtr Culture=FInternationalization::Get().GetCulture(DisplayCulture());
    UMCPerkCardWidget::ApplyDefinitionMetadata(Data,Definition,GetResolvedTranslationTable(),DisplayCulture(),bPersonal,Before);
    // These are the same optional before/after values as the existing gameplay preview.
    if(Perks) for(const auto& M:Definition.Modifiers) if(M.Condition==EMCRogueActivation::Always) {
        float Base=1.f;FText Stat;const auto* Hero=State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
        switch(M.Stat) {
        case EMCRogueStat::NormalSpeed:Stat=Copy(TEXT("UI.Reward.Stat.Speed"),LOCTEXT("Speed","Скорость"));break;
        case EMCRogueStat::MaxHealth:Stat=Copy(TEXT("UI.Reward.Stat.MaxHealth"),LOCTEXT("MaxHealth","Макс. HP"));Base=Hero && Hero->Status?Hero->Status->GetBaseMaxHealth():100;break;
        case EMCRogueStat::PickaxeDamage:case EMCRogueStat::ChainsawDamage:case EMCRogueStat::WeaponDamage:Stat=Copy(TEXT("UI.Reward.Stat.Damage"),LOCTEXT("Damage","Урон"));break;
        case EMCRogueStat::BrushPower:Stat=Copy(TEXT("UI.Reward.Stat.Cleaning"),LOCTEXT("Cleaning","Чистка"));break;
        case EMCRogueStat::CarryCapacity:Stat=Copy(TEXT("UI.Reward.Stat.Pieces"),LOCTEXT("Pieces","Куски"));Base=Hero && Hero->FoodCollection?Hero->FoodCollection->MaxPieces:6;break;
        default:continue;
        }
        const float Current=Perks->RogueValue(M.Stat,Base),Next=Perks->RogueValue(M.Stat,Base,nullptr,ID);
        FNumberFormattingOptions Rounded;Rounded.SetMaximumFractionalDigits(0);
        const FText CurrentText=FText::AsNumber(Base==1?Current*100:Current,&Rounded,Culture),NextText=FText::AsNumber(Base==1?Next*100:Next,&Rounded,Culture);
        const FText Delta=Format(Base==1?Copy(TEXT("UI.Reward.StatPercentFormat"),LOCTEXT("StatPercentFormat","{Stat} {Before}% → {After}%")):Copy(TEXT("UI.Reward.StatValueFormat"),LOCTEXT("StatValueFormat","{Stat} {Before} → {After}")),FFormatNamedArguments{{TEXT("Stat"),Stat},{TEXT("Before"),CurrentText},{TEXT("After"),NextText}});
        Data.StackPreview=Format(Copy(TEXT("UI.Reward.StackAndStat"),LOCTEXT("StackAndStat","{Stack}\n{Stat}")),FFormatNamedArguments{{TEXT("Stack"),Data.StackPreview},{TEXT("Stat"),Delta}});break;
    }
    if(bTeamBooster)
    {
        Data.Badge=Copy(TEXT("UI.Reward.TeamBoosterBadge"),LOCTEXT("TeamBoosterBadge","ВСЕЙ КОМАНДЕ · СРАЗУ"));
        Data.StackPreview=FText::GetEmpty();Data.BenefitTokens.Reset();Data.CostTokens.Reset();
        if(ID==TEXT("Fallback_Mouthwash")) Data.Description=Copy(TEXT("UI.Reward.TeamMouthwash"),LOCTEXT("TeamMouthwash","Мгновенная волна вокруг каждого живого союзника: убирает грязь и наносит урон врагам. Общие цели получают эффект один раз."));
        else if(ID==TEXT("Fallback_BrushFury")) Data.Description=Copy(TEXT("UI.Reward.TeamFury"),LOCTEXT("TeamFury","Все живые союзники сразу входят в ярость щётки на 5 секунд."));
        else if(ID==TEXT("Fallback_Grenade")) Data.Description=Copy(TEXT("UI.Reward.TeamGrenade"),LOCTEXT("TeamGrenade","Сразу вооружает каждого живого союзника. Его следующий нокдаун вызывает взрыв."));
    }
    return Data;
}
void UMCPerkChoiceWidget::BuildChoices(const TArray<FName>& IDs,EMCPerkPolarity Polarity,bool bPersonal,bool bReadOnly)
{
    BuildPresentationTree();if(!Cards || !Actions || !Title || !Subtitle || !Hint) return;
    bShowingChoices=true;bSelectionPending=false;bWaitingForTeam=false;bTeamBooster=false;bLevelPanel=bPersonal;bOptionalReward=false;bConfirming=false;
    CurrentChoiceIDs.Reset();CurrentDefinitions.Reset();
    Actions->SetVisibility(ESlateVisibility::Visible);
    NormalizePresentationLayout();
    Actions->ClearChildren();Cards->ClearChildren();Cards->SetVisibility(ESlateVisibility::Visible);Buttons.Reset();ButtonStyles.Reset();CardWidgets.Reset();GamepadSelection=0;NextStickNavigationAt=0;
    SetOrdinaryHeader();
    auto* PC=bApplyingPreview?nullptr:GetOwningPlayer<AMCPlayerController>();const auto* State=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    bNavigationHighlight=(PC && PC->IsUsingGamepad()) || ((bApplyingPreview || IsDesignTime()) &&
        (PreviewState==TEXT("Selected") || PreviewScenario==EMCGameplayUIPreviewScenario::Selected));
    for(int32 Index=0;Index<IDs.Num() && Index<3;++Index) {
        FMCPerkDefinition Definition;Definition.Polarity=Polarity;
        if(State && State->Perks) State->Perks->GetPerkDefinition(IDs[Index],Definition);
        else if(bApplyingPreview && PreviewDefinitions) if(const auto* Row=PreviewDefinitions->FindRow<FMCPerkDefinition>(IDs[Index],TEXT("UI Preview"),false)) Definition=*Row;
        CurrentChoiceIDs.Add(IDs[Index]);CurrentDefinitions.Add(Definition);
        auto* Card=WidgetTree->ConstructWidget<UMCPerkCardWidget>(CardWidgetClass?CardWidgetClass.Get():UMCPerkCardWidget::StaticClass());Card->PreviewCulture=DisplayCulture();
        Card->Configure(MakePresentation(IDs[Index],Definition,bPersonal,bApplyingPreview?0:INDEX_NONE),PC,Index,!bReadOnly);
        auto* CardSlot=Cards->AddChildToHorizontalBox(Card);CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));CardSlot->SetPadding(FMargin(6,0));CardSlot->SetVerticalAlignment(VAlign_Fill);
        CardWidgets.Add(Card);Buttons.Add(Card->GetChoiceButton());ButtonStyles.Add(Card->GetBaseButtonStyle());
    }
    RefreshGamepadSelection();ApplyTheme();
}
void UMCPerkChoiceWidget::ShowRogueChoices(const FMCRogueChestOffer& Offer)
{
    const bool Confirm=Offer.Kind==EMCRogueRewardKind::CursedContract && Offer.Step==2;
    TArray<FName> IDs=Offer.IDs;if(Confirm) IDs={Offer.Curse,Offer.Reward};
    BuildChoices(IDs,Offer.Kind==EMCRogueRewardKind::RiskyAltar?EMCPerkPolarity::Risky:Offer.Step==0?EMCPerkPolarity::Negative:EMCPerkPolarity::Positive,false,Confirm);
    CurrentRogueOffer=Offer;bOptionalReward=true;bConfirming=Confirm;
    if(!Title || !Subtitle || !Hint) return;
    RefreshRogueCopy(Offer);
    if(Confirm) {
        for(const auto& Card:CardWidgets) if(Card) Card->SetInteractionEnabled(false);
        AddAction(Copy(TEXT("UI.Reward.AcceptContract"),LOCTEXT("AcceptContract","ПРИНЯТЬ ДОГОВОР [ENTER]")),-3);
        Hint->SetText(Copy(TEXT("UI.Reward.ContractConfirmHint"),LOCTEXT("ContractConfirmHint","Принять — Enter / A. Отказаться — Escape / B.")));
    }
    AddAction(Copy(TEXT("UI.Reward.Decline"),LOCTEXT("Decline","ОТКАЗАТЬСЯ [ESC]")),-2);
}
void UMCPerkChoiceWidget::RefreshRogueCopy(const FMCRogueChestOffer& Offer)
{
    if(!Title || !Subtitle || !Hint) return;
    const bool Confirm=Offer.Kind==EMCRogueRewardKind::CursedContract && Offer.Step==2;
    if(Offer.Kind==EMCRogueRewardKind::RiskyAltar) {
        Title->SetText(Copy(TEXT("UI.Reward.RiskyTitle"),LOCTEXT("RiskyTitle","СИЛА С ЦЕНОЙ")));
        Subtitle->SetText(Copy(TEXT("UI.Reward.RiskySubtitle"),LOCTEXT("RiskySubtitle","Выберите один эффект. Цена останется до конца забега.")));
    } else {
        Title->SetText(Confirm?Copy(TEXT("UI.Reward.ContractConfirmTitle"),LOCTEXT("ContractConfirmTitle","ПОДТВЕРДИТЬ ДОГОВОР")):Offer.Step==0?Copy(TEXT("UI.Reward.ChooseCurse"),LOCTEXT("ChooseCurse","ВЫБЕРИТЕ ПРОКЛЯТИЕ")):Copy(TEXT("UI.Reward.ChooseReward"),LOCTEXT("ChooseReward","ВЫБЕРИТЕ НАГРАДУ")));
        FText Summary;
        if(Offer.Step==0) {
            FText Names;auto* PC=bApplyingPreview?nullptr:GetOwningPlayer<AMCPlayerController>();auto* State=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
            for(int32 I=0;I<Offer.RewardIDs.Num();++I) {
                FMCPerkDefinition Row;if(State && State->Perks) State->Perks->GetPerkDefinition(Offer.RewardIDs[I],Row);
                const auto Content=UMCUIDataLibrary::FindCardForGameplayID(CachedCardTable,Offer.RewardIDs[I],DisplayCulture(),GetResolvedTranslationTable());const FText Name=Content.bFound?Content.DisplayName:Row.DisplayName;
                Names=I==0?Name:Format(Copy(TEXT("UI.Reward.NameList"),LOCTEXT("NameList","{Previous} / {Next}")),FFormatNamedArguments{{TEXT("Previous"),Names},{TEXT("Next"),Name}});
            }
            Summary=Format(Copy(TEXT("UI.Reward.ContractRewardPreview"),LOCTEXT("ContractRewardPreview","Взамен получите одну редкую награду: {Rewards}")),FFormatNamedArguments{{TEXT("Rewards"),Names}});
        } else Summary=Confirm?Copy(TEXT("UI.Reward.ContractConfirmSubtitle"),LOCTEXT("ContractConfirmSubtitle","Обе карты будут применены вместе. До подтверждения ничего не получено.")):Copy(TEXT("UI.Reward.ContractChooseRewardSubtitle"),LOCTEXT("ContractChooseRewardSubtitle","После выбора награды покажем обе карты для общего подтверждения."));
        Subtitle->SetText(Summary);
    }
    if(Confirm) Hint->SetText(Copy(TEXT("UI.Reward.ContractConfirmHint"),LOCTEXT("ContractConfirmHint","Принять — Enter / A. Отказаться — Escape / B.")));
}
void UMCPerkChoiceWidget::UpdateLevelChoiceHeader(int32 TeamLevel,int32 PendingChoices)
{
    CurrentTeamLevel=TeamLevel;CurrentPendingChoices=PendingChoices;
    if(!Title || !Subtitle || !Hint) return;const FCulturePtr Culture=FInternationalization::Get().GetCulture(DisplayCulture());
    Title->SetText(Format(Copy(TEXT("UI.Reward.LevelTitle"),LOCTEXT("LevelTitle","КОМАНДА · УРОВЕНЬ {Level}")),FFormatNamedArguments{{TEXT("Level"),FText::AsNumber(TeamLevel,nullptr,Culture)}}));
    Subtitle->SetText(PendingChoices>1?Format(Copy(TEXT("UI.Reward.PendingChoices"),LOCTEXT("PendingChoices","Выберите личный перк · осталось выборов: {Count}")),FFormatNamedArguments{{TEXT("Count"),FText::AsNumber(PendingChoices,nullptr,Culture)}}):Copy(TEXT("UI.Reward.LevelSubtitle"),LOCTEXT("LevelSubtitle","Общие усилия открыли уровень. Выберите личный перк.")));
    const auto* PC=bApplyingPreview?nullptr:GetOwningPlayer<AMCPlayerController>();const auto* State=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    const bool Spent=bApplyingPreview?bPreviewRerollSpent:State && State->bRogueRerollSpent;
    if(Actions && Actions->GetChildrenCount()==0) AddAction(Copy(TEXT("UI.Reward.Reroll"),LOCTEXT("Reroll","ПЕРЕБРОСИТЬ [R]")),-1);
    if(Actions && Actions->GetChildrenCount()>0) Actions->GetChildAt(0)->SetIsEnabled((bApplyingPreview || State) && !Spent && !bSelectionPending);
    Hint->SetText(Spent?Copy(TEXT("UI.Reward.RerollSpentHint"),LOCTEXT("RerollSpentHint","1 / 2 / 3 или D-pad + A · переброс уже использован")):Copy(TEXT("UI.Reward.LevelHint"),LOCTEXT("LevelHint","1 / 2 / 3 или D-pad + A · переброс: R / Y")));RefreshGamepadSelection();
}
void UMCPerkChoiceWidget::SetSelectionPending(bool bPending)
{
    bSelectionPending=bPending;
    const auto* PC=bApplyingPreview?nullptr:GetOwningPlayer<AMCPlayerController>();const auto* State=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    for(const auto& Button:Buttons) if(Button) {
        const int32 Choice=Button->GetChoiceIndex();bool Enabled=!bPending && (!bConfirming || Choice<0);
        if(Choice==-1) Enabled&=(bApplyingPreview?!bPreviewRerollSpent:State && !State->bRogueRerollSpent);
        Button->SetIsEnabled(bConfirming && Choice>=0?true:Enabled);
    }
    if(Hint) Hint->SetText(bPending?Copy(TEXT("UI.Reward.SelectionPending"),LOCTEXT("SelectionPending","Подтверждаем выбор…")):bConfirming?Copy(TEXT("UI.Reward.ContractConfirmHint"),LOCTEXT("ContractConfirmHint","Принять — Enter / A. Отказаться — Escape / B.")):Copy(TEXT("UI.Reward.ActiveChoiceHint"),LOCTEXT("ActiveChoiceHint","Выбор: 1 / 2 / 3 или стик / D-pad + A")));
    RefreshGamepadSelection();
}
void UMCPerkChoiceWidget::ShowTeamWaiting(int32 Ready,int32 Total)
{
    CurrentReady=Ready;CurrentTotal=Total;
    BuildPresentationTree();
    bShowingChoices=false;bSelectionPending=false;bWaitingForTeam=true;
    bLevelPanel=bOptionalReward=bConfirming=false;
    for(const auto& Button:Buttons) if(Button) {Button->SetIsEnabled(false);Button->SetInteractionAllowed(false);}
    if(Cards) Cards->SetVisibility(ESlateVisibility::Collapsed);
    if(Actions) Actions->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* Size=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("RewardPanelSize")))) {Size->SetWidthOverride(760);Size->SetHeightOverride(280);}
    if(Title) Title->SetText(Copy(TEXT("UI.Reward.WaitingTitle"),LOCTEXT("WaitingTitle","ОЖИДАЕМ КОМАНДУ")));
    if(Subtitle) {Subtitle->SetText(Copy(TEXT("UI.Reward.WaitingSubtitle"),LOCTEXT("WaitingSubtitle","Вы готовы. Остальные ещё выбирают перки.")));Subtitle->SetFont(GetResolvedTheme()->MakeGameplayFont(22));}
    const FCulturePtr Culture=FInternationalization::Get().GetCulture(DisplayCulture());
    if(Hint) Hint->SetText(Format(Copy(TEXT("UI.Reward.TeamReady"),LOCTEXT("TeamReady","Готовы: {Ready} / {Total} · игра продолжится после общего выбора")),
        FFormatNamedArguments{{TEXT("Ready"),FText::AsNumber(FMath::Clamp(Ready,0,FMath::Max(0,Total)),nullptr,Culture)},
            {TEXT("Total"),FText::AsNumber(FMath::Max(0,Total),nullptr,Culture)}}));
    if(Hint) Hint->SetFont(GetResolvedTheme()->MakeGameplayFont(17));
}
void UMCPerkChoiceWidget::RefreshGamepadSelection()
{
    if(!Buttons.IsValidIndex(GamepadSelection) || !Buttons[GamepadSelection] || !Buttons[GamepadSelection]->GetIsEnabled() || !Buttons[GamepadSelection]->IsInteractionAllowed()) GamepadSelection=Buttons.IndexOfByPredicate([](const auto& Button){return Button && Button->GetIsEnabled() && Button->IsInteractionAllowed();});
    for(int32 Index=0;Index<Buttons.Num();++Index) if(Buttons[Index] && ButtonStyles.IsValidIndex(Index)) {FButtonStyle Style=ButtonStyles[Index];if(bNavigationHighlight && Index==GamepadSelection) Style.SetNormal(Style.Hovered);Buttons[Index]->SetStyle(Style);}
}
void UMCPerkChoiceWidget::MoveGamepadSelection(int32 Direction)
{
    if(Buttons.IsEmpty() || bSelectionPending) return;
    bNavigationHighlight=true;
    for(int32 Attempt=0;Attempt<Buttons.Num();++Attempt) {GamepadSelection=(GamepadSelection+Direction+Buttons.Num())%Buttons.Num();if(Buttons[GamepadSelection] && Buttons[GamepadSelection]->GetIsEnabled() && Buttons[GamepadSelection]->IsInteractionAllowed()) break;}
    RefreshGamepadSelection();
    if(Buttons.IsValidIndex(GamepadSelection) && Buttons[GamepadSelection]->GetIsEnabled())
        if(auto* PC=GetOwningPlayer<AMCPlayerController>()) Buttons[GamepadSelection]->SetUserFocus(PC);
}
void UMCPerkChoiceWidget::ApplyEditorPreview(float Time)
{
    TGuardValue<bool> PreviewGuard(bApplyingPreview,true);BuildPresentationTree();
    // Play updates only presentation time; rebuilding three trees every editor frame is unnecessary.
    // Details Preview invalidates this through ResetEditorPreview; Play retains
    // its content until the scenario, data-table choice or child class changes.
    if(bHasPreviewFixture && LastPreviewState==PreviewState && LastPreviewScenario==PreviewScenario
        && LastPreviewCulture==PreviewCulture && LastPreviewIDs==PreviewPerkIDs
        && LastPreviewCardTable.Get()==CachedCardTable && LastPreviewLocalizationTable.Get()==GetResolvedTranslationTable()
        && LastPreviewCardWidgetClass.Get()==CardWidgetClass.Get()) {
        SetRenderOpacity(Time<=0.f?1.f:FMath::Clamp(Time*6.f,0.f,1.f));return;
    }
    if(!PreviewDefinitions) PreviewDefinitions=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
    const TArray<FName> IDs=PreviewPerkIDs.IsEmpty()?TArray<FName>{TEXT("P01"),TEXT("P02"),TEXT("P03")}:PreviewPerkIDs;
    bPreviewRerollSpent=PreviewState==TEXT("RerollSpent");
    if(PreviewState==TEXT("ContractConfirm")) {
        FMCRogueChestOffer Offer;Offer.Kind=EMCRogueRewardKind::CursedContract;Offer.Step=2;Offer.Curse=TEXT("N01");Offer.Reward=TEXT("P30");ShowRogueChoices(Offer);
    } else if(PreviewState==TEXT("Risky")) {
        FMCRogueChestOffer Offer;Offer.Kind=EMCRogueRewardKind::RiskyAltar;Offer.IDs=PreviewPerkIDs.IsEmpty()?TArray<FName>{TEXT("X01"),TEXT("X02"),TEXT("X04")}:IDs;ShowRogueChoices(Offer);
    } else if(PreviewState==TEXT("Reward")) ShowChoices(IDs,EMCPerkPolarity::Positive);
    else ShowLevelChoices(IDs,2,1);
    if(PreviewState==TEXT("Pending")) SetSelectionPending(true);
    if(PreviewState==TEXT("WaitingForTeam")) ShowTeamWaiting(1,2);
    bHasPreviewFixture=true;LastPreviewState=PreviewState;LastPreviewCulture=PreviewCulture;LastPreviewIDs=PreviewPerkIDs;
    LastPreviewScenario=PreviewScenario;LastPreviewCardTable=CachedCardTable;LastPreviewLocalizationTable=GetResolvedTranslationTable();LastPreviewCardWidgetClass=CardWidgetClass.Get();
    SetRenderOpacity(Time<=0.f?1.f:FMath::Clamp(Time*6.f,0.f,1.f));
}
void UMCPerkChoiceWidget::ResetEditorPreview() {bHasPreviewFixture=false;SetRenderOpacity(1.f);}
void UMCPerkChoiceWidget::OnLocalizationChanged()
{
    bHasPreviewFixture=false;
    CacheDataTables();
    if (IsDesignTime() || bApplyingPreview)
    {
        ApplyEditorPreview(PreviewTime);
        return;
    }
    if (bWaitingForTeam)
    {
        ShowTeamWaiting(CurrentReady,CurrentTotal);
        ApplyTheme();
        return;
    }
    if (!bShowingChoices) {ApplyTheme();return;}
    // Re-present the existing offer, retaining controls, selection and pending
    // authority state. Localization never requests a new gameplay offer.
    auto* PC=GetOwningPlayer<AMCPlayerController>();
    for (int32 Index=0;Index<CardWidgets.Num();++Index)
    {
        auto* Card=CardWidgets[Index].Get();
        if (!Card || !CurrentChoiceIDs.IsValidIndex(Index) || !CurrentDefinitions.IsValidIndex(Index)) continue;
        auto* Button=Card->GetChoiceButton();
        const bool bEnabled=Button && Button->GetIsEnabled();
        Card->Configure(MakePresentation(CurrentChoiceIDs[Index],CurrentDefinitions[Index],bLevelPanel),PC,Index,!bConfirming,true);
        if (Button) Button->SetIsEnabled(bEnabled);
    }
    SetOrdinaryHeader();
    if (bLevelPanel) UpdateLevelChoiceHeader(CurrentTeamLevel,CurrentPendingChoices);
    else if (bOptionalReward) RefreshRogueCopy(CurrentRogueOffer);
    else if (bTeamBooster) SetTeamBoosterHeader();
    for (const auto& Button:Buttons) if (Button)
    {
        auto* Label=Cast<UTextBlock>(Button->GetContent());
        if (!Label) continue;
        switch (Button->GetChoiceIndex())
        {
        case -1:Label->SetText(Copy(TEXT("UI.Reward.Reroll"),LOCTEXT("Reroll","ПЕРЕБРОСИТЬ [R]")));break;
        case -2:Label->SetText(Copy(TEXT("UI.Reward.Decline"),LOCTEXT("Decline","ОТКАЗАТЬСЯ [ESC]")));break;
        case -3:Label->SetText(Copy(TEXT("UI.Reward.AcceptContract"),LOCTEXT("AcceptContract","ПРИНЯТЬ ДОГОВОР [ENTER]")));break;
        default:break;
        }
    }
    if (bSelectionPending) SetSelectionPending(true);
    ApplyTheme();
}
void UMCPerkChoiceWidget::NativeDestruct()
{
    UnobserveCardTable();
    Super::NativeDestruct();
}
void UMCPerkChoiceWidget::BeginDestroy()
{
    UnobserveCardTable();
    Super::BeginDestroy();
}
FReply UMCPerkChoiceWidget::NativeOnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(bNavigationHighlight && !Event.GetCursorDelta().IsNearlyZero()) {bNavigationHighlight=false;RefreshGamepadSelection();}
    return Super::NativeOnMouseMove(Geometry,Event);
}
FReply UMCPerkChoiceWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    // Keep one keyboard/gamepad routing path when the complete card button has focus.
    return NativeOnKeyDown(Geometry,Event);
}
FReply UMCPerkChoiceWidget::NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event)
{
    if(bWaitingForTeam) return FReply::Handled();
    if(bShowingChoices && Event.GetKey()==EKeys::Gamepad_RightY) {
        const float Delta=-Event.GetAnalogValue()*18.f;
        if(FMath::Abs(Event.GetAnalogValue())>.15f) {
            if(bConfirming) {for(const auto& Card:CardWidgets) if(Card) Card->ScrollContent(Delta);}
            else if(CardWidgets.IsValidIndex(GamepadSelection) && CardWidgets[GamepadSelection]) CardWidgets[GamepadSelection]->ScrollContent(Delta);
        }
        return FReply::Handled();
    }
    if(Event.GetKey()==EKeys::Gamepad_LeftX || Event.GetKey()==EKeys::Gamepad_LeftY) {
        const float Value=Event.GetAnalogValue();
        if(FMath::Abs(Value)>.55f && FPlatformTime::Seconds()>=NextStickNavigationAt) {
            const int32 Direction=(Event.GetKey()==EKeys::Gamepad_LeftY?-Value:Value)>0?1:-1;
            MoveGamepadSelection(Direction); NextStickNavigationAt=FPlatformTime::Seconds()+.22;
        }
        return FReply::Handled();
    }
    return Super::NativeOnAnalogValueChanged(Geometry,Event);
}

FReply UMCPerkChoiceWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if(IsDesignTime() || bApplyingPreview || !GetOwningPlayer<AMCPlayerController>()) return FReply::Handled();
    if(bWaitingForTeam) return FReply::Handled();
    const FKey Key=Event.GetKey();
    if(bShowingChoices && !bSelectionPending && !bConfirming && !Event.IsRepeat() && (Key==EKeys::Enter || Key==EKeys::SpaceBar)) {
        if(Buttons.IsValidIndex(GamepadSelection) && Buttons[GamepadSelection]->GetIsEnabled()) Buttons[GamepadSelection]->OnClicked.Broadcast();
        return FReply::Handled();
    }
    if(bShowingChoices && !bSelectionPending && Key.IsGamepadKey()) {
        if(Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Up) MoveGamepadSelection(-1);
        else if(Key==EKeys::Gamepad_DPad_Right || Key==EKeys::Gamepad_DPad_Down) MoveGamepadSelection(1);
        else if(!Event.IsRepeat()) {
            if(Key==EKeys::Gamepad_FaceButton_Bottom && Buttons.IsValidIndex(GamepadSelection) && Buttons[GamepadSelection]->GetIsEnabled())
                Buttons[GamepadSelection]->OnClicked.Broadcast();
            else if(Key==EKeys::Gamepad_FaceButton_Right && bOptionalReward) GetOwningPlayer<AMCPlayerController>()->ConfirmRogueReward(false);
            else if(Key==EKeys::Gamepad_FaceButton_Top && bLevelPanel) GetOwningPlayer<AMCPlayerController>()->RerollLevelPerks();
        }
        return FReply::Handled();
    }
    if (bShowingChoices && !bSelectionPending && !Event.IsRepeat())
    {
        if(bLevelPanel && Event.GetKey()==EKeys::R) {if(auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->RerollLevelPerks();return FReply::Handled();}
        if(bOptionalReward && Event.GetKey()==EKeys::Escape) {if(auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->ConfirmRogueReward(false);return FReply::Handled();}
        if(bConfirming) {
            if(Event.GetKey()==EKeys::Enter || Event.GetKey()==EKeys::One || Event.GetKey()==EKeys::NumPadOne) {GetOwningPlayer<AMCPlayerController>()->ConfirmRogueReward(true);return FReply::Handled();}
            if(Event.GetKey()==EKeys::Two || Event.GetKey()==EKeys::NumPadTwo) {GetOwningPlayer<AMCPlayerController>()->ConfirmRogueReward(false);return FReply::Handled();}
            return FReply::Handled();
        }
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

#undef LOCTEXT_NAMESPACE
