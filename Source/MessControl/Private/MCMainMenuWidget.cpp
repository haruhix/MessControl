#include "MCMainMenuWidget.h"

#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCSteamSessionSubsystem.h"
#include "MCUIDataLibrary.h"
#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"

namespace MCMainMenuPrivate
{
    FSlateFontInfo Font(FName Style,int32 Size)
    {
        // The saved runtime composite contains its CJK face in packaged games.
        const auto* Theme=UMCGameplayUIFactory::GetMasterTheme();
        return Theme?Theme->MakeFont(Size,Style==TEXT("Bold")):FCoreStyle::GetDefaultFontStyle(Style,Size);
    }
    const FName MouthMap(TEXT("/Game/Maps/L_Mouth"));
    const FName TutorialMap(TEXT("/Game/Maps/L_Tutorial"));
    const FLinearColor TextColor(.94f,.96f,.97f);
    const FLinearColor Accent(.26f,.78f,.65f);

    FButtonStyle EmbeddedButtonStyle(const FButtonStyle& Base, const FMCEmbeddedMenuStyle& Theme, bool bPrimary)
    {
        FButtonStyle Style=Base;
        const FLinearColor Fill=bPrimary?Theme.AccentColor:Theme.ButtonColor;
        const FLinearColor Hover=FMath::Lerp(Fill,Theme.TextColor,.16f);
        const FLinearColor Pressed(Fill.R*.72f,Fill.G*.72f,Fill.B*.72f,Fill.A);
        const FLinearColor Disabled(Fill.R,Fill.G,Fill.B,Fill.A*.35f);
        const float Radius=FMath::Clamp(Theme.CornerRadius,0.f,32.f);
        Style.SetNormal(FSlateRoundedBoxBrush(Fill,Radius));
        Style.SetHovered(FSlateRoundedBoxBrush(Hover,Radius));
        Style.SetPressed(FSlateRoundedBoxBrush(Pressed,Radius));
        Style.SetDisabled(FSlateRoundedBoxBrush(Disabled,Radius));
        Style.SetNormalPadding(FMargin(0));
        Style.SetPressedPadding(FMargin(0));
        return Style;
    }

    UMCSteamSessionSubsystem* Sessions(const UUserWidget* Widget)
    {
        UGameInstance* Instance=Widget->GetGameInstance();
        return Instance?Instance->GetSubsystem<UMCSteamSessionSubsystem>():nullptr;
    }
}

void UMCMainMenuActionButton::Configure(UMCMainMenuWidget* InMenu, EMCMainMenuAction InAction)
{
    Menu=InMenu; Action=InAction;
    OnClicked.AddUniqueDynamic(this,&ThisClass::Clicked);
}

void UMCMainMenuActionButton::Clicked()
{
    if (Menu) Menu->HandleAction(Action);
}

FString UMCMainMenuWidget::LocalizedCopy(FName Key,const FString& Fallback) const
{
    return UMCUIDataLibrary::ResolveText(UMCUIDataLibrary::GetDefaultLocalizationTable(),Key,FText::AsCultureInvariant(Fallback)).ToString();
}
FString UMCMainMenuWidget::FormatCopy(FName Key,const FString& Fallback,const FFormatNamedArguments& Arguments) const
{
    return UMCUIDataLibrary::FormatTextForCulture(UMCUIDataLibrary::ResolveText(
        UMCUIDataLibrary::GetDefaultLocalizationTable(),Key,FText::AsCultureInvariant(Fallback)),Arguments).ToString();
}

void UMCMainMenuWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);

    Backdrop=WidgetTree->ConstructWidget<UBorder>();
    Backdrop->SetBrushColor(FLinearColor(.015f,.025f,.035f,.98f));
    Backdrop->SetPadding(FMargin(28));
    Backdrop->SetHorizontalAlignment(HAlign_Center);
    Backdrop->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget=Backdrop;

    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);
    Scale->SetStretchDirection(EStretchDirection::DownOnly);
    Backdrop->SetContent(Scale);
    auto* Size=WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(540); Size->SetHeightOverride(680);
    Card=WidgetTree->ConstructWidget<UBorder>();
    Card->SetBrushColor(FLinearColor::Transparent);
    Card->SetPadding(FMargin(0));
    Scale->SetContent(Card);
    Card->SetContent(Size);
    auto* Main=WidgetTree->ConstructWidget<UVerticalBox>(); Size->SetContent(Main);
    TitleText=WidgetTree->ConstructWidget<UTextBlock>();
    TitleText->SetText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.BrandTitle"),TEXT("MESS CONTROL"))));
    TitleText->SetFont(MCMainMenuPrivate::Font(TEXT("Bold"),36));
    TitleText->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::Accent));
    Main->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0,0,0,20));

    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
    Main->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Content=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Content);
    StatusText=WidgetTree->ConstructWidget<UTextBlock>();
    StatusText->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),14));
    StatusText->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::Accent));
    StatusText->SetAutoWrapText(true);
    Main->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0,14,0,0));
    HintText=WidgetTree->ConstructWidget<UTextBlock>();
    HintText->SetText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.NavigationHint"),TEXT("Мышь / ↑↓ / D-pad · Enter / A — выбрать · Esc / B — назад"))));
    HintText->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),12));
    HintText->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::TextColor));
    HintText->SetAutoWrapText(true);
    Main->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0,10,0,0));
    ShowPage(EPage::Main);
}

void UMCMainMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    FocusFirst();
}

void UMCMainMenuWidget::NativeDestruct()
{
    // Never retain an unconfirmed display mode after the menu is removed or the world travels.
    if (bVideoConfirmationPending) RevertVideoSettings();
    Super::NativeDestruct();
}

UTextBlock* UMCMainMenuWidget::AddText(const FString& Text, int32 Size)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Text));
    Label->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),Size));
    Label->SetColorAndOpacity(FSlateColor(MenuTextColor()));
    Label->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,4,0,8));
    return Label;
}

