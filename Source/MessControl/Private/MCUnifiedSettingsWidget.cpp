#include "MCUnifiedSettingsWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCUniversalUIWidget.h"
#include "MCVoiceSettingsWidget.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCUnifiedSettings"

namespace
{
    FLinearColor MCUnifiedSettingsPrivate_Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    const FLinearColor MCUnifiedSettingsPrivate_Blue=MCUnifiedSettingsPrivate_Color(TEXT("4C79AC")), MCUnifiedSettingsPrivate_Navy=MCUnifiedSettingsPrivate_Color(TEXT("2B2B83"));
    const FLinearColor MCUnifiedSettingsPrivate_Inner=MCUnifiedSettingsPrivate_Color(TEXT("315D73")), MCUnifiedSettingsPrivate_Paper=MCUnifiedSettingsPrivate_Color(TEXT("FFF9F8"));

    struct MCUnifiedSettingsPrivate_FControl {const TCHAR* Name;const TCHAR* Key;const TCHAR* Copy;int32 Tab;int32 Kind;float Min=0,Max=1,Value=1;};
    // kind: 0 picker, 1 slider, 2 checkbox, 3 forwarded voice button, 4 live text, 5 meter.
    const MCUnifiedSettingsPrivate_FControl MCUnifiedSettingsPrivate_Controls[]={
        {TEXT("ResolutionPicker"),TEXT("UI.Menu.Resolution"),TEXT("РАЗРЕШЕНИЕ ЭКРАНА"),0,0},
        {TEXT("WindowModePicker"),TEXT("UI.Menu.WindowMode"),TEXT("РЕЖИМ ЭКРАНА"),0,0},
        {TEXT("FrameRatePicker"),TEXT("UI.Settings.FrameRate"),TEXT("ЛИМИТ КАДРОВ"),0,0},
        {TEXT("RenderScaleSlider"),TEXT("UI.Settings.RenderScale"),TEXT("МАСШТАБ РЕНДЕРА"),0,1,50,100,70},
        {TEXT("QualityPicker"),TEXT("UI.Menu.Quality"),TEXT("ОБЩИЙ ПРЕСЕТ"),0,0},
        {TEXT("TexturePicker"),TEXT("UI.Settings.Textures"),TEXT("ТЕКСТУРЫ"),0,0},
        {TEXT("ShadowPicker"),TEXT("UI.Settings.Shadows"),TEXT("ТЕНИ"),0,0},
        {TEXT("EffectsPicker"),TEXT("UI.Settings.Effects"),TEXT("ЭФФЕКТЫ"),0,0},
        {TEXT("PostProcessPicker"),TEXT("UI.Settings.PostProcess"),TEXT("ПОСТОБРАБОТКА"),0,0},
        {TEXT("AAPicker"),TEXT("UI.Settings.AA"),TEXT("СГЛАЖИВАНИЕ"),0,0},
        {TEXT("ViewDistancePicker"),TEXT("UI.Settings.ViewDistance"),TEXT("ДАЛЬНОСТЬ ПРОРИСОВКИ"),0,0},
        {TEXT("VSyncCheck"),TEXT("UI.Menu.VSync"),TEXT("VSYNC"),0,2},
        {TEXT("CameraSmoothingCheck"),TEXT("UI.Settings.CameraSmoothing"),TEXT("СГЛАЖИВАНИЕ КАМЕРЫ"),0,2},
        {TEXT("MasterSlider"),TEXT("UI.Settings.Master"),TEXT("ОБЩАЯ ГРОМКОСТЬ"),1,1},
        {TEXT("MusicSlider"),TEXT("UI.Settings.Music"),TEXT("МУЗЫКА"),1,1},
        {TEXT("AmbienceSlider"),TEXT("UI.Settings.Ambience"),TEXT("ОКРУЖЕНИЕ"),1,1},
        {TEXT("SFXSlider"),TEXT("UI.Settings.SFX"),TEXT("ЗВУКОВЫЕ ЭФФЕКТЫ"),1,1},
        {TEXT("LanguagePicker"),TEXT("UI.Settings.Language"),TEXT("ЯЗЫК ИНТЕРФЕЙСА"),2,0},
        {TEXT("LanguageFallback"),TEXT("UI.Settings.LanguageFallback"),TEXT("Если перевод ещё не заполнен, используется русский текст. Изменение языка применяется после нажатия «Применить»."),2,4},
        {TEXT("MouseSlider"),TEXT("UI.Settings.Mouse"),TEXT("ЧУВСТВИТЕЛЬНОСТЬ МЫШИ"),3,1,.1f,5,1},
        {TEXT("GamepadSlider"),TEXT("UI.Settings.Gamepad"),TEXT("СКОРОСТЬ КАМЕРЫ ГЕЙМПАДА"),3,1,.1f,5,1},
        {TEXT("InvertYCheck"),TEXT("UI.Settings.InvertY"),TEXT("ИНВЕРСИЯ КАМЕРЫ ПО ВЕРТИКАЛИ"),3,2},
        {TEXT("VoiceEnabled"),TEXT("UI.Voice.Enabled"),TEXT("ГОЛОСОВОЙ ЧАТ ВКЛЮЧЁН"),4,2},
        {TEXT("VoiceMode"),TEXT("UI.Voice.Mode"),TEXT("РЕЖИМ МИКРОФОНА"),4,0},
        {TEXT("VoiceKey"),TEXT("UI.Voice.Key"),TEXT("НАЗНАЧИТЬ КЛАВИШУ РАЗГОВОРА"),4,3},
        {TEXT("VoiceSelfMute"),TEXT("UI.Voice.SelfMute"),TEXT("ОТКЛЮЧИТЬ СВОЙ МИКРОФОН"),4,2},
        {TEXT("VoiceGain"),TEXT("UI.Voice.Gain"),TEXT("УСИЛЕНИЕ МИКРОФОНА"),4,1,0,3,1},
        {TEXT("VoiceThreshold"),TEXT("UI.Voice.Threshold"),TEXT("ПОРОГ АКТИВАЦИИ ГОЛОСОМ"),4,1,0,1,.08f},
        {TEXT("VoiceVolume"),TEXT("UI.Voice.Volume"),TEXT("ГРОМКОСТЬ ГОЛОСОВ"),4,1},
        {TEXT("VoiceDevice"),TEXT("UI.Voice.SystemDevice"),TEXT("ИСПОЛЬЗУЕТСЯ СИСТЕМНЫЙ МИКРОФОН"),4,4},
        {TEXT("VoiceDeviceSettings"),TEXT("UI.Voice.DeviceSettings"),TEXT("НАСТРОИТЬ МИКРОФОН В WINDOWS"),4,3},
        {TEXT("VoiceTest"),TEXT("UI.Voice.Test"),TEXT("ПРОВЕРИТЬ МИКРОФОН"),4,3},
        {TEXT("VoiceInputMeter"),TEXT("UI.Voice.InputLevel"),TEXT("УРОВЕНЬ МИКРОФОНА"),4,5},
        {TEXT("VoicePlayer"),TEXT("UI.Voice.Players"),TEXT("ГОЛОС УЧАСТНИКА"),4,0},
        {TEXT("VoicePlayerMute"),TEXT("UI.Voice.PlayerMute"),TEXT("НЕ СЛЫШАТЬ ЭТОГО ИГРОКА"),4,2},
        {TEXT("VoiceStatus"),TEXT("UI.Voice.Status"),TEXT("Голос доступен в сетевой комнате. Проверка микрофона — вне комнаты."),4,4},
    };
    FName MCUnifiedSettingsPrivate_ViewName(const TCHAR* Name) {return FName(*(FString(TEXT("SettingsV2_"))+Name));}
}

