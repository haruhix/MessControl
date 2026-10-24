#include "MCMainMenuWidget.h"

#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCPlayerController.h"
#include "MCSteamSessionSubsystem.h"
#include "MCUIDataLibrary.h"
#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "MCGameUserSettings.h"
#include "MCVoiceSettingsWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"

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
    constexpr float FrameRates[]={0,30,60,90,120,144,165,240};
    const TCHAR* QualityControlNames[]={TEXT("TexturePicker"),TEXT("ShadowPicker"),TEXT("EffectsPicker"),TEXT("PostProcessPicker"),TEXT("AAPicker"),TEXT("ViewDistancePicker")};
    const TCHAR* SliderNames[]={TEXT("RenderScaleSlider"),TEXT("MasterSlider"),TEXT("MusicSlider"),TEXT("AmbienceSlider"),TEXT("SFXSlider"),TEXT("MouseSlider"),TEXT("GamepadSlider")};
    TArray<FString> LanguageCodes() {return {TEXT("ru"),TEXT("en"),TEXT("de"),TEXT("fr"),TEXT("es"),TEXT("pt-BR"),TEXT("zh-Hans"),TEXT("zh-Hant"),TEXT("ja"),TEXT("ko")};}
    TArray<FString> LanguageNames() {return {TEXT("Русский"),TEXT("English"),TEXT("Deutsch"),TEXT("Français"),TEXT("Español"),TEXT("Português (Brasil)"),TEXT("简体中文"),TEXT("繁體中文"),TEXT("日本語"),TEXT("한국어")};}

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

void UMCMainMenuPicker::SetPresentationOptions(const TArray<FString>& InOptions,int32 SelectedIndex)
{
    // DefaultOptions is private in stock UMG. It is the serialized source used
    // by PostLoad; AddOption alone only fills the non-reflected runtime array.
    if(auto* Property=FindFProperty<FArrayProperty>(UComboBoxString::StaticClass(),TEXT("DefaultOptions")))
        *Property->ContainerPtrToValuePtr<TArray<FString>>(this)=InOptions;
    ClearOptions();for(const auto& Option:InOptions) AddOption(Option);
    if(!InOptions.IsEmpty()) SetSelectedIndex(FMath::Clamp(SelectedIndex,0,InOptions.Num()-1));
}

void UMCMainMenuPicker::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
    Super::PostDuplicate(DuplicateMode);
    RestorePresentationOptions();
}

void UMCMainMenuPicker::PostLoad()
{
    Super::PostLoad();
    // StaticDuplicateObjectEx routes PostDuplicate and then PostLoad. Stock
    // ComboBoxString::PostLoad appends every default again, so normalize after
    // that callback too. This also handles a disk load after PostInitProperties.
    RestorePresentationOptions();
}

void UMCMainMenuPicker::RestorePresentationOptions()
{
    auto* Defaults=FindFProperty<FArrayProperty>(UComboBoxString::StaticClass(),TEXT("DefaultOptions"));
    auto* Selection=FindFProperty<FStrProperty>(UComboBoxString::StaticClass(),TEXT("SelectedOption"));
    if(!Defaults || !Selection) return;
    const auto SavedOptions=*Defaults->ContainerPtrToValuePtr<TArray<FString>>(this);
    const FString Selected=Selection->GetPropertyValue_InContainer(this);
    ClearOptions();for(const FString& Option:SavedOptions) AddOption(Option);
    // An intentionally empty/removed Designer selection stays unselected.
    SetSelectedOption(Selected);
}

FString UMCMainMenuWidget::LocalizedCopy(FName Key,const FString& Fallback) const
{
    const FString Copy=Localized(Key,FText::AsCultureInvariant(Fallback)).ToString();
    LocalizationKeysByCopy.Add(Copy,Key);
    return Copy;
}
FString UMCMainMenuWidget::FormatCopy(FName Key,const FString& Fallback,const FFormatNamedArguments& Arguments) const
{
    return UMCUIDataLibrary::FormatTextForCulture(UMCUIDataLibrary::ResolveText(
        GetResolvedTranslationTable(),Key,FText::AsCultureInvariant(Fallback)),Arguments).ToString();
}

void UMCMainMenuWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    BuildPresentationTree();
    if (!IsDesignTime()) ShowPage(EPage::Main);
}

FName UMCMainMenuWidget::PageWidgetName(const TCHAR* Suffix) const
{
    static const TCHAR* PageNames[]={TEXT("Main"),TEXT("Host"),TEXT("Join"),TEXT("Settings"),TEXT("Credits"),
        TEXT("Exit"),TEXT("Lobby"),TEXT("Leave"),TEXT("VideoConfirm"),TEXT("Pause"),TEXT("Connection")};
    return FName(*FString::Printf(TEXT("Menu%s_%s"),PageNames[FMath::Clamp(int32(Page),0,UE_ARRAY_COUNT(PageNames)-1)],Suffix));
}

void UMCMainMenuWidget::CacheRootControls()
{
    if (!WidgetTree) return;
    Backdrop=Cast<UBorder>(WidgetTree->FindWidget(TEXT("MenuBackdrop")));
    Card=Cast<UBorder>(WidgetTree->FindWidget(TEXT("MenuCard")));
    TitleText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("MenuTitle")));
    StatusText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("MenuStatus")));
    HintText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("MenuNavigationHint")));
    Pages=Cast<UWidgetSwitcher>(WidgetTree->FindWidget(TEXT("MenuPages")));
    CollapseRetiredMainMenuCopy();
}

void UMCMainMenuWidget::CollapseRetiredMainMenuCopy()
{
    if (!WidgetTree) return;
    const FName TaglineKey(TEXT("UI.MainMenu.Tagline"));
    const FName NavigationKey(TEXT("UI.MainMenu.NavigationHint"));
    WidgetTree->ForEachWidget([this,TaglineKey,NavigationKey](UWidget* Widget)
    {
        if(auto* Button=Cast<UMCMainMenuActionButton>(Widget))
        {
            if(Button->Action==EMCMainMenuAction::Tutorial)
            {
                Button->SetVisibility(ESlateVisibility::Collapsed);
                if(auto* ButtonSlot=Cast<UVerticalBoxSlot>(Button->Slot)) ButtonSlot->SetPadding(FMargin(0));
            }
            else if(Button->Action==EMCMainMenuAction::Credits) Button->SetIsEnabled(false);
            return;
        }
        if(Widget->GetFName()==TEXT("MenuHost_TutorialCheck"))
        {
            Widget->SetVisibility(ESlateVisibility::Collapsed);
            Widget->SetIsEnabled(false);
            if(auto* CheckSlot=Cast<UVerticalBoxSlot>(Widget->Slot)) CheckSlot->SetPadding(FMargin(0));
            return;
        }
        auto* Text=Cast<UTextBlock>(Widget);
        if (!Text) return;
        const FName* Binding=TextBindings.Find(Text->GetFName());
        const bool bRetiredBinding=Binding && (*Binding==TaglineKey || *Binding==NavigationKey
            || *Binding==TEXT("UI.MainMenu.TutorialOptionHint"));
        // Keep stable template names, and preserve artist copy explicitly rebound to another key.
        const bool bRetiredUnbound=!Binding && (Text->GetFName()==TEXT("MenuMain_Text1") ||
            Text->GetFName()==TEXT("MenuNavigationHint"));
        if (!bRetiredBinding && !bRetiredUnbound) return;
        Text->SetVisibility(ESlateVisibility::Collapsed);
        if (auto* Slot=Cast<UVerticalBoxSlot>(Text->Slot)) Slot->SetPadding(FMargin(0));
    });
}

void UMCMainMenuWidget::BuildPresentationTree()
{
    if (!WidgetTree) return;
    if (WidgetTree->RootWidget) { CacheRootControls(); return; }
    bNativeLayout=true;
    TGuardValue<bool> Building(bBuildingTemplate,true);
    Backdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("MenuBackdrop"));
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
    Card=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("MenuCard"));
    Card->SetBrushColor(FLinearColor::Transparent);
    Card->SetPadding(FMargin(0));
    Scale->SetContent(Card);
    Card->SetContent(Size);
    auto* Main=WidgetTree->ConstructWidget<UVerticalBox>(); Size->SetContent(Main);
    TitleText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("MenuTitle"));
    TitleText->SetText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.BrandTitle"),TEXT("MESS CONTROL"))));
    TitleText->SetFont(MCMainMenuPrivate::Font(TEXT("Bold"),36));
    TitleText->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::Accent));
    Main->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0,0,0,20));

    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
    Main->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Pages=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("MenuPages"));
    Scroll->AddChild(Pages);
    StatusText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("MenuStatus"));
    StatusText->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),14));
    StatusText->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::Accent));
    StatusText->SetAutoWrapText(true);
    Main->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0,14,0,0));
    HintText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("MenuNavigationHint"));
    HintText->SetText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.NavigationHint"),TEXT("Мышь / ↑↓ / D-pad · Enter / A — выбрать · Esc / B — назад"))));
    HintText->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),12));
    HintText->SetColorAndOpacity(FSlateColor(MCMainMenuPrivate::TextColor));
    HintText->SetAutoWrapText(true);
    Main->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0,10,0,0));

    TextBindings.Add(TEXT("MenuNavigationHint"),TEXT("UI.MainMenu.NavigationHint"));
    const EPage PreviousPage=Page;
    for (int32 Index=0;Index<=int32(EPage::Connection);++Index)
    {
        Page=EPage(Index);
        TemplateTextIndex=0;
        Content=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),PageWidgetName(TEXT("Page")));
        Pages->AddChild(Content);
        BuildPageContents();
    }
    Page=PreviousPage;
    Pages->SetActiveWidgetIndex(int32(Page));
    Content=Cast<UVerticalBox>(Pages->GetActiveWidget());
    Buttons.Reset();OnlineButtons.Reset();GamepadTargets.Reset();
    CollapseRetiredMainMenuCopy();
}