UMCMainMenuActionButton* UMCMainMenuWidget::AddButton(const FString& Text, EMCMainMenuAction Action)
{
    auto* Button=WidgetTree->ConstructWidget<UMCMainMenuActionButton>();
    Button->Configure(this,Action);
    Button->SetBackgroundColor(FLinearColor(.07f,.15f,.19f));
    const bool bEmbedded=EmbeddedRoot!=EEmbeddedRoot::None;
    const bool bPrimary=Action==EMCMainMenuAction::CreateLobby || Action==EMCMainMenuAction::Host
        || Action==EMCMainMenuAction::JoinRoom || Action==EMCMainMenuAction::JoinAddress
        || Action==EMCMainMenuAction::ApplySettings || Action==EMCMainMenuAction::KeepSettings;
    if (bEmbedded)
    {
        Button->SetStyle(MCMainMenuPrivate::EmbeddedButtonStyle(Button->GetStyle(),EmbeddedStyle,bPrimary));
        Button->SetBackgroundColor(FLinearColor::White);
    }
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
    static const TCHAR* ActionNames[]={TEXT("Tutorial"),TEXT("CreateLobby"),TEXT("JoinLobby"),TEXT("Settings"),TEXT("Credits"),TEXT("Exit"),TEXT("Back"),TEXT("Host"),TEXT("FindRooms"),TEXT("JoinRoom"),TEXT("JoinAddress"),TEXT("ApplySettings"),TEXT("KeepSettings"),TEXT("RevertSettings"),TEXT("StartLobby"),TEXT("Invite"),TEXT("LeaveLobby"),TEXT("ConfirmLeave"),TEXT("ConfirmExit"),TEXT("Resume"),TEXT("Emotes")};
    static_assert(UE_ARRAY_COUNT(ActionNames)==int32(EMCMainMenuAction::Emotes)+1,"Keep action keys aligned with the existing menu actions");
    FName Key(*(FString(TEXT("UI.MainMenu.Action."))+ActionNames[FMath::Clamp(int32(Action),0,int32(UE_ARRAY_COUNT(ActionNames))-1)]));
    if(Action==EMCMainMenuAction::Back && (Page==EPage::Exit || Page==EPage::Leave)) Key=TEXT("UI.MainMenu.Action.Cancel");
    if(Action==EMCMainMenuAction::LeaveLobby && Page==EPage::Pause) Key=TEXT("UI.MainMenu.Action.ReturnToMenu");
    Label->SetText(FText::FromString(LocalizedCopy(Key,Text)));
    Label->SetFont(MCMainMenuPrivate::Font(TEXT("Bold"),19));
    Label->SetColorAndOpacity(FSlateColor(bEmbedded && bPrimary?EmbeddedStyle.CardColor:MenuTextColor()));
    Label->SetJustification(ETextJustify::Center);
    auto* ButtonPadding=WidgetTree->ConstructWidget<UBorder>();
    ButtonPadding->SetBrushColor(FLinearColor::Transparent); ButtonPadding->SetPadding(FMargin(14,9));
    ButtonPadding->SetContent(Label); Button->SetContent(ButtonPadding);
    Content->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,4));
    Buttons.Add(Button);
    GamepadTargets.Add(Button);
    return Button;
}

UComboBoxString* UMCMainMenuWidget::AddPicker(const FString& Label)
{
    AddText(Label,15);
    auto* Picker=WidgetTree->ConstructWidget<UComboBoxString>();
    Picker->OnGenerateWidgetEvent.BindDynamic(this,&ThisClass::GeneratePickerItem);
    if (EmbeddedRoot!=EEmbeddedRoot::None)
    {
        FComboBoxStyle Style=Picker->GetWidgetStyle();
        Style.ComboButtonStyle.SetButtonStyle(MCMainMenuPrivate::EmbeddedButtonStyle(Style.ComboButtonStyle.ButtonStyle,EmbeddedStyle,false));
        Style.ComboButtonStyle.SetMenuBorderBrush(FSlateRoundedBoxBrush(EmbeddedStyle.CardColor,FMath::Clamp(EmbeddedStyle.CornerRadius,0.f,32.f)));
        Picker->SetWidgetStyle(Style);
    }
    GamepadTargets.Add(Picker);
    Content->AddChildToVerticalBox(Picker)->SetPadding(FMargin(0,0,0,8));
    return Picker;
}

UWidget* UMCMainMenuWidget::GeneratePickerItem(FString Item)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Item));
    Label->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),17));
    Label->SetColorAndOpacity(FSlateColor(MenuTextColor()));
    return Label;
}

void UMCMainMenuWidget::FocusFirst()
{
    if (!GetOwningPlayer()) return;
    for (UButton* Button:Buttons)
    {
        if (Button && Button->GetIsEnabled()) { Button->SetUserFocus(GetOwningPlayer()); return; }
    }
    SetUserFocus(GetOwningPlayer());
}

void UMCMainMenuWidget::FocusMenu()
{
    if (!bEmbeddedCloseRequested) FocusFirst();
}

const FLinearColor& UMCMainMenuWidget::MenuTextColor() const
{
    return EmbeddedRoot==EEmbeddedRoot::None?MCMainMenuPrivate::TextColor:EmbeddedStyle.TextColor;
}

const FLinearColor& UMCMainMenuWidget::MenuAccentColor() const
{
    return EmbeddedRoot==EEmbeddedRoot::None?MCMainMenuPrivate::Accent:EmbeddedStyle.AccentColor;
}

bool UMCMainMenuWidget::CanLaunchEmbeddedDay() const
{
    return EmbeddedRoot==EEmbeddedRoot::None
        || (EmbeddedRoot==EEmbeddedRoot::Connection && EmbeddedSelectedDay==1);
}

