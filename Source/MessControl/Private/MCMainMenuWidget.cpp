#include "MCMainMenuWidget.h"

#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCSteamSessionSubsystem.h"
#include "Blueprint/WidgetTree.h"
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
    const FName MouthMap(TEXT("/Game/Maps/L_Mouth"));
    const FLinearColor TextColor(.94f,.96f,.97f);
    const FLinearColor Accent(.26f,.78f,.65f);

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

void UMCMainMenuWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);

    auto* Backdrop=WidgetTree->ConstructWidget<UBorder>();
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
    Scale->SetContent(Size);
    auto* Main=WidgetTree->ConstructWidget<UVerticalBox>(); Size->SetContent(Main);
    auto* Title=WidgetTree->ConstructWidget<UTextBlock>();
    Title->SetText(FText::FromString(TEXT("MESS CONTROL")));
    Title->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),36));
    Title->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::Accent));
    Main->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,0,0,20));

    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
    Main->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Content=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Content);
    StatusText=WidgetTree->ConstructWidget<UTextBlock>();
    StatusText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));
    StatusText->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::Accent));
    StatusText->SetAutoWrapText(true);
    Main->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0,14,0,0));
    auto* Hint=WidgetTree->ConstructWidget<UTextBlock>();
    Hint->SetText(FText::FromString(TEXT("Мышь / ↑↓ / D-pad · Enter / A — выбрать · Esc / B — назад")));
    Hint->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12));
    Hint->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::TextColor));
    Hint->SetAutoWrapText(true);
    Main->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0,10,0,0));
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
    Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size));
    Label->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::TextColor));
    Label->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,4,0,8));
    return Label;
}

UMCMainMenuActionButton* UMCMainMenuWidget::AddButton(const FString& Text, EMCMainMenuAction Action)
{
    auto* Button=WidgetTree->ConstructWidget<UMCMainMenuActionButton>();
    Button->Configure(this,Action);
    Button->SetBackgroundColor(FLinearColor(.07f,.15f,.19f));
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Text));
    Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),19));
    Label->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::TextColor));
    Label->SetJustification(ETextJustify::Center);
    auto* ButtonPadding=WidgetTree->ConstructWidget<UBorder>();
    ButtonPadding->SetBrushColor(FLinearColor::Transparent); ButtonPadding->SetPadding(FMargin(14,9));
    ButtonPadding->SetContent(Label); Button->SetContent(ButtonPadding);
    Content->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,4));
    Buttons.Add(Button);
    return Button;
}

UComboBoxString* UMCMainMenuWidget::AddPicker(const FString& Label)
{
    AddText(Label,15);
    auto* Picker=WidgetTree->ConstructWidget<UComboBoxString>();
    Content->AddChildToVerticalBox(Picker)->SetPadding(FMargin(0,0,0,8));
    return Picker;
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
    FocusFirst();
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
    if (StatusText) StatusText->SetText(FText::FromString(Text));
}

void UMCMainMenuWidget::ShowLobby()
{
    bLobby=true;
    bPauseMenu=false;
    if (Content) ShowPage(EPage::Lobby);
}

void UMCMainMenuWidget::ShowPause()
{
    bPauseMenu=true;
    bLobby=false;
    if (Content) ShowPage(EPage::Pause);
}

