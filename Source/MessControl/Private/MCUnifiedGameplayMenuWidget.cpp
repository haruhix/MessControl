#include "MCUnifiedGameplayMenuWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCMainMenuWidget.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CheckBox.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCUnifiedGameplayMenu"
namespace
{
    using MCUnifiedGameplayMenuPrivate_K=EMCGameplayMenuPresentationKind;
    FLinearColor MCUnifiedGameplayMenuPrivate_Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    const FLinearColor MCUnifiedGameplayMenuPrivate_Blue=MCUnifiedGameplayMenuPrivate_Color(TEXT("4C76AE")),MCUnifiedGameplayMenuPrivate_Navy=MCUnifiedGameplayMenuPrivate_Color(TEXT("16226B")),MCUnifiedGameplayMenuPrivate_Inner=MCUnifiedGameplayMenuPrivate_Color(TEXT("315D73"));
    const FLinearColor MCUnifiedGameplayMenuPrivate_Paper=MCUnifiedGameplayMenuPrivate_Color(TEXT("FFF9F8")),MCUnifiedGameplayMenuPrivate_Pink=MCUnifiedGameplayMenuPrivate_Color(TEXT("AE4C6C"));
    const TCHAR* MCUnifiedGameplayMenuPrivate_PageNames[]={TEXT("MenuPausePage"),TEXT("MenuSettingsPage"),TEXT("MenuCreditsPage"),TEXT("MenuLeavePage"),TEXT("MenuExitPage"),TEXT("MenuVideoConfirmPage"),TEXT("MenuConnectionPage"),TEXT("MenuAudioPage")};
    FName MCUnifiedGameplayMenuPrivate_ViewName(FName Name) {return FName(*(TEXT("GameplayMenuV2_")+Name.ToString()));}
    FName MCUnifiedGameplayMenuPrivate_PagePart(const TCHAR* Part,int32 Page) {return FName(*FString::Printf(TEXT("GameplayMenuV2%s%d"),Part,Page));}
    UButton* MCUnifiedGameplayMenuPrivate_InnerButton(UMCUniversalUIWidget* View) {return View && View->WidgetTree?View->WidgetTree->FindWidget<UButton>(TEXT("UniversalSurface")):nullptr;}
    struct MCUnifiedGameplayMenuPrivate_FDefinition {int32 Page;const TCHAR* Name;MCUnifiedGameplayMenuPrivate_K Kind;const TCHAR* Key;const TCHAR* Copy;bool Footer=false;};
    // Designer fixtures use the real source names. Runtime only reads live copy/controls.
    const MCUnifiedGameplayMenuPrivate_FDefinition MCUnifiedGameplayMenuPrivate_Definitions[]={
        {0,TEXT("MenuResume"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Resume"),TEXT("RESUME")},
        {0,TEXT("MenuPauseInvite"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("hub.invite"),TEXT("INVITE FRIENDS")},
        {0,TEXT("MenuEmotes"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Emotes"),TEXT("EMOTES AND SIGNALS")},
        {0,TEXT("MenuSettings"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Settings"),TEXT("SETTINGS")},
        {0,TEXT("MenuCredits"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Credits"),TEXT("CREDITS")},
        {0,TEXT("MenuLeave"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Leave"),TEXT("MAIN MENU")},
        {0,TEXT("MenuExit"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Exit"),TEXT("EXIT GAME")},
        {1,TEXT("MenuResolutionLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.Resolution"),TEXT("SCREEN RESOLUTION")},
        {1,TEXT("MenuResolutionPicker"),MCUnifiedGameplayMenuPrivate_K::Picker,TEXT(""),TEXT("")},
        {1,TEXT("MenuModeLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.WindowMode"),TEXT("SCREEN MODE")},
        {1,TEXT("MenuModePicker"),MCUnifiedGameplayMenuPrivate_K::Picker,TEXT(""),TEXT("")},
        {1,TEXT("MenuQualityLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.Quality"),TEXT("GRAPHICS QUALITY")},
        {1,TEXT("MenuQualityPicker"),MCUnifiedGameplayMenuPrivate_K::Picker,TEXT(""),TEXT("")},
        {1,TEXT("MenuVSync"),MCUnifiedGameplayMenuPrivate_K::Check,TEXT("UI.Menu.VSync"),TEXT("VSYNC")},
        {1,TEXT("MenuCameraSmoothing"),MCUnifiedGameplayMenuPrivate_K::Check,TEXT("UI.Settings.CameraSmoothing"),TEXT("CAMERA SMOOTHING")},
        {1,TEXT("MenuAudioSettings"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.AudioSettings"),TEXT("AUDIO AND MICROPHONE")},
        {1,TEXT("MenuApply"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Apply"),TEXT("APPLY AND SAVE"),true},
        {1,TEXT("MenuSettingsBack"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Common.Back"),TEXT("BACK"),true},
        {2,TEXT("MenuCreditsBody"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.CreditsBody"),TEXT("Mess Control")},
        {2,TEXT("MenuCreditsEngine"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.CreditsEngine"),TEXT("Unreal Engine")},
        {2,TEXT("MenuCreditsBack"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Common.Back"),TEXT("BACK"),true},
        {3,TEXT("MenuLeaveBody"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.LeaveHostBody"),TEXT("As host, leaving closes the room for the other players.")},
        {3,TEXT("MenuLeaveCancel"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Common.Cancel"),TEXT("CANCEL"),true},
        {3,TEXT("MenuLeaveConfirm"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.LeaveConfirm"),TEXT("LEAVE TO MAIN MENU"),true},
        {4,TEXT("MenuExitCancel"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Common.Cancel"),TEXT("CANCEL"),true},
        {4,TEXT("MenuExitConfirm"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Exit"),TEXT("EXIT GAME"),true},
        {5,TEXT("MenuVideoConfirmBody"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.VideoConfirmBody"),TEXT("The previous screen mode returns automatically after 15 seconds.")},
        {5,TEXT("MenuVideoKeep"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.VideoKeep"),TEXT("KEEP SETTINGS"),true},
        {5,TEXT("MenuVideoRevert"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.VideoRevert"),TEXT("RESTORE PREVIOUS MODE"),true},
        {6,TEXT("MenuConnectionBody"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.ConnectionBody"),TEXT("Create a room or join your friends. Up to four players.")},
        {6,TEXT("MenuHost"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Host"),TEXT("CREATE ROOM")},
        {6,TEXT("MenuFindRooms"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.FindRooms"),TEXT("FIND ROOMS")},
        {6,TEXT("MenuRoomLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.Room"),TEXT("FRIENDS ROOM")},
        {6,TEXT("MenuRoomPicker"),MCUnifiedGameplayMenuPrivate_K::Picker,TEXT(""),TEXT("")},
        {6,TEXT("MenuJoinRoom"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.JoinRoom"),TEXT("JOIN ROOM")},
        {6,TEXT("MenuInvite"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.Invite"),TEXT("INVITE FRIENDS IN STEAM")},
        {6,TEXT("MenuAddressLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.Address"),TEXT("IP ADDRESS OR HOST NAME · PORT 7777")},
        {6,TEXT("MenuAddress"),MCUnifiedGameplayMenuPrivate_K::Editable,TEXT(""),TEXT("127.0.0.1")},
        {6,TEXT("MenuJoinAddress"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Menu.JoinAddress"),TEXT("CONNECT BY IP")},
        {6,TEXT("MenuConnectionBack"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Common.Back"),TEXT("BACK"),true},
        {7,TEXT("MenuAudioImmediateHint"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Menu.AudioImmediate"),TEXT("Audio and microphone changes are applied and saved immediately.")},
        {7,TEXT("MenuMasterSliderLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Settings.Master"),TEXT("MASTER VOLUME")},
        {7,TEXT("MenuMasterSlider"),MCUnifiedGameplayMenuPrivate_K::Slider,TEXT(""),TEXT("")},
        {7,TEXT("MenuMusicSliderLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Settings.Music"),TEXT("MUSIC")},
        {7,TEXT("MenuMusicSlider"),MCUnifiedGameplayMenuPrivate_K::Slider,TEXT(""),TEXT("")},
        {7,TEXT("MenuAmbienceSliderLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Settings.Ambience"),TEXT("AMBIENCE")},
        {7,TEXT("MenuAmbienceSlider"),MCUnifiedGameplayMenuPrivate_K::Slider,TEXT(""),TEXT("")},
        {7,TEXT("MenuSFXSliderLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Settings.SFX"),TEXT("SOUND EFFECTS")},
        {7,TEXT("MenuSFXSlider"),MCUnifiedGameplayMenuPrivate_K::Slider,TEXT(""),TEXT("")},
        {7,TEXT("MenuVoiceHeading"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Settings.Tab.Voice"),TEXT("VOICE")},
        {7,TEXT("VoiceEnabled"),MCUnifiedGameplayMenuPrivate_K::Check,TEXT("UI.Voice.Enabled"),TEXT("VOICE CHAT ENABLED")},
        {7,TEXT("VoiceModeLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Voice.Mode"),TEXT("MICROPHONE MODE")},
        {7,TEXT("VoiceMode"),MCUnifiedGameplayMenuPrivate_K::Picker,TEXT(""),TEXT("")},
        {7,TEXT("VoiceKey"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Voice.Key"),TEXT("BIND PUSH-TO-TALK KEY")},
        {7,TEXT("VoiceSelfMute"),MCUnifiedGameplayMenuPrivate_K::Check,TEXT("UI.Voice.SelfMute"),TEXT("MUTE MY MICROPHONE")},
        {7,TEXT("VoiceGainLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Voice.Gain"),TEXT("MICROPHONE GAIN")},
        {7,TEXT("VoiceGain"),MCUnifiedGameplayMenuPrivate_K::Slider,TEXT(""),TEXT("")},
        {7,TEXT("VoiceThresholdLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Voice.Threshold"),TEXT("VOICE ACTIVATION THRESHOLD")},
        {7,TEXT("VoiceThreshold"),MCUnifiedGameplayMenuPrivate_K::Slider,TEXT(""),TEXT("")},
        {7,TEXT("VoiceVolumeLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Voice.Volume"),TEXT("VOICE VOLUME")},
        {7,TEXT("VoiceVolume"),MCUnifiedGameplayMenuPrivate_K::Slider,TEXT(""),TEXT("")},
        {7,TEXT("VoiceDevice"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Voice.SystemDevice"),TEXT("SYSTEM MICROPHONE")},
        {7,TEXT("VoiceDeviceSettings"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Voice.DeviceSettings"),TEXT("WINDOWS MICROPHONE SETTINGS")},
        {7,TEXT("VoiceTest"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Voice.Test"),TEXT("TEST SAVED MICROPHONE SETTINGS")},
        {7,TEXT("VoiceInputMeter"),MCUnifiedGameplayMenuPrivate_K::Meter,TEXT(""),TEXT("")},
        {7,TEXT("VoicePlayersLabel"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Voice.Players"),TEXT("PARTICIPANT VOICE")},
        {7,TEXT("VoicePlayer"),MCUnifiedGameplayMenuPrivate_K::Picker,TEXT(""),TEXT("")},
        {7,TEXT("VoicePlayerMute"),MCUnifiedGameplayMenuPrivate_K::Check,TEXT("UI.Voice.PlayerMute"),TEXT("MUTE THIS PLAYER")},
        {7,TEXT("VoiceStatus"),MCUnifiedGameplayMenuPrivate_K::Text,TEXT("UI.Voice.Status"),TEXT("Voice is available in a network room.")},
        {7,TEXT("MenuAudioBack"),MCUnifiedGameplayMenuPrivate_K::Button,TEXT("UI.Common.Back"),TEXT("BACK"),true},
    };
}
void UMCUnifiedGameplayMenuBinding::Clicked() {if(Owner.IsValid()) Owner->ForwardButton(this);}
void UMCUnifiedGameplayMenuBinding::PickerChanged(FString Value,ESelectInfo::Type) {if(Owner.IsValid()) Owner->ForwardPicker(this,Value);}
void UMCUnifiedGameplayMenuBinding::SliderChanged(float Value) {if(Owner.IsValid()) Owner->ForwardSlider(this,Value);}
void UMCUnifiedGameplayMenuBinding::CheckChanged(bool Value) {if(Owner.IsValid()) Owner->ForwardCheck(this,Value);}
void UMCUnifiedGameplayMenuBinding::TextChanged(const FText& Value) {if(Owner.IsValid()) Owner->ForwardText(this,Value);}
void UMCUnifiedGameplayMenuBinding::TextCommitted(const FText& Value,ETextCommit::Type Commit) {if(Owner.IsValid()) Owner->ForwardTextCommit(this,Value,Commit);}
UMCUnifiedGameplayMenuWidget::UMCUnifiedGameplayMenuWidget() {bUseNativeLayout=false;bOverridePreviewUIVersion=true;SetIsFocusable(true);}
UMCUniversalUIWidget* UMCUnifiedGameplayMenuWidget::MakeShape(FName Name,FLinearColor Fill,bool bButton)
{
    auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Shape->Kind=bButton?EMCUniversalUIKind::Button:EMCUniversalUIKind::Panel;Shape->bUseNativeLayout=false;Shape->bUseThemeShape=false;Shape->bCustomColors=true;
    Shape->CustomFill=Fill;Shape->CustomText=MCUnifiedGameplayMenuPrivate_Paper;Shape->CustomOutline=MCUnifiedGameplayMenuPrivate_Navy;Shape->CustomShadow=MCUnifiedGameplayMenuPrivate_Navy;
    Shape->CornerRadius=18;Shape->OutlineWidth=3;Shape->ContentPadding=FMargin(20,12);Shape->ShadowOffset={5,7};
    Shape->Initialize();Shape->BuildPresentationTree();return Shape;
}
UTextBlock* UMCUnifiedGameplayMenuWidget::MakeText(FName Name,int32 Size)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Label->SetAutoWrapText(true);Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Label->SetJustification(ETextJustify::Center);
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);Label->SetColorAndOpacity(MCUnifiedGameplayMenuPrivate_Paper);Label->SetFont(GetResolvedTheme()->MakeGameplayFont(Size));
    BindStyle(Name,EMCUIColorToken::Cream,EMCUIStyleTarget::Text,Size,true);return Label;
}

void UMCUnifiedGameplayMenuWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(!WidgetTree->RootWidget) {
        auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("GameplayMenuV2Size"));Size->SetMinDesiredWidth(660);Size->SetMaxDesiredWidth(1000);WidgetTree->RootWidget=Size;
        auto* Frame=MakeShape(TEXT("GameplayMenuV2Frame"),MCUnifiedGameplayMenuPrivate_Blue);Frame->ContentPadding=FMargin(20);Frame->ApplyTheme();Size->SetContent(Frame);
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("GameplayMenuV2Column"));Frame->SetContentForSlot(TEXT("Content"),Column);
        auto* Header=MakeShape(TEXT("GameplayMenuV2Heading"),MCUnifiedGameplayMenuPrivate_Paper);Header->CustomText=MCUnifiedGameplayMenuPrivate_Pink;Header->FontSize=32;Header->ContentPadding=FMargin(28,9);Header->ApplyTheme();
        auto* HeaderSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("GameplayMenuV2HeadingBounds"));HeaderSize->SetMaxDesiredWidth(900);HeaderSize->SetContent(Header);
        auto* HeaderCell=Column->AddChildToVerticalBox(HeaderSize);HeaderCell->SetHorizontalAlignment(HAlign_Center);HeaderCell->SetPadding(FMargin(8,0,8,16));
        auto* Pages=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("GameplayMenuV2Pages"));Column->AddChildToVerticalBox(Pages);
        for(int32 I=0;I<8;++I) {
            auto* Page=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),MCUnifiedGameplayMenuPrivate_PagePart(TEXT("Page"),I));Pages->AddChild(Page);
            auto* Interior=MakeShape(MCUnifiedGameplayMenuPrivate_PagePart(TEXT("Inner"),I),MCUnifiedGameplayMenuPrivate_Inner);Interior->ShadowOffset={0,0};Interior->ContentPadding=FMargin(16);Interior->ApplyTheme();
            Page->AddChildToVerticalBox(Interior)->SetPadding(FMargin(0,0,6,10));
            auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedGameplayMenuPrivate_PagePart(TEXT("ScrollBounds"),I));Bounds->SetMaxDesiredHeight(610);Interior->SetContentForSlot(TEXT("Content"),Bounds);
            auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),MCUnifiedGameplayMenuPrivate_PagePart(TEXT("Scroll"),I));Scroll->SetAllowOverscroll(false);Scroll->SetScrollbarThickness({6,6});Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);Bounds->SetContent(Scroll);
            auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),MCUnifiedGameplayMenuPrivate_PagePart(TEXT("Body"),I));Scroll->AddChild(Body);
            auto* Footer=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),MCUnifiedGameplayMenuPrivate_PagePart(TEXT("Footer"),I));Footer->SetExplicitWrapSize(true);Footer->SetWrapSize(920);Footer->SetInnerSlotPadding({18,12});Footer->SetHorizontalAlignment(HAlign_Center);
            Page->AddChildToVerticalBox(Footer)->SetPadding(FMargin(8,4,8,8));
        }
        for(const TCHAR* Name:{TEXT("GameplayMenuV2NetworkNote"),TEXT("GameplayMenuV2Status"),TEXT("GameplayMenuV2Hint")}) {
            auto* Label=MakeText(FName(Name),FString(Name).EndsWith(TEXT("Status"))?18:16);Label->SetWrapTextAt(900);
            Column->AddChildToVerticalBox(Label)->SetPadding(FMargin(10,5,10,0));
        }
    }
    for(const auto& Def:MCUnifiedGameplayMenuPrivate_Definitions) {
        FMCGameplayMenuPresentationControl Fixture;Fixture.Name=Def.Name;Fixture.Kind=Def.Kind;Fixture.bFooter=Def.Footer;
        Fixture.Text=Localized(FName(Def.Key),FText::AsCultureInvariant(Def.Copy));EnsureControl(Def.Page,Fixture);
    }
}
FMCUnifiedGameplayMenuControl& UMCUnifiedGameplayMenuWidget::EnsureControl(int32 PageIndex,const FMCGameplayMenuPresentationControl& Definition)
{
    if(auto* Existing=Controls.FindByPredicate([&](const auto& Item){return Item.PageIndex==PageIndex && Item.Name==Definition.Name;})) return *Existing;
    FMCUnifiedGameplayMenuControl Row;Row.Name=Definition.Name;Row.PageIndex=PageIndex;Row.Kind=Definition.Kind;
    const FName Name=MCUnifiedGameplayMenuPrivate_ViewName(Definition.Name),BoundsName(*(Name.ToString()+TEXT("Bounds")));
    Row.Host=WidgetTree->FindWidget<USizeBox>(BoundsName);Row.Control=WidgetTree->FindWidget(Name);Row.Surface=Cast<UMCUniversalUIWidget>(Row.Control);
    if(!Row.Host || !Row.Control) {
        Row.Host=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),BoundsName);Row.Host->SetMaxDesiredWidth(Definition.bFooter?440:880);
        if(Definition.Kind!=MCUnifiedGameplayMenuPrivate_K::Text && !Definition.bFooter) Row.Host->SetMinDesiredWidth(400);
        if(Definition.Kind==MCUnifiedGameplayMenuPrivate_K::Text) {
            auto* Label=MakeText(Name,18);Label->SetWrapTextAt(830);Label->SetText(Definition.Text);Row.Control=Label;
        } else if(Definition.Kind==MCUnifiedGameplayMenuPrivate_K::Button) {
            Row.Surface=MakeShape(Name,Definition.bFooter?MCUnifiedGameplayMenuPrivate_Pink:MCUnifiedGameplayMenuPrivate_Blue,true);Row.Surface->FontSize=22;Row.Surface->Label=Definition.Text;
            Row.Surface->ContentPadding=FMargin(26,11);Row.Surface->ApplyTheme();Row.Control=Row.Surface;
        } else if(Definition.Kind==MCUnifiedGameplayMenuPrivate_K::Picker) {
            auto* Picker=WidgetTree->ConstructWidget<UMCMainMenuPicker>(UMCMainMenuPicker::StaticClass(),Name);
            Picker->InitializePresentationFont(GetResolvedTheme()->MakeGameplayFont(18));Picker->InitializePresentationForeground(MCUnifiedGameplayMenuPrivate_Navy);
            Picker->SetContentPadding(FMargin(16,9));Picker->SetMaxListHeight(340);Picker->SetHasDownArrow(true);Picker->SetEnableGamepadNavigationMode(true);
            auto Style=Picker->GetWidgetStyle();auto ButtonStyle=Style.ComboButtonStyle.ButtonStyle;
            ButtonStyle.SetNormal(FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Paper,14,MCUnifiedGameplayMenuPrivate_Navy,3));ButtonStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White,14,MCUnifiedGameplayMenuPrivate_Navy,3));
            ButtonStyle.SetPressed(FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Blue,14,MCUnifiedGameplayMenuPrivate_Navy,3));ButtonStyle.SetDisabled(FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Paper.CopyWithNewOpacity(.5f),14,MCUnifiedGameplayMenuPrivate_Navy,3));
            ButtonStyle.SetNormalPadding(FMargin(5));ButtonStyle.SetPressedPadding(FMargin(5));Style.ComboButtonStyle.SetButtonStyle(ButtonStyle);Picker->SetWidgetStyle(Style);Row.Control=Picker;
        } else if(Definition.Kind==MCUnifiedGameplayMenuPrivate_K::Check) {
            auto* Check=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),Name);
            auto* Label=MakeText(FName(*(Name.ToString()+TEXT("Label"))),19);Label->SetJustification(ETextJustify::Left);Label->SetWrapTextAt(760);Label->SetText(Definition.Text);Check->SetContent(Label);
            auto Style=Check->GetWidgetStyle();FSlateBrush Empty=FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Paper,5,MCUnifiedGameplayMenuPrivate_Navy,3),Checked=FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Color(TEXT("74E3D5")),5,MCUnifiedGameplayMenuPrivate_Navy,3);Empty.ImageSize=FVector2D(26,26);Checked.ImageSize=FVector2D(26,26);
            Style.UncheckedImage=Style.UncheckedHoveredImage=Style.UncheckedPressedImage=Empty;Style.CheckedImage=Style.CheckedHoveredImage=Style.CheckedPressedImage=Checked;Style.Padding=FMargin(10,6);Check->SetWidgetStyle(Style);Row.Control=Check;
        } else if(Definition.Kind==MCUnifiedGameplayMenuPrivate_K::Slider) {
            auto* Slider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),Name);Slider->SetMinValue(0);Slider->SetMaxValue(1);Slider->SetStepSize(.01f);
            auto Style=Slider->GetWidgetStyle();Style.NormalBarImage=FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Paper,8,MCUnifiedGameplayMenuPrivate_Navy,2);Style.HoveredBarImage=Style.DisabledBarImage=Style.NormalBarImage;
            Style.NormalThumbImage=FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Paper,8,MCUnifiedGameplayMenuPrivate_Navy,3);Style.NormalThumbImage.ImageSize=FVector2D(32,30);Style.HoveredThumbImage=Style.DisabledThumbImage=Style.NormalThumbImage;Style.BarThickness=14;
            Slider->SetWidgetStyle(Style);Slider->SetIndentHandle(true);Slider->SetSliderBarColor(FLinearColor::White);Slider->SetSliderHandleColor(FLinearColor::White);Row.Host->SetMinDesiredHeight(42);Row.Control=Slider;
        } else if(Definition.Kind==MCUnifiedGameplayMenuPrivate_K::Editable) {
            auto* Edit=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),Name);Edit->SetText(Definition.Text);Edit->SetForegroundColor(MCUnifiedGameplayMenuPrivate_Navy);
            auto Style=Edit->GetWidgetStyle();Style.SetFont(GetResolvedTheme()->MakeGameplayFont(20));Style.BackgroundImageNormal=Style.BackgroundImageHovered=Style.BackgroundImageFocused=FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Paper,12,MCUnifiedGameplayMenuPrivate_Navy,3);Style.Padding=FMargin(14,10);
            Edit->SetWidgetStyle(Style);
            // UE5.8 forwards the supplied style address to Slate. Retain the
            // widget-owned copy, including when an authored view is already live.
            Edit->SetWidgetStyle(Edit->GetWidgetStyle());Row.Control=Edit;
        } else {
            auto* Meter=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),Name);auto Style=Meter->GetWidgetStyle();Style.BackgroundImage=FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Navy,7);Style.FillImage=FSlateRoundedBoxBrush(MCUnifiedGameplayMenuPrivate_Color(TEXT("74E3D5")),7);Meter->SetWidgetStyle(Style);Meter->SetFillColorAndOpacity(FLinearColor::White);Row.Host->SetMinDesiredHeight(18);Row.Control=Meter;
        }
        Row.Host->SetContent(Row.Control);
        if(Definition.bFooter) {
            if(auto* Footer=WidgetTree->FindWidget<UWrapBox>(MCUnifiedGameplayMenuPrivate_PagePart(TEXT("Footer"),PageIndex))) Footer->AddChildToWrapBox(Row.Host);
        } else if(auto* Body=WidgetTree->FindWidget<UVerticalBox>(MCUnifiedGameplayMenuPrivate_PagePart(TEXT("Body"),PageIndex))) {
            auto* Cell=Body->AddChildToVerticalBox(Row.Host);Cell->SetHorizontalAlignment(HAlign_Center);Cell->SetPadding(FMargin(6,Definition.Kind==MCUnifiedGameplayMenuPrivate_K::Text?7:0,6,Definition.Kind==MCUnifiedGameplayMenuPrivate_K::Text?5:12));
        }
    }
    return Controls.Add_GetRef(MoveTemp(Row));
}

UWidget* UMCUnifiedGameplayMenuBinding::GeneratePickerItem(FString Item) {return Owner.IsValid()?Owner->MakePickerItem(this,Item):nullptr;}
UWidget* UMCUnifiedGameplayMenuWidget::MakePickerItem(UMCUnifiedGameplayMenuBinding* Binding,const FString& Item)
{
    if(!WidgetTree) return nullptr;
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetText(FText::FromString(Item));Label->SetAutoWrapText(false);
    Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Label->SetColorAndOpacity(MCUnifiedGameplayMenuPrivate_Navy);Label->SetFont(GetResolvedTheme()->MakeGameplayFont(18));
    const float Width=Binding && Binding->View?Binding->View->GetPaintSpaceGeometry().GetLocalSize().X:0;
    Label->SetWrapTextAt(Width>1?FMath::Max(200.f,Width-48):600.f);
    FMCUnifiedGameplayPickerLabel Retained;Retained.Label=Label;Retained.Picker=Binding?Binding->View.Get():nullptr;GeneratedPickerLabels.Add(Retained);return Label;
}
void UMCUnifiedGameplayMenuWidget::BindControl(FMCUnifiedGameplayMenuControl& View,const FMCGameplayMenuPresentationControl& Source,FName PageName)
{
    if(IsDesignTime() || !Backend.IsValid() || Source.Kind==MCUnifiedGameplayMenuPrivate_K::Text || Source.Kind==MCUnifiedGameplayMenuPrivate_K::Meter || !View.Control) return;
    if(!View.Binding) {
        View.Binding=NewObject<UMCUnifiedGameplayMenuBinding>(this);View.Binding->Owner=this;View.Binding->View=View.Control;
        if(View.Surface) View.Surface->OnClicked.AddUniqueDynamic(View.Binding,&UMCUnifiedGameplayMenuBinding::Clicked);
        else if(auto* Picker=Cast<UComboBoxString>(View.Control)) {
            Picker->OnSelectionChanged.AddUniqueDynamic(View.Binding,&UMCUnifiedGameplayMenuBinding::PickerChanged);
            Picker->OnGenerateWidgetEvent.BindDynamic(View.Binding,&UMCUnifiedGameplayMenuBinding::GeneratePickerItem);
        } else if(auto* Slider=Cast<USlider>(View.Control)) Slider->OnValueChanged.AddUniqueDynamic(View.Binding,&UMCUnifiedGameplayMenuBinding::SliderChanged);
        else if(auto* Check=Cast<UCheckBox>(View.Control)) Check->OnCheckStateChanged.AddUniqueDynamic(View.Binding,&UMCUnifiedGameplayMenuBinding::CheckChanged);
        else if(auto* Edit=Cast<UEditableTextBox>(View.Control)) {
            Edit->OnTextChanged.AddUniqueDynamic(View.Binding,&UMCUnifiedGameplayMenuBinding::TextChanged);Edit->OnTextCommitted.AddUniqueDynamic(View.Binding,&UMCUnifiedGameplayMenuBinding::TextCommitted);
        }
    }
    View.Binding->Source=Source.Source;View.Binding->PageName=PageName;
}
void UMCUnifiedGameplayMenuWidget::Present(const FMCGameplayMenuPresentationSnapshot& Snapshot)
{
    if(!WidgetTree) return;TGuardValue<bool> Guard(bSynchronizing,true);
    const bool NewPage=ActivePageIndex!=Snapshot.PageIndex || ActiveSourcePage!=Snapshot.PageName;
    ActivePageIndex=FMath::Clamp(Snapshot.PageIndex,0,7);ActiveSourcePage=Snapshot.PageName;
    if(auto* Pages=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("GameplayMenuV2Pages"))) Pages->SetActiveWidgetIndex(ActivePageIndex);
    if(auto* Header=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("GameplayMenuV2Heading"))) {
        Header->SetLabel(NAME_None,Snapshot.Heading);if(Header->WidgetTree) if(auto* Label=Header->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(820);}
    }
    auto Copy=[this](FName Name,const FText& Text) {if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) {Label->SetText(Text);Label->SetVisibility(Text.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}};
    Copy(TEXT("GameplayMenuV2NetworkNote"),Snapshot.NetworkNote);Copy(TEXT("GameplayMenuV2Status"),Snapshot.Status);Copy(TEXT("GameplayMenuV2Hint"),Snapshot.Hint);
    TSet<FName> Visible;
    for(const auto& Source:Snapshot.Controls) {
        auto& Row=EnsureControl(ActivePageIndex,Source);Visible.Add(Source.Name);if(Row.Host) Row.Host->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        BindControl(Row,Source,Snapshot.PageName);if(!Row.Control) continue;Row.Control->SetIsEnabled(Source.bEnabled && !Snapshot.bClosing);
        if(Row.Kind==MCUnifiedGameplayMenuPrivate_K::Text) {if(auto* Label=Cast<UTextBlock>(Row.Control)) Label->SetText(Source.Text);}
        else if(Row.Surface) {
            Row.Surface->SetLabel(NAME_None,Source.Text);
            if(Row.Surface->WidgetTree) if(auto* Label=Row.Surface->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(Source.bFooter?370:780);}
        } else if(auto* Picker=Cast<UMCMainMenuPicker>(Row.Control)) {
            bool Different=Picker->GetOptionCount()!=Source.Options.Num();
            for(int32 I=0;!Different && I<Source.Options.Num();++I) Different=Picker->GetOptionAtIndex(I)!=Source.Options[I];
            if(Different) Picker->SetPresentationOptions(Source.Options,Source.SelectedIndex);
            if(Source.SelectedIndex==INDEX_NONE) Picker->ClearSelection();else if(Picker->GetSelectedIndex()!=Source.SelectedIndex) Picker->SetSelectedIndex(Source.SelectedIndex);
        } else if(auto* Check=Cast<UCheckBox>(Row.Control)) {
            Check->SetIsChecked(Source.bChecked);if(auto* Label=Cast<UTextBlock>(Check->GetContent())) Label->SetText(Source.Text);
        } else if(auto* Slider=Cast<USlider>(Row.Control)) {Slider->SetMinValue(Source.Minimum);Slider->SetMaxValue(Source.Maximum);Slider->SetStepSize(Source.Step);if(Slider->GetValue()!=Source.Value) Slider->SetValue(Source.Value);}
        else if(auto* Edit=Cast<UEditableTextBox>(Row.Control)) {if(!Edit->GetText().IdenticalTo(Source.Text)) Edit->SetText(Source.Text);}
        else if(auto* Meter=Cast<UProgressBar>(Row.Control)) Meter->SetPercent(Source.Value);
    }
    for(auto& Row:Controls) if(Row.PageIndex==ActivePageIndex && !Visible.Contains(Row.Name)) {
        if(Row.Host) Row.Host->SetVisibility(ESlateVisibility::Collapsed);if(Row.Binding) Row.Binding->Source.Reset();
    }
    // Every text label held by Slate has a reflected GC owner for the view lifetime.
    for(auto& Generated:GeneratedPickerLabels) if(Generated.Label && Generated.Picker.IsValid()) {
        const float Width=Generated.Picker->GetPaintSpaceGeometry().GetLocalSize().X;if(Width>1) Generated.Label->SetWrapTextAt(FMath::Max(200.f,Width-48));
    }
    if(NewPage && Backend.IsValid()) if(auto* Target=GetDefaultFocusTarget()) if(auto* PC=GetOwningPlayer()) Target->SetUserFocus(PC);
}
void UMCUnifiedGameplayMenuWidget::SetBackend(UMCGameplayMenuWidget* Source)
{
    if(Backend.Get()!=Source) {Backend=Source;ActiveSourcePage=NAME_None;for(auto& Row:Controls) if(Row.Binding) Row.Binding->Source.Reset();}
    if(Source) RefreshFromBackend();
}
void UMCUnifiedGameplayMenuWidget::RefreshFromBackend()
{
    if(IsDesignTime() || !Backend.IsValid()) return;BuildPresentationTree();Present(Backend->GetPresentationSnapshot());
}
bool UMCUnifiedGameplayMenuWidget::CanForward(const UMCUnifiedGameplayMenuBinding* Binding) const
{
    return !bSynchronizing && !IsDesignTime() && Backend.IsValid() && Binding && Binding->Owner.Get()==this && Binding->Source.IsValid() &&
        Backend->IsPresentationControlCurrent(Binding->Source.Get(),Binding->PageName);
}
void UMCUnifiedGameplayMenuWidget::ForwardButton(UMCUnifiedGameplayMenuBinding* Binding)
{
    if(!CanForward(Binding)) return;Backend->TryActivatePresentationButton(Cast<UButton>(Binding->Source.Get()),Binding->PageName);RefreshFromBackend();
    if(IsCapturingKey()) SetKeyboardFocus();
}
void UMCUnifiedGameplayMenuWidget::ForwardPicker(UMCUnifiedGameplayMenuBinding* Binding,const FString& Value)
{
    if(!CanForward(Binding)) return;if(auto* Source=Cast<UComboBoxString>(Binding->Source.Get())) if(Source->FindOptionIndex(Value)!=INDEX_NONE) Source->SetSelectedOption(Value);RefreshFromBackend();
}
void UMCUnifiedGameplayMenuWidget::ForwardSlider(UMCUnifiedGameplayMenuBinding* Binding,float Value)
{
    if(!CanForward(Binding) || !FMath::IsFinite(Value)) return;
    // UE5.8 SetValue already fires the source delegate, including immediate Audio/Voice saves.
    if(auto* Source=Cast<USlider>(Binding->Source.Get())) Source->SetValue(FMath::Clamp(Value,Source->GetMinValue(),Source->GetMaxValue()));RefreshFromBackend();
}
void UMCUnifiedGameplayMenuWidget::ForwardCheck(UMCUnifiedGameplayMenuBinding* Binding,bool Value)
{
    if(!CanForward(Binding)) return;if(auto* Source=Cast<UCheckBox>(Binding->Source.Get())) if(Source->IsChecked()!=Value) {Source->SetIsChecked(Value);Source->OnCheckStateChanged.Broadcast(Value);}RefreshFromBackend();
}
void UMCUnifiedGameplayMenuWidget::ForwardText(UMCUnifiedGameplayMenuBinding* Binding,const FText& Value)
{
    if(!CanForward(Binding)) return;if(auto* Source=Cast<UEditableTextBox>(Binding->Source.Get())) if(!Source->GetText().IdenticalTo(Value)) {Source->SetText(Value);Source->OnTextChanged.Broadcast(Value);}
}
void UMCUnifiedGameplayMenuWidget::ForwardTextCommit(UMCUnifiedGameplayMenuBinding* Binding,const FText& Value,ETextCommit::Type Commit)
{
    if(!CanForward(Binding)) return;ForwardText(Binding,Value);if(auto* Source=Cast<UEditableTextBox>(Binding->Source.Get())) Source->OnTextCommitted.Broadcast(Value,Commit);
}
UWidget* UMCUnifiedGameplayMenuWidget::ViewFocusForSource(const UWidget* Source) const
{
    for(const auto& Row:Controls) if(Row.PageIndex==ActivePageIndex && Row.Binding && Row.Binding->Source.Get()==Source && Row.Control && Row.Control->GetIsEnabled() && Row.Host && Row.Host->IsVisible())
        return Row.Surface?static_cast<UWidget*>(MCUnifiedGameplayMenuPrivate_InnerButton(Row.Surface)):Row.Control.Get();
    return nullptr;
}
UWidget* UMCUnifiedGameplayMenuWidget::GetDefaultFocusTarget() const
{
    if(Backend.IsValid()) if(auto* Target=ViewFocusForSource(Backend->GetNextPresentationFocus(nullptr,1))) return Target;
    return const_cast<UMCUnifiedGameplayMenuWidget*>(this);
}
bool UMCUnifiedGameplayMenuWidget::MovePresentationFocus(int32 Direction)
{
    if(!Backend.IsValid() || !GetOwningPlayer()) return false;const UWidget* Source=nullptr;
    for(const auto& Row:Controls) if(Row.PageIndex==ActivePageIndex && Row.Control && (Row.Control->HasUserFocus(GetOwningPlayer()) || Row.Control->HasUserFocusedDescendants(GetOwningPlayer()))) {
        if(auto* Picker=Cast<UComboBoxString>(Row.Control);Picker && Picker->IsOpen()) return false;
        if(Cast<UEditableTextBox>(Row.Control)) return false;
        Source=Row.Binding?Row.Binding->Source.Get():nullptr;break;
    }
    UWidget* Next=Backend->GetNextPresentationFocus(Source,Direction);UWidget* Target=ViewFocusForSource(Next);if(!Target) return false;Target->SetUserFocus(GetOwningPlayer());
    if(auto* Scroll=WidgetTree->FindWidget<UScrollBox>(MCUnifiedGameplayMenuPrivate_PagePart(TEXT("Scroll"),ActivePageIndex))) Scroll->ScrollWidgetIntoView(Target,true,EDescendantScrollDestination::IntoView,8);return true;
}
bool UMCUnifiedGameplayMenuWidget::IsCapturingKey() const {return Backend.IsValid() && Backend->IsPresentationKeyCaptureActive();}
bool UMCUnifiedGameplayMenuWidget::RouteSourceKey(const FKeyEvent& Event)
{
    if(IsDesignTime() || !Backend.IsValid()) return false;
    if(IsCapturingKey()) {const bool Handled=Backend->RoutePresentationKey(Event);RefreshFromBackend();return Handled;}
    const FKey Key=Event.GetKey();
    if(Key==EKeys::Up || Key==EKeys::Down || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_DPad_Down)
        return MovePresentationFocus(Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Up?-1:1);
    const bool Handled=Backend->RoutePresentationKey(Event);if(Handled) RefreshFromBackend();return Handled;
}
bool UMCUnifiedGameplayMenuWidget::RouteSourceMouseButton(const FPointerEvent& Event)
{
    if(IsDesignTime() || !Backend.IsValid()) return false;const bool Handled=Backend->RoutePresentationMouseButton(Event);if(Handled) RefreshFromBackend();return Handled;
}
bool UMCUnifiedGameplayMenuWidget::RouteSourceAnalog(const FAnalogInputEvent& Event)
{
    if(IsDesignTime() || !Backend.IsValid()) return false;
    if(IsCapturingKey()) return true;
    if(Event.GetKey()==EKeys::Gamepad_LeftY && FMath::Abs(Event.GetAnalogValue())>.55f) {
        if(FPlatformTime::Seconds()<NextNavigationAt) return true;
        if(MovePresentationFocus(Event.GetAnalogValue()>0?-1:1)) {NextNavigationAt=FPlatformTime::Seconds()+.22;return true;}return false;
    }
    return Backend->RoutePresentationAnalog(Event);
}
void UMCUnifiedGameplayMenuWidget::RequestBack() {RouteSourceKey(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));}

void UMCUnifiedGameplayMenuWidget::PresentPreview()
{
    if(Backend.IsValid()) return;
    int32 Index=FMath::Clamp(PreviewMenuPage,0,7);
    const TCHAR* States[]={TEXT("Pause"),TEXT("Settings"),TEXT("Credits"),TEXT("Leave"),TEXT("Exit"),TEXT("VideoConfirm"),TEXT("Connection"),TEXT("Audio")};
    for(int32 I=0;I<8;++I) if(PreviewState==FName(States[I])) {Index=I;break;}
    FMCGameplayMenuPresentationSnapshot Snapshot;Snapshot.PageIndex=Index;Snapshot.PageName=MCUnifiedGameplayMenuPrivate_PageNames[Index];
    auto Copy=[this](const TCHAR* Key,const TCHAR* Fallback) {return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),FName(Key),FText::AsCultureInvariant(Fallback),PreviewCulture);};
    const TCHAR* HeadingKeys[]={TEXT("UI.Menu.PauseHeading"),TEXT("UI.Menu.Settings"),TEXT("UI.Menu.Credits"),TEXT("UI.Menu.LeaveHeading"),TEXT("UI.Menu.ExitHeading"),TEXT("UI.Menu.VideoHeading"),TEXT("UI.Menu.ConnectionHeading"),TEXT("UI.Menu.AudioSettings")};
    const TCHAR* Headings[]={TEXT("MENU"),TEXT("SETTINGS"),TEXT("CREDITS"),TEXT("LEAVE ROOM?"),TEXT("EXIT GAME?"),TEXT("KEEP SCREEN MODE?"),TEXT("PLAY WITH FRIENDS"),TEXT("AUDIO AND MICROPHONE")};
    Snapshot.Heading=Copy(HeadingKeys[Index],Headings[Index]);Snapshot.Hint=Copy(TEXT("UI.Menu.NavigationHint"),TEXT("Up/Down / D-pad · Enter / A · Esc / B"));
    if(Index==0) Snapshot.NetworkNote=Copy(TEXT("UI.Menu.NetworkNote"),TEXT("In multiplayer, the shift continues while this menu is open."));
    if(Index==5) Snapshot.Status=FText::Format(Copy(TEXT("UI.Menu.VideoCountdown"),TEXT("Automatic restore: {Seconds} seconds.")),FFormatNamedArguments{{TEXT("Seconds"),FText::AsNumber(15)}});
    for(const auto& Def:MCUnifiedGameplayMenuPrivate_Definitions) if(Def.Page==Index) {
        FMCGameplayMenuPresentationControl Control;Control.Name=Def.Name;Control.Kind=Def.Kind;Control.Text=Copy(Def.Key,Def.Copy);Control.bFooter=Def.Footer;
        Control.bEnabled=Control.Name!=TEXT("MenuCredits");
        if(Def.Kind==MCUnifiedGameplayMenuPrivate_K::Picker) {
            const FString Name=Control.Name.ToString();
            if(Name.Contains(TEXT("Resolution"))) Control.Options={TEXT("1920 × 1080"),TEXT("1280 × 720"),TEXT("2560 × 1440")};
            else if(Name==TEXT("MenuModePicker")) Control.Options={Copy(TEXT("UI.Menu.Fullscreen"),TEXT("FULLSCREEN")).ToString(),Copy(TEXT("UI.Menu.Borderless"),TEXT("BORDERLESS")).ToString(),Copy(TEXT("UI.Menu.Windowed"),TEXT("WINDOWED")).ToString()};
            else if(Name==TEXT("MenuQualityPicker")) Control.Options={Copy(TEXT("UI.Menu.QualityHigh"),TEXT("HIGH")).ToString(),Copy(TEXT("UI.Menu.QualityEpic"),TEXT("EPIC")).ToString()};
            else if(Name==TEXT("VoiceMode")) Control.Options={Copy(TEXT("UI.Voice.PushToTalk"),TEXT("PUSH TO TALK")).ToString(),Copy(TEXT("UI.Voice.OpenMic"),TEXT("OPEN MICROPHONE")).ToString()};
            else Control.Options={TEXT("PLAYER · 2 / 4")};Control.SelectedIndex=0;
        } else if(Def.Kind==MCUnifiedGameplayMenuPrivate_K::Slider) {
            Control.Value=Control.Name==TEXT("VoiceThreshold")?.08f:1.f;Control.Maximum=Control.Name==TEXT("VoiceGain")?3.f:1.f;
        } else if(Def.Kind==MCUnifiedGameplayMenuPrivate_K::Meter) Control.Value=.55f;
        else if(Def.Kind==MCUnifiedGameplayMenuPrivate_K::Check) Control.bChecked=Control.Name==TEXT("VoiceEnabled") || Control.Name==TEXT("MenuVSync");
        if(PreviewScenario==EMCGameplayUIPreviewScenario::LongText && !Control.Text.IsEmpty()) {
            const FString Original=Control.Text.ToString();Control.Text=FText::FromString(Original+TEXT(" · ")+Original+TEXT(" · ")+Original);
        }
        if(PreviewScenario==EMCGameplayUIPreviewScenario::Pending && Index==6) Control.bEnabled=false;
        Snapshot.Controls.Add(MoveTemp(Control));
    }
    Present(Snapshot);
}
void UMCUnifiedGameplayMenuWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);BuildPresentationTree();if(Backend.IsValid() && !IsDesignTime()) RefreshFromBackend();else PresentPreview();
}
void UMCUnifiedGameplayMenuWidget::ApplyTheme()
{
    Super::ApplyTheme();
    for(auto& Generated:GeneratedPickerLabels) if(Generated.Label) {Generated.Label->SetFont(GetResolvedTheme()->MakeGameplayFont(18));Generated.Label->SetColorAndOpacity(MCUnifiedGameplayMenuPrivate_Navy);}
}
void UMCUnifiedGameplayMenuWidget::OnLocalizationChanged()
{
    if(Backend.IsValid() && !IsDesignTime()) RefreshFromBackend();else if(IsDesignTime()) PresentPreview();
}
void UMCUnifiedGameplayMenuWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);RefreshElapsed+=DeltaSeconds;if(RefreshElapsed>=.1f) {RefreshElapsed=0;RefreshFromBackend();}
}
FReply UMCUnifiedGameplayMenuWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    return RouteSourceKey(Event)?FReply::Handled():Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCUnifiedGameplayMenuWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    return RouteSourceMouseButton(Event)?FReply::Handled():Super::NativeOnPreviewMouseButtonDown(Geometry,Event);
}
FReply UMCUnifiedGameplayMenuWidget::NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event)
{
    return RouteSourceAnalog(Event)?FReply::Handled():Super::NativeOnAnalogValueChanged(Geometry,Event);
}

#undef LOCTEXT_NAMESPACE