void UMCMainMenuWidget::ApplyPresentation()
{
    const bool bEmbedded=EmbeddedRoot!=EEmbeddedRoot::None;
    if (Backdrop)
    {
        const FLinearColor Color=bEmbedded && EmbeddedStyle.bTransparentBackdrop?FLinearColor::Transparent:
            bEmbedded?EmbeddedStyle.CardColor:FLinearColor(.015f,.025f,.035f,.98f);
        Backdrop->SetBrush(FSlateColorBrush(Color));
        Backdrop->SetBrushColor(FLinearColor::White);
    }
    if (Card)
    {
        Card->SetBrush(FSlateRoundedBoxBrush(bEmbedded?EmbeddedStyle.CardColor:FLinearColor::Transparent,
            FMath::Clamp(EmbeddedStyle.CornerRadius,0.f,32.f)));
        Card->SetBrushColor(FLinearColor::White);
        Card->SetPadding(FMargin(bEmbedded?24.f:0.f));
    }
    if (TitleText)
    {
        FString Title=LocalizedCopy(TEXT("UI.MainMenu.BrandTitle"),TEXT("MESS CONTROL"));
        if (EmbeddedRoot==EEmbeddedRoot::Connection)
        {
            const FString Species=EmbeddedSelectedDay==1?LocalizedCopy(TEXT("UI.MainMenu.Species.Human"),TEXT("HUMAN")):EmbeddedSelectedDay==2?LocalizedCopy(TEXT("UI.MainMenu.Species.Predator"),TEXT("PREDATOR")):
                EmbeddedSelectedDay==3?LocalizedCopy(TEXT("UI.MainMenu.Species.Vampire"),TEXT("VAMPIRE")):LocalizedCopy(TEXT("UI.MainMenu.ComingSoon"),TEXT("COMING SOON"));
            Title=EmbeddedSelectedDay>=1 && EmbeddedSelectedDay<=7?
                FormatCopy(TEXT("UI.MainMenu.EmbeddedDayTitle"),TEXT("DAY {Day} / {Species}"),{{TEXT("Day"),FText::AsCultureInvariant(FString::Printf(TEXT("%02d"),EmbeddedSelectedDay))},{TEXT("Species"),FText::AsCultureInvariant(Species)}}):LocalizedCopy(TEXT("UI.MainMenu.DayUnavailable"),TEXT("DAY —"));
        }
        TitleText->SetText(FText::FromString(Title));
        TitleText->SetFont(MCMainMenuPrivate::Font(TEXT("Bold"),bEmbedded?30:36));
        TitleText->SetColorAndOpacity(FSlateColor(MenuAccentColor()));
    }
    if (StatusText) StatusText->SetColorAndOpacity(FSlateColor(MenuAccentColor()));
    if (HintText) HintText->SetColorAndOpacity(FSlateColor(MenuTextColor()));
}

void UMCMainMenuWidget::LeaveEmbeddedMode()
{
    if (bVideoConfirmationPending) RevertVideoSettings();
    EmbeddedRoot=EEmbeddedRoot::None;
    EmbeddedSelectedDay=0;
    bEmbeddedCloseRequested=false;
}

void UMCMainMenuWidget::ShowEmbeddedConnection(int32 SelectedDay)
{
    if (bVideoConfirmationPending) RevertVideoSettings();
    EmbeddedRoot=EEmbeddedRoot::Connection;
    EmbeddedSelectedDay=SelectedDay;
    bEmbeddedCloseRequested=false;
    bLobby=false;
    bPauseMenu=false;
    ShowPage(EPage::Connection);
}

void UMCMainMenuWidget::ShowEmbeddedSettings()
{
    if (bVideoConfirmationPending) RevertVideoSettings();
    EmbeddedRoot=EEmbeddedRoot::Settings;
    EmbeddedSelectedDay=0;
    bEmbeddedCloseRequested=false;
    bLobby=false;
    bPauseMenu=false;
    ShowPage(EPage::Settings);
}

void UMCMainMenuWidget::RequestBack()
{
    HandleAction(EMCMainMenuAction::Back);
}

bool UMCMainMenuWidget::MoveGamepadFocus(int32 Direction)
{
    if(GamepadTargets.IsEmpty()) return false;
    auto* PC=GetOwningPlayer();
    int32 Current=GamepadTargets.IndexOfByPredicate([&](const auto& Target){return Target && (Target->HasUserFocus(PC) || Target->HasFocusedDescendants());});
    if(GamepadTargets.IsValidIndex(Current))
        if(const auto* Picker=Cast<UComboBoxString>(GamepadTargets[Current]);Picker && Picker->IsOpen()) return false;
    for(int32 Attempt=0;Attempt<GamepadTargets.Num();++Attempt) {
        Current=(Current+Direction+GamepadTargets.Num())%GamepadTargets.Num();
        auto* Target=GamepadTargets[Current].Get();
        if(Target && Target->GetIsEnabled() && Target->IsVisible()) {Target->SetUserFocus(GetOwningPlayer());return true;}
    }
    return true;
}

bool UMCMainMenuWidget::NormalizeJoinAddress(const FString& Input, FString& OutAddress)
{
    OutAddress.Reset();
    FString Address=Input.TrimStartAndEnd();
    if (Address.IsEmpty() || Address.Len()>260) return false;
    FString Host=Address;
    FString PortText=TEXT("7777");
    int32 Colon=INDEX_NONE;
    if (Address.FindChar(TEXT(':'),Colon))
    {
        Host=Address.Left(Colon); PortText=Address.Mid(Colon+1);
        if (PortText.IsEmpty() || PortText.Len()>5) return false;
        for (const TCHAR Character:PortText) if (Character<TEXT('0') || Character>TEXT('9')) return false;
    }
    if (Host.IsEmpty() || Host.Len()>253) return false;
    const int32 Port=FCString::Atoi(*PortText);
    if (Port<1 || Port>65535) return false;
    TArray<FString> Labels;
    Host.ParseIntoArray(Labels,TEXT("."),false);
    bool bNumeric=true;
    for (const FString& Label:Labels)
    {
        if (Label.IsEmpty() || Label.Len()>63 || Label.StartsWith(TEXT("-")) || Label.EndsWith(TEXT("-"))) return false;
        for (const TCHAR Character:Label)
        {
            const bool bDigit=Character>=TEXT('0') && Character<=TEXT('9');
            const bool bLetter=(Character>=TEXT('a') && Character<=TEXT('z')) || (Character>=TEXT('A') && Character<=TEXT('Z'));
            if (!bDigit && !bLetter && Character!=TEXT('-')) return false;
            bNumeric&=bDigit;
        }
    }
    if (bNumeric && Labels.Num()>1)
    {
        if (Labels.Num()!=4) return false;
        for (const FString& Label:Labels) if (Label.Len()>3 || FCString::Atoi(*Label)>255) return false;
    }
    // FURL treats a colon before any dot as a protocol separator, so localhost:7777
    // is still ambiguous. The explicit unreal:// protocol bypasses that legacy branch.
    OutAddress=FString::Printf(TEXT("unreal://%s:%d"),*Host,Port);
    return true;
}