UWidget* UMCMainMenuWidget::GetActiveMenuPageRoot() const
{
    return Pages?Pages->GetActiveWidget():nullptr;
}

void UMCMainMenuWidget::ApplyEditorPreview(float Time)
{
    PreviewTime=FMath::Max(0.f,Time);
    BuildPresentationTree();
    if (Pages)
    {
        const int64 StatePage=StaticEnum<EMCMainMenuPage>()->GetValueByNameString(PreviewState.ToString());
        Page=PreviewState.ToString().StartsWith(TEXT("Settings"))?EPage::Settings:
            StatePage>=0 && StatePage<=int64(EPage::Connection)?EPage(StatePage):PreviewPage;
        if (auto* Root=WidgetTree->FindWidget(PageWidgetName(TEXT("Page")))) Pages->SetActiveWidget(Root);
    }
    if(Page==EPage::Settings)
    {
        CacheSettingsControls();
        int32 Tab=PreviewSettingsTab;
        if(PreviewState==TEXT("SettingsAudio")) Tab=1;
        else if(PreviewState==TEXT("SettingsLanguage")) Tab=2;
        else if(PreviewState==TEXT("SettingsControls")) Tab=3;
        else if(PreviewState==TEXT("SettingsVoice")) Tab=4;
        SwitchSettingsTab(Tab);RefreshSettingsValueLabels();
    }
    ApplyTheme();
}

void UMCMainMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (!IsDesignTime()) { ApplyPresentation(); RefreshPageData(false); FocusFirst(); }
}

void UMCMainMenuWidget::OnLocalizationChanged()
{
    Super::OnLocalizationChanged();
    if (!IsDesignTime() && Pages)
    {
        ApplyPresentation();
        // Only localized option copy changes; unsaved selections, resolution and VSync remain intact.
        if (Page==EPage::Settings && WindowModePicker && QualityPicker)
        {
            RefreshSettingsOptionLabels(WindowModePicker->GetSelectedIndex(),QualityPicker->GetSelectedIndex(),QualityPicker->GetOptionCount()>5);
            RefreshSettingsQualityOptions(true);RefreshSettingsValueLabels();
        }
        RefreshPageData(false);
    }
}

void UMCMainMenuWidget::NativeDestruct()
{
    if(VoiceSettings) VoiceSettings->StopInteraction();
    // Never retain an unconfirmed display mode after the menu is removed or the world travels.
    if (bVideoConfirmationPending) RevertVideoSettings();
    Super::NativeDestruct();
}

UTextBlock* UMCMainMenuWidget::AddText(const FString& Text, int32 Size, const TCHAR* Suffix)
{
    const FString GeneratedSuffix=FString::Printf(TEXT("Text%d"),++TemplateTextIndex);
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),PageWidgetName(Suffix?Suffix:*GeneratedSuffix));
    const bool bDynamic=Suffix && (FCString::Strcmp(Suffix,TEXT("ModeBody"))==0 || FCString::Strcmp(Suffix,TEXT("Players"))==0);
    if (!bDynamic) if (const FName* Key=LocalizationKeysByCopy.Find(Text)) TextBindings.Add(Label->GetFName(),*Key);
    Label->SetText(FText::FromString(Text));
    Label->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),Size));
    Label->SetColorAndOpacity(FSlateColor(MenuTextColor()));
    Label->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,4,0,8));
    return Label;
}

UMCMainMenuActionButton* UMCMainMenuWidget::AddButton(const FString& Text, EMCMainMenuAction Action)
{
    const FString Suffix=FString::Printf(TEXT("Action%d"),int32(Action));
    auto* Button=WidgetTree->ConstructWidget<UMCMainMenuActionButton>(UMCMainMenuActionButton::StaticClass(),PageWidgetName(*Suffix));
    Button->Action=Action;
    if (!bBuildingTemplate) Button->Configure(this,Action);
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
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),PageWidgetName(*(Suffix+TEXT("Label"))));
    static const TCHAR* ActionNames[]={TEXT("Tutorial"),TEXT("CreateLobby"),TEXT("JoinLobby"),TEXT("Settings"),TEXT("Credits"),TEXT("Exit"),TEXT("Back"),TEXT("Host"),TEXT("FindRooms"),TEXT("JoinRoom"),TEXT("JoinAddress"),TEXT("ApplySettings"),TEXT("KeepSettings"),TEXT("RevertSettings"),TEXT("StartLobby"),TEXT("Invite"),TEXT("LeaveLobby"),TEXT("ConfirmLeave"),TEXT("ConfirmExit"),TEXT("Resume"),TEXT("Emotes")};
    static_assert(UE_ARRAY_COUNT(ActionNames)==int32(EMCMainMenuAction::Emotes)+1,"Keep action keys aligned with the existing menu actions");
    FName Key(*(FString(TEXT("UI.MainMenu.Action."))+ActionNames[FMath::Clamp(int32(Action),0,int32(UE_ARRAY_COUNT(ActionNames))-1)]));
    if(Action==EMCMainMenuAction::CreateLobby) Key=TEXT("UI.MainMenu.Action.PlayHub");
    if(Action==EMCMainMenuAction::Back && (Page==EPage::Exit || Page==EPage::Leave)) Key=TEXT("UI.MainMenu.Action.Cancel");
    if(Action==EMCMainMenuAction::LeaveLobby && Page==EPage::Pause) Key=TEXT("UI.MainMenu.Action.ReturnToMenu");
    if(Page==EPage::Settings && Action==EMCMainMenuAction::ApplySettings) Key=TEXT("UI.Settings.Apply");
    if(Page==EPage::Settings && Action==EMCMainMenuAction::Back) Key=TEXT("UI.Settings.Back");
    Label->SetText(FText::FromString(LocalizedCopy(Key,Text)));
    TextBindings.Add(Label->GetFName(),Key);
    Label->SetFont(MCMainMenuPrivate::Font(TEXT("Bold"),19));
    Label->SetColorAndOpacity(FSlateColor(bEmbedded && bPrimary?EmbeddedStyle.CardColor:MenuTextColor()));
    Label->SetJustification(ETextJustify::Center);
    if(Page==EPage::Settings)
    {
        BindStyle(Button->GetFName(),EMCUIColorToken::Button,EMCUIStyleTarget::Button);
        BindStyle(Label->GetFName(),EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,19,true);
    }
    auto* ButtonPadding=WidgetTree->ConstructWidget<UBorder>();
    ButtonPadding->SetBrushColor(FLinearColor::Transparent); ButtonPadding->SetPadding(FMargin(14,9));
    ButtonPadding->SetContent(Label); Button->SetContent(ButtonPadding);
    Content->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,4));
    Buttons.Add(Button);
    GamepadTargets.Add(Button);
    return Button;
}

UComboBoxString* UMCMainMenuWidget::AddPicker(const FString& Label, const TCHAR* Suffix)
{
    const FString LabelSuffix=FString(Suffix)+TEXT("Label");
    AddText(Label,15,*LabelSuffix);
    auto* Picker=WidgetTree->ConstructWidget<UMCMainMenuPicker>(UMCMainMenuPicker::StaticClass(),PageWidgetName(Suffix));
    Picker->InitializePresentationFont(MCMainMenuPrivate::Font(TEXT("Regular"),17));
    if (!bBuildingTemplate) Picker->OnGenerateWidgetEvent.BindDynamic(this,&ThisClass::GeneratePickerItem);
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
    const auto* ThemeData=GetResolvedTheme();
    Label->SetColorAndOpacity(FSlateColor(Page==EPage::Settings && ThemeData?ThemeData->Palette.Ink:MenuTextColor()));
    return Label;
}