void UMCMainMenuWidget::ShowPage(EPage NewPage)
{
    Page=NewPage;
    Content->ClearChildren(); Buttons.Reset(); OnlineButtons.Reset();
    RoomPicker=nullptr; AddressBox=nullptr; ResolutionPicker=nullptr; WindowModePicker=nullptr;
    QualityPicker=nullptr; VSyncCheck=nullptr; LobbyPlayers=nullptr; StartButton=nullptr;
    LastSearchRevision=INDEX_NONE;
    SetStatus(TEXT(""));
    auto* Steam=MCMainMenuPrivate::Sessions(this);
    const bool bSteam=Steam && Steam->CanUseSteam();
    LastSessionStatus=Steam?Steam->GetStatus():FString();

    switch (Page)
    {
    case EPage::Main:
        AddText(TEXT("Четыре маленьких зуба. Один рот. Семь дней хаоса."),16);
        AddButton(TEXT("Tutorial"),EMCMainMenuAction::Tutorial);
        AddButton(TEXT("Create Lobby"),EMCMainMenuAction::CreateLobby);
        AddButton(TEXT("Join Lobby"),EMCMainMenuAction::JoinLobby);
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Credits"),EMCMainMenuAction::Credits);
        AddButton(TEXT("Exit"),EMCMainMenuAction::Exit);
        AddText(TEXT("Tutorial — отдельная учебная смена. Create Lobby — обычная игра с друзьями."),14);
        break;
    case EPage::Host:
        AddText(TEXT("CREATE LOBBY"),24);
        AddText(bSteam?FString::Printf(TEXT("Steam · %s\nДо четырёх игроков. После загрузки комнаты хост запускает смену кнопкой Start."),*Steam->GetPlayerName()):
            TEXT("Локальная комната по IP · UDP 7777\nДрузья подключаются через Join Lobby. Хост запускает смену кнопкой Start."));
        OnlineButtons.Add(AddButton(TEXT("Create"),EMCMainMenuAction::Host));
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Join:
        AddText(TEXT("JOIN LOBBY"),24);
        if (bSteam)
        {
            OnlineButtons.Add(AddButton(TEXT("Find Steam Lobbies"),EMCMainMenuAction::FindRooms));
            RoomPicker=AddPicker(TEXT("Комната друга"));
            OnlineButtons.Add(AddButton(TEXT("Join Selected Lobby"),EMCMainMenuAction::JoinRoom));
        }
        AddText(bSteam?TEXT("Для локальных тестов можно подключиться по IP."):TEXT("В редакторе Steam отключён. Введи IP хоста и при необходимости порт: 192.168.1.10:7777."),14);
        AddressBox=WidgetTree->ConstructWidget<UEditableTextBox>();
        AddressBox->SetText(FText::FromString(TEXT("127.0.0.1:7777")));
        AddressBox->SetHintText(FText::FromString(TEXT("Адрес хоста")));
        Content->AddChildToVerticalBox(AddressBox)->SetPadding(FMargin(0,8));
        OnlineButtons.Add(AddButton(TEXT("Join by IP"),EMCMainMenuAction::JoinAddress));
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Settings:
        BuildSettings();
        break;
    case EPage::Credits:
        AddText(TEXT("CREDITS"),24);
        AddText(TEXT("Mess Control\nКооперативная игра про маленькие зубы и большой беспорядок во рту."));
        AddText(TEXT("Движок: Unreal Engine\nUnreal Engine — товарный знак Epic Games, Inc.\nИмена команды и авторов материалов будут добавлены после утверждения титров."),15);
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Exit:
        AddText(TEXT("Выйти из игры?"),24);
        AddButton(TEXT("Cancel"),EMCMainMenuAction::Back);
        AddButton(TEXT("Exit Game"),EMCMainMenuAction::ConfirmExit);
        break;
    case EPage::Lobby:
        AddText(TEXT("LOBBY"),24);
        LobbyPlayers=AddText(TEXT("Загружаем участников…"));
        AddText(TEXT("Хост запускает обычную смену, когда команда собрана."),15);
        StartButton=AddButton(TEXT("Start / Play"),EMCMainMenuAction::StartLobby);
        if (bSteam) OnlineButtons.Add(AddButton(TEXT("Invite Steam Friends"),EMCMainMenuAction::Invite));
        else AddText(TEXT("Друзья: Join Lobby → IP этого компьютера:7777."),14);
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Leave Lobby"),EMCMainMenuAction::LeaveLobby);
        RefreshLobby();
        break;
    case EPage::Pause:
        AddText(TEXT("MENU"),24);
        AddButton(TEXT("Resume"),EMCMainMenuAction::Resume);
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Credits"),EMCMainMenuAction::Credits);
        AddButton(TEXT("Return to Main Menu"),EMCMainMenuAction::LeaveLobby);
        AddButton(TEXT("Exit"),EMCMainMenuAction::Exit);
        AddText(TEXT("В сетевой игре смена продолжается, пока это меню открыто."),14);
        break;
    case EPage::Leave:
        AddText(TEXT("Выйти из комнаты?"),24);
        AddText(GetOwningPlayer() && GetOwningPlayer()->HasAuthority()?
            TEXT("Ты — хост. При выходе соединение остальных игроков с комнатой закроется."):
            TEXT("Ты вернёшься в главное меню."),16);
        AddButton(TEXT("Cancel"),EMCMainMenuAction::Back);
        AddButton(TEXT("Leave to Main Menu"),EMCMainMenuAction::ConfirmLeave);
        break;
    case EPage::VideoConfirm:
        AddText(TEXT("Сохранить режим экрана?"),24);
        AddText(TEXT("Если изображение пропало, предыдущий режим вернётся автоматически через 15 секунд."));
        AddButton(TEXT("Keep"),EMCMainMenuAction::KeepSettings);
        AddButton(TEXT("Revert"),EMCMainMenuAction::RevertSettings);
        break;
    }
    if (bSteam && (Page==EPage::Host || Page==EPage::Join)) SetStatus(LastSessionStatus);
    else if (Page==EPage::Main && Steam && Steam->HadFailure()) SetStatus(LastSessionStatus);
    FocusFirst();
}