void UMCMainMenuWidget::SetStatus(const FString& Text)
{
    if(!StatusText) return;
    UDataTable* Table=UMCUIDataLibrary::GetDefaultLocalizationTable();FName Key=NAME_None;
    if(!Text.IsEmpty() && Table && Table->GetRowStruct()==FMCUILocalizationRow::StaticStruct()) {
        for(const auto& Entry:Table->GetRowMap()) {
            if(!Entry.Key.ToString().StartsWith(TEXT("UI.Connection.Status."),ESearchCase::CaseSensitive)) continue;
            const auto* Row=reinterpret_cast<const FMCUILocalizationRow*>(Entry.Value);
            const FString* Source=Row?FTextInspector::GetSourceString(Row->SourceText):nullptr;
            if(!Row || (Source?*Source:Row->SourceText.ToString())!=Text) continue;
            if(!Key.IsNone()) {Key=NAME_None;break;}
            Key=Entry.Key;
        }
    }
    StatusText->SetText(UMCUIDataLibrary::ResolveText(Table,Key,FText::AsCultureInvariant(Text)));
}

void UMCMainMenuWidget::ShowLobby()
{
    LeaveEmbeddedMode();
    bLobby=true;
    bPauseMenu=false;
    if (Content) ShowPage(EPage::Lobby);
}

void UMCMainMenuWidget::ShowPause()
{
    LeaveEmbeddedMode();
    bPauseMenu=true;
    bLobby=false;
    if (Content) ShowPage(EPage::Pause);
}