void UMCMainMenuWidget::FocusFirst()
{
    if (!GetOwningPlayer()) return;
    for (UButton* Button:Buttons)
    {
        if (Button && Button->GetIsEnabled() && Button->IsVisible()) { Button->SetUserFocus(GetOwningPlayer()); return; }
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

void UMCMainMenuWidget::ApplyEmbeddedDesignerStyle(bool bEmbedded)
{
    if (!WidgetTree) return;
    bEmbedded&=bUseEmbeddedPresentationStyle;
    if (bEmbedded && !bEmbeddedStyleApplied)
    {
        if (Backdrop) {DesignerBackdropBrush=Backdrop->Background;DesignerBackdropColor=Backdrop->GetBrushColor();}
        if (Card) {DesignerCardBrush=Card->Background;DesignerCardColor=Card->GetBrushColor();DesignerCardPadding=Card->GetPadding();}
        WidgetTree->ForEachWidget([this](UWidget* Widget)
        {
            if (auto* Button=Cast<UButton>(Widget)) {DesignerButtonStyles.Add(Button,Button->GetStyle());DesignerButtonColors.Add(Button,Button->GetBackgroundColor());}
            if (auto* Text=Cast<UTextBlock>(Widget)) DesignerTextColors.Add(Text,Text->GetColorAndOpacity());
        });
        bEmbeddedStyleApplied=true;
    }
    if (!bEmbedded)
    {
        if (!bEmbeddedStyleApplied) return;
        if (Backdrop) {Backdrop->SetBrush(DesignerBackdropBrush);Backdrop->SetBrushColor(DesignerBackdropColor);}
        if (Card) {Card->SetBrush(DesignerCardBrush);Card->SetBrushColor(DesignerCardColor);Card->SetPadding(DesignerCardPadding);}
        for (const auto& Entry:DesignerButtonStyles) if (IsValid(Entry.Key)) Entry.Key->SetStyle(Entry.Value);
        for (const auto& Entry:DesignerButtonColors) if (IsValid(Entry.Key)) Entry.Key->SetBackgroundColor(Entry.Value);
        for (const auto& Entry:DesignerTextColors) if (IsValid(Entry.Key)) Entry.Key->SetColorAndOpacity(Entry.Value);
        DesignerButtonStyles.Reset();DesignerButtonColors.Reset();DesignerTextColors.Reset();
        bEmbeddedStyleApplied=false;
        return;
    }
    if (Backdrop) {Backdrop->SetBrush(FSlateColorBrush(EmbeddedStyle.bTransparentBackdrop?FLinearColor::Transparent:EmbeddedStyle.CardColor));Backdrop->SetBrushColor(FLinearColor::White);}
    if (Card) {Card->SetBrush(FSlateRoundedBoxBrush(EmbeddedStyle.CardColor,FMath::Clamp(EmbeddedStyle.CornerRadius,0.f,32.f)));Card->SetBrushColor(FLinearColor::White);Card->SetPadding(FMargin(24));}
    for (const auto& Entry:DesignerTextColors) if (IsValid(Entry.Key)) Entry.Key->SetColorAndOpacity(FSlateColor(EmbeddedStyle.TextColor));
    for (const auto& Entry:DesignerButtonStyles)
    {
        auto* Button=Cast<UMCMainMenuActionButton>(Entry.Key);
        if (!Button) continue;
        const bool bPrimary=Button->Action==EMCMainMenuAction::CreateLobby || Button->Action==EMCMainMenuAction::Host
            || Button->Action==EMCMainMenuAction::JoinRoom || Button->Action==EMCMainMenuAction::JoinAddress
            || Button->Action==EMCMainMenuAction::ApplySettings || Button->Action==EMCMainMenuAction::KeepSettings;
        Button->SetStyle(MCMainMenuPrivate::EmbeddedButtonStyle(Entry.Value,EmbeddedStyle,bPrimary));
        Button->SetBackgroundColor(FLinearColor::White);
        if (bPrimary) UWidgetTree::ForWidgetAndChildren(Button,[this](UWidget* Child)
        {if (auto* Text=Cast<UTextBlock>(Child)) Text->SetColorAndOpacity(FSlateColor(EmbeddedStyle.CardColor));});
    }
    if (TitleText) TitleText->SetColorAndOpacity(FSlateColor(EmbeddedStyle.AccentColor));
    if (StatusText) StatusText->SetColorAndOpacity(FSlateColor(EmbeddedStyle.AccentColor));
}

void UMCMainMenuWidget::ApplyPresentation()
{
    CollapseRetiredMainMenuCopy();
    const bool bEmbedded=EmbeddedRoot!=EEmbeddedRoot::None;
    // Settings share the project's theme rather than inheriting the old
    // embedded menu's independent palette. Authored bindings stay opt-in.
    ApplyEmbeddedDesignerStyle(bEmbedded && Page!=EPage::Settings);
    if(Page==EPage::Settings) {ApplyTheme();return;}
    if (Backdrop && bNativeLayout)
    {
        const FLinearColor Color=bEmbedded && EmbeddedStyle.bTransparentBackdrop?FLinearColor::Transparent:
            bEmbedded?EmbeddedStyle.CardColor:FLinearColor(.015f,.025f,.035f,.98f);
        Backdrop->SetBrush(FSlateColorBrush(Color));
        Backdrop->SetBrushColor(FLinearColor::White);
    }
    if (Card && bNativeLayout)
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
        if (bNativeLayout)
        {
            TitleText->SetFont(MCMainMenuPrivate::Font(TEXT("Bold"),bEmbedded?30:36));
            TitleText->SetColorAndOpacity(FSlateColor(MenuAccentColor()));
        }
    }
    if (StatusText && bNativeLayout) StatusText->SetColorAndOpacity(FSlateColor(MenuAccentColor()));
    if (HintText && bNativeLayout) HintText->SetColorAndOpacity(FSlateColor(MenuTextColor()));
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
    UDataTable* Table=GetResolvedTranslationTable();FName Key=NAME_None;
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
    ShowPage(EPage::Lobby);
}

void UMCMainMenuWidget::ShowPause()
{
    LeaveEmbeddedMode();
    bPauseMenu=true;
    bLobby=false;
    ShowPage(EPage::Pause);
}

void UMCMainMenuWidget::BuildPageContents()
{
    switch (Page)
    {
    case EPage::Connection:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.ConnectionHeading"),TEXT("PLAY TOGETHER")),24,TEXT("AvailableHeading"));
        AddText(LocalizedCopy(TEXT("UI.MainMenu.ConnectionBody"),TEXT("Создай комнату для обычной смены или присоединись к друзьям.")),17,TEXT("AvailableBody"));
        OnlineButtons.Add(AddButton(TEXT("Create Lobby"),EMCMainMenuAction::CreateLobby));
        OnlineButtons.Add(AddButton(TEXT("Join Lobby"),EMCMainMenuAction::JoinLobby));
        AddText(LocalizedCopy(TEXT("UI.MainMenu.ComingSoon"),TEXT("COMING SOON")),24,TEXT("UnavailableHeading"));
        AddText(LocalizedCopy(TEXT("UI.MainMenu.DayUnavailableBody"),TEXT("Игровой контент этого дня ещё не подключён. Пока доступна игра в Day 01.")),17,TEXT("UnavailableBody"));
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Main:
        // Retain Text1 identity for saved Designer trees; collapse its copy and slot after construction.
        AddText(LocalizedCopy(TEXT("UI.MainMenu.Tagline"),TEXT("Четыре маленьких зуба. Один рот. Семь дней хаоса.")),16);
        AddButton(TEXT("Tutorial"),EMCMainMenuAction::Tutorial);
        AddButton(TEXT("Play"),EMCMainMenuAction::CreateLobby);
        AddButton(TEXT("Join Lobby"),EMCMainMenuAction::JoinLobby);
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Credits"),EMCMainMenuAction::Credits);
        AddButton(TEXT("Exit"),EMCMainMenuAction::Exit);
        AddText(LocalizedCopy(TEXT("UI.MainMenu.HubFlowHint"),TEXT("Play — сразу в хаб. Пригласите друзей и встаньте вместе в Ready-зону, чтобы начать игру.")),14);
        break;
    case EPage::Host:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.HostHeading"),TEXT("CREATE LOBBY")),24);
        HostTutorialCheck=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),PageWidgetName(TEXT("TutorialCheck")));
        GamepadTargets.Add(HostTutorialCheck);
        {
            auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),PageWidgetName(TEXT("TutorialLabel")));
            TextBindings.Add(Label->GetFName(),TEXT("UI.MainMenu.StartWithTutorial"));
            Label->SetText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.StartWithTutorial"),TEXT("Начать с обучения"))));
            Label->SetFont(MCMainMenuPrivate::Font(TEXT("Regular"),17));
            Label->SetColorAndOpacity(FSlateColor(MenuTextColor()));
            HostTutorialCheck->SetContent(Label);
            HostTutorialCheck->SetIsChecked(bHostStartWithTutorial);

            Content->AddChildToVerticalBox(HostTutorialCheck)->SetPadding(FMargin(0,6,0,12));
        }
        AddText(LocalizedCopy(TEXT("UI.MainMenu.TutorialOptionHint"),TEXT("С обучением: сначала учебная карта, затем обычная игра. Без галочки — сразу обычная карта.")),14);
        AddText(LocalizedCopy(TEXT("UI.MainMenu.HostLocalBody"),TEXT("Локальная комната по IP · UDP 7777\nДрузья подключаются через Join Lobby. Хост запускает смену кнопкой Start.")),17,TEXT("ModeBody"));
        OnlineButtons.Add(AddButton(TEXT("Create"),EMCMainMenuAction::Host));
        AddButton(TEXT("Back"),EMCMainMenuAction::Back);
        break;
    case EPage::Join:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.JoinHeading"),TEXT("JOIN LOBBY")),24);
        {
            OnlineButtons.Add(AddButton(TEXT("Find Steam Lobbies"),EMCMainMenuAction::FindRooms));
            RoomPicker=AddPicker(LocalizedCopy(TEXT("UI.MainMenu.FriendRoom"),TEXT("Комната друга")),TEXT("RoomPicker"));
            OnlineButtons.Add(AddButton(TEXT("Join Selected Lobby"),EMCMainMenuAction::JoinRoom));
        }
        AddText(LocalizedCopy(TEXT("UI.MainMenu.EditorJoinHint"),TEXT("В редакторе Steam отключён. Введи IP хоста и при необходимости порт: 192.168.1.10:7777.")),14,TEXT("ModeBody"));
        AddressBox=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),PageWidgetName(TEXT("AddressBox")));
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
        LobbyPlayers=AddText(LocalizedCopy(TEXT("UI.MainMenu.LoadingPlayers"),TEXT("Загружаем участников…")),17,TEXT("Players"));
        AddText(LocalizedCopy(TEXT("UI.MainMenu.LobbyStartHint"),TEXT("Хост запускает обычную смену, когда команда собрана.")),15);
        StartButton=AddButton(TEXT("Start / Play"),EMCMainMenuAction::StartLobby);
        OnlineButtons.Add(AddButton(TEXT("Invite Steam Friends"),EMCMainMenuAction::Invite));
        AddText(LocalizedCopy(TEXT("UI.MainMenu.LobbyLocalHint"),TEXT("Друзья: Join Lobby → IP этого компьютера:7777.")),14,TEXT("LocalHint"));
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Leave Lobby"),EMCMainMenuAction::LeaveLobby);
        break;
    case EPage::Pause:
        AddText(LocalizedCopy(TEXT("UI.MainMenu.PauseHeading"),TEXT("MENU")),24);
        AddButton(TEXT("Resume"),EMCMainMenuAction::Resume);
        OnlineButtons.Add(AddButton(TEXT("Invite Steam Friends"),EMCMainMenuAction::Invite));
        AddButton(TEXT("Emotes / Сигналы"),EMCMainMenuAction::Emotes);
        AddButton(TEXT("Settings"),EMCMainMenuAction::Settings);
        AddButton(TEXT("Credits"),EMCMainMenuAction::Credits);
        AddButton(TEXT("Return to Main Menu"),EMCMainMenuAction::LeaveLobby);
        AddButton(TEXT("Exit"),EMCMainMenuAction::Exit);
        AddText(LocalizedCopy(TEXT("UI.Menu.NetworkNote"),TEXT("В сетевой игре смена продолжается, пока это меню открыто.")),14);
        break;
    case EPage::Leave:
        AddText(LocalizedCopy(TEXT("UI.Menu.LeaveHeading"),TEXT("Выйти из комнаты?")),24);
        AddText(LocalizedCopy(TEXT("UI.Menu.LeaveClientBody"),TEXT("Ты вернёшься в главное меню.")),16,TEXT("ModeBody"));
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
}