void UMCUnifiedSettingsBinding::PickerChanged(FString Value,ESelectInfo::Type) {if(Owner.IsValid()) Owner->ForwardPicker(SourceName,Value);}
void UMCUnifiedSettingsBinding::SliderChanged(float Value) {if(Owner.IsValid()) Owner->ForwardSlider(SourceName,Value);}
void UMCUnifiedSettingsBinding::CheckChanged(bool Value) {if(Owner.IsValid()) Owner->ForwardCheck(SourceName,Value);}
void UMCUnifiedSettingsBinding::Clicked() {if(Owner.IsValid()) Owner->ForwardAction(SourceName,Action);}

UMCUnifiedSettingsWidget::UMCUnifiedSettingsWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;SetIsFocusable(true);
}

void UMCUnifiedSettingsWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {BindControls();return;}
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("SettingsV2Size"));
    Size->SetMaxDesiredWidth(1000);WidgetTree->RootWidget=Size;
    auto* Root=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("SettingsV2Root"));Size->SetContent(Root);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("SettingsV2Column"));
    auto* MainCell=Root->AddChildToOverlay(Column);MainCell->SetHorizontalAlignment(HAlign_Center);MainCell->SetVerticalAlignment(VAlign_Top);
    auto Shape=[&](FName Name,FLinearColor Fill,float Radius,FVector2D Shadow,bool bButton=false)
    {
        auto* Panel=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
        Panel->Kind=bButton?EMCUniversalUIKind::Button:EMCUniversalUIKind::Panel;
        Panel->bCustomColors=true;Panel->bUseThemeShape=false;Panel->CustomFill=Fill;Panel->CustomOutline=MCUnifiedSettingsPrivate_Navy;Panel->CustomShadow=MCUnifiedSettingsPrivate_Navy;
        Panel->CornerRadius=Radius;Panel->OutlineWidth=3;Panel->ShadowOffset=Shadow;Panel->ContentPadding=FMargin(14);
        Panel->Label=FText::GetEmpty();Panel->Initialize();Panel->BuildPresentationTree();return Panel;
    };
    auto AddText=[&](FName Name,FName Key,const TCHAR* Copy,int32 FontSize)
    {
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Text->SetText(Localized(Key,FText::AsCultureInvariant(Copy)));Text->SetAutoWrapText(true);Text->SetJustification(ETextJustify::Center);
        TextBindings.Add(Name,Key);BindStyle(Name,EMCUIColorToken::Cream,EMCUIStyleTarget::Text,FontSize,true);return Text;
    };
    auto AddAction=[&](FName Name,FName Key,const TCHAR* Copy,int32 FontSize=23)
    {
        auto* Button=Shape(Name,MCUnifiedSettingsPrivate_Blue,18,{5,7},true);
        Button->LabelKey=Key;Button->Label=FText::AsCultureInvariant(Copy);Button->CustomText=MCUnifiedSettingsPrivate_Paper;Button->FontSize=FontSize;
        Button->ContentPadding=FMargin(26,14);Button->ApplyTheme();return Button;
    };
    // Tabs take their width from real label size and padding. Longer translations
    // wrap the row instead of scaling text or overlapping the following panel.
    auto* Tabs=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("SettingsV2Tabs"));
    Tabs->SetExplicitWrapSize(true);Tabs->SetWrapSize(1000);Tabs->SetInnerSlotPadding({0,3});
    Column->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0,0,0,2));
    const TCHAR* TabNames[]={TEXT("Graphics"),TEXT("Audio"),TEXT("Language"),TEXT("Controls"),TEXT("Voice")};
    const TCHAR* TabCopies[]={TEXT("ГРАФИКА"),TEXT("АУДИО"),TEXT("ЯЗЫК"),TEXT("УПРАВЛЕНИЕ"),TEXT("ГОЛОС")};
    for(int32 I=0;I<5;++I) {
        auto* Tab=AddAction(FName(*FString::Printf(TEXT("SettingsV2Tab%d"),I)),
            FName(*(FString(TEXT("UI.Settings.Tab."))+TabNames[I])),TabCopies[I],18);
        Tab->ShadowOffset={0,0};Tab->ContentPadding=FMargin(17,15);Tab->ApplyTheme();
        Tabs->AddChildToWrapBox(Tab)->SetVerticalAlignment(VAlign_Bottom);
    }
    auto* Outer=Shape(TEXT("SettingsV2Outer"),MCUnifiedSettingsPrivate_Blue,18,{6,9});
    auto* InnerPanel=Shape(TEXT("SettingsV2Inner"),MCUnifiedSettingsPrivate_Inner,16,{0,0});
    Outer->SetContentForSlot(TEXT("Content"),InnerPanel);
    Column->AddChildToVerticalBox(Outer)->SetPadding(FMargin(0,0,8,12));
    auto* Viewport=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("SettingsV2ScrollViewport"));
    Viewport->SetMinDesiredHeight(64);Viewport->SetMaxDesiredHeight(636);
    InnerPanel->SetContentForSlot(TEXT("Content"),Viewport);
    auto* Pages=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("SettingsV2Sections"));
    Viewport->SetContent(Pages);
    UVerticalBox* Bodies[5];
    for(int32 I=0;I<5;++I) {
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),FName(*FString::Printf(TEXT("SettingsV2Scroll%d"),I)));
        Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);Pages->AddChild(Scroll);
        Bodies[I]=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),FName(*FString::Printf(TEXT("SettingsV2Body%d"),I)));
        Scroll->AddChild(Bodies[I]);
    }
    for(const auto& Definition:MCUnifiedSettingsPrivate_Controls) {
        auto* Body=Bodies[Definition.Tab];
        const FName Name=MCUnifiedSettingsPrivate_ViewName(Definition.Name),Key(Definition.Key);
        const FName LabelName(*(Name.ToString()+TEXT("Label")));
        if(Definition.Kind==4) {
            auto* Label=AddText(Name,Key,Definition.Copy,16);
            Body->AddChildToVerticalBox(Label)->SetPadding(FMargin(16,10,16,14));continue;
        }
        if(Definition.Kind!=2 && Definition.Kind!=3 && Definition.Kind!=5) Body->AddChildToVerticalBox(AddText(LabelName,Key,Definition.Copy,20))->SetPadding(FMargin(4,9,4,4));
        UWidget* Control=nullptr;
        auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetMinDesiredWidth(400);Width->SetMaxDesiredWidth(944);Width->SetMinDesiredHeight(48);
        if(Definition.Kind==0) {
            auto* Picker=WidgetTree->ConstructWidget<UMCMainMenuPicker>(UMCMainMenuPicker::StaticClass(),Name);
            Picker->InitializePresentationFont(GetResolvedTheme()->MakeFont(23,true));
            Picker->InitializePresentationForeground(MCUnifiedSettingsPrivate_Navy);
            Picker->SetPresentationTextCentered(true);
            Picker->SetHasDownArrow(false);Picker->SetContentPadding(FMargin(12,4));
            auto Style=Picker->GetWidgetStyle();
            auto ButtonStyle=Style.ComboButtonStyle.ButtonStyle;
            ButtonStyle.SetNormal(FSlateRoundedBoxBrush(MCUnifiedSettingsPrivate_Paper,15,MCUnifiedSettingsPrivate_Navy,3));
            ButtonStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White,15,MCUnifiedSettingsPrivate_Navy,3));
            ButtonStyle.SetPressed(FSlateRoundedBoxBrush(MCUnifiedSettingsPrivate_Blue,15,MCUnifiedSettingsPrivate_Navy,3));
            ButtonStyle.SetDisabled(FSlateRoundedBoxBrush(MCUnifiedSettingsPrivate_Paper.CopyWithNewOpacity(.45f),15,MCUnifiedSettingsPrivate_Navy,3));
            ButtonStyle.SetNormalPadding(FMargin(8,3));ButtonStyle.SetPressedPadding(FMargin(8,3));
            Style.ComboButtonStyle.SetButtonStyle(ButtonStyle);Picker->SetWidgetStyle(Style);
            if(FString(Definition.Name)==TEXT("ResolutionPicker")) Picker->SetPresentationOptions({TEXT("1920 × 1080"),TEXT("1280 × 720")});
            else if(FString(Definition.Name)==TEXT("WindowModePicker")) Picker->SetPresentationOptions({TEXT("Borderless"),TEXT("Fullscreen"),TEXT("Windowed")});
            else if(FString(Definition.Name)==TEXT("FrameRatePicker")) Picker->SetPresentationOptions({TEXT("400 FPS"),TEXT("60 FPS"),TEXT("120 FPS")});
            else if(FString(Definition.Name)==TEXT("LanguagePicker")) Picker->SetPresentationOptions({TEXT("Русский"),TEXT("English"),TEXT("Deutsch"),TEXT("日本語")});
            else if(FString(Definition.Name)==TEXT("VoiceMode")) Picker->SetPresentationOptions({Localized(TEXT("UI.Voice.PushToTalk"),LOCTEXT("PTT","Нажми, чтобы говорить")).ToString(),Localized(TEXT("UI.Voice.OpenMic"),LOCTEXT("Mic","Открытый микрофон")).ToString()});
            else if(FString(Definition.Name)==TEXT("VoicePlayer")) Picker->SetPresentationOptions({LOCTEXT("PlayerSample","ИГРОК").ToString()});
            else Picker->SetPresentationOptions({Localized(TEXT("UI.MainMenu.Option.Quality.High"),LOCTEXT("High","Высоко")).ToString()});
            Control=Picker;
        } else if(Definition.Kind==1) {
            auto* Slider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),Name);
            Slider->SetMinValue(Definition.Min);Slider->SetMaxValue(Definition.Max);Slider->SetStepSize(FString(Definition.Name).Contains(TEXT("RenderScale"))?1.f:.01f);Slider->SetValue(Definition.Value);
            Slider->SetIndentHandle(true);Slider->SetSliderBarColor(FLinearColor::White);Slider->SetSliderHandleColor(FLinearColor::White);
            auto Style=Slider->GetWidgetStyle();
            Style.NormalBarImage=FSlateRoundedBoxBrush(MCUnifiedSettingsPrivate_Paper,9,MCUnifiedSettingsPrivate_Navy,3);
            Style.HoveredBarImage=Style.NormalBarImage;Style.DisabledBarImage=Style.NormalBarImage;
            Style.NormalThumbImage=FSlateRoundedBoxBrush(MCUnifiedSettingsPrivate_Paper,8,MCUnifiedSettingsPrivate_Navy,3);Style.NormalThumbImage.ImageSize=FVector2D(66,34);
            Style.HoveredThumbImage=Style.NormalThumbImage;Style.DisabledThumbImage=Style.NormalThumbImage;Style.BarThickness=18;
            Slider->SetWidgetStyle(Style);
            auto* Overlay=WidgetTree->ConstructWidget<UOverlay>();auto* SliderCell=Overlay->AddChildToOverlay(Slider);SliderCell->SetHorizontalAlignment(HAlign_Fill);SliderCell->SetVerticalAlignment(VAlign_Center);
            auto* Value=AddText(FName(*(Name.ToString()+TEXT("Value"))),NAME_None,TEXT("70%"),18);
            TextBindings.Remove(Value->GetFName());Value->SetColorAndOpacity(MCUnifiedSettingsPrivate_Navy);Value->SetAutoWrapText(false);Value->SetVisibility(ESlateVisibility::HitTestInvisible);
            // The value is an independent editable label in the same overlay as the thumb.
            auto* ValueSlot=Overlay->AddChildToOverlay(Value);ValueSlot->SetVerticalAlignment(VAlign_Center);ValueSlot->SetHorizontalAlignment(HAlign_Center);
            Control=Overlay;
        } else if(Definition.Kind==2) {
            auto* Check=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),Name);
            Check->SetContent(AddText(LabelName,Key,Definition.Copy,19));Control=Check;
        } else if(Definition.Kind==3) {
            auto* Button=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
            Button->Kind=EMCUniversalUIKind::Button;Button->bCustomColors=true;Button->bUseThemeShape=false;
            Button->CustomFill=MCUnifiedSettingsPrivate_Paper;Button->CustomOutline=MCUnifiedSettingsPrivate_Navy;Button->CustomText=MCUnifiedSettingsPrivate_Navy;Button->CustomShadow=MCUnifiedSettingsPrivate_Navy;
            Button->CornerRadius=14;Button->OutlineWidth=3;Button->ShadowOffset={0,3};Button->FontSize=18;
            Button->LabelKey=Key;Button->Label=FText::AsCultureInvariant(Definition.Copy);Button->ContentPadding=FMargin(10,7);
            Button->Initialize();Button->BuildPresentationTree();Control=Button;
        } else {
            auto* Meter=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),Name);
            Meter->SetPercent(.4f);Control=Meter;
        }
        Width->SetContent(Control);auto* Cell=Body->AddChildToVerticalBox(Width);Cell->SetHorizontalAlignment(HAlign_Center);Cell->SetPadding(FMargin(4,0,4,18));
    }
    auto* Status=AddText(TEXT("SettingsV2Status"),NAME_None,TEXT(""),15);TextBindings.Remove(Status->GetFName());
    Column->AddChildToVerticalBox(Status)->SetPadding(FMargin(12,2,12,8));
    auto* Apply=AddAction(TEXT("SettingsV2Apply"),TEXT("UI.Settings.Apply"),TEXT("ПРИМЕНИТЬ И СОХРАНИТЬ"));
    auto* ApplyCell=Column->AddChildToVerticalBox(Apply);ApplyCell->SetHorizontalAlignment(HAlign_Center);ApplyCell->SetPadding(FMargin(10,8,16,14));
    auto* Back=AddAction(TEXT("SettingsV2Back"),TEXT("UI.Settings.Back"),TEXT("НАЗАД"));
    auto* BackCell=Column->AddChildToVerticalBox(Back);BackCell->SetHorizontalAlignment(HAlign_Center);BackCell->SetPadding(FMargin(10,0,16,10));
    // Confirmation belongs to this root too; all of its shapes size to content.
    auto* Confirm=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("SettingsV2VideoConfirm"));
    auto* ConfirmCell=Root->AddChildToOverlay(Confirm);ConfirmCell->SetHorizontalAlignment(HAlign_Fill);ConfirmCell->SetVerticalAlignment(VAlign_Fill);
    auto* Scrim=WidgetTree->ConstructWidget<UBorder>();Scrim->SetBrushColor(FLinearColor(0,0,0,.72f));
    auto* ScrimCell=Confirm->AddChildToOverlay(Scrim);ScrimCell->SetHorizontalAlignment(HAlign_Fill);ScrimCell->SetVerticalAlignment(VAlign_Fill);
    auto* ConfirmWidth=WidgetTree->ConstructWidget<USizeBox>();ConfirmWidth->SetMinDesiredWidth(520);ConfirmWidth->SetMaxDesiredWidth(900);
    auto* Frame=Shape(TEXT("SettingsV2VideoFrame"),MCUnifiedSettingsPrivate_Inner,20,{5,8});Frame->ContentPadding=FMargin(30);Frame->ApplyTheme();ConfirmWidth->SetContent(Frame);
    auto* FrameCell=Confirm->AddChildToOverlay(ConfirmWidth);FrameCell->SetHorizontalAlignment(HAlign_Center);FrameCell->SetVerticalAlignment(VAlign_Center);FrameCell->SetPadding(FMargin(16));
    auto* ConfirmColumn=WidgetTree->ConstructWidget<UVerticalBox>();Frame->SetContentForSlot(TEXT("Content"),ConfirmColumn);
    ConfirmColumn->AddChildToVerticalBox(AddText(TEXT("SettingsV2VideoMessage"),TEXT("UI.Menu.VideoConfirmBody"),
        TEXT("Если изображение пропало, предыдущий режим вернётся автоматически через 15 секунд."),22))->SetPadding(FMargin(0,0,0,14));
    auto* Countdown=AddText(TEXT("SettingsV2VideoCountdown"),NAME_None,TEXT(""),18);TextBindings.Remove(Countdown->GetFName());
    ConfirmColumn->AddChildToVerticalBox(Countdown)->SetPadding(FMargin(0,0,0,18));
    auto* ConfirmActions=WidgetTree->ConstructWidget<UWrapBox>();ConfirmActions->SetExplicitWrapSize(true);ConfirmActions->SetWrapSize(800);
    ConfirmActions->SetInnerSlotPadding({20,16});ConfirmActions->SetHorizontalAlignment(HAlign_Center);
    ConfirmColumn->AddChildToVerticalBox(ConfirmActions)->SetHorizontalAlignment(HAlign_Center);
    ConfirmActions->AddChildToWrapBox(AddAction(TEXT("SettingsV2KeepVideo"),TEXT("UI.MainMenu.Action.KeepSettings"),TEXT("СОХРАНИТЬ")));
    ConfirmActions->AddChildToWrapBox(AddAction(TEXT("SettingsV2RevertVideo"),TEXT("UI.MainMenu.Action.RevertSettings"),TEXT("ВЕРНУТЬ")));
    Confirm->SetVisibility(ESlateVisibility::Collapsed);
    BindControls();SelectTab(PreviewSettingsTab);RefreshValueLabels();UpdateLayout();
}