void UMCMainMenuWidget::ShowPage(EPage NewPage)
{
    Page=NewPage;
    if (!Content) return;
    ApplyPresentation();
    Content->ClearChildren(); Buttons.Reset(); OnlineButtons.Reset(); GamepadTargets.Reset(); NextStickNavigationAt=0;
    RoomPicker=nullptr; AddressBox=nullptr; ResolutionPicker=nullptr; WindowModePicker=nullptr;
    QualityPicker=nullptr; VSyncCheck=nullptr; HostTutorialCheck=nullptr; LobbyPlayers=nullptr; StartButton=nullptr;
    LastSearchRevision=INDEX_NONE;
    SetStatus(TEXT(""));
    auto* Steam=MCMainMenuPrivate::Sessions(this);
    const bool bSteam=Steam && Steam->CanUseSteam();
    LastSessionStatus=Steam?Steam->GetStatus():FString();

    switch (Page)
    {
    case EPage::Connection:
        if (CanLaunchEmbeddedDay())
        {
            AddText(LocalizedCopy(TEXT("UI.MainMenu.ConnectionHeading"),TEXT("PLAY TOGETHER")),24);
            AddText(LocalizedCopy(TEXT("UI.MainMenu.ConnectionBody"),TEXT("Создай комнату для обычной смены или присоединись к друзьям.")));
            OnlineButtons.Add(AddButton(TEXT("Create Lobby"),EMCMainMenuAction::CreateLobby));
            OnlineButtons.Add(AddButton(TEXT("Join Lobby"),EMCMainMenuAction::JoinLobby));
        }
        else
        {
            AddText(LocalizedCopy(TEXT("UI.MainMenu.ComingSoon"),TEXT("COMING SOON")),24);
            AddText(LocalizedCopy(TEXT("UI.MainMenu.DayUnavailableBody"),TEXT("Игровой контент этого дня ещё не подключён. Пока доступна игра в Day 01.")));
        }
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Main:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.Tagline"),TEXT("Четыре маленьких зуба. Один рот. Семь дней хаоса.")),16);
        AddButton(TEXT("Tutorial"),EMCMainMenuAction::Tutorial);
        AddButton(TEXT("Create Lobby"),EMCMainMenuAction::CreateLobby);
        AddButton(TEXT("Join Lobby"),EMCMainMenuAction::JoinLobby);
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Credits"),EMCMainMenuAction::Credits);
        AddButton(TEXT("Exit"),EMCMainMenuAction::Exit);
        AddText(LocalizedCopy(TEXT("UI.MainMenu.ModeHint"),TEXT("Tutorial — отдельная учебная смена. Create Lobby — обычная игра с друзьями.")),14);
        break;
    case EPage::Host:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.HostHeading"),TEXT("CREATE LOBBY")),24);
        HostTutorialCheck=WidgetTree->ConstructWidget<UCheckBox>();
        GamepadTargets.Add(HostTutorialCheck);
        {
            auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
            Label->SetText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.StartWithTutorial"),TEXT("Начать с обучения"))));
            Label->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),17));
            Label->SetColorAndOpacity(FSlateColor(MenuTextColor()));
            HostTutorialCheck->SetContent(Label);
            HostTutorialCheck->SetIsChecked(bHostStartWithTutorial);
            HostTutorialCheck->OnCheckStateChanged.AddDynamic(this,&ThisClass::HostTutorialChanged);
            Content->AddChildToVerticalBox(HostTutorialCheck)->SetPadding(FMargin(0,6,0,12));
        }
        AddText(LocalizedCopy(TEXT("UI.MainMenu.TutorialOptionHint"),TEXT("С обучением: сначала учебная карта, затем обычная игра. Без галочки — сразу обычная карта.")),14);
        AddText(bSteam?FormatCopy(TEXT("UI.MainMenu.HostSteamBody"),TEXT("Steam · {Host}\nДо четырёх игроков. После загрузки комнаты хост запускает смену кнопкой Start."),{{TEXT("Host"),FText::AsCultureInvariant(Steam->GetPlayerName())}}):
            LocalizedCopy(TEXT("UI.MainMenu.HostLocalBody"),TEXT("Локальная комната по IP · UDP 7777\nДрузья подключаются через Join Lobby. Хост запускает смену кнопкой Start.")));
        OnlineButtons.Add(AddButton(TEXT("Create"),EMCMainMenuAction::Host));
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Join:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.JoinHeading"),TEXT("JOIN LOBBY")),24);
        if (bSteam)
        {
            OnlineButtons.Add(AddButton(TEXT("Find Steam Lobbies"),EMCMainMenuAction::FindRooms));
            RoomPicker=AddPicker(LocalizedCopy(TEXT("UI.MainMenu.FriendRoom"),TEXT("Комната друга")));
            OnlineButtons.Add(AddButton(TEXT("Join Selected Lobby"),EMCMainMenuAction::JoinRoom));
        }
        AddText(bSteam?LocalizedCopy(TEXT("UI.MainMenu.LocalJoinHint"),TEXT("Для локальных тестов можно подключиться по IP.")):LocalizedCopy(TEXT("UI.MainMenu.EditorJoinHint"),TEXT("В редакторе Steam отключён. Введи IP хоста и при необходимости порт: 192.168.1.10:7777.")),14);
        AddressBox=WidgetTree->ConstructWidget<UEditableTextBox>();
        GamepadTargets.Add(AddressBox);
        AddressBox->SetText(FText::FromString(TEXT("127.0.0.1:7777")));
        AddressBox->SetHintText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.HostAddress"),TEXT("Адрес хоста"))));
        if (EmbeddedRoot!=EEmbeddedRoot::None)
        {
            FEditableTextBoxStyle Style=AddressBox->GetWidgetStyle();
            Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(EmbeddedStyle.ButtonColor,8.f));
            Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(EmbeddedStyle.ButtonColor,8.f));
            Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(EmbeddedStyle.ButtonColor,8.f));
            AddressBox->SetWidgetStyle(Style);
            AddressBox->SetForegroundColor(MenuTextColor());
        }
        {
            auto AddressStyle=AddressBox->GetWidgetStyle();AddressStyle.SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),17));
            AddressBox->SetWidgetStyle(AddressStyle);AddressBox->SetWidgetStyle(AddressBox->GetWidgetStyle());
        }
        Content->AddChildToVerticalBox(AddressBox)->SetPadding(FMargin(0,8));
        OnlineButtons.Add(AddButton(TEXT("Join by IP"),EMCMainMenuAction::JoinAddress));
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Settings:
        BuildSettings();
        break;
    case EPage::Credits:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.CreditsHeading"),TEXT("CREDITS")),24);
        AddText(LocalizedCopy(TEXT("UI.Menu.CreditsBody"),TEXT("Mess Control\nКооперативная игра про маленькие зубы и большой беспорядок во рту.")));
        AddText(LocalizedCopy(TEXT("UI.Menu.CreditsEngine"),TEXT("Движок: Unreal Engine\nUnreal Engine — товарный знак Epic Games, Inc.\nИмена команды и авторов материалов будут добавлены после утверждения титров.")),15);
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Exit:
        AddText(LocalizedCopy(TEXT("UI.Menu.ExitHeading"),TEXT("Выйти из игры?")),24);
        AddButton(TEXT("Cancel"),EMCMainMenuAction::Back);
        AddButton(TEXT("Exit Game"),EMCMainMenuAction::ConfirmExit);
        break;
    case EPage::Lobby:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.LobbyHeading"),TEXT("LOBBY")),24);
        LobbyPlayers=AddText(LocalizedCopy(TEXT("UI.MainMenu.LoadingPlayers"),TEXT("Загружаем участников…")));
        AddText(LocalizedCopy(TEXT("UI.MainMenu.LobbyStartHint"),TEXT("Хост запускает обычную смену, когда команда собрана.")),15);
        StartButton=AddButton(TEXT("Start / Play"),EMCMainMenuAction::StartLobby);
        if (bSteam) OnlineButtons.Add(AddButton(TEXT("Invite Steam Friends"),EMCMainMenuAction::Invite));
        else AddText(LocalizedCopy(TEXT("UI.MainMenu.LobbyLocalHint"),TEXT("Друзья: Join Lobby → IP этого компьютера:7777.")),14);
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Leave Lobby"),EMCMainMenuAction::LeaveLobby);
        RefreshLobby();
        break;
    case EPage::Pause:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.PauseHeading"),TEXT("MENU")),24);
        AddButton(TEXT("Resume"),EMCMainMenuAction::Resume);
        AddButton(TEXT("Emotes / Сигналы"),EMCMainMenuAction::Emotes);
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Credits"),EMCMainMenuAction::Credits);
        AddButton(TEXT("Return to Main Menu"),EMCMainMenuAction::LeaveLobby);
        AddButton(TEXT("Exit"),EMCMainMenuAction::Exit);
        AddText(LocalizedCopy(TEXT("UI.Menu.NetworkNote"),TEXT("В сетевой игре смена продолжается, пока это меню открыто.")),14);
        break;
    case EPage::Leave:
        AddText(LocalizedCopy(TEXT("UI.Menu.LeaveHeading"),TEXT("Выйти из комнаты?")),24);
        AddText(GetOwningPlayer() && GetOwningPlayer()->HasAuthority()?
            LocalizedCopy(TEXT("UI.Menu.LeaveHostBody"),TEXT("Ты — хост. При выходе соединение остальных игроков с комнатой закроется.")):
            LocalizedCopy(TEXT("UI.Menu.LeaveClientBody"),TEXT("Ты вернёшься в главное меню.")),16);
        AddButton(TEXT("Cancel"),EMCMainMenuAction::Back);
        AddButton(TEXT("Leave to Main Menu"),EMCMainMenuAction::ConfirmLeave);
        break;
    case EPage::VideoConfirm:
        AddText(LocalizedCopy(TEXT("UI.Menu.VideoHeading"),TEXT("Сохранить режим экрана?")),24);
        AddText(LocalizedCopy(TEXT("UI.Menu.VideoConfirmBody"),TEXT("Если изображение пропало, предыдущий режим вернётся автоматически через 15 секунд.")));
        AddButton(TEXT("Keep"),EMCMainMenuAction::KeepSettings);
        AddButton(TEXT("Revert"),EMCMainMenuAction::RevertSettings);
        break;
    }
    if (bSteam && (Page==EPage::Host || Page==EPage::Join)) SetStatus(LastSessionStatus);
    else if ((Page==EPage::Main || Page==EPage::Connection) && Steam && Steam->HadFailure()) SetStatus(LastSessionStatus);
    if (Steam)
        for (UButton* Button:OnlineButtons) if (Button) Button->SetIsEnabled(!Steam->IsBusy());
    FocusFirst();
}