void UMCMainMenuWidget::CachePageControls()
{
    Buttons.Reset(); OnlineButtons.Reset(); GamepadTargets.Reset();
    auto Find=[this](const TCHAR* Suffix) {return WidgetTree?WidgetTree->FindWidget(PageWidgetName(Suffix)):nullptr;};
    RoomPicker=Cast<UComboBoxString>(Find(TEXT("RoomPicker")));
    AddressBox=Cast<UEditableTextBox>(Find(TEXT("AddressBox")));
    ResolutionPicker=Cast<UComboBoxString>(Find(TEXT("ResolutionPicker")));
    WindowModePicker=Cast<UComboBoxString>(Find(TEXT("WindowModePicker")));
    QualityPicker=Cast<UComboBoxString>(Find(TEXT("QualityPicker")));
    VSyncCheck=Cast<UCheckBox>(Find(TEXT("VSyncCheck")));
    HostTutorialCheck=Cast<UCheckBox>(Find(TEXT("TutorialCheck")));
    LobbyPlayers=Cast<UTextBlock>(Find(TEXT("Players")));
    StartButton=nullptr;
    auto* ActiveRoot=GetActiveMenuPageRoot();
    if (!ActiveRoot) return;
    // Earlier saved Pause shells predate Hub invitations. Add only the missing
    // action, leaving every artist-owned child and layout otherwise in place.
    const FString InviteSuffix=FString::Printf(TEXT("Action%d"),int32(EMCMainMenuAction::Invite));
    if(Page==EPage::Pause && Content && !Find(*InviteSuffix))
    {
        AddButton(TEXT("Invite Steam Friends"),EMCMainMenuAction::Invite);
        Buttons.Reset();OnlineButtons.Reset();GamepadTargets.Reset();
    }
    UWidgetTree::ForWidgetAndChildren(ActiveRoot,[this](UWidget* Widget)
    {
        if (auto* Button=Cast<UMCMainMenuActionButton>(Widget))
        {
            Button->Configure(this,Button->Action);
            if(Button->Action==EMCMainMenuAction::CreateLobby)
                UWidgetTree::ForWidgetAndChildren(Button,[this](UWidget* Child) {
                    if(auto* Label=Cast<UTextBlock>(Child)) {
                        TextBindings.Add(Label->GetFName(),TEXT("UI.MainMenu.Action.PlayHub"));
                        Label->SetText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.Action.PlayHub"),TEXT("Play"))));
                    }
                });
            Buttons.Add(Button);GamepadTargets.Add(Button);
            switch(Button->Action)
            {
            case EMCMainMenuAction::CreateLobby: case EMCMainMenuAction::JoinLobby:
                if (Page==EPage::Connection) OnlineButtons.Add(Button); break;
            case EMCMainMenuAction::Host: case EMCMainMenuAction::FindRooms:
            case EMCMainMenuAction::JoinRoom: case EMCMainMenuAction::JoinAddress:
            case EMCMainMenuAction::Invite: OnlineButtons.Add(Button); break;
            case EMCMainMenuAction::StartLobby: StartButton=Button; break;
            default: break;
            }
        }
        else if (auto* Picker=Cast<UComboBoxString>(Widget))
        {
            // Keep authored picker fonts/content; the existing choices remain runtime data.
            GamepadTargets.Add(Picker);
            if (bNativeLayout) Picker->OnGenerateWidgetEvent.BindDynamic(this,&ThisClass::GeneratePickerItem);
        }
        else if (Cast<USlider>(Widget)) GamepadTargets.Add(Widget);
        else if (Cast<UEditableTextBox>(Widget) || Cast<UCheckBox>(Widget)) GamepadTargets.Add(Widget);
    });
    if (HostTutorialCheck)
    {
        HostTutorialCheck->OnCheckStateChanged.AddUniqueDynamic(this,&ThisClass::HostTutorialChanged);
        HostTutorialCheck->SetIsChecked(bHostStartWithTutorial);
    }
    if(Page==EPage::Settings)
    {
        CacheSettingsControls();
        // WidgetSwitcher does not change UObject visibility on inactive pages.
        // Keep controller navigation inside the selected settings section.
        GamepadTargets.RemoveAll([this](const auto& Entry)
        {
            UWidget* Widget=Entry.Get();
            for(auto* Parent=Widget?Widget->GetParent():nullptr;Parent;Parent=Parent->GetParent())
                if(SettingsSections && Parent->GetParent()==SettingsSections.Get() && Parent!=SettingsSections->GetActiveWidget()) return true;
            return false;
        });
        if(VoiceSettings)
        {
            TArray<UWidget*> VoiceControls;VoiceSettings->GetFocusableControls(VoiceControls);
            for(auto* Control:VoiceControls)
            {
                GamepadTargets.Remove(Control);
                if(auto* Button=Cast<UButton>(Control)) Buttons.Remove(Button);
                if(ActiveSettingsTab==4) {GamepadTargets.AddUnique(Control);if(auto* Button=Cast<UButton>(Control)) Buttons.AddUnique(Button);}
            }
        }
    }
}