void UMCUnifiedSettingsWidget::BindControls()
{
    if(!WidgetTree) return;
    // Adapters are transient. Rebind after loading a saved authored tree.
    for(UMCUnifiedSettingsBinding* Binding:Bindings) if(Binding) {
        if(auto* Picker=WidgetTree->FindWidget<UComboBoxString>(Binding->ViewWidgetName)) Picker->OnSelectionChanged.RemoveDynamic(Binding,&UMCUnifiedSettingsBinding::PickerChanged);
        if(auto* Slider=WidgetTree->FindWidget<USlider>(Binding->ViewWidgetName)) Slider->OnValueChanged.RemoveDynamic(Binding,&UMCUnifiedSettingsBinding::SliderChanged);
        if(auto* Check=WidgetTree->FindWidget<UCheckBox>(Binding->ViewWidgetName)) Check->OnCheckStateChanged.RemoveDynamic(Binding,&UMCUnifiedSettingsBinding::CheckChanged);
        if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(Binding->ViewWidgetName)) Button->OnClicked.RemoveDynamic(Binding,&UMCUnifiedSettingsBinding::Clicked);
    }
    Bindings.Reset();
    for(const auto& Definition:MCUnifiedSettingsPrivate_Controls) {
        auto* Control=WidgetTree->FindWidget(MCUnifiedSettingsPrivate_ViewName(Definition.Name));if(!Control) continue;
        auto* Binding=NewObject<UMCUnifiedSettingsBinding>(this);Binding->Owner=this;Binding->SourceName=Definition.Name;Binding->ViewWidgetName=MCUnifiedSettingsPrivate_ViewName(Definition.Name);Bindings.Add(Binding);
        if(auto* Picker=Cast<UComboBoxString>(Control)) {
            Picker->OnSelectionChanged.AddUniqueDynamic(Binding,&UMCUnifiedSettingsBinding::PickerChanged);
            Picker->OnGenerateWidgetEvent.Unbind();
            if(auto* MenuPicker=Cast<UMCMainMenuPicker>(Picker)) MenuPicker->SetPresentationTextCentered(true);
        }
        if(auto* Slider=Cast<USlider>(Control)) Slider->OnValueChanged.AddUniqueDynamic(Binding,&UMCUnifiedSettingsBinding::SliderChanged);
        if(auto* Check=Cast<UCheckBox>(Control)) Check->OnCheckStateChanged.AddUniqueDynamic(Binding,&UMCUnifiedSettingsBinding::CheckChanged);
        if(auto* Button=Cast<UMCUniversalUIWidget>(Control)) Button->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedSettingsBinding::Clicked);
    }
    struct FAction {const TCHAR* Name;EMCMainMenuAction Action;};
    const FAction Actions[]={{TEXT("SettingsV2Apply"),EMCMainMenuAction::ApplySettings},{TEXT("SettingsV2Back"),EMCMainMenuAction::Back},
        {TEXT("SettingsV2KeepVideo"),EMCMainMenuAction::KeepSettings},{TEXT("SettingsV2RevertVideo"),EMCMainMenuAction::RevertSettings}};
    auto BindAction=[&](const TCHAR* Name,EMCMainMenuAction Action) {
        if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(Name)) {
            auto* Binding=NewObject<UMCUnifiedSettingsBinding>(this);Binding->Owner=this;Binding->Action=Action;Binding->ViewWidgetName=Name;Bindings.Add(Binding);
            Button->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedSettingsBinding::Clicked);
        }
    };
    for(const auto& Action:Actions) BindAction(Action.Name,Action.Action);
    for(int32 I=0;I<5;++I) BindAction(*FString::Printf(TEXT("SettingsV2Tab%d"),I),EMCMainMenuAction(int32(EMCMainMenuAction::SettingsGraphics)+I));
}