void UMCMainMenuWidget::BuildSettings()
{
    AddText(LocalizedCopy(TEXT("UI.MainMenu.SettingsHeading"),TEXT("SETTINGS")),24);
    UGameUserSettings* Settings=UGameUserSettings::GetGameUserSettings();
    if (!Settings) { AddText(LocalizedCopy(TEXT("UI.Menu.SettingsUnavailable"),TEXT("Настройки экрана недоступны."))); AddButton(TEXT("Back"),EMCMainMenuAction::Back); return; }
    ResolutionPicker=AddPicker(LocalizedCopy(TEXT("UI.Menu.Resolution"),TEXT("Разрешение")));
    Resolutions.Reset();
    UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
    Resolutions.RemoveAll([](const FIntPoint& Point){return Point.X<1024 || Point.Y<600;});
    const FIntPoint Current=Settings->GetScreenResolution();
    if (Current.X>0 && Current.Y>0) Resolutions.AddUnique(Current);
    if (Resolutions.IsEmpty()) Resolutions.Add(FIntPoint(1280,720));
    Resolutions.Sort([](const FIntPoint& A,const FIntPoint& B){return A.X==B.X?A.Y<B.Y:A.X<B.X;});
    for (const FIntPoint& Resolution:Resolutions)
        ResolutionPicker->AddOption(FString::Printf(TEXT("%d × %d"),Resolution.X,Resolution.Y));
    ResolutionPicker->SetSelectedIndex(FMath::Max(0,Resolutions.IndexOfByKey(Current)));
    WindowModePicker=AddPicker(LocalizedCopy(TEXT("UI.Menu.WindowMode"),TEXT("Режим экрана")));
    WindowModePicker->AddOption(LocalizedCopy(TEXT("UI.MainMenu.Option.Fullscreen"),TEXT("Fullscreen")));
    WindowModePicker->AddOption(LocalizedCopy(TEXT("UI.MainMenu.Option.Borderless"),TEXT("Borderless")));
    WindowModePicker->AddOption(LocalizedCopy(TEXT("UI.MainMenu.Option.Windowed"),TEXT("Windowed")));
    WindowModePicker->SetSelectedIndex(static_cast<int32>(Settings->GetFullscreenMode()));
    QualityPicker=AddPicker(LocalizedCopy(TEXT("UI.Menu.Quality"),TEXT("Качество графики")));
    const TCHAR* QualityNames[]={TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")};
    for(int32 Index=0;Index<UE_ARRAY_COUNT(QualityNames);++Index)
        QualityPicker->AddOption(LocalizedCopy(FName(*(TEXT("UI.MainMenu.Option.Quality.")+FString(QualityNames[Index]))),QualityNames[Index]));
    const int32 Quality=Settings->GetOverallScalabilityLevel();
    if (Quality<0) { QualityPicker->AddOption(LocalizedCopy(TEXT("UI.MainMenu.Option.QualityCustom"),TEXT("Custom (keep current)"))); QualityPicker->SetSelectedIndex(5); }
    else QualityPicker->SetSelectedIndex(FMath::Clamp(Quality,0,4));
    VSyncCheck=WidgetTree->ConstructWidget<UCheckBox>();
    GamepadTargets.Add(VSyncCheck);
    auto* VSyncLabel=WidgetTree->ConstructWidget<UTextBlock>();
    VSyncLabel->SetText(FText::FromString(LocalizedCopy(TEXT("UI.Menu.VSync"),TEXT("VSync"))));
    VSyncLabel->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),17));
    VSyncLabel->SetColorAndOpacity(FSlateColor(MenuTextColor()));
    VSyncCheck->SetContent(VSyncLabel); VSyncCheck->SetIsChecked(Settings->IsVSyncEnabled());
    Content->AddChildToVerticalBox(VSyncCheck)->SetPadding(FMargin(0,6,0,12));
    AddButton(TEXT("Apply & Save"),EMCMainMenuAction::ApplySettings);
    AddButton(TEXT("Back"),EMCMainMenuAction::Back);
}

void UMCMainMenuWidget::HostTutorialChanged(bool bChecked)
{
    bHostStartWithTutorial=bChecked;
}

void UMCMainMenuWidget::ApplySettings()
{
    UGameUserSettings* Settings=UGameUserSettings::GetGameUserSettings();
    if (!Settings || !ResolutionPicker || !WindowModePicker || !QualityPicker || !VSyncCheck) return;
    const int32 ResolutionIndex=ResolutionPicker->GetSelectedIndex();
    if (!Resolutions.IsValidIndex(ResolutionIndex)) return;
    PreviousResolution=Settings->GetScreenResolution();
    PreviousWindowMode=static_cast<int32>(Settings->GetFullscreenMode());
    const int32 Mode=FMath::Clamp(WindowModePicker->GetSelectedIndex(),0,2);
    Settings->SetScreenResolution(Resolutions[ResolutionIndex]);
    Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(Mode));
    const int32 Quality=QualityPicker->GetSelectedIndex();
    if (Quality>=0 && Quality<=4) Settings->SetOverallScalabilityLevel(Quality);
    Settings->SetVSyncEnabled(VSyncCheck->IsChecked());
    Settings->ApplySettings(false);
    Settings->SaveSettings();
    if (PreviousResolution!=Settings->GetScreenResolution() || PreviousWindowMode!=Mode)
    {
        bVideoConfirmationPending=true;
        VideoConfirmationEndsAt=FPlatformTime::Seconds()+15.;
        ShowPage(EPage::VideoConfirm);
    }
    else SetStatus(LocalizedCopy(TEXT("UI.Menu.SettingsSaved"),TEXT("Настройки применены и сохранены.")));
}