void UMCMainMenuWidget::ShowPage(EPage NewPage)
{
    if(NewPage==EPage::Credits && !IsDesignTime())
        NewPage=bLobby?EPage::Lobby:bPauseMenu?EPage::Pause:EPage::Main;
    if(VoiceSettings && NewPage!=EPage::Settings) VoiceSettings->StopInteraction();
    Page=NewPage;
    BuildPresentationTree();
    if (!Pages) return;
    auto* PageRoot=WidgetTree->FindWidget(PageWidgetName(TEXT("Page")));
    if (!PageRoot || Pages->GetChildIndex(PageRoot)==INDEX_NONE) return;
    Pages->SetActiveWidget(PageRoot);
    Content=Cast<UVerticalBox>(PageRoot);
    NextStickNavigationAt=0;LastSearchRevision=INDEX_NONE;
    ApplyPresentation();
    CachePageControls();
    SetStatus(TEXT(""));
    RefreshPageData();
    FocusFirst();
}

void UMCMainMenuWidget::RefreshPageData(bool bResetSettings)
{
    auto* Steam=MCMainMenuPrivate::Sessions(this);
    const bool bSteam=Steam && Steam->CanUseSteam();
    LastSessionStatus=Steam?Steam->GetStatus():FString();
    auto Visible=[this](const TCHAR* Suffix,bool bShow)
    {
        if (auto* Widget=WidgetTree->FindWidget(PageWidgetName(Suffix)))
            Widget->SetVisibility(bShow?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    };
    auto Copy=[this](const TCHAR* Suffix,const FString& Text)
    {
        if (auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(PageWidgetName(Suffix)))) Label->SetText(FText::FromString(Text));
    };
    auto ActionVisible=[&Visible](EMCMainMenuAction Action,bool bShow)
    { Visible(*FString::Printf(TEXT("Action%d"),int32(Action)),bShow); };
    switch (Page)
    {
    case EPage::Connection:
        Visible(TEXT("AvailableHeading"),CanLaunchEmbeddedDay());Visible(TEXT("AvailableBody"),CanLaunchEmbeddedDay());
        Visible(TEXT("UnavailableHeading"),!CanLaunchEmbeddedDay());Visible(TEXT("UnavailableBody"),!CanLaunchEmbeddedDay());
        ActionVisible(EMCMainMenuAction::CreateLobby,CanLaunchEmbeddedDay());ActionVisible(EMCMainMenuAction::JoinLobby,CanLaunchEmbeddedDay());
        break;
    case EPage::Host:
        Copy(TEXT("ModeBody"),bSteam?FormatCopy(TEXT("UI.MainMenu.HostSteamBody"),TEXT("Steam · {Host}\nДо четырёх игроков. После загрузки комнаты хост запускает смену кнопкой Start."),{{TEXT("Host"),FText::AsCultureInvariant(Steam->GetPlayerName())}}):
            LocalizedCopy(TEXT("UI.MainMenu.HostLocalBody"),TEXT("Локальная комната по IP · UDP 7777\nДрузья подключаются через Join Lobby. Хост запускает смену кнопкой Start.")));
        break;
    case EPage::Join:
        if (AddressBox) AddressBox->SetHintText(FText::FromString(LocalizedCopy(TEXT("UI.MainMenu.HostAddress"),TEXT("Адрес хоста"))));
        ActionVisible(EMCMainMenuAction::FindRooms,bSteam);ActionVisible(EMCMainMenuAction::JoinRoom,bSteam);
        Visible(TEXT("RoomPicker"),bSteam);Visible(TEXT("RoomPickerLabel"),bSteam);
        Copy(TEXT("ModeBody"),bSteam?LocalizedCopy(TEXT("UI.MainMenu.LocalJoinHint"),TEXT("Для локальных тестов можно подключиться по IP.")):
            LocalizedCopy(TEXT("UI.MainMenu.EditorJoinHint"),TEXT("В редакторе Steam отключён. Введи IP хоста и при необходимости порт: 192.168.1.10:7777.")));
        break;
    case EPage::Settings: if (bResetSettings) RefreshSettingsControls(); break;
    case EPage::Lobby:
        ActionVisible(EMCMainMenuAction::Invite,bSteam);Visible(TEXT("LocalHint"),!bSteam);RefreshLobby();break;
    case EPage::Pause:
        ActionVisible(EMCMainMenuAction::Invite,bSteam && Steam->HasRoom() && GetWorld() && GetWorld()->GetGameState<AMCHubGameState>());break;
    case EPage::Leave:
        Copy(TEXT("ModeBody"),GetOwningPlayer() && GetOwningPlayer()->HasAuthority()?
            LocalizedCopy(TEXT("UI.Menu.LeaveHostBody"),TEXT("Ты — хост. При выходе соединение остальных игроков с комнатой закроется.")):
            LocalizedCopy(TEXT("UI.Menu.LeaveClientBody"),TEXT("Ты вернёшься в главное меню.")));
        break;
    default: break;
    }
    if (bSteam && (Page==EPage::Host || Page==EPage::Join)) SetStatus(LastSessionStatus);
    else if ((Page==EPage::Main || Page==EPage::Connection) && Steam && Steam->HadFailure()) SetStatus(LastSessionStatus);
    if (Steam) for (UButton* Button:OnlineButtons) if (Button) Button->SetIsEnabled(!Steam->IsBusy());
}

