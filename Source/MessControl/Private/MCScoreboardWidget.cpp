#include "MCScoreboardWidget.h"
#include "MCGameplayHUD.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCUIDataLibrary.h"
#include "MCVoiceChatSubsystem.h"
#include "MCGameUserSettings.h"
#include "MCPlayerController.h"
#include "MCHubGameState.h"
#include "MCSteamSessionSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCScoreboard"

namespace MCScoreboardPrivate
{
    USizeBox* Width(UWidgetTree* Tree,UWidget* Child,float Value,FName Name=NAME_None)
    {
        auto* Result=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);Result->SetWidthOverride(Value);Result->SetContent(Child);return Result;
    }
}
void UMCScoreboardMuteButton::Configure(UMCScoreboardWidget* Scoreboard,APlayerState* Player)
{
    OwnerScoreboard=Scoreboard;TargetPlayer=Player;
    OnPressed.AddUniqueDynamic(this,&ThisClass::Pressed);
    OnClicked.AddUniqueDynamic(this,&ThisClass::Clicked);
}
void UMCScoreboardMuteButton::Pressed() {PressedPlayer=TargetPlayer;bPressedTarget=true;}
APlayerState* UMCScoreboardMuteButton::GetActionTarget() const
{ return bPressedTarget?PressedPlayer.Get():TargetPlayer.Get(); }
void UMCScoreboardMuteButton::Clicked()
{
    APlayerState* Target=GetActionTarget();bPressedTarget=false;PressedPlayer.Reset();
    if(OwnerScoreboard.IsValid()) OwnerScoreboard->TogglePlayerVoice(Target);
}
void UMCScoreboardWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(true);SetVisibility(ESlateVisibility::SelfHitTestInvisible);BuildPresentationTree();
}
void UMCScoreboardWidget::BuildPresentationTree()
{
    if(!WidgetTree || Players) return;
    if(WidgetTree->RootWidget) {Players=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("ScorePlayers")));RebuildRows(4);EnsureVoiceControls();EnsureHubPartyControls();return;}
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=Root;
    auto* Background=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ScoreFrame"));
    Background->SetPadding(FMargin(18));
    BindStyle(TEXT("ScoreFrame"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();Scale->SetStretch(EStretch::ScaleToFit);Scale->SetStretchDirection(EStretchDirection::DownOnly);
    auto* DesignSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ScoreDesignSize"));DesignSize->SetWidthOverride(1060);DesignSize->SetHeightOverride(552);DesignSize->SetContent(Background);Scale->SetContent(DesignSize);
    auto* Panel=Root->AddChildToCanvas(Scale);Panel->SetAnchors(FAnchors(0,0,1,1));Panel->SetOffsets(FMargin(24));
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Background->SetContent(Box);
    auto Label=[&](FName Name,const FText& Copy,int32 Size,EMCUIColorToken Token){auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Text->SetText(Copy);Text->SetAutoWrapText(true);BindStyle(Name,Token,EMCUIStyleTarget::Text,Size,true);return Text;};
    auto* Heading=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ScoreHeading"));Heading->SetPadding(FMargin(18,10));BindStyle(TEXT("ScoreHeading"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    auto* HeadingRow=WidgetTree->ConstructWidget<UHorizontalBox>();Heading->SetContent(HeadingRow);
    auto* TitleIcon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("ScoreHeadingTooth"));TitleIcon->bTooth=true;TitleIcon->bHappy=true;
    auto* TitleIconSize=MCScoreboardPrivate::Width(WidgetTree,TitleIcon,44);TitleIconSize->SetHeightOverride(48);
    HeadingRow->AddChildToHorizontalBox(TitleIconSize)->SetPadding(FMargin(0,0,14,0));
    auto* TitleSlot=HeadingRow->AddChildToHorizontalBox(Label(TEXT("ScoreTitle"),Localized(TEXT("UI.Scoreboard.Title"),LOCTEXT("Title","КОМАНДА")),30,EMCUIColorToken::Ink));TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TitleSlot->SetVerticalAlignment(VAlign_Center);
    auto* TabBadge=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ScoreTabBadge"));TabBadge->SetPadding(FMargin(12,6));BindStyle(TEXT("ScoreTabBadge"),EMCUIColorToken::Mint,EMCUIStyleTarget::Background);TabBadge->SetContent(Label(TEXT("ScoreTabKey"),FText::AsCultureInvariant(TEXT("TAB")),20,EMCUIColorToken::Ink));HeadingRow->AddChildToHorizontalBox(TabBadge)->SetVerticalAlignment(VAlign_Center);
    Box->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0,0,0,8));
    auto* HeaderFrame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ScoreColumnsFrame"));HeaderFrame->SetPadding(FMargin(12,10));BindStyle(TEXT("ScoreColumnsFrame"),EMCUIColorToken::SurfaceAlt,EMCUIStyleTarget::Background);
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();HeaderFrame->SetContent(Header);
    Box->AddChildToVerticalBox(HeaderFrame)->SetPadding(FMargin(0,0,0,2));
    Header->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,WidgetTree->ConstructWidget<UTextBlock>(),60));
    Header->AddChildToHorizontalBox(Label(TEXT("ScorePlayerHeader"),Localized(TEXT("UI.Scoreboard.Player"),LOCTEXT("Player","Игрок")),20,EMCUIColorToken::Ink))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* PointsHeader=Label(TEXT("ScorePointsHeader"),Localized(TEXT("UI.Scoreboard.Points"),LOCTEXT("Points","Очки")),20,EMCUIColorToken::Ink);PointsHeader->SetJustification(ETextJustify::Center);
    Header->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,PointsHeader,120));
    auto* PingHeader=Label(TEXT("ScorePingHeader"),FText::AsCultureInvariant(TEXT("PING")),20,EMCUIColorToken::Ink);PingHeader->SetJustification(ETextJustify::Center);
    Header->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,PingHeader,128));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("ScoreScroll"));Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::Never);Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Players=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ScorePlayers"));Scroll->AddChild(Players);
    auto* Footer=Label(TEXT("ScoreFooterText"),Localized(TEXT("UI.Scoreboard.Footer"),LOCTEXT("Footer","Корона — хост комнаты · Отпусти TAB, чтобы закрыть")),16,EMCUIColorToken::Ink);
    auto* FooterFrame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ScoreFooterFrame"));FooterFrame->SetPadding(FMargin(16,10));FooterFrame->SetContent(Footer);BindStyle(TEXT("ScoreFooterFrame"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    Box->AddChildToVerticalBox(FooterFrame)->SetPadding(FMargin(0,8,0,0));RebuildRows(4);EnsureVoiceControls();EnsureHubPartyControls();ApplyTheme();
}
void UMCScoreboardWidget::EnsureVoiceControls()
{
    if(!WidgetTree || !Players) return;
    // Migrate the saved artist tree by appending one column, retaining its widgets,
    // materials and layout. This is idempotent and never clears/rebuilds the tree.
    auto AllowChildren=[](UWidget* Widget)
    {
        if(Widget && Widget->GetVisibility()==ESlateVisibility::HitTestInvisible)
            Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    };
    AllowChildren(this);
    for(UWidget* Parent=Players;Parent;Parent=Parent->GetParent()) AllowChildren(Parent);
    auto* HeaderFrame=Cast<UBorder>(WidgetTree->FindWidget(TEXT("ScoreColumnsFrame")));
    auto* Header=HeaderFrame?Cast<UHorizontalBox>(HeaderFrame->GetContent()):nullptr;
    // The saved v10 presentation predates ScoreColumnsFrame. Resolve its
    // actual header row from the stable ping label rather than requiring the
    // newer native wrapper or guessing an automatically generated row name.
    if(!Header) for(UWidget* Parent=WidgetTree->FindWidget(TEXT("ScorePingHeader"));Parent;Parent=Parent->GetParent())
        if((Header=Cast<UHorizontalBox>(Parent))) break;
    if(Header && !WidgetTree->FindWidget(TEXT("ScoreVoiceHeader")))
    {
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ScoreVoiceHeader"));
        Label->SetText(Localized(TEXT("UI.Scoreboard.Voice"),LOCTEXT("Voice","Голос")));
        Label->SetJustification(ETextJustify::Center);Label->SetAutoWrapText(true);
        Header->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,Label,148,TEXT("ScoreVoiceColumn")));
        BindStyle(TEXT("ScoreVoiceHeader"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,20,true);
    }
    for(int32 Index=0;Index<Rows.Num();++Index)
    {
        auto& Controls=Rows[Index];if(!Controls.Background) continue;
        const FString Prefix=FString::Printf(TEXT("ScorePlayer%d"),Index+1);
        Controls.MuteButton=Cast<UMCScoreboardMuteButton>(WidgetTree->FindWidget(FName(Prefix+TEXT("Mute"))));
        Controls.MuteLabel=Cast<UTextBlock>(WidgetTree->FindWidget(FName(Prefix+TEXT("MuteLabel"))));
        auto* Row=Cast<UHorizontalBox>(Controls.Background->GetContent());
        if(!Controls.MuteButton && Row)
        {
            Controls.MuteButton=WidgetTree->ConstructWidget<UMCScoreboardMuteButton>(UMCScoreboardMuteButton::StaticClass(),FName(Prefix+TEXT("Mute")));
            Controls.MuteLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(Prefix+TEXT("MuteLabel")));
            Controls.MuteLabel->SetAutoWrapText(true);Controls.MuteLabel->SetJustification(ETextJustify::Center);
            Controls.MuteButton->SetContent(Controls.MuteLabel);
            auto* VoiceSlot=Row->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,Controls.MuteButton,148,FName(Prefix+TEXT("VoiceColumn"))));
            VoiceSlot->SetVerticalAlignment(VAlign_Center);VoiceSlot->SetPadding(FMargin(8,0,0,0));
        }
        if(Controls.MuteButton)
        {
            Controls.MuteButton->Configure(this,nullptr);
            for(UWidget* Parent=Controls.MuteButton;Parent;Parent=Parent->GetParent()) AllowChildren(Parent);
            BindStyle(Controls.MuteButton->GetFName(),EMCUIColorToken::Button,EMCUIStyleTarget::Button);
        }
        if(Controls.MuteLabel) BindStyle(Controls.MuteLabel->GetFName(),EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,16,true);
    }
}
bool UMCScoreboardWidget::CanControlPlayerVoice(const APlayerState* Player,bool bLocalPlayer)
{ return IsValid(Player) && !Player->IsABot() && (bLocalPlayer || Player->GetUniqueId().IsValid()); }
void UMCScoreboardWidget::TogglePlayerVoice(APlayerState* Player)
{
    if(IsDesignTime() || !IsValid(Player) || !GetWorld() || !GetWorld()->IsGameWorld() || !GetGameInstance()) return;
    const auto* State=GetWorld()->GetGameState();
    if(!State || !State->PlayerArray.Contains(Player)) return; // Disconnected/removed press target is never retargeted.
    const auto* Controller=GetOwningPlayer();
    const bool bLocal=Controller && Controller->PlayerState==Player;
    if(!CanControlPlayerVoice(Player,bLocal)) return;
    auto* Voice=GetGameInstance()->GetSubsystem<UMCVoiceChatSubsystem>();
    const auto* Settings=UMCGameUserSettings::Get();if(!Voice || !Settings) return;
    // Use the individual preference, not global voice enable: a disabled channel
    // must not turn every row into an impossible-to-unmute state.
    const bool bMuted=bLocal?Settings->GetSelfMuted():Settings->IsVoicePlayerMuted(Player->GetUniqueId()->ToString());
    Voice->SetPlayerMuted(Player,!bMuted);Refresh();
}
void UMCScoreboardWidget::FocusVoiceControls()
{
    if(IsHubPartyPresentation() && HubInviteButton && HubInviteButton->GetIsEnabled())
    {HubInviteButton->SetUserFocus(GetOwningPlayer());return;}
    for(const auto& Row:Rows) if(Row.MuteButton && Row.Background && Row.Background->IsVisible() && Row.MuteButton->GetIsEnabled())
    {Row.MuteButton->SetUserFocus(GetOwningPlayer());return;}
    SetUserFocus(GetOwningPlayer());
}
void UMCScoreboardWidget::MoveVoiceFocus(int32 Direction)
{
    TArray<UMCScoreboardMuteButton*> Buttons;int32 Current=INDEX_NONE;
    for(const auto& Row:Rows) if(Row.MuteButton && Row.Background && Row.Background->IsVisible() && Row.MuteButton->GetIsEnabled())
    {if(Row.MuteButton->HasUserFocus(GetOwningPlayer())) Current=Buttons.Num();Buttons.Add(Row.MuteButton);}
    if(Buttons.IsEmpty()) return;
    const int32 Next=Current==INDEX_NONE?0:(Current+Direction+Buttons.Num())%Buttons.Num();
    Buttons[Next]->SetUserFocus(GetOwningPlayer());
    if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("ScoreScroll")))) Scroll->ScrollWidgetIntoView(Buttons[Next],true,EDescendantScrollDestination::IntoView);
}
FReply UMCScoreboardWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    const FKey Key=Event.GetKey();
    if(Key==EKeys::Tab || Key==EKeys::Gamepad_Special_Left) {
        if(IsHubPartyPresentation() && !Event.IsRepeat()) ClosePartyPanel();
        return FReply::Handled();
    }
    // Hub uses ordinary UMG directional focus across invitations, privacy and
    // mute controls. Arena keeps its existing participant-only navigation.
    if(IsHubPartyPresentation()) {
        if(Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right) {ClosePartyPanel();return FReply::Handled();}
        return Super::NativeOnPreviewKeyDown(Geometry,Event);
    }
    if(Key==EKeys::Down || Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Down || Key==EKeys::Gamepad_DPad_Up)
    {MoveVoiceFocus(Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Up?-1:1);return FReply::Handled();}
    if(Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right)
    {if(!IsHubPartyPresentation()) ClosePartyPanel();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCScoreboardWidget::NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Tab || Event.GetKey()==EKeys::Gamepad_Special_Left)
    {if(!IsHubPartyPresentation()) ClosePartyPanel();return FReply::Handled();}
    return Super::NativeOnKeyUp(Geometry,Event);
}
bool UMCScoreboardWidget::IsHubPartyPresentation() const
{
    if(IsDesignTime()) return PreviewState.ToString().StartsWith(TEXT("Hub"));
    const auto* PC=GetOwningPlayer<AMCPlayerController>();return PC && PC->IsHubWorld();
}
void UMCScoreboardWidget::EnsureHubPartyControls()
{
    if(!WidgetTree) return;
    HubPartyControls=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("HubPartyControls")));
    if(!HubPartyControls) {
        // Add one named branch to the existing artist column. No existing
        // labels, rows, brushes or slots are replaced by this migration.
        auto* Heading=WidgetTree->FindWidget(TEXT("ScoreHeading"));
        auto* Column=Heading?Cast<UVerticalBox>(Heading->GetParent()):nullptr;
        if(!Column) for(UWidget* Parent=WidgetTree->FindWidget(TEXT("ScoreTitle"));Parent;Parent=Parent->GetParent())
            if((Column=Cast<UVerticalBox>(Parent))) break;
        if(!Column) return;
        HubPartyControls=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("HubPartyControls"));
        Column->AddChildToVerticalBox(HubPartyControls)->SetPadding(FMargin(0,4,0,10));
        Column->ShiftChild(Heading?Column->GetChildIndex(Heading)+1:0,HubPartyControls);
        auto Label=[&](FName Name,int32 Size,EMCUIColorToken Color)->UTextBlock* {
            auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
            Text->SetAutoWrapText(true);HubPartyControls->AddChildToVerticalBox(Text)->SetPadding(FMargin(4,3));
            BindStyle(Name,Color,EMCUIStyleTarget::Text,Size,true);return Text;
        };
        Label(TEXT("HubPartySummary"),20,EMCUIColorToken::OnMarine);
        auto Button=[&](FName Name,FName LabelName)->UButton* {
            auto* Result=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
            auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),LabelName);
            Text->SetAutoWrapText(true);Text->SetJustification(ETextJustify::Center);Result->SetContent(Text);
            BindStyle(Name,EMCUIColorToken::Button,EMCUIStyleTarget::Button);
            BindStyle(LabelName,EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,18,true);return Result;
        };
        HubPartyControls->AddChildToVerticalBox(Button(TEXT("HubPartyInvite"),TEXT("HubPartyInviteLabel")))->SetPadding(FMargin(0,6));
        Label(TEXT("HubPartyPrivacyTitle"),17,EMCUIColorToken::OnMarine);
        auto* Privacy=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HubPartyPrivacy"));
        HubPartyControls->AddChildToVerticalBox(Privacy)->SetPadding(FMargin(0,4));
        for(const auto& Name:TArray<FName>{TEXT("HubPartyPublic"),TEXT("HubPartyFriends"),TEXT("HubPartyInviteOnly")}) {
            auto* Entry=Privacy->AddChildToHorizontalBox(Button(Name,FName(Name.ToString()+TEXT("Label"))));
            Entry->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Entry->SetPadding(FMargin(3,0));
        }
        Label(TEXT("HubPartyStatus"),16,EMCUIColorToken::OnMarine);
        HubPartyControls->AddChildToVerticalBox(Button(TEXT("HubPartyClose"),TEXT("HubPartyCloseLabel")))->SetPadding(FMargin(0,6,0,0));
    }
    HubPartySummary=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubPartySummary")));
    HubPartyStatus=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubPartyStatus")));
    HubPrivacyControls=Cast<UHorizontalBox>(WidgetTree->FindWidget(TEXT("HubPartyPrivacy")));
    HubInviteButton=Cast<UButton>(WidgetTree->FindWidget(TEXT("HubPartyInvite")));
    HubPublicButton=Cast<UButton>(WidgetTree->FindWidget(TEXT("HubPartyPublic")));
    HubFriendsButton=Cast<UButton>(WidgetTree->FindWidget(TEXT("HubPartyFriends")));
    HubInviteOnlyButton=Cast<UButton>(WidgetTree->FindWidget(TEXT("HubPartyInviteOnly")));
    HubCloseButton=Cast<UButton>(WidgetTree->FindWidget(TEXT("HubPartyClose")));
    if(HubInviteButton) HubInviteButton->OnClicked.AddUniqueDynamic(this,&ThisClass::InviteHubFriends);
    if(HubPublicButton) HubPublicButton->OnClicked.AddUniqueDynamic(this,&ThisClass::SetPublicRoom);
    if(HubFriendsButton) HubFriendsButton->OnClicked.AddUniqueDynamic(this,&ThisClass::SetFriendsRoom);
    if(HubInviteOnlyButton) HubInviteOnlyButton->OnClicked.AddUniqueDynamic(this,&ThisClass::SetInviteRoom);
    if(HubCloseButton) HubCloseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ClosePartyPanel);
}
void UMCScoreboardWidget::RefreshHubPartyControls()
{
    if(!HubPartyControls) return;
    const bool Hub=IsHubPartyPresentation();
    HubPartyControls->SetVisibility(Hub?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    // Points have no meaning in a social Hub. Retain the existing columns for
    // arena scoreboards and avoid introducing any experience/level system.
    auto PointsColumn=[](UWidget* Widget,bool Show) {
        if(!Widget) return;
        UWidget* Host=Cast<USizeBox>(Widget->GetParent())?Widget->GetParent():Widget;
        Host->SetVisibility(Show?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    };
    PointsColumn(WidgetTree->FindWidget(TEXT("ScorePointsHeader")),!Hub);
    for(const auto& Row:Rows) PointsColumn(Row.Points,!Hub);
    if(!Hub) return;
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const auto* State=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    auto* Sessions=GetGameInstance()?GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>():nullptr;
    const bool Preview=IsDesignTime();
    const bool Host=Preview?PreviewState!=TEXT("HubGuest"):State && State->IsHost(PC);
    const bool Known=Preview?PreviewState!=TEXT("HubLocal"):State && State->bRoomPrivacyKnown;
    const bool Online=Preview?Known:Sessions && Sessions->CanUseSteam() && Sessions->HasRoom();
    const bool Pending=Preview?PreviewScenario==EMCGameplayUIPreviewScenario::Pending:State && State->bRoomPrivacyUpdatePending;
    const EMCRoomPrivacy Privacy=Preview?EMCRoomPrivacy::FriendsOnly:State?State->RoomPrivacy:EMCRoomPrivacy::FriendsOnly;
    FText HostName;
    for(const auto& Entry:PlayerEntries) if(Entry.bHost) {HostName=FText::AsCultureInvariant(Entry.Nickname);break;}
    if(HostName.IsEmpty()) HostName=Localized(TEXT("hub.party.host_pending"),LOCTEXT("PartyHostPending","Подключение…"));
    const FText Summary=FText::Format(Localized(TEXT("hub.party.summary"),LOCTEXT("PartySummary","Хост: {Host}  ·  Игроки: {Count} / 4")),
        FFormatNamedArguments{{TEXT("Host"),HostName},{TEXT("Count"),PlayerEntries.Num()}});
    if(HubPartySummary) HubPartySummary->SetText(Summary);
    auto Copy=[&](FName Name,FName Key,const FText& Fallback) {
        if(auto* Text=Cast<UTextBlock>(WidgetTree->FindWidget(Name))) Text->SetText(Localized(Key,Fallback));
    };
    Copy(TEXT("HubPartyInviteLabel"),TEXT("hub.party.invite"),LOCTEXT("PartyInvite","Пригласить друзей"));
    Copy(TEXT("HubPartyPublicLabel"),TEXT("hub.privacy.public"),LOCTEXT("RoomPublic","Открытая"));
    Copy(TEXT("HubPartyFriendsLabel"),TEXT("hub.privacy.friends"),LOCTEXT("RoomFriends","Только друзья"));
    Copy(TEXT("HubPartyInviteOnlyLabel"),TEXT("hub.privacy.invite"),LOCTEXT("RoomInvite","По приглашению"));
    Copy(TEXT("HubPartyCloseLabel"),TEXT("hub.party.close"),LOCTEXT("PartyClose","Вернуться в хаб"));
    Copy(TEXT("HubPartyPrivacyTitle"),Online?TEXT("hub.party.privacy_title"):TEXT("hub.party.local_title"),
        Online?LOCTEXT("PartyPrivacyTitle","Доступ в комнату"):LOCTEXT("PartyLocalTitle","Локальный хаб"));
    if(Known) if(auto* Title=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubPartyPrivacyTitle")))) {
        const FText Mode=Privacy==EMCRoomPrivacy::Public?Localized(TEXT("hub.privacy.public"),LOCTEXT("RoomPublic","Открытая")):
            Privacy==EMCRoomPrivacy::FriendsOnly?Localized(TEXT("hub.privacy.friends"),LOCTEXT("RoomFriends","Только друзья")):
            Localized(TEXT("hub.privacy.invite"),LOCTEXT("RoomInvite","По приглашению"));
        Title->SetText(FText::Format(Localized(TEXT("hub.party.privacy_value"),LOCTEXT("PartyPrivacyValue","Доступ: {Mode}")),FFormatNamedArguments{{TEXT("Mode"),Mode}}));
    }
    if(HubPrivacyControls) HubPrivacyControls->SetVisibility(Host && Online?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    const bool CanChange=Host && Online && !Pending && (Preview || (Sessions && Sessions->CanChangeRoomPrivacy()));
    for(const auto& Entry:TArray<TPair<UButton*,EMCRoomPrivacy>>{{HubPublicButton,EMCRoomPrivacy::Public},{HubFriendsButton,EMCRoomPrivacy::FriendsOnly},{HubInviteOnlyButton,EMCRoomPrivacy::InviteOnly}})
        if(Entry.Key) {
            Entry.Key->SetIsEnabled(CanChange);
            BindStyle(Entry.Key->GetFName(),Known && Entry.Value==Privacy?EMCUIColorToken::Mint:EMCUIColorToken::Button,EMCUIStyleTarget::Button);
        }
    if(HubInviteButton) HubInviteButton->SetIsEnabled(PlayerEntries.Num()<4 && !Pending && (Preview?Known:Sessions && Sessions->CanInviteFriends()));
    FText Notice;
    if(Pending) Notice=Localized(TEXT("hub.party.updating"),LOCTEXT("PartyUpdating","Меняем доступ…"));
    else if(Online && !Known) Notice=Localized(TEXT("hub.party.privacy_unknown"),LOCTEXT("PartyPrivacyUnknown","Не удалось подтвердить доступ в комнату. Хост может повторно выбрать режим."));
    else if(!Online) Notice=Localized(TEXT("hub.party.local_notice"),LOCTEXT("PartyLocalNotice","Приглашения доступны в Steam. К локальному хабу можно подключиться по IP."));
    else if(PlayerEntries.Num()>=4) Notice=Localized(TEXT("hub.party.full"),LOCTEXT("PartyFull","Команда в сборе · свободных мест нет"));
    else Notice=Localized(Privacy==EMCRoomPrivacy::Public?TEXT("hub.party.public_notice"):Privacy==EMCRoomPrivacy::FriendsOnly?TEXT("hub.party.friends_notice"):TEXT("hub.party.invite_notice"),
        Privacy==EMCRoomPrivacy::Public?LOCTEXT("PartyPublicNotice","Открытая комната · игроки могут найти ваш хаб"):
        Privacy==EMCRoomPrivacy::FriendsOnly?LOCTEXT("PartyFriendsNotice","Друзья могут подключаться сами или по приглашению"):
        LOCTEXT("PartyInviteNotice","Вход в хаб только по приглашению"));
    if(State && !State->RoomPrivacyNotice.IsEmpty()) {
        Notice=State->RoomPrivacyNotice;
        // Authority replicates semantic FText identity; each client resolves its
        // own language rather than receiving the host's translated display copy.
        if(const TOptional<FString> Key=FTextInspector::GetKey(Notice);Key.IsSet()) {
            const TPair<const TCHAR*,const TCHAR*> Keys[]={
                {TEXT("PrivacyUpdating"),TEXT("hub.privacy.status.updating")},
                {TEXT("PrivacyUpdated"),TEXT("hub.privacy.status.updated")},
                {TEXT("PrivacyRestoring"),TEXT("hub.privacy.status.restoring")},
                {TEXT("PrivacyRestored"),TEXT("hub.privacy.status.restored")},
                {TEXT("PrivacyUnconfirmed"),TEXT("hub.privacy.status.unconfirmed")}};
            for(const auto& Entry:Keys) if(Key.GetValue()==Entry.Key) {Notice=Localized(FName(Entry.Value),Notice);break;}
        }
    }
    if(HubPartyStatus) HubPartyStatus->SetText(Notice);
}
void UMCScoreboardWidget::InviteHubFriends()
{
    if(IsDesignTime()) return;
    if(auto* Sessions=GetGameInstance()?GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>():nullptr;Sessions && Sessions->CanInviteFriends()) Sessions->InviteFriends();
    Refresh();
}
void UMCScoreboardWidget::SetPublicRoom() {if(!IsDesignTime()) if(auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->RequestHubRoomPrivacy(EMCRoomPrivacy::Public);}
void UMCScoreboardWidget::SetFriendsRoom() {if(!IsDesignTime()) if(auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->RequestHubRoomPrivacy(EMCRoomPrivacy::FriendsOnly);}
void UMCScoreboardWidget::SetInviteRoom() {if(!IsDesignTime()) if(auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->RequestHubRoomPrivacy(EMCRoomPrivacy::InviteOnly);}
void UMCScoreboardWidget::ClosePartyPanel() {if(auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->HideScoreboard();}
void UMCScoreboardWidget::BeginHubPartyPresentation()
{
    PartyOpenElapsed=0;
    if(IsHubPartyPresentation() && bUseSoftPartyPresentation) {SetRenderOpacity(PartyFadeSeconds>0?0.f:1.f);SetRenderTranslation(FVector2D(0,PartyFadeSeconds>0?8.f:0.f));}
    else {SetRenderOpacity(1);SetRenderTranslation(FVector2D::ZeroVector);}
}
void UMCScoreboardWidget::RefreshSoftPartySurfaces(float Dt)
{
    if(!bUseSoftPartyPresentation || !IsHubPartyPresentation() || !WidgetTree) return;
    const auto& P=GetResolvedTheme()->Palette;
    auto Panel=[&](FName Name,FLinearColor Color,float Radius) {
        if(auto* Frame=Cast<UBorder>(WidgetTree->FindWidget(Name))) {Frame->SetBrush(FSlateRoundedBoxBrush(Color,Radius));Frame->SetBrushColor(FLinearColor::White);}
    };
    FLinearColor Background=P.Marine;Background.A=FMath::Clamp(PartyPanelOpacity,0.f,1.f);
    Panel(TEXT("ScoreFrame"),Background,PartyCornerRadius);
    Panel(TEXT("ScoreHeading"),P.Cream,PartyCornerRadius*.65f);
    Panel(TEXT("ScoreTabBadge"),P.Mint,PartyCornerRadius*.65f);
    if(auto* Badge=Cast<UBorder>(WidgetTree->FindWidget(TEXT("ScoreTabBadge")))) {
        Badge->SetPadding(FMargin(12,6));
        if(bUseNativeLayout) if(auto* BadgeSlot=Cast<UHorizontalBoxSlot>(Badge->Slot)) BadgeSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }
    if(auto* Key=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ScoreTabKey")))) {
        Key->SetAutoWrapText(false);Key->SetWrapTextAt(0.f);Key->SetColorAndOpacity(P.Ink);
    }
    Panel(TEXT("ScoreColumnsFrame"),P.SurfaceAlt,PartyCornerRadius*.5f);
    Panel(TEXT("ScoreFooterFrame"),P.Cream,PartyCornerRadius*.5f);
    for(int32 Index=0;Index<Rows.Num();++Index) if(Rows[Index].Background) {
        const bool Host=PlayerEntries.IsValidIndex(Index) && PlayerEntries[Index].bHost;
        Panel(Rows[Index].Background->GetFName(),Host?P.Mint:Index%2==0?P.Surface:P.SurfaceAlt,PartyCornerRadius*.65f);
    }
    TArray<UButton*> Buttons={HubInviteButton,HubPublicButton,HubFriendsButton,HubInviteOnlyButton,HubCloseButton};
    for(const auto& Row:Rows) if(Row.MuteButton) Buttons.Add(Row.MuteButton);
    const auto* State=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    const EMCRoomPrivacy Privacy=IsDesignTime()?EMCRoomPrivacy::FriendsOnly:State?State->RoomPrivacy:EMCRoomPrivacy::FriendsOnly;
    const bool Known=IsDesignTime()?PreviewState!=TEXT("HubLocal"):State && State->bRoomPrivacyKnown;
    for(UButton* Button:Buttons) if(Button) {
        const bool Selected=Known && ((Button==HubPublicButton && Privacy==EMCRoomPrivacy::Public)
            || (Button==HubFriendsButton && Privacy==EMCRoomPrivacy::FriendsOnly) || (Button==HubInviteOnlyButton && Privacy==EMCRoomPrivacy::InviteOnly));
        const bool Highlight=Button==HubInviteButton || Selected;
        float& Hover=PartyHoverAmounts.FindOrAdd(Button);
        const float Target=Button->IsHovered() || Button->HasUserFocus(GetOwningPlayer())?1.f:0.f;
        Hover=PartyHoverSeconds>0?FMath::FInterpConstantTo(Hover,Target,Dt,1.f/PartyHoverSeconds):Target;
        const FLinearColor Base=!Button->GetIsEnabled() && !Selected?P.ButtonDisabled:Highlight?P.Mint:P.Button;
        const FLinearColor Tint=Button->IsPressed()?P.ButtonPressed:FMath::Lerp(Base,P.ButtonHover,Hover);
        const FSlateRoundedBoxBrush Shape(FLinearColor::White,PartyCornerRadius);
        auto Style=Button->GetStyle();Style.SetNormal(Shape);Style.SetHovered(Shape);Style.SetPressed(Shape);Style.SetDisabled(Shape);
        Style.SetNormalPadding(FMargin(14,10));Style.SetPressedPadding(FMargin(14,10));
        Button->SetStyle(Style);Button->SetBackgroundColor(Tint);
        if(auto* Label=Cast<UTextBlock>(Button->GetContent())) Label->SetColorAndOpacity(Highlight?P.Ink:P.ButtonText);
    }
}
void UMCScoreboardWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt);UpdatePresentationLayout(Geometry.GetLocalSize());if(IsDesignTime() || !IsVisible()) return;RefreshElapsed+=Dt;
    if(IsHubPartyPresentation() && bUseSoftPartyPresentation) {
        PartyOpenElapsed+=Dt;
        const float Alpha=PartyFadeSeconds>0?FMath::Clamp(PartyOpenElapsed/PartyFadeSeconds,0.f,1.f):1.f;
        const float Ease=1.f-FMath::Pow(1.f-Alpha,3.f);SetRenderOpacity(Ease);
        SetRenderTranslation(FVector2D(0,8.f*(1.f-Ease)));RefreshSoftPartySurfaces(Dt);
    }
    if(RefreshElapsed>=.25f) {RefreshElapsed=0;Refresh();}
}
void UMCScoreboardWidget::RebuildRows(int32 Count)
{
    if(!Players) return;
    Count=FMath::Max(4,Count);
    for(int32 Index=Rows.Num();Index<Count;++Index)
    {
        const FString Prefix=FString::Printf(TEXT("ScorePlayer%d"),Index+1);
        if(auto* Existing=Cast<UBorder>(WidgetTree->FindWidget(FName(Prefix+TEXT("Row"))))) {
            auto& Controls=Rows.AddDefaulted_GetRef();Controls.Background=Existing;
            Controls.Name=Cast<UTextBlock>(WidgetTree->FindWidget(FName(Prefix+TEXT("Name"))));Controls.Points=Cast<UTextBlock>(WidgetTree->FindWidget(FName(Prefix+TEXT("Points"))));Controls.Ping=Cast<UTextBlock>(WidgetTree->FindWidget(FName(Prefix+TEXT("Ping"))));Controls.Icon=Cast<UMCHUDIcon>(WidgetTree->FindWidget(FName(Prefix+TEXT("Icon"))));continue;
        }
        auto* Background=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(Prefix+TEXT("Row")));Background->SetPadding(FMargin(12,8));
        Players->AddChildToVerticalBox(Background)->SetPadding(FMargin(0,3));
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Background->SetContent(Row);
        FMCScoreboardRow& Controls=Rows.AddDefaulted_GetRef();Controls.Background=Background;
        Controls.Icon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),FName(Prefix+TEXT("Icon")));Controls.Icon->bTooth=true;
        auto* Portrait=MCScoreboardPrivate::Width(WidgetTree,Controls.Icon,44);Portrait->SetHeightOverride(52);
        Row->AddChildToHorizontalBox(Portrait)->SetPadding(FMargin(0,0,16,0));
        auto Label=[&](const TCHAR* Suffix,int32 Size){auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(Prefix+Suffix));BindStyle(Text->GetFName(),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,Size,true);return Text;};
        Controls.Name=Label(TEXT("Name"),22);Controls.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Controls.Name->SetClipping(EWidgetClipping::ClipToBounds);
        auto* NameSlot=Row->AddChildToHorizontalBox(Controls.Name);NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameSlot->SetVerticalAlignment(VAlign_Center);
        Controls.Points=Label(TEXT("Points"),24);Controls.Points->SetJustification(ETextJustify::Center);
        Row->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,Controls.Points,120))->SetVerticalAlignment(VAlign_Center);
        Controls.Ping=Label(TEXT("Ping"),22);Controls.Ping->SetJustification(ETextJustify::Center);
        Row->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,Controls.Ping,128))->SetVerticalAlignment(VAlign_Center);
    }
}
void UMCScoreboardWidget::Refresh()
{
    if(IsDesignTime()) return;if(!Players) BuildPresentationTree();
    TWeakObjectPtr<APlayerState> FocusedPlayer;
    for(const auto& Row:Rows) if(Row.MuteButton && Row.MuteButton->HasUserFocus(GetOwningPlayer()))
        FocusedPlayer=Row.MuteButton->GetActionTarget();
    PlayerEntries.Reset();
    const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
    if(GS)
    {
        TArray<APlayerState*> States;
        for(const auto& State:GS->PlayerArray) if(IsValid(State)) States.Add(State.Get());
        States.Sort([](const APlayerState& A,const APlayerState& B)
        {
            const auto* Left=Cast<AMCPlayerState>(&A);const auto* Right=Cast<AMCPlayerState>(&B);
            const int32 LP=Left?Left->Points:FMath::RoundToInt(A.GetScore()),RP=Right?Right->Points:FMath::RoundToInt(B.GetScore());
            return LP!=RP?LP>RP:A.GetPlayerId()<B.GetPlayerId();
        });
        for(APlayerState* State:States)
        {
            FMCScoreboardEntry& Entry=PlayerEntries.AddDefaulted_GetRef();
            const auto* Player=Cast<AMCPlayerState>(State);const auto* Hero=Cast<AMCToothCharacter>(State->GetPawn());
            Entry.Nickname=State->GetPlayerName();Entry.Points=Player?Player->Points:FMath::RoundToInt(State->GetScore());
            Entry.PingMilliseconds=FMath::Max(0.f,State->GetPingInMilliseconds());
            Entry.bHost=Player && Player->bSessionHost;
            Entry.PlayerColor=Player?Player->PlayerColor:Hero?Hero->GetPlayerColor():FLinearColor::White;
            Entry.bSpectating=!Hero || !Hero->Status || !Hero->Status->IsAlive();
            Entry.PlayerState=State;
            Entry.bLocalPlayer=GetOwningPlayer() && GetOwningPlayer()->PlayerState==State;
            Entry.bVoiceControlEnabled=CanControlPlayerVoice(State,Entry.bLocalPlayer) && GetGameInstance() && GetGameInstance()->GetSubsystem<UMCVoiceChatSubsystem>();
            if(const auto* Settings=UMCGameUserSettings::Get())
                Entry.bVoiceMuted=Entry.bLocalPlayer?Settings->GetSelfMuted():State->GetUniqueId().IsValid() && Settings->IsVoicePlayerMuted(State->GetUniqueId()->ToString());
        }
    }
    const int32 PreviousRows=Rows.Num();RebuildRows(PlayerEntries.Num());
    if(Rows.Num()!=PreviousRows) EnsureVoiceControls();
    ApplyTheme();OnScoreboardUpdated();
    if(FocusedPlayer.IsValid()) for(int32 Index=0;Index<PlayerEntries.Num() && Index<Rows.Num();++Index)
        if(PlayerEntries[Index].PlayerState==FocusedPlayer.Get() && Rows[Index].MuteButton && Rows[Index].MuteButton->GetIsEnabled()
            && !Rows[Index].MuteButton->HasUserFocus(GetOwningPlayer()))
        {
            Rows[Index].MuteButton->SetUserFocus(GetOwningPlayer());
            if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("ScoreScroll")))) Scroll->ScrollWidgetIntoView(Rows[Index].MuteButton,true,EDescendantScrollDestination::IntoView);
            break;
        }
}
void UMCScoreboardWidget::PresentEntries()
{
    const auto& Palette=GetResolvedTheme()->Palette;const FString Culture=IsDesignTime()?PreviewCulture:FString();
    for(int32 Index=0;Index<Rows.Num();++Index)
    {
        auto& Controls=Rows[Index];const bool Available=PlayerEntries.IsValidIndex(Index);
        if(Controls.Background) Controls.Background->SetVisibility(Available?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        if(!Available) {if(Controls.MuteButton) Controls.MuteButton->Configure(this,nullptr);continue;}
        const FMCScoreboardEntry& Entry=PlayerEntries[Index];FMCScoreboardRow& Row=Rows[Index];
        if(Row.Background) BindStyle(Row.Background->GetFName(),Entry.bHost?EMCUIColorToken::Mint:Index%2==0?EMCUIColorToken::Surface:EMCUIColorToken::SurfaceAlt,EMCUIStyleTarget::Background);
        const FText Name=FText::AsCultureInvariant(Entry.Nickname);
        if(Row.Name) {Row.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Row.Name->SetClipping(EWidgetClipping::ClipToBounds);Row.Name->SetText(Entry.bSpectating?UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Scoreboard.ObserverName"),LOCTEXT("ObserverName","{Name} · наблюдает")),FFormatNamedArguments{{TEXT("Name"),Name}},Culture):Name);Row.Name->SetColorAndOpacity(Entry.bSpectating?Palette.Muted:Palette.Ink);Row.Name->SetToolTipText(Row.Name->GetText());}
        if(Row.Points) Row.Points->SetText(UMCUIDataLibrary::NumberForCulture(Entry.Points,Culture));
        if(Row.Ping) Row.Ping->SetText(UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Scoreboard.PingValue"),LOCTEXT("PingValue","{Ping} мс")),FFormatNamedArguments{{TEXT("Ping"),UMCUIDataLibrary::NumberForCulture(Entry.bHost?0:FMath::RoundToInt(Entry.PingMilliseconds),Culture)}},Culture));
        if(Row.Icon) {Row.Icon->Tint=Entry.PlayerColor;Row.Icon->InkTint=Palette.Ink;Row.Icon->EmptyTint=Palette.Muted;Row.Icon->HostTint=Palette.Warning;Row.Icon->bHost=Entry.bHost;Row.Icon->bDead=Entry.bSpectating;Row.Icon->InvalidateLayoutAndVolatility();}
        if(Row.MuteButton)
        {
            Row.MuteButton->Configure(this,Entry.PlayerState);Row.MuteButton->SetIsEnabled(Entry.bVoiceControlEnabled);
            Row.MuteButton->SetToolTipText(Entry.bLocalPlayer?
                Localized(TEXT("UI.Scoreboard.SelfMuteHint"),LOCTEXT("SelfMuteHint","Включить или выключить свой микрофон")):
                Localized(TEXT("UI.Scoreboard.MuteHint"),LOCTEXT("MuteHint","Отключить голос этого игрока только для себя")));
        }
        if(Row.MuteLabel) Row.MuteLabel->SetText(!Entry.bVoiceControlEnabled?
            Localized(TEXT("UI.Scoreboard.NoVoice"),LOCTEXT("NoVoice","—")):
            Entry.bLocalPlayer?(Entry.bVoiceMuted?Localized(TEXT("UI.Scoreboard.SelfUnmute"),LOCTEXT("SelfUnmute","Микрофон ВКЛ")):Localized(TEXT("UI.Scoreboard.SelfMute"),LOCTEXT("SelfMute","Микрофон ВЫКЛ"))):
            Entry.bVoiceMuted?Localized(TEXT("UI.Scoreboard.Unmute"),LOCTEXT("Unmute","Включить")):Localized(TEXT("UI.Scoreboard.Mute"),LOCTEXT("Mute","Отключить")));
    }
}
void UMCScoreboardWidget::UpdatePresentationLayout(FVector2D Area)
{
    if(!bUseNativeLayout || !WidgetTree || !Players) return;
    auto* Size=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("ScoreDesignSize")));
    auto* Frame=Cast<UBorder>(WidgetTree->FindWidget(TEXT("ScoreFrame")));
    auto* Column=Frame?Cast<UVerticalBox>(Frame->GetContent()):nullptr;
    auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("ScoreScroll")));
    // The saved v10 tree predates these stable native wrapper names. Its
    // actual ancestors are the contract, while artist opt-out still wins.
    if(!Size) for(UWidget* Parent=Frame?Frame->GetParent():nullptr;Parent;Parent=Parent->GetParent())
        if((Size=Cast<USizeBox>(Parent))) break;
    if(!Scroll) for(UWidget* Parent=Players->GetParent();Parent;Parent=Parent->GetParent())
        if((Scroll=Cast<UScrollBox>(Parent))) break;
    if(!Size || !Column || !Scroll) return;
    if(Area.IsNearlyZero()) Area=GetCachedGeometry().GetLocalSize();
    if(Area.X<=1 || Area.Y<=1) Area=FVector2D(1920,1080);
    if(!bLayoutDirty && Area.Equals(LastLayoutArea,.5f)) return;
    LastLayoutArea=Area;
    const float Width=FMath::Min(1060.f,FMath::Max(620.f,float(Area.X)-48.f));
    Size->SetWidthOverride(Width);
    for(const TCHAR* Name:{TEXT("ScoreTitle"),TEXT("ScoreFooterText"),TEXT("ScorePlayerHeader"),TEXT("ScorePointsHeader"),TEXT("ScorePingHeader"),TEXT("ScoreVoiceHeader")})
        if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(Name))) {
            const FName LabelName=Label->GetFName();
            const float Wrap=LabelName==TEXT("ScoreTitle")?Width-196.f:LabelName==TEXT("ScoreFooterText")?Width-68.f:
                LabelName==TEXT("ScorePointsHeader")?120.f:LabelName==TEXT("ScorePingHeader")?128.f:LabelName==TEXT("ScoreVoiceHeader")?148.f:Width-524.f;
            Label->SetAutoWrapText(true);Label->SetWrapTextAt(FMath::Max(90.f,Wrap));Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        }
    // Existing saved rows retain their widgets; only the opt-in native layout
    // gives the points/ping columns room for the newly appended voice controls.
    auto ColumnWidth=[](UWidget* Widget,float Value)
    {if(Widget) if(auto* Parent=Cast<USizeBox>(Widget->GetParent())) Parent->SetWidthOverride(Value);};
    ColumnWidth(WidgetTree->FindWidget(TEXT("ScorePointsHeader")),120);
    ColumnWidth(WidgetTree->FindWidget(TEXT("ScorePingHeader")),128);
    for(const auto& Row:Rows) {ColumnWidth(Row.Points,120);ColumnWidth(Row.Ping,128);}
    for(FName Name:{FName(TEXT("HubPartySummary")),FName(TEXT("HubPartyStatus"))})
        if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(Name))) Label->SetWrapTextAt(Width-84.f);
    Scroll->SetAllowOverscroll(false);
    Column->ForceLayoutPrepass();Players->ForceLayoutPrepass();
    float RowsHeight=0;
    for(int32 Index=0;Index<Rows.Num();++Index) if(PlayerEntries.IsValidIndex(Index) && Rows[Index].Background) {
        UBorder* Row=Rows[Index].Background;
        const auto* RowSlot=Cast<UVerticalBoxSlot>(Row->Slot);
        RowsHeight+=Row->GetDesiredSize().Y+(RowSlot?RowSlot->GetPadding().GetDesiredSize().Y:0.f);
    }
    if(!PlayerEntries.IsEmpty() && RowsHeight<1.f) {bLayoutDirty=true;return;}
    float Height=Frame->GetPadding().GetDesiredSize().Y+8.f;
    for(UWidget* Child:Column->GetAllChildren()) {
        const auto* ChildSlot=Cast<UVerticalBoxSlot>(Child->Slot);
        Height+=(Child==Scroll?RowsHeight:Child->GetDesiredSize().Y)+(ChildSlot?ChildSlot->GetPadding().GetDesiredSize().Y:0.f);
    }
    const float MaximumHeight=FMath::Max(220.f,float(Area.Y)-48.f);
    bool CanScale=false;
    for(UWidget* Parent=Size->GetParent();Parent;Parent=Parent->GetParent()) if(Cast<UScaleBox>(Parent)) {CanScale=true;break;}
    const bool FitWholeParty=IsHubPartyPresentation() && CanScale;
    Size->SetHeightOverride(FitWholeParty?FMath::Max(220.f,FMath::CeilToFloat(Height)):FMath::Clamp(FMath::CeilToFloat(Height),220.f,MaximumHeight));
    // TAB is a held overlay. Keeping the ordinary four rows fully visible is
    // preferable to a permanently allocated blank fifth row.
    Scroll->SetScrollBarVisibility(!FitWholeParty && Height>MaximumHeight?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    bLayoutDirty=false;
}
void UMCScoreboardWidget::ApplyTheme()
{
    if(!WidgetTree) return;PresentEntries();RefreshHubPartyControls();
    BindStyle(TEXT("ScoreFrame"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    BindStyle(TEXT("ScoreColumnsFrame"),EMCUIColorToken::SurfaceAlt,EMCUIStyleTarget::Background);
    BindStyle(TEXT("ScoreHeading"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    BindStyle(TEXT("ScoreFooterFrame"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    BindStyle(TEXT("ScoreTitle"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,30,true);
    for(const auto* HeaderName:{TEXT("ScorePlayerHeader"),TEXT("ScorePointsHeader"),TEXT("ScorePingHeader"),TEXT("ScoreVoiceHeader")}) BindStyle(HeaderName,WidgetTree->FindWidget(TEXT("ScoreColumnsFrame"))?EMCUIColorToken::Ink:EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,20,true);
    for(auto& Row:Rows) {
        if(Row.Name) BindStyle(Row.Name->GetFName(),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,22,true);
        if(Row.Points) BindStyle(Row.Points->GetFName(),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,24,true);
        if(Row.Ping) BindStyle(Row.Ping->GetFName(),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,22,true);
    }
    BindStyle(TEXT("ScoreFooterText"),WidgetTree->FindWidget(TEXT("ScoreFooterFrame"))?EMCUIColorToken::Ink:EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,16);
    if(IsHubPartyPresentation() && bUseSoftPartyPresentation) {
        // These surfaces are analytic rounded brushes. Do not allocate Phase1
        // material instances only to replace them immediately below every .25s.
        TArray<FMCUIStyleBinding> AllBindings=StyleBindings;
        StyleBindings.RemoveAll([](const FMCUIStyleBinding& Binding) {return Binding.Target==EMCUIStyleTarget::Background || Binding.Target==EMCUIStyleTarget::Button;});
        Super::ApplyTheme();StyleBindings=MoveTemp(AllBindings);
    } else Super::ApplyTheme();
    // Entry-specific semantic colors must survive the shared text bindings.
    const auto& Palette=GetResolvedTheme()->Palette;
    for(int32 Index=0;Index<Rows.Num();++Index) if(PlayerEntries.IsValidIndex(Index) && Rows[Index].Name) Rows[Index].Name->SetColorAndOpacity(PlayerEntries[Index].bSpectating?Palette.Muted:Palette.Ink);
    if(auto* Icon=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("ScoreHeadingTooth")))) {Icon->Tint=Palette.Accent;Icon->InkTint=Palette.Ink;Icon->InvalidateLayoutAndVolatility();}
    auto Copy=[&](FName WidgetName,FName Key,const FText& Fallback){if(auto* Text=Cast<UTextBlock>(WidgetTree->FindWidget(WidgetName))) Text->SetText(Localized(Key,Fallback));};
    Copy(TEXT("ScoreTitle"),TEXT("UI.Scoreboard.Title"),LOCTEXT("Title","КОМАНДА"));
    Copy(TEXT("ScorePlayerHeader"),TEXT("UI.Scoreboard.Player"),LOCTEXT("Player","Игрок"));
    Copy(TEXT("ScorePointsHeader"),TEXT("UI.Scoreboard.Points"),LOCTEXT("Points","Очки"));
    Copy(TEXT("ScoreVoiceHeader"),TEXT("UI.Scoreboard.Voice"),LOCTEXT("Voice","Голос"));
    if(IsHubPartyPresentation()) {
        Copy(TEXT("ScoreFooterText"),TEXT("hub.party.footer"),LOCTEXT("PartyFooter","TAB / ESC — закрыть · Голос отключается только для вас"));
        if(auto* Key=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ScoreTabKey")))) Key->SetText(FText::AsCultureInvariant(TEXT("TAB / ESC")));
    } else {
        Copy(TEXT("ScoreFooterText"),TEXT("UI.Scoreboard.VoiceFooter"),LOCTEXT("VoiceFooter","TAB — держать · ↑/↓ — участник · Enter/A — голос · Отпусти TAB, чтобы закрыть"));
        if(auto* Key=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ScoreTabKey")))) Key->SetText(FText::AsCultureInvariant(TEXT("TAB")));
    }
    RefreshSoftPartySurfaces(0.f);bLayoutDirty=true;UpdatePresentationLayout();
}
void UMCScoreboardWidget::ApplyEditorPreview(float Time)
{
    PreviewTime=FMath::Max(0.f,Time);BuildPresentationTree();PlayerEntries.Reset();
    if(PreviewScenario!=EMCGameplayUIPreviewScenario::Empty) for(int32 Index=0;Index<4;++Index) {
        auto& Entry=PlayerEntries.AddDefaulted_GetRef();Entry.Nickname=PreviewScenario==EMCGameplayUIPreviewScenario::LongText?TEXT("Очень_длинное_имя_игрока_для_проверки_вёрстки"):FString::Printf(TEXT("P%d"),Index+1);Entry.Points=920-Index*180;Entry.PingMilliseconds=Index==0?47.f:28.f+Index*14.f;Entry.bHost=Index==0;Entry.bSpectating=Index==3;Entry.PlayerColor=Index==2?GetResolvedTheme()->Palette.BossHealth:GetResolvedTheme()->Palette.Accent;Entry.bLocalPlayer=Index==0;Entry.bVoiceControlEnabled=Index<3;Entry.bVoiceMuted=Index==2;
    }
    RebuildRows(4);ApplyTheme();
}
#undef LOCTEXT_NAMESPACE