void UMCUnifiedSettingsWidget::SetBackend(UMCMainMenuWidget* Menu)
{
    if(Backend.Get()!=Menu) {Backend=Menu;RefreshFromBackend();}
}
UWidget* UMCUnifiedSettingsWidget::SourceControl(FName Name) const {return Backend.IsValid()?Backend->GetSettingsPresentationControl(Name):nullptr;}
void UMCUnifiedSettingsWidget::SelectTab(int32 Index)
{
    Index=FMath::Clamp(Index,0,4);
    if(WidgetTree) {
        if(auto* Pages=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("SettingsV2Sections"))) Pages->SetActiveWidgetIndex(Index);
        for(int32 I=0;I<5;++I) if(auto* Tab=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(*FString::Printf(TEXT("SettingsV2Tab%d"),I)))) {
            Tab->CustomFill=I==Index?MCUnifiedSettingsPrivate_Blue:MCUnifiedSettingsPrivate_Paper;Tab->CustomText=I==Index?MCUnifiedSettingsPrivate_Paper:MCUnifiedSettingsPrivate_Navy;Tab->ApplyTheme();
        }
    }
}
void UMCUnifiedSettingsWidget::RefreshFromBackend()
{
    if(!WidgetTree || !Backend.IsValid() || IsDesignTime()) return;
    Backend->RefreshSettingsVoicePresentation();
    TGuardValue<bool> Guard(bSynchronizing,true);
    SelectTab(Backend->GetActiveSettingsTab());
    for(const auto& Definition:MCUnifiedSettingsPrivate_Controls) {
        auto* Original=SourceControl(Definition.Name);auto* View=WidgetTree->FindWidget(MCUnifiedSettingsPrivate_ViewName(Definition.Name));
        if(!View) continue;
        View->SetIsEnabled(Original && Original->GetIsEnabled());
        if(auto* To=Cast<UComboBoxString>(View)) if(auto* From=Cast<UComboBoxString>(Original)) {
            bool Same=To->GetOptionCount()==From->GetOptionCount();
            if(Same) for(int32 I=0;I<From->GetOptionCount();++I) if(To->GetOptionAtIndex(I)!=From->GetOptionAtIndex(I)) {Same=false;break;}
            if(!Same) {To->ClearOptions();for(int32 I=0;I<From->GetOptionCount();++I) To->AddOption(From->GetOptionAtIndex(I));}
            if(To->GetSelectedIndex()!=From->GetSelectedIndex()) To->SetSelectedIndex(From->GetSelectedIndex());
        }
        if(auto* To=Cast<USlider>(View)) if(auto* From=Cast<USlider>(Original)) {
            To->SetMinValue(From->GetMinValue());To->SetMaxValue(From->GetMaxValue());To->SetStepSize(From->GetStepSize());To->SetValue(From->GetValue());
        }
        if(auto* To=Cast<UCheckBox>(View)) if(auto* From=Cast<UCheckBox>(Original)) To->SetCheckedState(From->GetCheckedState());
        if(auto* To=Cast<UTextBlock>(View)) if(auto* From=Cast<UTextBlock>(Original)) To->SetText(From->GetText());
        if(auto* To=Cast<UProgressBar>(View)) if(auto* From=Cast<UProgressBar>(Original)) To->SetPercent(From->GetPercent());
        if(Definition.Kind==3) if(auto* To=Cast<UMCUniversalUIWidget>(View))
            if(auto* From=Cast<UTextBlock>(SourceControl(FName(*(FString(Definition.Name)+TEXT("Label")))))) {
                To->LabelKey=NAME_None;To->Label=From->GetText();To->ApplyTheme();
            }
    }
    if(auto* Status=WidgetTree->FindWidget<UTextBlock>(TEXT("SettingsV2Status")))
        if(auto* From=Cast<UTextBlock>(SourceControl(TEXT("Status")))) Status->SetText(From->GetText());
    const bool Confirm=Backend->GetCurrentMenuPage()==EMCMainMenuPage::VideoConfirm;
    if(auto* Modal=WidgetTree->FindWidget(TEXT("SettingsV2VideoConfirm"))) Modal->SetVisibility(Confirm?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(auto* Pages=WidgetTree->FindWidget(TEXT("SettingsV2Sections"))) Pages->SetIsEnabled(!Confirm);
    for(const TCHAR* Name:{TEXT("SettingsV2Apply"),TEXT("SettingsV2Back")})
        if(auto* Button=WidgetTree->FindWidget(Name)) Button->SetIsEnabled(!Confirm);
    for(int32 I=0;I<5;++I)
        if(auto* Tab=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("SettingsV2Tab%d"),I)))) Tab->SetIsEnabled(!Confirm);
    if(Confirm!=bVideoModalActive) {
        bVideoModalActive=Confirm;
        if(GetOwningPlayer()) if(auto* Focus=GetDefaultFocusTarget()) Focus->SetUserFocus(GetOwningPlayer());
    }
    if(auto* Countdown=WidgetTree->FindWidget<UTextBlock>(TEXT("SettingsV2VideoCountdown")))
        Countdown->SetText(UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Menu.VideoCountdown"),LOCTEXT("VideoCountdown","Автоматический возврат: {Seconds} сек.")),
            {{TEXT("Seconds"),UMCUIDataLibrary::NumberForCulture(FMath::CeilToInt(Backend->GetVideoConfirmationSecondsRemaining()),PreviewCulture)}},PreviewCulture));
    RefreshValueLabels();UpdateLayout();
}
void UMCUnifiedSettingsWidget::RefreshValueLabels()
{
    if(!WidgetTree) return;
    for(const auto& Definition:MCUnifiedSettingsPrivate_Controls) if(Definition.Kind==1) {
        const FName Name=MCUnifiedSettingsPrivate_ViewName(Definition.Name);
        auto* Slider=WidgetTree->FindWidget<USlider>(Name);
        auto* Label=WidgetTree->FindWidget<UTextBlock>(FName(*(Name.ToString()+TEXT("Value"))));
        if(!Slider || !Label) continue;
        const bool Scale=FString(Definition.Name)==TEXT("RenderScaleSlider");
        const bool Sensitivity=FString(Definition.Name)==TEXT("MouseSlider") || FString(Definition.Name)==TEXT("GamepadSlider");
        const float Number=Slider->GetValue()*(Scale || Sensitivity?1.f:100.f);
        const FText Value=Sensitivity?UMCUIDataLibrary::DecimalForCulture(Number,2,PreviewCulture):
            UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt64(Number),PreviewCulture);
        Label->SetText(FText::FromString(Value.ToString()+(Sensitivity?TEXT(""):TEXT("%"))));
        Label->SetColorAndOpacity(MCUnifiedSettingsPrivate_Navy);
        // Measure the actual fixed-size glyphs; the handle grows to the value,
        // including 100% / 300%, rather than squeezing or clipping its text.
        FVector2D TextSize(50,24);
        if(FSlateApplication::IsInitialized())
            TextSize=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label->GetText(),Label->GetFont());
        const float ThumbWidth=FMath::Max(52.f,float(TextSize.X)+18.f);
        const float ThumbHeight=FMath::Max(34.f,float(TextSize.Y)+8.f);
        Label->SetMinDesiredWidth(ThumbWidth);
        auto Style=Slider->GetWidgetStyle();
        if(!FMath::IsNearlyEqual(float(Style.NormalThumbImage.ImageSize.X),ThumbWidth,.5f) ||
           !FMath::IsNearlyEqual(float(Style.NormalThumbImage.ImageSize.Y),ThumbHeight,.5f)) {
            Style.NormalThumbImage.ImageSize=FVector2D(ThumbWidth,ThumbHeight);
            Style.HoveredThumbImage.ImageSize=FVector2D(ThumbWidth,ThumbHeight);
            Style.DisabledThumbImage.ImageSize=FVector2D(ThumbWidth,ThumbHeight);
            Slider->SetWidgetStyle(Style);
        }
        if(auto* Cell=Cast<UOverlaySlot>(Label->Slot)) {
            const float Fraction=FMath::Clamp((Slider->GetValue()-Slider->GetMinValue())/FMath::Max(.001f,Slider->GetMaxValue()-Slider->GetMinValue()),0.f,1.f);
            const float Width=Slider->GetPaintSpaceGeometry().GetLocalSize().X;
            Cell->SetHorizontalAlignment(HAlign_Left);
            // Match SSlider::OnPaint: IndentHandle adds one extra thumb
            // width to its travel padding, not just half a width at the ends.
            const float Indentation=Slider->HasIndentHandle()?ThumbWidth:0.f;
            const float Left=.5f*Indentation+FMath::Max(0.f,(Width>0?Width:400.f)-Indentation-ThumbWidth)*Fraction;
            Cell->SetPadding(FMargin(Left,0,0,0));
        }
    }
}
void UMCUnifiedSettingsWidget::ForwardPicker(FName Name,const FString& Value)
{
    if(bSynchronizing || IsDesignTime()) return;
    if(auto* Source=Cast<UComboBoxString>(SourceControl(Name))) Source->SetSelectedOption(Value);
    RefreshFromBackend();
}
void UMCUnifiedSettingsWidget::ForwardSlider(FName Name,float Value)
{
    if(bSynchronizing || IsDesignTime()) return;
    if(auto* Source=Cast<USlider>(SourceControl(Name))) {Source->SetValue(Value);Source->OnValueChanged.Broadcast(Value);}
    RefreshFromBackend();
}
void UMCUnifiedSettingsWidget::ForwardCheck(FName Name,bool Value)
{
    if(bSynchronizing || IsDesignTime()) return;
    if(auto* Source=Cast<UCheckBox>(SourceControl(Name))) {Source->SetIsChecked(Value);Source->OnCheckStateChanged.Broadcast(Value);}
    RefreshFromBackend();
}
void UMCUnifiedSettingsWidget::ForwardAction(FName Name,EMCMainMenuAction Action)
{
    if(IsDesignTime()) {
        if(Action>=EMCMainMenuAction::SettingsGraphics && Action<=EMCMainMenuAction::SettingsVoice)
            {PreviewSettingsTab=int32(Action)-int32(EMCMainMenuAction::SettingsGraphics);SelectTab(PreviewSettingsTab);}
        return;
    }
    if(!Backend.IsValid()) return;
    if(!Name.IsNone()) {
        if(auto* Source=Cast<UButton>(SourceControl(Name))) Source->OnClicked.Broadcast();
        if(Name==TEXT("VoiceKey")) SetKeyboardFocus();
    } else Backend->HandleAction(Action);
    RefreshFromBackend();
}
bool UMCUnifiedSettingsWidget::RouteRebindKey(FKey Key)
{
    return !IsDesignTime() && Backend.IsValid() && Backend->RouteSettingsVoiceKey(Key);
}
bool UMCUnifiedSettingsWidget::IsVoiceRebinding() const {return Backend.IsValid() && Backend->IsSettingsVoiceRebinding();}
UWidget* UMCUnifiedSettingsWidget::GetDefaultFocusTarget() const
{
    if(!WidgetTree) return nullptr;
    if(Backend.IsValid() && Backend->GetCurrentMenuPage()==EMCMainMenuPage::VideoConfirm)
        if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("SettingsV2KeepVideo"))) return Button->WidgetTree?Button->WidgetTree->FindWidget(TEXT("UniversalSurface")):Button;
    const int32 Index=Backend.IsValid()?Backend->GetActiveSettingsTab():PreviewSettingsTab;
    if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(*FString::Printf(TEXT("SettingsV2Tab%d"),Index))))
        return Button->WidgetTree?Button->WidgetTree->FindWidget(TEXT("UniversalSurface")):Button;
    return const_cast<UMCUnifiedSettingsWidget*>(this);
}
void UMCUnifiedSettingsWidget::UpdateLayout()
{
    if(!WidgetTree) return;
    const FVector2D Area=GetPaintSpaceGeometry().GetLocalSize();
    if(Area.X<=1 || Area.Y<=1) return;
    LastLayoutViewport=Area;
    const float Width=FMath::Max(1.f,FMath::Min(1000.f,float(Area.X)-12.f));
    const float AvailableHeight=FMath::Max(1.f,float(Area.Y)-12.f);
    auto* Size=WidgetTree->FindWidget<USizeBox>(TEXT("SettingsV2Size"));
    auto* Column=WidgetTree->FindWidget<UVerticalBox>(TEXT("SettingsV2Column"));
    auto* Viewport=WidgetTree->FindWidget<USizeBox>(TEXT("SettingsV2ScrollViewport"));
    auto* Outer=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("SettingsV2Outer"));
    auto* Inner=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("SettingsV2Inner"));
    if(!Size || !Column || !Viewport || !Outer || !Inner) return;
    Size->ClearMinDesiredWidth();Size->SetMaxDesiredWidth(Width);Size->ClearMinDesiredHeight();
    if(auto* Cell=Cast<UOverlaySlot>(Column->Slot)) Cell->SetHorizontalAlignment(HAlign_Center);
    if(auto* Tabs=WidgetTree->FindWidget<UWrapBox>(TEXT("SettingsV2Tabs"))) {
        Tabs->SetExplicitWrapSize(true);Tabs->SetWrapSize(Width+1.f);
        // Equal fifths reproduce the PSD tabs. Their full-size labels may wrap
        // vertically for a translation, without leaving an unused right edge.
        for(int32 I=0;I<5;++I) if(auto* Tab=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(*FString::Printf(TEXT("SettingsV2Tab%d"),I)))) {
            auto* Bounds=WidgetTree->FindWidget<USizeBox>(FName(*FString::Printf(TEXT("SettingsV2Tab%dBounds"),I)));
            if(!Bounds && Tab->GetParent()==Tabs) {
                Tab->RemoveFromParent();Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*FString::Printf(TEXT("SettingsV2Tab%dBounds"),I)));
                Bounds->SetContent(Tab);Tabs->AddChildToWrapBox(Bounds)->SetVerticalAlignment(VAlign_Bottom);
            }
            if(Bounds) {Bounds->ClearMinDesiredWidth();Bounds->ClearMaxDesiredWidth();Bounds->SetWidthOverride(Width/5.f);}
            Tab->ContentPadding=FMargin(8,15);Tab->ApplyTheme();
            if(Tab->WidgetTree) if(auto* Caption=Tab->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {
                Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(FMath::Max(1.f,Width/5.f-20.f));Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
            }
        }
    }
    const float ContentWidth=FMath::Max(1.f,Width-8.f-Outer->ContentPadding.Left-Outer->ContentPadding.Right-Inner->ContentPadding.Left-Inner->ContentPadding.Right);
    Viewport->SetWidthOverride(ContentWidth);Viewport->ClearMinDesiredWidth();Viewport->ClearMaxDesiredWidth();
    // The PSD fields occupy roughly three fifths of the inner panel. Keep
    // their centers aligned; only unusually long values earn more width.
    for(const auto& Definition:MCUnifiedSettingsPrivate_Controls) {
        auto* Control=WidgetTree->FindWidget(MCUnifiedSettingsPrivate_ViewName(Definition.Name));
        UWidget* ControlContainer=Control;
        if(Definition.Kind==1 && Control && Cast<UOverlay>(Control->GetParent())) ControlContainer=Control->GetParent();
        if(auto* Bounds=ControlContainer?Cast<USizeBox>(ControlContainer->GetParent()):nullptr) {
            const float Maximum=FMath::Max(1.f,ContentWidth-8.f);
            float ControlWidth=ContentWidth*.64f;
            if(auto* Picker=Cast<UComboBoxString>(Control);Picker && FSlateApplication::IsInitialized()) {
                const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
                ControlWidth=FMath::Max(ControlWidth,float(Measure->Measure(Picker->GetSelectedOption(),Picker->GetFont()).X)+44.f);
            }
            Bounds->ClearMinDesiredWidth();Bounds->SetMaxDesiredWidth(Maximum);
            const float FieldWidth=FMath::Min(Maximum,ControlWidth);
            Bounds->SetWidthOverride(FieldWidth);
            if(auto* Cell=Cast<UVerticalBoxSlot>(Bounds->Slot)) Cell->SetHorizontalAlignment(HAlign_Center);
        }
    }
    WidgetTree->ForEachWidget([&](UWidget* Widget) {
        if(auto* Text=Cast<UTextBlock>(Widget)) {
            Text->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
            Text->SetAutoWrapText(false);Text->SetWrapTextAt(FMath::Max(1.f,ContentWidth-32.f));
        }
        if(auto* Form=Cast<UMCUniversalUIWidget>(Widget);Form && Form->Kind==EMCUniversalUIKind::Button && Form->WidgetTree) {
            if(auto* Caption=Form->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {
                Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetAutoWrapText(false);
                const bool IsTab=Form->GetName().StartsWith(TEXT("SettingsV2Tab"));
                const float CaptionWidth=IsTab?Width/5.f:Width;
                Caption->SetWrapTextAt(FMath::Max(1.f,CaptionWidth-Form->ContentPadding.Left-Form->ContentPadding.Right-12.f));
            }
        }
    });
    const float Scale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    auto MeasuredHeight=[&](UWidget* Widget) {
        if(!Widget || !Widget->IsVisible()) return 0.f;
        const auto Slate=Widget->TakeWidget();Slate->MarkPrepassAsDirty();Slate->SlatePrepass(Scale);
        float Height=FMath::CeilToFloat(Widget->GetDesiredSize().Y);
        if(const auto* Cell=Cast<UVerticalBoxSlot>(Widget->Slot)) Height+=Cell->GetPadding().Top+Cell->GetPadding().Bottom;
        return Height;
    };
    float Reserved=Outer->ContentPadding.Top+Outer->ContentPadding.Bottom+Inner->ContentPadding.Top+Inner->ContentPadding.Bottom;
    if(const auto* Cell=Cast<UVerticalBoxSlot>(Outer->Slot)) Reserved+=Cell->GetPadding().Top+Cell->GetPadding().Bottom;
    for(const TCHAR* Name:{TEXT("SettingsV2Tabs"),TEXT("SettingsV2Status"),TEXT("SettingsV2Apply"),TEXT("SettingsV2Back")}) Reserved+=MeasuredHeight(WidgetTree->FindWidget(Name));
    const float ScrollHeight=FMath::Max(1.f,AvailableHeight-Reserved);
    Viewport->ClearMinDesiredHeight();Viewport->ClearMaxDesiredHeight();Viewport->SetHeightOverride(ScrollHeight);
    for(int32 I=0;I<5;++I) if(auto* Scroll=WidgetTree->FindWidget<UScrollBox>(FName(*FString::Printf(TEXT("SettingsV2Scroll%d"),I)))) Scroll->SetAllowOverscroll(false);
    // The same available width applies to the confirmation's content-sized frame.
    if(auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("SettingsV2VideoFrame"))) {
        if(auto* Bounds=Cast<USizeBox>(Frame->GetParent())) {Bounds->ClearMinDesiredWidth();Bounds->SetMaxDesiredWidth(FMath::Max(1.f,Width-32.f));}
        if(auto* Body=Cast<UVerticalBox>(Frame->GetContentForSlot(TEXT("Content")))) for(int32 I=0;I<Body->GetChildrenCount();++I)
            if(auto* Row=Cast<UWrapBox>(Body->GetChildAt(I))) Row->SetWrapSize(FMath::Max(1.f,Width-92.f));
    }
}
void UMCUnifiedSettingsWidget::ApplyTheme() {Super::ApplyTheme();RefreshValueLabels();UpdateLayout();}
void UMCUnifiedSettingsWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    if(IsDesignTime() && WidgetTree) {
        // Saved picker examples are strings, not TextBindings. Re-resolve only
        // this transient fixture's semantic options after the requested culture changes.
        TGuardValue<bool> PreviewSync(bSynchronizing,true);
        auto Seed=[&](const TCHAR* Name,const TArray<FString>& Options,int32 SampleIndex) {
            if(auto* Picker=WidgetTree->FindWidget<UMCMainMenuPicker>(MCUnifiedSettingsPrivate_ViewName(Name)))
                Picker->SetPresentationOptions(Options,FMath::Clamp(SampleIndex,0,FMath::Max(0,Options.Num()-1)));
        };
        auto Copy=[&](FName Key,const TCHAR* Fallback) {return Localized(Key,FText::AsCultureInvariant(Fallback)).ToString();};
        auto* Mode=WidgetTree->FindWidget<UMCMainMenuPicker>(MCUnifiedSettingsPrivate_ViewName(TEXT("WindowModePicker")));
        Seed(TEXT("WindowModePicker"),{Copy(TEXT("UI.MainMenu.Option.Borderless"),TEXT("Borderless")),Copy(TEXT("UI.MainMenu.Option.Fullscreen"),TEXT("Fullscreen")),Copy(TEXT("UI.MainMenu.Option.Windowed"),TEXT("Windowed"))},Mode?FMath::Max(0,Mode->GetSelectedIndex()):0);
        const TCHAR* LevelNames[]={TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")};
        TArray<FString> Levels;for(const TCHAR* Name:LevelNames) Levels.Add(Copy(FName(*(FString(TEXT("UI.MainMenu.Option.Quality."))+Name)),Name));
        for(const TCHAR* Name:{TEXT("QualityPicker"),TEXT("TexturePicker"),TEXT("ShadowPicker"),TEXT("EffectsPicker"),TEXT("PostProcessPicker"),TEXT("AAPicker"),TEXT("ViewDistancePicker")}) {
            auto* Picker=WidgetTree->FindWidget<UMCMainMenuPicker>(MCUnifiedSettingsPrivate_ViewName(Name));
            // The former single-option High specimen becomes index2; a selected
            // real quality index in the transient fixture stays selected.
            const int32 Index=Picker && Picker->GetOptionCount()>1?FMath::Max(0,Picker->GetSelectedIndex()):2;
            auto Options=Levels;if(FString(Name)==TEXT("QualityPicker")) Options.Add(Copy(TEXT("UI.MainMenu.Option.QualityCustom"),TEXT("Custom (keep current)")));
            Seed(Name,Options,Index);
        }
        auto* VoiceMode=WidgetTree->FindWidget<UMCMainMenuPicker>(MCUnifiedSettingsPrivate_ViewName(TEXT("VoiceMode")));
        Seed(TEXT("VoiceMode"),{Copy(TEXT("UI.Voice.PushToTalk"),TEXT("Push to talk")),Copy(TEXT("UI.Voice.OpenMic"),TEXT("Open microphone"))},VoiceMode?FMath::Max(0,VoiceMode->GetSelectedIndex()):0);
        Seed(TEXT("VoicePlayer"),{Copy(TEXT("UI.V2.Preview.Player"),TEXT("PLAYER"))},0);
    }
    SelectTab(PreviewSettingsTab);RefreshValueLabels();UpdateLayout();
}
void UMCUnifiedSettingsWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(!LastLayoutViewport.Equals(Geometry.GetLocalSize(),.5f)) UpdateLayout();
    if(IsDesignTime()) return;
    RefreshElapsed+=DeltaSeconds;if(RefreshElapsed<.1f) return;RefreshElapsed=0;RefreshFromBackend();
}
void UMCUnifiedSettingsWidget::NativeDestruct()
{
    // A presentation-only viewport re-add must retain the weak source bridge.
    // The original menu still owns pending controls and microphone interaction.
    Super::NativeDestruct();
}
FReply UMCUnifiedSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(RouteRebindKey(Event.GetKey())) {RefreshFromBackend();return FReply::Handled();}
    if(Backend.IsValid() && (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right))
        {Backend->RequestBack();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCUnifiedSettingsWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(RouteRebindKey(Event.GetEffectingButton())) {RefreshFromBackend();return FReply::Handled();}
    return Super::NativeOnPreviewMouseButtonDown(Geometry,Event);
}

#undef LOCTEXT_NAMESPACE