void UMCMainMenuWidget::BuildSettings()
{
    auto* PageBody=Content.Get();
    auto* Heading=AddText(LocalizedCopy(TEXT("UI.Settings.Title"),TEXT("Настройки")),24,TEXT("Heading"));
    BindStyle(Heading->GetFName(),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,24,true);
    auto* Tabs=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),PageWidgetName(TEXT("Tabs")));
    PageBody->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0,0,0,10));
    const TCHAR* TabNames[]={TEXT("Graphics"),TEXT("Audio"),TEXT("Language"),TEXT("Controls"),TEXT("Voice")};
    const TCHAR* TabCopy[]={TEXT("Графика"),TEXT("Аудио"),TEXT("Язык"),TEXT("Управление"),TEXT("Голос")};
    for(int32 Index=0;Index<5;++Index)
    {
        const FString Suffix=FString(TEXT("Tab"))+TabNames[Index];
        auto* Tab=WidgetTree->ConstructWidget<UMCMainMenuActionButton>(UMCMainMenuActionButton::StaticClass(),PageWidgetName(*Suffix));
        Tab->Action=EMCMainMenuAction(int32(EMCMainMenuAction::SettingsGraphics)+Index);
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),PageWidgetName(*(Suffix+TEXT("Label"))));
        const FName Key(*(FString(TEXT("UI.Settings.Tab."))+TabNames[Index]));
        Text->SetText(Localized(Key,FText::AsCultureInvariant(TabCopy[Index])));Text->SetJustification(ETextJustify::Center);
        TextBindings.Add(Text->GetFName(),Key);
        auto* Pad=WidgetTree->ConstructWidget<UBorder>();Pad->SetBrushColor(FLinearColor::Transparent);Pad->SetPadding(FMargin(5,10));Pad->SetContent(Text);Tab->SetContent(Pad);
        auto* TabSlot=Tabs->AddChildToHorizontalBox(Tab);TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TabSlot->SetPadding(FMargin(0,0,4,0));
        BindStyle(Tab->GetFName(),EMCUIColorToken::Button,EMCUIStyleTarget::Button);
        BindStyle(Text->GetFName(),EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,14,true);
    }
    auto* Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),PageWidgetName(TEXT("Panel")));
    Panel->SetPadding(FMargin(16,12));PageBody->AddChildToVerticalBox(Panel);
    BindStyle(Panel->GetFName(),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    auto* SectionSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),PageWidgetName(TEXT("SectionSize")));
    SectionSize->SetHeightOverride(320);Panel->SetContent(SectionSize);
    SettingsSections=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),PageWidgetName(TEXT("Sections")));
    SectionSize->SetContent(SettingsSections);
    UVerticalBox* Bodies[5];
    for(int32 Index=0;Index<5;++Index)
    {
        const FString Section=FString(TabNames[Index])+TEXT("Section");
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),PageWidgetName(*Section));
        SettingsSections->AddChild(Scroll);
        Bodies[Index]=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),PageWidgetName(*(Section+TEXT("Body"))));Scroll->AddChild(Bodies[Index]);
    }
    auto Picker=[this](FName Key,const TCHAR* Copy,const TCHAR* Suffix)
    {
        auto* Widget=AddPicker(LocalizedCopy(Key,Copy),Suffix);
        BindStyle(PageWidgetName(*(FString(Suffix)+TEXT("Label"))),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,15);
        return CastChecked<UMCMainMenuPicker>(Widget);
    };
    Content=Bodies[0];
    ResolutionPicker=Picker(TEXT("UI.Menu.Resolution"),TEXT("Разрешение"),TEXT("ResolutionPicker"));
    CastChecked<UMCMainMenuPicker>(ResolutionPicker)->SetPresentationOptions({TEXT("1920 × 1080")});
    WindowModePicker=Picker(TEXT("UI.Menu.WindowMode"),TEXT("Режим экрана"),TEXT("WindowModePicker"));
    CastChecked<UMCMainMenuPicker>(WindowModePicker)->SetPresentationOptions({LocalizedCopy(TEXT("UI.MainMenu.Option.Fullscreen"),TEXT("Fullscreen")),LocalizedCopy(TEXT("UI.MainMenu.Option.Borderless"),TEXT("Borderless")),LocalizedCopy(TEXT("UI.MainMenu.Option.Windowed"),TEXT("Windowed"))},1);
    auto* FrameRate=Picker(TEXT("UI.Settings.FrameRate"),TEXT("Лимит кадров"),TEXT("FrameRatePicker"));
    TArray<FString> Rates={LocalizedCopy(TEXT("UI.Settings.Unlimited"),TEXT("Без ограничений"))};
    for(int32 Index=1;Index<UE_ARRAY_COUNT(MCMainMenuPrivate::FrameRates);++Index) Rates.Add(FString::Printf(TEXT("%d FPS"),int32(MCMainMenuPrivate::FrameRates[Index])));
    FrameRate->SetPresentationOptions(Rates);
    AddSettingsSlider(TEXT("UI.Settings.RenderScale"),TEXT("Масштаб рендера"),TEXT("RenderScaleSlider"),50,100,1,100);
    QualityPicker=Picker(TEXT("UI.Menu.Quality"),TEXT("Общий пресет"),TEXT("QualityPicker"));
    const TCHAR* GroupKeys[]={TEXT("Textures"),TEXT("Shadows"),TEXT("Effects"),TEXT("PostProcess"),TEXT("AA"),TEXT("ViewDistance")};
    const TCHAR* GroupCopy[]={TEXT("Текстуры"),TEXT("Тени"),TEXT("Эффекты"),TEXT("Постобработка"),TEXT("Сглаживание"),TEXT("Дальность прорисовки")};
    TArray<FString> Levels;
    const TCHAR* QualityNames[]={TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")};
    for(const TCHAR* Name:QualityNames) Levels.Add(LocalizedCopy(FName(*(FString(TEXT("UI.MainMenu.Option.Quality."))+Name)),Name));
    auto OverallLevels=Levels;OverallLevels.Add(LocalizedCopy(TEXT("UI.MainMenu.Option.QualityCustom"),TEXT("Custom (keep current)")));
    CastChecked<UMCMainMenuPicker>(QualityPicker)->SetPresentationOptions(OverallLevels,2);
    for(int32 Index=0;Index<6;++Index)
        Picker(FName(*(FString(TEXT("UI.Settings."))+GroupKeys[Index])),GroupCopy[Index],MCMainMenuPrivate::QualityControlNames[Index])->SetPresentationOptions(Levels,2);
    VSyncCheck=AddSettingsCheck(TEXT("UI.Menu.VSync"),TEXT("VSync"),TEXT("VSyncCheck"));
    Content=Bodies[1];
    AddSettingsSlider(TEXT("UI.Settings.Master"),TEXT("Общая громкость"),TEXT("MasterSlider"),0,1,.01f,1);
    AddSettingsSlider(TEXT("UI.Settings.Music"),TEXT("Музыка"),TEXT("MusicSlider"),0,1,.01f,1);
    AddSettingsSlider(TEXT("UI.Settings.Ambience"),TEXT("Окружение"),TEXT("AmbienceSlider"),0,1,.01f,1);
    AddSettingsSlider(TEXT("UI.Settings.SFX"),TEXT("Звуковые эффекты"),TEXT("SFXSlider"),0,1,.01f,1);
    Content=Bodies[2];
    Picker(TEXT("UI.Settings.Language"),TEXT("Язык интерфейса"),TEXT("LanguagePicker"))->SetPresentationOptions(MCMainMenuPrivate::LanguageNames());
    auto* LanguageHint=AddText(LocalizedCopy(TEXT("UI.Settings.LanguageFallback"),TEXT("Если перевод ещё не заполнен, используется русский текст. Изменение языка применяется после нажатия «Применить».")),15,TEXT("LanguageFallback"));
    BindStyle(LanguageHint->GetFName(),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,15);
    Content=Bodies[3];
    AddSettingsSlider(TEXT("UI.Settings.Mouse"),TEXT("Чувствительность мыши"),TEXT("MouseSlider"),.1f,5,.05f,1);
    AddSettingsSlider(TEXT("UI.Settings.Gamepad"),TEXT("Скорость камеры геймпада"),TEXT("GamepadSlider"),.1f,5,.05f,1);
    InvertYCheck=AddSettingsCheck(TEXT("UI.Settings.InvertY"),TEXT("Инверсия камеры по вертикали"),TEXT("InvertYCheck"));
    VoiceSettings=WidgetTree->ConstructWidget<UMCVoiceSettingsWidget>(UMCGameplayUIFactory::ResolveClass(TEXT("VoiceSettings"),UMCVoiceSettingsWidget::StaticClass()),PageWidgetName(TEXT("VoicePanel")));
    if(VoiceSettings) {VoiceSettings->Theme=GetResolvedTheme();Bodies[4]->AddChildToVerticalBox(VoiceSettings);VoiceSettings->BuildPresentationTree();VoiceSettings->ApplyTheme();}
    Content=PageBody;
    AddButton(TEXT("Применить"),EMCMainMenuAction::ApplySettings);
    AddButton(TEXT("Назад"),EMCMainMenuAction::Back);
    SettingsSections->SetActiveWidgetIndex(0);CacheSettingsControls();RefreshSettingsValueLabels();
}

USlider* UMCMainMenuWidget::AddSettingsSlider(FName Key,const TCHAR* Copy,const TCHAR* Suffix,float Minimum,float Maximum,float Step,float Value)
{
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Content->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,8,0,2));
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),PageWidgetName(*(FString(Suffix)+TEXT("Label"))));
    Label->SetText(Localized(Key,FText::AsCultureInvariant(Copy)));TextBindings.Add(Label->GetFName(),Key);
    Row->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    BindStyle(Label->GetFName(),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,15);
    auto* Number=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),PageWidgetName(*(FString(Suffix)+TEXT("Value"))));
    Number->SetJustification(ETextJustify::Right);Row->AddChildToHorizontalBox(Number);
    BindStyle(Number->GetFName(),EMCUIColorToken::Accent,EMCUIStyleTarget::Text,15,true);
    auto* Slider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),PageWidgetName(Suffix));
    Slider->SetMinValue(Minimum);Slider->SetMaxValue(Maximum);Slider->SetStepSize(Step);Slider->SetValue(Value);
    Content->AddChildToVerticalBox(Slider)->SetPadding(FMargin(0,3,0,13));
    BindStyle(Slider->GetFName(),EMCUIColorToken::Accent,EMCUIStyleTarget::Slider);
    return Slider;
}

UCheckBox* UMCMainMenuWidget::AddSettingsCheck(FName Key,const TCHAR* Copy,const TCHAR* Suffix)
{
    auto* Check=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),PageWidgetName(Suffix));
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),PageWidgetName(*(FString(Suffix)+TEXT("Label"))));
    Label->SetText(Localized(Key,FText::AsCultureInvariant(Copy)));Label->SetAutoWrapText(true);TextBindings.Add(Label->GetFName(),Key);
    BindStyle(Label->GetFName(),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,15);
    Check->SetContent(Label);Content->AddChildToVerticalBox(Check)->SetPadding(FMargin(0,8,0,12));return Check;
}

