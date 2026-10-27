#include "MCVoiceSettingsWidget.h"
#include "MCVoiceChatSubsystem.h"
#include "MCGameUserSettings.h"
#include "MCGameplayUITheme.h"
#include "MCMainMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"

#define LOCTEXT_NAMESPACE "MCVoiceSettings"

UTextBlock* UMCVoiceSettingsWidget::AddLabel(UVerticalBox* Parent,FName Name,FName Key,const TCHAR* Copy)
{
    auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Text->SetText(Localized(Key,FText::AsCultureInvariant(Copy)));Text->SetAutoWrapText(true);
    Parent->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,7,0,3));
    TextBindings.Add(Name,Key);BindStyle(Name,EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,14);
    return Text;
}
USlider* UMCVoiceSettingsWidget::AddSlider(UVerticalBox* Parent,FName Name,FName Key,const TCHAR* Copy,float Maximum,float Value)
{
    AddLabel(Parent,FName(*(Name.ToString()+TEXT("Label"))),Key,Copy);
    auto* Slider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),Name);
    Slider->SetMinValue(0);Slider->SetMaxValue(Maximum);Slider->SetStepSize(.01f);Slider->SetValue(Value);
    Parent->AddChildToVerticalBox(Slider)->SetPadding(FMargin(3,2,3,8));
    BindStyle(Name,EMCUIColorToken::Accent,EMCUIStyleTarget::Slider);
    return Slider;
}
UButton* UMCVoiceSettingsWidget::AddButton(UVerticalBox* Parent,FName Name,FName Key,const TCHAR* Copy)
{
    auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
    auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*(Name.ToString()+TEXT("Label"))));
    Text->SetText(Localized(Key,FText::AsCultureInvariant(Copy)));Text->SetAutoWrapText(true);Text->SetJustification(ETextJustify::Center);
    Button->SetContent(Text);Parent->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,5));
    TextBindings.Add(Text->GetFName(),Key);BindStyle(Name,EMCUIColorToken::Button,EMCUIStyleTarget::Button);
    BindStyle(Text->GetFName(),EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,14,true);
    return Button;
}
UCheckBox* UMCVoiceSettingsWidget::AddCheck(UVerticalBox* Parent,FName Name,FName Key,const TCHAR* Copy)
{
    auto* Check=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),Name);
    auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*(Name.ToString()+TEXT("Label"))));
    Text->SetText(Localized(Key,FText::AsCultureInvariant(Copy)));Text->SetAutoWrapText(true);Check->SetContent(Text);
    Parent->AddChildToVerticalBox(Check)->SetPadding(FMargin(0,5));
    TextBindings.Add(Text->GetFName(),Key);BindStyle(Text->GetFName(),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,14);
    return Check;
}
void UMCVoiceSettingsWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!WidgetTree->RootWidget)
    {
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("VoiceSettingsRoot"));WidgetTree->RootWidget=Body;
        AddCheck(Body,TEXT("VoiceEnabled"),TEXT("UI.Voice.Enabled"),TEXT("Голосовой чат включён"));
        AddLabel(Body,TEXT("VoiceModeLabel"),TEXT("UI.Voice.Mode"),TEXT("Режим микрофона"));
        auto* Mode=WidgetTree->ConstructWidget<UMCMainMenuPicker>(UMCMainMenuPicker::StaticClass(),TEXT("VoiceMode"));
        Mode->InitializePresentationFont(GetResolvedTheme()->MakeGameplayFont(14));
        Mode->SetPresentationOptions({Localized(TEXT("UI.Voice.PushToTalk"),LOCTEXT("PTT","Нажми, чтобы говорить")).ToString(),Localized(TEXT("UI.Voice.OpenMic"),LOCTEXT("OpenMic","Открытый микрофон")).ToString()});
        Body->AddChildToVerticalBox(Mode);
        AddButton(Body,TEXT("VoiceKey"),TEXT("UI.Voice.Key"),TEXT("Назначить клавишу разговора"));
        AddCheck(Body,TEXT("VoiceSelfMute"),TEXT("UI.Voice.SelfMute"),TEXT("Отключить свой микрофон"));
        AddSlider(Body,TEXT("VoiceGain"),TEXT("UI.Voice.Gain"),TEXT("Усиление микрофона"),3,1);
        AddSlider(Body,TEXT("VoiceThreshold"),TEXT("UI.Voice.Threshold"),TEXT("Порог активации голосом"),1,.08f);
        AddSlider(Body,TEXT("VoiceVolume"),TEXT("UI.Voice.Volume"),TEXT("Громкость голосов"),1,1);
        AddLabel(Body,TEXT("VoiceDevice"),TEXT("UI.Voice.SystemDevice"),TEXT("Используется системный микрофон"));
        AddButton(Body,TEXT("VoiceDeviceSettings"),TEXT("UI.Voice.DeviceSettings"),TEXT("Настроить микрофон в Windows"));
        AddButton(Body,TEXT("VoiceTest"),TEXT("UI.Voice.Test"),TEXT("Проверить сохранённые настройки микрофона"));
        auto* Meter=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("VoiceInputMeter"));Body->AddChildToVerticalBox(Meter)->SetPadding(FMargin(0,5));
        BindStyle(TEXT("VoiceInputMeter"),EMCUIColorToken::Accent,EMCUIStyleTarget::Progress);
        AddLabel(Body,TEXT("VoicePlayersLabel"),TEXT("UI.Voice.Players"),TEXT("Голос участника"));
        auto* Picker=WidgetTree->ConstructWidget<UMCMainMenuPicker>(UMCMainMenuPicker::StaticClass(),TEXT("VoicePlayer"));
        Picker->InitializePresentationFont(GetResolvedTheme()->MakeGameplayFont(14));Body->AddChildToVerticalBox(Picker);
        AddCheck(Body,TEXT("VoicePlayerMute"),TEXT("UI.Voice.PlayerMute"),TEXT("Не слышать этого игрока"));
        AddLabel(Body,TEXT("VoiceStatus"),TEXT("UI.Voice.Status"),TEXT("Голос доступен в сетевой комнате. Проверка микрофона — вне комнаты."));
    }
    EnabledCheck=WidgetTree->FindWidget<UCheckBox>(TEXT("VoiceEnabled"));SelfMuteCheck=WidgetTree->FindWidget<UCheckBox>(TEXT("VoiceSelfMute"));
    PlayerMuteCheck=WidgetTree->FindWidget<UCheckBox>(TEXT("VoicePlayerMute"));ModePicker=WidgetTree->FindWidget<UComboBoxString>(TEXT("VoiceMode"));
    PlayerPicker=WidgetTree->FindWidget<UComboBoxString>(TEXT("VoicePlayer"));GainSlider=WidgetTree->FindWidget<USlider>(TEXT("VoiceGain"));
    ThresholdSlider=WidgetTree->FindWidget<USlider>(TEXT("VoiceThreshold"));VolumeSlider=WidgetTree->FindWidget<USlider>(TEXT("VoiceVolume"));
    KeyButton=WidgetTree->FindWidget<UButton>(TEXT("VoiceKey"));KeyLabel=WidgetTree->FindWidget<UTextBlock>(TEXT("VoiceKeyLabel"));
    TestButton=WidgetTree->FindWidget<UButton>(TEXT("VoiceTest"));DeviceButton=WidgetTree->FindWidget<UButton>(TEXT("VoiceDeviceSettings"));
    StatusLabel=WidgetTree->FindWidget<UTextBlock>(TEXT("VoiceStatus"));DeviceLabel=WidgetTree->FindWidget<UTextBlock>(TEXT("VoiceDevice"));InputMeter=WidgetTree->FindWidget<UProgressBar>(TEXT("VoiceInputMeter"));
}
UMCVoiceChatSubsystem* UMCVoiceSettingsWidget::Voice() const
{
    return !IsDesignTime() && GetWorld() && GetWorld()->IsGameWorld() && GetGameInstance()?GetGameInstance()->GetSubsystem<UMCVoiceChatSubsystem>():nullptr;
}
void UMCVoiceSettingsWidget::BindControls()
{
    if(KeyButton) KeyButton->OnClicked.AddUniqueDynamic(this,&ThisClass::RebindClicked);
    if(TestButton) TestButton->OnClicked.AddUniqueDynamic(this,&ThisClass::MicTestClicked);
    if(DeviceButton) DeviceButton->OnClicked.AddUniqueDynamic(this,&ThisClass::DeviceClicked);
    if(PlayerPicker) PlayerPicker->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::PlayerChanged);
    if(PlayerMuteCheck) PlayerMuteCheck->OnCheckStateChanged.AddUniqueDynamic(this,&ThisClass::PlayerMuteChanged);
    if(EnabledCheck) EnabledCheck->OnCheckStateChanged.AddUniqueDynamic(this,&ThisClass::PreferenceToggleChanged);
    if(SelfMuteCheck) SelfMuteCheck->OnCheckStateChanged.AddUniqueDynamic(this,&ThisClass::PreferenceToggleChanged);
    if(ModePicker) ModePicker->OnSelectionChanged.AddUniqueDynamic(this,&ThisClass::PreferenceModeChanged);
    for(USlider* Slider:{GainSlider.Get(),ThresholdSlider.Get(),VolumeSlider.Get()})
        if(Slider) Slider->OnValueChanged.AddUniqueDynamic(this,&ThisClass::PreferenceValueChanged);
}
void UMCVoiceSettingsWidget::PreferenceToggleChanged(bool) {CommitImmediatePreferences();}
void UMCVoiceSettingsWidget::PreferenceModeChanged(FString,ESelectInfo::Type) {CommitImmediatePreferences();}
void UMCVoiceSettingsWidget::PreferenceValueChanged(float) {CommitImmediatePreferences();}
void UMCVoiceSettingsWidget::CommitImmediatePreferences()
{
    if(!bApplyChangesImmediately || bUpdating || !bDraftLoaded || IsDesignTime()) return;
    ApplyPendingSettings();
    if(auto* Settings=UMCGameUserSettings::Get()) {Settings->ApplyGameplaySettings();Settings->SaveSettings();}
    RefreshLive();
}
void UMCVoiceSettingsWidget::NativeConstruct()
{
    Super::NativeConstruct();BuildPresentationTree();
    if(!IsDesignTime()) {BindControls();if(!bDraftLoaded) LoadPendingSettings();}
}
void UMCVoiceSettingsWidget::NativeDestruct() {StopInteraction();Super::NativeDestruct();}
void UMCVoiceSettingsWidget::LoadPendingSettings()
{
    BuildPresentationTree();if(IsDesignTime() || !Voice()) return;
    const auto* Settings=UMCGameUserSettings::Get();if(!Settings) return;
    TGuardValue<bool> Guard(bUpdating,true);
    if(EnabledCheck) EnabledCheck->SetIsChecked(Settings->GetVoiceEnabled());
    if(SelfMuteCheck) SelfMuteCheck->SetIsChecked(Settings->GetSelfMuted());
    if(ModePicker) ModePicker->SetSelectedIndex(Settings->GetVoiceInputMode()==EMCVoiceInputMode::OpenMic?1:0);
    if(GainSlider) GainSlider->SetValue(Settings->GetMicrophoneGain());
    if(ThresholdSlider) ThresholdSlider->SetValue(Settings->GetVoiceActivationThreshold());
    if(VolumeSlider) VolumeSlider->SetValue(Settings->GetVoiceVolume());
    PendingKey=Settings->GetPushToTalkKey();bDraftLoaded=true;bAwaitingKey=false;
    if(auto* Backend=Voice()) Backend->RefreshInputDevices();
    RefreshLive();
}
void UMCVoiceSettingsWidget::ApplyPendingSettings()
{
    if(IsDesignTime() || !bDraftLoaded || !Voice()) return;
    StopInteraction();auto* Settings=UMCGameUserSettings::Get();if(!Settings) return;
    if(EnabledCheck) Settings->SetVoiceEnabled(EnabledCheck->IsChecked());
    if(SelfMuteCheck) Settings->SetSelfMuted(SelfMuteCheck->IsChecked());
    if(ModePicker) Settings->SetVoiceInputMode(ModePicker->GetSelectedIndex()==1?EMCVoiceInputMode::OpenMic:EMCVoiceInputMode::PushToTalk);
    if(GainSlider) Settings->SetMicrophoneGain(GainSlider->GetValue());
    if(ThresholdSlider) Settings->SetVoiceActivationThreshold(ThresholdSlider->GetValue());
    if(VolumeSlider) Settings->SetVoiceVolume(VolumeSlider->GetValue());
    if(PendingKey.IsValid()) Settings->SetPushToTalkKey(PendingKey);
    if(auto* Backend=Voice()) Backend->ApplyPreferences();
}
void UMCVoiceSettingsWidget::StopInteraction()
{
    if(auto* Backend=Voice()) {if(bAwaitingKey) {Backend->SetInputSuppressed(false);Backend->ReleasePushToTalk();}if(bOwnsMicTest) Backend->StopMicTest();}
    bAwaitingKey=false;bOwnsMicTest=false;
}
void UMCVoiceSettingsWidget::RebindClicked() {bAwaitingKey=true;if(auto* Backend=Voice()) Backend->SetInputSuppressed(true);SetIsFocusable(true);SetKeyboardFocus();RefreshLive();}
bool UMCVoiceSettingsWidget::RouteRebindKey(FKey Key)
{
    if(!bAwaitingKey) return false;
    if(Key.IsModifierKey() || Key.IsAnalog()) return true;
    if(Key!=EKeys::Escape && Key.IsValid()) PendingKey=Key;
    bAwaitingKey=false;if(auto* Backend=Voice()) {Backend->SetInputSuppressed(false);Backend->ReleasePushToTalk();}CommitImmediatePreferences();RefreshLive();return true;
}
FReply UMCVoiceSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    return RouteRebindKey(Event.GetKey())?FReply::Handled():Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCVoiceSettingsWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    return RouteRebindKey(Event.GetEffectingButton())?FReply::Handled():Super::NativeOnPreviewMouseButtonDown(Geometry,Event);
}
void UMCVoiceSettingsWidget::OnLocalizationChanged()
{
    TGuardValue<bool> Guard(bUpdating,true);
    BuildPresentationTree();
    if(auto* Picker=Cast<UMCMainMenuPicker>(ModePicker))
    {
        const int32 Index=FMath::Max(0,Picker->GetSelectedIndex());
        Picker->SetPresentationOptions({Localized(TEXT("UI.Voice.PushToTalk"),LOCTEXT("PTT","Нажми, чтобы говорить")).ToString(),Localized(TEXT("UI.Voice.OpenMic"),LOCTEXT("OpenMic","Открытый микрофон")).ToString()});Picker->SetSelectedIndex(Index);
    }
    RefreshLive();
}
void UMCVoiceSettingsWidget::MicTestClicked()
{
    if(auto* Backend=Voice()) {if(bOwnsMicTest) StopInteraction();else bOwnsMicTest=Backend->StartMicTest();}
    RefreshLive();
}
void UMCVoiceSettingsWidget::DeviceClicked() {if(auto* Backend=Voice()) Backend->OpenSystemInputSettings();}
void UMCVoiceSettingsWidget::PlayerChanged(FString,ESelectInfo::Type)
{
    TGuardValue<bool> Guard(bUpdating,true);
    const int32 Index=PlayerPicker?PlayerPicker->GetSelectedIndex():INDEX_NONE;
    const auto* Backend=Voice();const bool Valid=Backend && Players.IsValidIndex(Index) && Players[Index].IsValid();
    if(PlayerMuteCheck) {PlayerMuteCheck->SetIsEnabled(Valid);PlayerMuteCheck->SetIsChecked(Valid && Backend->IsPlayerMuted(Players[Index].Get()));}
}
void UMCVoiceSettingsWidget::PlayerMuteChanged(bool Muted)
{
    if(bUpdating) return;const int32 Index=PlayerPicker?PlayerPicker->GetSelectedIndex():INDEX_NONE;
    if(auto* Backend=Voice();Backend && Players.IsValidIndex(Index) && Players[Index].IsValid()) Backend->SetPlayerMuted(Players[Index].Get(),Muted);
}
void UMCVoiceSettingsWidget::RefreshLive()
{
    auto ValueLabel=[this](const TCHAR* Name,const TCHAR* Key,const FText& Fallback,const USlider* Slider,float Multiplier)
    {if(Slider) if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) Label->SetText(FText::Format(Localized(Key,Fallback),FText::AsNumber(FMath::RoundToInt(Slider->GetValue()*Multiplier))));};
    ValueLabel(TEXT("VoiceGainLabel"),TEXT("UI.Voice.GainValue"),LOCTEXT("GainValue","Усиление микрофона: {0}%"),GainSlider,100);
    ValueLabel(TEXT("VoiceThresholdLabel"),TEXT("UI.Voice.ThresholdValue"),LOCTEXT("ThresholdValue","Порог активации голосом: {0}%"),ThresholdSlider,100);
    ValueLabel(TEXT("VoiceVolumeLabel"),TEXT("UI.Voice.VolumeValue"),LOCTEXT("VolumeValue","Громкость голосов: {0}%"),VolumeSlider,100);
    if(KeyLabel) KeyLabel->SetText(bAwaitingKey?Localized(TEXT("UI.Voice.PressKey"),LOCTEXT("PressKey","Нажмите клавишу · Esc — отмена")):FText::Format(Localized(TEXT("UI.Voice.KeyValue"),LOCTEXT("KeyValue","Клавиша разговора: {0}")),PendingKey.GetDisplayName()));
    auto* Backend=Voice();if(!Backend) return;
    if(StatusLabel) StatusLabel->SetText(Backend->GetStatusText());
    if(InputMeter) InputMeter->SetPercent(Backend->GetInputLevel());
    if(bOwnsMicTest && !Backend->IsMicTestActive()) bOwnsMicTest=false;
    if(auto* Label=WidgetTree->FindWidget<UTextBlock>(TEXT("VoiceTestLabel"))) Label->SetText(bOwnsMicTest?Localized(TEXT("UI.Voice.StopTest"),LOCTEXT("StopTest","Остановить проверку микрофона")):Localized(TEXT("UI.Voice.Test"),LOCTEXT("Test","Проверить сохранённые настройки микрофона")));
    if(DeviceLabel) {const auto Devices=Backend->GetInputDevices();FString Device;for(const auto& Item:Devices) if(Item.bDefault) {Device=Item.Name;break;}
        DeviceLabel->SetText(FText::Format(Localized(TEXT("UI.Voice.SystemDeviceValue"),LOCTEXT("Device","Системный микрофон Windows: {0}. После смены устройства может потребоваться перезапуск игры.")),FText::FromString(Device.IsEmpty()?TEXT("—"):Device)));}
    FString Signature;TArray<TWeakObjectPtr<APlayerState>> Current;
    if(const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr) for(APlayerState* Player:State->PlayerArray)
        if(Player && !Player->IsABot() && (!GetOwningPlayer() || Player!=GetOwningPlayer()->PlayerState)) {Current.Add(Player);Signature+=Player->GetPathName()+Player->GetPlayerName();}
    if(Signature!=PlayerSignature && PlayerPicker) {PlayerSignature=Signature;Players=MoveTemp(Current);TGuardValue<bool> Guard(bUpdating,true);PlayerPicker->ClearOptions();for(const auto& Player:Players) PlayerPicker->AddOption(Player->GetPlayerName());if(Players.Num()) PlayerPicker->SetSelectedIndex(0);}
    PlayerChanged(FString(),ESelectInfo::Direct);
}
void UMCVoiceSettingsWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt);RefreshElapsed+=Dt;if(RefreshElapsed<.15f) return;RefreshElapsed=0;if(!IsDesignTime()) RefreshLive();
}
void UMCVoiceSettingsWidget::ApplyEditorPreview(float Time)
{
    BuildPresentationTree();Super::ApplyEditorPreview(Time);if(InputMeter) InputMeter->SetPercent(.55f+.2f*FMath::Sin(Time*5));
}
void UMCVoiceSettingsWidget::GetFocusableControls(TArray<UWidget*>& Out) const
{
    UWidget* Controls[]={EnabledCheck.Get(),ModePicker.Get(),KeyButton.Get(),SelfMuteCheck.Get(),GainSlider.Get(),ThresholdSlider.Get(),VolumeSlider.Get(),DeviceButton.Get(),TestButton.Get(),PlayerPicker.Get(),PlayerMuteCheck.Get()};
    for(auto* Control:Controls) if(Control) Out.Add(Control);
}
#undef LOCTEXT_NAMESPACE