void UMCMainMenuWidget::BuildSettings()
{
    AddText(TEXT("SETTINGS"),24);
    UGameUserSettings* Settings=UGameUserSettings::GetGameUserSettings();
    if (!Settings) { AddText(TEXT("Настройки экрана недоступны.")); AddButton(TEXT("Back"),EMCMainMenuAction::Back); return; }
    ResolutionPicker=AddPicker(TEXT("Разрешение"));
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
    WindowModePicker=AddPicker(TEXT("Режим экрана"));
    WindowModePicker->AddOption(TEXT("Fullscreen"));
    WindowModePicker->AddOption(TEXT("Borderless"));
    WindowModePicker->AddOption(TEXT("Windowed"));
    WindowModePicker->SetSelectedIndex(static_cast<int32>(Settings->GetFullscreenMode()));
    QualityPicker=AddPicker(TEXT("Качество графики"));
    for (const TCHAR* Label:{TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")}) QualityPicker->AddOption(Label);
    const int32 Quality=Settings->GetOverallScalabilityLevel();
    if (Quality<0) { QualityPicker->AddOption(TEXT("Custom (keep current)")); QualityPicker->SetSelectedIndex(5); }
    else QualityPicker->SetSelectedIndex(FMath::Clamp(Quality,0,4));
    VSyncCheck=WidgetTree->ConstructWidget<UCheckBox>();
    auto* VSyncLabel=WidgetTree->ConstructWidget<UTextBlock>();
    VSyncLabel->SetText(FText::FromString(TEXT("VSync")));
    VSyncLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),17));
    VSyncLabel->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::TextColor));
    VSyncCheck->SetContent(VSyncLabel); VSyncCheck->SetIsChecked(Settings->IsVSyncEnabled());
    Content->AddChildToVerticalBox(VSyncCheck)->SetPadding(FMargin(0,6,0,12));
    AddButton(TEXT("Apply & Save"),EMCMainMenuAction::ApplySettings);
    AddButton(TEXT("Back"),EMCMainMenuAction::Back);
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
    else SetStatus(TEXT("Настройки применены и сохранены."));
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
    FString Players=FString::Printf(TEXT("Игроков: %d / %d\nЗагрузились: %d / %d\n"),State->PlayerArray.Num(),State->RunSettings.MaxPlayers,
        State->LobbyLoadedPlayers,State->PlayerArray.Num());
    for (const APlayerState* Player:State->PlayerArray)
        if (Player) Players+=FString::Printf(TEXT("• %s\n"),*Player->GetPlayerName());
    LobbyPlayers->SetText(FText::FromString(Players));
    const bool bHost=GetOwningPlayer() && GetOwningPlayer()->HasAuthority();
    if (StartButton) StartButton->SetIsEnabled(bHost && State->bLobbyWaiting && State->LobbyLoadedPlayers>=State->PlayerArray.Num()
        && State->PlayerArray.Num()>0);
    if (!bHost) SetStatus(TEXT("Ожидаем, пока хост нажмёт Start / Play."));
}

void UMCMainMenuWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if (bVideoConfirmationPending && FPlatformTime::Seconds()>=VideoConfirmationEndsAt)
    {
        RevertVideoSettings(); ShowPage(EPage::Settings); SetStatus(TEXT("Предыдущий режим экрана восстановлен."));
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
        SetStatus(FString::Printf(TEXT("Автоматический возврат: %d сек."),FMath::CeilToInt(VideoConfirmationEndsAt-FPlatformTime::Seconds())));
}

FReply UMCMainMenuWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right)
    {
        HandleAction(EMCMainMenuAction::Back);
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

void UMCMainMenuWidget::HandleAction(EMCMainMenuAction Action)
{
    auto* Steam=MCMainMenuPrivate::Sessions(this);
    auto* PC=GetOwningPlayer();
    switch (Action)
    {
    case EMCMainMenuAction::Tutorial:
        if (Steam && Steam->IsBusy()) { SetStatus(TEXT("Дождись завершения подключения.")); break; }
        UGameplayStatics::OpenLevel(this,MCMainMenuPrivate::MouthMap,true,TEXT("MCTutorial=1"));
        break;
    case EMCMainMenuAction::CreateLobby: ShowPage(EPage::Host); break;
    case EMCMainMenuAction::JoinLobby: ShowPage(EPage::Join); break;
    case EMCMainMenuAction::Settings: ShowPage(EPage::Settings); break;
    case EMCMainMenuAction::Credits: ShowPage(EPage::Credits); break;
    case EMCMainMenuAction::Exit: ShowPage(EPage::Exit); break;
    case EMCMainMenuAction::Back:
        if (bVideoConfirmationPending) { RevertVideoSettings(); ShowPage(EPage::Settings); }
        else if (Page==EPage::Main) ShowPage(EPage::Exit);
        else if (Page==EPage::Lobby) ShowPage(EPage::Leave);
        else if (Page==EPage::Pause) HandleAction(EMCMainMenuAction::Resume);
        else ShowPage(bLobby?EPage::Lobby:bPauseMenu?EPage::Pause:EPage::Main);
        break;
    case EMCMainMenuAction::Host:
        if (Steam && Steam->IsBusy()) break;
        if (Steam && Steam->CanUseSteam()) Steam->HostRoom(true);
        else UGameplayStatics::OpenLevel(this,MCMainMenuPrivate::MouthMap,true,TEXT("listen?MCLobby=1"));
        break;
    case EMCMainMenuAction::FindRooms: if (Steam) Steam->FindRooms(); break;
    case EMCMainMenuAction::JoinRoom:
        if (Steam && RoomPicker && RoomPicker->GetSelectedIndex()!=INDEX_NONE) Steam->JoinRoom(RoomPicker->GetSelectedIndex());
        else SetStatus(TEXT("Сначала найди и выбери комнату."));
        break;
    case EMCMainMenuAction::JoinAddress:
        if (PC && AddressBox && (!Steam || !Steam->IsBusy()))
        {
            FString Address;
            if (NormalizeJoinAddress(AddressBox->GetText().ToString(),Address)) PC->ClientTravel(Address,TRAVEL_Absolute);
            else SetStatus(TEXT("Введи IP или имя компьютера, при необходимости с портом. Например: 192.168.1.10:7777."));
        }
        break;
    case EMCMainMenuAction::ApplySettings: ApplySettings(); break;
    case EMCMainMenuAction::KeepSettings:
        bVideoConfirmationPending=false;
        if (UGameUserSettings* Settings=UGameUserSettings::GetGameUserSettings()) { Settings->ConfirmVideoMode(); Settings->SaveSettings(); }
        ShowPage(EPage::Settings); SetStatus(TEXT("Настройки сохранены."));
        break;
    case EMCMainMenuAction::RevertSettings:
        RevertVideoSettings(); ShowPage(EPage::Settings); SetStatus(TEXT("Предыдущий режим экрана восстановлен."));
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
    }
}