void UMCMainMenuWidget::CacheSettingsControls()
{
    if(!WidgetTree || Page!=EPage::Settings) return;
    SettingsSections=Cast<UWidgetSwitcher>(WidgetTree->FindWidget(PageWidgetName(TEXT("Sections"))));
    EnsureVoiceSettingsPresentation();
    SettingsPickers.Reset();SettingsSliders.Reset();
    for(const TCHAR* Name:{TEXT("FrameRatePicker"),TEXT("LanguagePicker"),TEXT("TexturePicker"),TEXT("ShadowPicker"),TEXT("EffectsPicker"),TEXT("PostProcessPicker"),TEXT("AAPicker"),TEXT("ViewDistancePicker")})
        if(auto* Picker=Cast<UComboBoxString>(WidgetTree->FindWidget(PageWidgetName(Name)))) SettingsPickers.Add(FName(Name),Picker);
    for(const TCHAR* Name:MCMainMenuPrivate::SliderNames)
        if(auto* Slider=Cast<USlider>(WidgetTree->FindWidget(PageWidgetName(Name))))
        {
            SettingsSliders.Add(FName(Name),Slider);
            if(!bBuildingTemplate && !IsDesignTime()) Slider->OnValueChanged.AddUniqueDynamic(this,&ThisClass::SettingsSliderChanged);
        }
    InvertYCheck=Cast<UCheckBox>(WidgetTree->FindWidget(PageWidgetName(TEXT("InvertYCheck"))));
    if(!bBuildingTemplate && !IsDesignTime())
    {
        if(QualityPicker) QualityPicker->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::SettingsPresetChanged);
        for(const TCHAR* Name:MCMainMenuPrivate::QualityControlNames)
            if(auto* Picker=SettingsPickers.FindRef(FName(Name)).Get()) Picker->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::SettingsGroupChanged);
    }
}

void UMCMainMenuWidget::SwitchSettingsTab(int32 Index)
{
    if(Page!=EPage::Settings) return;
    if(VoiceSettings && Index!=4) VoiceSettings->StopInteraction();
    ActiveSettingsTab=FMath::Clamp(Index,0,4);
    if(SettingsSections) SettingsSections->SetActiveWidgetIndex(ActiveSettingsTab);
    const TCHAR* Names[]={TEXT("TabGraphics"),TEXT("TabAudio"),TEXT("TabLanguage"),TEXT("TabControls"),TEXT("TabVoice")};
    for(int32 Tab=0;Tab<5;++Tab)
        BindStyle(PageWidgetName(Names[Tab]),Tab==ActiveSettingsTab?EMCUIColorToken::Accent:EMCUIColorToken::Button,EMCUIStyleTarget::Button);
    CachePageControls();ApplyTheme();
}

void UMCMainMenuWidget::RefreshSettingsValueLabels()
{
    if(!WidgetTree || Page!=EPage::Settings) return;
    for(const auto& Pair:SettingsSliders)
    {
        auto* Slider=Pair.Value.Get();if(!Slider) continue;
        auto* Label=WidgetTree->FindWidget<UTextBlock>(PageWidgetName(*(Pair.Key.ToString()+TEXT("Value"))));
        if(!Label) continue;
        const bool Sensitivity=Pair.Key==TEXT("MouseSlider") || Pair.Key==TEXT("GamepadSlider");
        const float Value=Slider->GetValue();
        const FString Culture=IsDesignTime()?PreviewCulture:FString();
        if(Sensitivity) Label->SetText(FText::FromString(UMCUIDataLibrary::DecimalForCulture(Value,2,Culture).ToString()+TEXT(" ×")));
        else Label->SetText(FText::FromString(UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt(Pair.Key==TEXT("RenderScaleSlider")?Value:Value*100.f),Culture).ToString()+TEXT("%")));
    }
}

void UMCMainMenuWidget::SettingsSliderChanged(float Value)
{
    if(!bUpdatingSettingsControls) RefreshSettingsValueLabels();
}

void UMCMainMenuWidget::SettingsPresetChanged(FString Value,ESelectInfo::Type SelectionType)
{
    if(bUpdatingSettingsControls || !QualityPicker) return;
    const int32 Preset=QualityPicker->GetSelectedIndex();if(Preset<0 || Preset>4) return;
    TGuardValue<bool> Guard(bUpdatingSettingsControls,true);
    for(const TCHAR* Name:MCMainMenuPrivate::QualityControlNames)
        if(auto* Picker=SettingsPickers.FindRef(FName(Name)).Get()) Picker->SetSelectedIndex(Preset);
}

void UMCMainMenuWidget::SettingsGroupChanged(FString Value,ESelectInfo::Type SelectionType)
{
    if(!bUpdatingSettingsControls && QualityPicker && QualityPicker->GetOptionCount()>5)
    {
        TGuardValue<bool> Guard(bUpdatingSettingsControls,true);QualityPicker->SetSelectedIndex(5);
    }
}