void UMCMainMenuWidget::RevertVideoSettings()
{
    bVideoConfirmationPending=false;
    if (UGameUserSettings* Settings=UGameUserSettings::GetGameUserSettings())
    {
        Settings->SetScreenResolution(PreviousResolution);
        Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(PreviousWindowMode));
        Settings->ApplyResolutionSettings(false);
        Settings->ConfirmVideoMode();
        Settings->SaveSettings();
    }
}

void UMCMainMenuWidget::RefreshLobby()
{
    const auto* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if (!State || !LobbyPlayers) return;
    FString Players=FormatCopy(TEXT("UI.MainMenu.LobbyCounts"),TEXT("Игроков: {Count} / {Maximum}\nЗагрузились: {Loaded} / {Count}\n"),
        {{TEXT("Count"),State->PlayerArray.Num()},{TEXT("Maximum"),State->RunSettings.MaxPlayers},{TEXT("Loaded"),State->LobbyLoadedPlayers}});
    for (const APlayerState* Player:State->PlayerArray)
        if (Player) Players+=FString::Printf(TEXT("• %s\n"),*Player->GetPlayerName());
    LobbyPlayers->SetText(FText::FromString(Players));
    const bool bHost=GetOwningPlayer() && GetOwningPlayer()->HasAuthority();
    if (StartButton) StartButton->SetIsEnabled(bHost && State->bLobbyWaiting && State->LobbyLoadedPlayers>=State->PlayerArray.Num()
        && State->PlayerArray.Num()>0);
    if (!bHost) SetStatus(LocalizedCopy(TEXT("UI.MainMenu.WaitForHost"),TEXT("Ожидаем, пока хост нажмёт Start / Play.")));
}

void UMCMainMenuWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    for(UButton* Button:Buttons) if(Button)
        Button->SetBackgroundColor(Button->HasUserFocus(GetOwningPlayer())?FLinearColor(.16f,.42f,.38f):FLinearColor(.07f,.15f,.19f));
    if (bVideoConfirmationPending && FPlatformTime::Seconds()>=VideoConfirmationEndsAt)
    {
        RevertVideoSettings(); ShowPage(EPage::Settings); SetStatus(LocalizedCopy(TEXT("UI.Menu.VideoRestored"),TEXT("Предыдущий режим экрана восстановлен.")));
    }
    RefreshAccumulator+=DeltaSeconds;
    if (RefreshAccumulator<.2f) return;
    RefreshAccumulator=0;
    auto* Steam=MCMainMenuPrivate::Sessions(this);
    if (Steam)
    {
        for (UButton* Button:OnlineButtons) if (Button) Button->SetIsEnabled(!Steam->IsBusy());
        if ((Page==EPage::Host || Page==EPage::Join) && Steam->GetStatus()!=LastSessionStatus)
        {
            LastSessionStatus=Steam->GetStatus();
            SetStatus(LastSessionStatus);
        }
        if (RoomPicker && LastSearchRevision!=Steam->GetSearchRevision())
        {
            LastSearchRevision=Steam->GetSearchRevision(); RoomPicker->ClearOptions();
            for (const FString& Room:Steam->GetRoomLabels()) RoomPicker->AddOption(Room);
            if (RoomPicker->GetOptionCount()>0) RoomPicker->SetSelectedIndex(0);
        }
    }
    if (Page==EPage::Lobby) RefreshLobby();
    if (bVideoConfirmationPending)
        SetStatus(FormatCopy(TEXT("UI.Menu.VideoCountdown"),TEXT("Автоматический возврат: {Seconds} сек."),{{TEXT("Seconds"),FMath::CeilToInt(VideoConfirmationEndsAt-FPlatformTime::Seconds())}}));
}

FReply UMCMainMenuWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    const FKey Key=Event.GetKey();
    if((Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_DPad_Down) && MoveGamepadFocus(Key==EKeys::Gamepad_DPad_Up?-1:1))
        return FReply::Handled();
    if(Key==EKeys::Gamepad_FaceButton_Bottom && !Event.IsRepeat()) {
        for(UButton* Button:Buttons) if(Button && Button->GetIsEnabled() && Button->HasUserFocus(GetOwningPlayer())) {
            Button->OnClicked.Broadcast(); return FReply::Handled();
        }
    }
    if(Event.GetKey()==EKeys::Gamepad_Special_Right && bPauseMenu && !Event.IsRepeat()) {
        HandleAction(EMCMainMenuAction::Back); return FReply::Handled();
    }
    if (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right)
    {
        RequestBack();
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

FReply UMCMainMenuWidget::NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event)
{
    if(Event.GetKey()==EKeys::Gamepad_LeftY && FMath::Abs(Event.GetAnalogValue())>.55f) {
        if(FPlatformTime::Seconds()<NextStickNavigationAt) return FReply::Handled();
        if(FPlatformTime::Seconds()>=NextStickNavigationAt && MoveGamepadFocus(Event.GetAnalogValue()>0?-1:1)) {
            NextStickNavigationAt=FPlatformTime::Seconds()+.22;
            return FReply::Handled();
        }
    }
    return Super::NativeOnAnalogValueChanged(Geometry,Event);
}

void UMCMainMenuWidget::HandleAction(EMCMainMenuAction Action)
{
    if (bEmbeddedCloseRequested) return;
    auto* Steam=MCMainMenuPrivate::Sessions(this);
    auto* PC=GetOwningPlayer();
    const bool bLaunchAction=Action==EMCMainMenuAction::Tutorial || Action==EMCMainMenuAction::CreateLobby
        || Action==EMCMainMenuAction::JoinLobby || Action==EMCMainMenuAction::Host
        || Action==EMCMainMenuAction::FindRooms || Action==EMCMainMenuAction::JoinRoom
        || Action==EMCMainMenuAction::JoinAddress || Action==EMCMainMenuAction::StartLobby;
    if (bLaunchAction && !CanLaunchEmbeddedDay())
    {
        SetStatus(LocalizedCopy(TEXT("UI.MainMenu.DayNotConnected"),TEXT("Игровой контент этого дня ещё не подключён.")));
        return;
    }
    switch (Action)
    {
    case EMCMainMenuAction::Tutorial:
        if (Steam && Steam->IsBusy()) { SetStatus(LocalizedCopy(TEXT("UI.MainMenu.ConnectionBusy"),TEXT("Дождись завершения подключения."))); break; }
        UGameplayStatics::OpenLevel(this,MCMainMenuPrivate::TutorialMap,true,TEXT("MCTutorial=1"));
        break;
    case EMCMainMenuAction::CreateLobby: ShowPage(EPage::Host); break;
    case EMCMainMenuAction::JoinLobby: ShowPage(EPage::Join); break;
    case EMCMainMenuAction::Settings: ShowPage(EPage::Settings); break;
    case EMCMainMenuAction::Credits: ShowPage(EPage::Credits); break;
    case EMCMainMenuAction::Exit: ShowPage(EPage::Exit); break;
    case EMCMainMenuAction::Back:
        if (bVideoConfirmationPending) { RevertVideoSettings(); ShowPage(EPage::Settings); }
        else if (EmbeddedRoot!=EEmbeddedRoot::None)
        {
            if (EmbeddedRoot==EEmbeddedRoot::Connection && Steam && Steam->IsBusy())
            {
                SetStatus(LocalizedCopy(TEXT("UI.MainMenu.ConnectionBusy"),TEXT("Дождись завершения подключения.")));
                break;
            }
            const EPage RootPage=EmbeddedRoot==EEmbeddedRoot::Connection?EPage::Connection:EPage::Settings;
            if (Page!=RootPage) ShowPage(RootPage);
            else
            {
                bEmbeddedCloseRequested=true;
                OnBackRequested.Broadcast();
            }
        }
        else if (Page==EPage::Main) ShowPage(EPage::Exit);
        else if (Page==EPage::Lobby) ShowPage(EPage::Leave);
        else if (Page==EPage::Pause) HandleAction(EMCMainMenuAction::Resume);
        else ShowPage(bLobby?EPage::Lobby:bPauseMenu?EPage::Pause:EPage::Main);
        break;
    case EMCMainMenuAction::Host:
        if (Steam && Steam->IsBusy()) break;
        if (Steam && Steam->CanUseSteam()) Steam->HostRoom(true,bHostStartWithTutorial);
        else UGameplayStatics::OpenLevel(this,bHostStartWithTutorial?MCMainMenuPrivate::TutorialMap:MCMainMenuPrivate::MouthMap,
            true,bHostStartWithTutorial?TEXT("listen?MCLobby=1?MCTutorial=1"):TEXT("listen?MCLobby=1"));
        break;
    case EMCMainMenuAction::FindRooms: if (Steam) Steam->FindRooms(); break;
    case EMCMainMenuAction::JoinRoom:
        if (Steam && RoomPicker && RoomPicker->GetSelectedIndex()!=INDEX_NONE) Steam->JoinRoom(RoomPicker->GetSelectedIndex());
        else SetStatus(LocalizedCopy(TEXT("UI.Menu.ChooseRoom"),TEXT("Сначала найди и выбери комнату.")));
        break;
    case EMCMainMenuAction::JoinAddress:
        if (PC && AddressBox && (!Steam || !Steam->IsBusy()))
        {
            FString Address;
            if (NormalizeJoinAddress(AddressBox->GetText().ToString(),Address)) PC->ClientTravel(Address,TRAVEL_Absolute);
            else SetStatus(LocalizedCopy(TEXT("UI.Menu.InvalidAddress"),TEXT("Введи IP или имя компьютера, при необходимости с портом. Например: 192.168.1.10:7777.")));
        }
        break;
    case EMCMainMenuAction::ApplySettings: ApplySettings(); break;
    case EMCMainMenuAction::KeepSettings:
        bVideoConfirmationPending=false;
        if (UGameUserSettings* Settings=UGameUserSettings::GetGameUserSettings()) { Settings->ConfirmVideoMode(); Settings->SaveSettings(); }
        ShowPage(EPage::Settings); SetStatus(LocalizedCopy(TEXT("UI.MainMenu.SettingsSavedShort"),TEXT("Настройки сохранены.")));
        break;
    case EMCMainMenuAction::RevertSettings:
        RevertVideoSettings(); ShowPage(EPage::Settings); SetStatus(LocalizedCopy(TEXT("UI.Menu.VideoRestored"),TEXT("Предыдущий режим экрана восстановлен.")));
        break;
    case EMCMainMenuAction::StartLobby:
        if (auto* GamePC=Cast<AMCPlayerController>(PC)) GamePC->ServerStartLobby();
        break;
    case EMCMainMenuAction::Invite: if (Steam) Steam->InviteFriends(); break;
    case EMCMainMenuAction::LeaveLobby: ShowPage(EPage::Leave); break;
    case EMCMainMenuAction::ConfirmLeave:
        if (auto* GamePC=Cast<AMCPlayerController>(PC)) GamePC->ReturnToMainMenu();
        else if (Steam) Steam->LeaveRoom();
        break;
    case EMCMainMenuAction::ConfirmExit:
        UKismetSystemLibrary::QuitGame(this,PC,EQuitPreference::Quit,false);
        break;
    case EMCMainMenuAction::Resume:
        if (auto* GamePC=Cast<AMCPlayerController>(PC)) GamePC->TogglePauseMenu();
        break;
    case EMCMainMenuAction::Emotes:
        if(auto* GamePC=Cast<AMCPlayerController>(PC)) { GamePC->TogglePauseMenu(); GamePC->ToggleEmotes(); }
        break;
    }
}