void UMCMainMenuWidget::RefreshSettingsQualityOptions(bool bPreserveSelection)
{
    TGuardValue<bool> Guard(bUpdatingSettingsControls,true);
    const TCHAR* Names[]={TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")};
    for(const TCHAR* Name:MCMainMenuPrivate::QualityControlNames)
        if(auto* Picker=SettingsPickers.FindRef(FName(Name)).Get())
        {
            const int32 Selected=bPreserveSelection?Picker->GetSelectedIndex():2;
            Picker->ClearOptions();
            for(const TCHAR* Level:Names) Picker->AddOption(LocalizedCopy(FName(*(FString(TEXT("UI.MainMenu.Option.Quality."))+Level)),Level));
            Picker->SetSelectedIndex(FMath::Clamp(Selected,0,4));
        }
}

void UMCMainMenuWidget::RefreshSettingsControls()
{
    if(VoiceSettings) VoiceSettings->LoadPendingSettings();
    UGameUserSettings* Settings=UGameUserSettings::GetGameUserSettings();
    if (!Settings || !ResolutionPicker || !WindowModePicker || !QualityPicker || !VSyncCheck) return;
    CacheSettingsControls();
    TGuardValue<bool> Guard(bUpdatingSettingsControls,true);
    ResolutionPicker->ClearOptions();
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
    const int32 Quality=Settings->GetOverallScalabilityLevel();
    RefreshSettingsOptionLabels(static_cast<int32>(Settings->GetFullscreenMode()),Quality<0?5:FMath::Clamp(Quality,0,4),Quality<0);
    VSyncCheck->SetIsChecked(Settings->IsVSyncEnabled());
    RefreshSettingsQualityOptions(false);
    const int32 Qualities[]={Settings->GetTextureQuality(),Settings->GetShadowQuality(),Settings->GetVisualEffectQuality(),Settings->GetPostProcessingQuality(),Settings->GetAntiAliasingQuality(),Settings->GetViewDistanceQuality()};
    for(int32 Index=0;Index<6;++Index)
        if(auto* Picker=SettingsPickers.FindRef(FName(MCMainMenuPrivate::QualityControlNames[Index])).Get()) Picker->SetSelectedIndex(FMath::Clamp(Qualities[Index],0,4));
    if(auto* FrameRate=SettingsPickers.FindRef(TEXT("FrameRatePicker")).Get())
    {
        FrameRate->ClearOptions();FrameRate->AddOption(LocalizedCopy(TEXT("UI.Settings.Unlimited"),TEXT("Без ограничений")));
        int32 Selected=0;
        for(int32 Index=1;Index<UE_ARRAY_COUNT(MCMainMenuPrivate::FrameRates);++Index)
        {
            FrameRate->AddOption(FString::Printf(TEXT("%d FPS"),int32(MCMainMenuPrivate::FrameRates[Index])));
            if(FMath::IsNearlyEqual(Settings->GetFrameRateLimit(),MCMainMenuPrivate::FrameRates[Index])) Selected=Index;
        }
        // Preserve an externally authored cap instead of silently converting it to unlimited.
        if(Selected==0 && Settings->GetFrameRateLimit()>0)
        {
            FrameRate->AddOption(FString::Printf(TEXT("%.0f FPS"),Settings->GetFrameRateLimit()));Selected=FrameRate->GetOptionCount()-1;
        }
        FrameRate->SetSelectedIndex(Selected);
    }
    float Normalized,Scale,Min,Max;Settings->GetResolutionScaleInformationEx(Normalized,Scale,Min,Max);
    if(auto* Slider=SettingsSliders.FindRef(TEXT("RenderScaleSlider")).Get()) {Slider->SetMinValue(FMath::Max(1.f,Min));Slider->SetMaxValue(FMath::Max(100.f,Max));Slider->SetValue(Scale);}
    if(auto* Preferences=UMCGameUserSettings::Get())
    {
        const TCHAR* Names[]={TEXT("MasterSlider"),TEXT("MusicSlider"),TEXT("AmbienceSlider"),TEXT("SFXSlider"),TEXT("MouseSlider"),TEXT("GamepadSlider")};
        const float Values[]={Preferences->GetMasterVolume(),Preferences->GetMusicVolume(),Preferences->GetAmbienceVolume(),Preferences->GetSFXVolume(),Preferences->GetMouseSensitivity(),Preferences->GetGamepadSensitivity()};
        for(int32 Index=0;Index<6;++Index) if(auto* Slider=SettingsSliders.FindRef(FName(Names[Index])).Get()) Slider->SetValue(Values[Index]);
        if(InvertYCheck) InvertYCheck->SetIsChecked(Preferences->GetInvertY());
        if(auto* Picker=SettingsPickers.FindRef(TEXT("LanguagePicker")).Get())
        {
            const auto Codes=MCMainMenuPrivate::LanguageCodes();
            const FString Language=Preferences->GetLanguageCode().IsEmpty()?FInternationalization::Get().GetCurrentLanguage()->GetName():Preferences->GetLanguageCode();
            int32 Selected=Codes.IndexOfByKey(Language);
            if(Selected==INDEX_NONE) Selected=Codes.IndexOfByPredicate([&Language](const auto& Code){return Language.StartsWith(Code+TEXT("-"));});
            Picker->SetSelectedIndex(FMath::Max(0,Selected));
        }
    }
    RefreshSettingsValueLabels();
}

void UMCMainMenuWidget::RefreshSettingsOptionLabels(int32 WindowModeIndex,int32 QualityIndex,bool bIncludeCustomQuality)
{
    if (!WindowModePicker || !QualityPicker) return;
    TGuardValue<bool> Guard(bUpdatingSettingsControls,true);
    WindowModePicker->ClearOptions();QualityPicker->ClearOptions();
    WindowModePicker->AddOption(LocalizedCopy(TEXT("UI.MainMenu.Option.Fullscreen"),TEXT("Fullscreen")));
    WindowModePicker->AddOption(LocalizedCopy(TEXT("UI.MainMenu.Option.Borderless"),TEXT("Borderless")));
    WindowModePicker->AddOption(LocalizedCopy(TEXT("UI.MainMenu.Option.Windowed"),TEXT("Windowed")));
    WindowModePicker->SetSelectedIndex(WindowModeIndex);
    const TCHAR* QualityNames[]={TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")};
    for(int32 Index=0;Index<UE_ARRAY_COUNT(QualityNames);++Index)
        QualityPicker->AddOption(LocalizedCopy(FName(*(TEXT("UI.MainMenu.Option.Quality.")+FString(QualityNames[Index]))),QualityNames[Index]));
    if (bIncludeCustomQuality) QualityPicker->AddOption(LocalizedCopy(TEXT("UI.MainMenu.Option.QualityCustom"),TEXT("Custom (keep current)")));
    QualityPicker->SetSelectedIndex(QualityIndex);
}

void UMCMainMenuWidget::HostTutorialChanged(bool bChecked)
{
    bHostStartWithTutorial=IsDesignTime() && bChecked;
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
    const auto Selected=[this](FName Name)
    {const auto* Picker=SettingsPickers.FindRef(Name).Get();return Picker?Picker->GetSelectedIndex():INDEX_NONE;};
    const int32 Texture=Selected(TEXT("TexturePicker")),Shadow=Selected(TEXT("ShadowPicker")),Effects=Selected(TEXT("EffectsPicker"));
    const int32 Post=Selected(TEXT("PostProcessPicker")),AA=Selected(TEXT("AAPicker")),Distance=Selected(TEXT("ViewDistancePicker"));
    if(Texture>=0) Settings->SetTextureQuality(Texture);if(Shadow>=0) Settings->SetShadowQuality(Shadow);
    if(Effects>=0) Settings->SetVisualEffectQuality(Effects);if(Post>=0) Settings->SetPostProcessingQuality(Post);
    if(AA>=0) Settings->SetAntiAliasingQuality(AA);if(Distance>=0) Settings->SetViewDistanceQuality(Distance);
    const int32 FrameRate=Selected(TEXT("FrameRatePicker"));
    if(FrameRate>=0 && FrameRate<UE_ARRAY_COUNT(MCMainMenuPrivate::FrameRates)) Settings->SetFrameRateLimit(MCMainMenuPrivate::FrameRates[FrameRate]);
    if(const auto* Scale=SettingsSliders.FindRef(TEXT("RenderScaleSlider")).Get()) Settings->SetResolutionScaleValueEx(Scale->GetValue());
    if(auto* Preferences=UMCGameUserSettings::Get())
    {
        auto Value=[this](FName Name,float Fallback) {const auto* Slider=SettingsSliders.FindRef(Name).Get();return Slider?Slider->GetValue():Fallback;};
        Preferences->SetMasterVolume(Value(TEXT("MasterSlider"),Preferences->GetMasterVolume()));
        Preferences->SetMusicVolume(Value(TEXT("MusicSlider"),Preferences->GetMusicVolume()));
        Preferences->SetAmbienceVolume(Value(TEXT("AmbienceSlider"),Preferences->GetAmbienceVolume()));
        Preferences->SetSFXVolume(Value(TEXT("SFXSlider"),Preferences->GetSFXVolume()));
        Preferences->SetMouseSensitivity(Value(TEXT("MouseSlider"),Preferences->GetMouseSensitivity()));
        Preferences->SetGamepadSensitivity(Value(TEXT("GamepadSlider"),Preferences->GetGamepadSensitivity()));
        if(InvertYCheck) Preferences->SetInvertY(InvertYCheck->IsChecked());
        const auto Codes=MCMainMenuPrivate::LanguageCodes();const int32 Language=Selected(TEXT("LanguagePicker"));
        if(Codes.IsValidIndex(Language)) Preferences->SetLanguageCode(Codes[Language]);
    }
    Settings->SetVSyncEnabled(VSyncCheck->IsChecked());
    if(VoiceSettings) VoiceSettings->ApplyPendingSettings();
    Settings->ApplySettings(false);
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
    if (bNativeLayout && EmbeddedRoot==EEmbeddedRoot::None && Page!=EPage::Settings) for(UButton* Button:Buttons) if(Button)
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
        if ((Page==EPage::Host || Page==EPage::Join || Page==EPage::Main || Page==EPage::Connection || Page==EPage::Pause)
            && Steam->GetStatus()!=LastSessionStatus)
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
    if(VoiceSettings && VoiceSettings->RouteRebindKey(Event.GetKey())) return FReply::Handled();
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

bool UMCMainMenuWidget::PlayToHub(const UObject* WorldContextObject)
{
    UWorld* World=GEngine?GEngine->GetWorldFromContextObject(WorldContextObject,EGetWorldErrorMode::ReturnNull):nullptr;
    if(!World || !World->IsGameWorld() || World->GetNetMode()==NM_Client) return false;
    if(UGameInstance* Instance=World->GetGameInstance())
        if(auto* Sessions=Instance->GetSubsystem<UMCSteamSessionSubsystem>()) return Sessions->EnterHub();
    // Isolated native/menu fixtures do not initialize instance subsystems.
    // Keep their offline path equivalent, without accessing an online backend.
    const FWorldContext* Context=GEngine->GetWorldContextFromWorld(World);
    if((Context && !Context->TravelURL.IsEmpty()) || World->GetMapName().EndsWith(TEXT("Hub"),ESearchCase::IgnoreCase)) return false;
    UGameplayStatics::OpenLevel(World,FName(TEXT("/Game/Maps/Hub")),true,TEXT("listen?game=/Script/MessControl.MCHubGameMode"));
    return true;
}

void UMCMainMenuWidget::HandleAction(EMCMainMenuAction Action)
{
    if (IsDesignTime()) return; // Preview pages never launch travel, sessions or settings changes.
    if(Action==EMCMainMenuAction::Tutorial || Action==EMCMainMenuAction::Credits) return;
    if (bEmbeddedCloseRequested) return;
    auto* Steam=MCMainMenuPrivate::Sessions(this);
    auto* PC=GetOwningPlayer();
    const bool bLaunchAction=Action==EMCMainMenuAction::Tutorial
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
    case EMCMainMenuAction::SettingsGraphics: SwitchSettingsTab(0);break;
    case EMCMainMenuAction::SettingsAudio: SwitchSettingsTab(1);break;
    case EMCMainMenuAction::SettingsLanguage: SwitchSettingsTab(2);break;
    case EMCMainMenuAction::SettingsControls: SwitchSettingsTab(3);break;
    case EMCMainMenuAction::SettingsVoice: SwitchSettingsTab(4);break;
    case EMCMainMenuAction::Tutorial:
        break;
    case EMCMainMenuAction::CreateLobby: PlayToHub(this); break;
    case EMCMainMenuAction::JoinLobby: ShowPage(EPage::Join); break;
    case EMCMainMenuAction::Settings: ShowPage(EPage::Settings); break;
    case EMCMainMenuAction::Credits: break;
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
        bHostStartWithTutorial=false;
        if (Steam && Steam->CanUseSteam()) Steam->HostRoom(true,bHostStartWithTutorial);
        else UGameplayStatics::OpenLevel(this,bHostStartWithTutorial?MCMainMenuPrivate::TutorialMap:FName(TEXT("/Game/Maps/Hub")),
            true,bHostStartWithTutorial?TEXT("listen?MCLobby=1?MCTutorial=1?MCHubAfterTutorial=1"):TEXT("listen?game=/Script/MessControl.MCHubGameMode"));
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
