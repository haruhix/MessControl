#include "MCUnifiedFrontendWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Components/OverlaySlot.h"
#include "Components/Overlay.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCUnifiedFrontend"

namespace
{
    FLinearColor MCUnifiedFrontendPrivate_Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    const FLinearColor MCUnifiedFrontendPrivate_Blue=MCUnifiedFrontendPrivate_Color(TEXT("4C79AC")), MCUnifiedFrontendPrivate_Navy=MCUnifiedFrontendPrivate_Color(TEXT("2B2B83"));
    const FLinearColor MCUnifiedFrontendPrivate_Inner=MCUnifiedFrontendPrivate_Color(TEXT("315D73")), MCUnifiedFrontendPrivate_Paper=MCUnifiedFrontendPrivate_Color(TEXT("FFF9F8"));
    enum MCUnifiedFrontendPrivate_EItemKind : uint8 { MCUnifiedFrontendPrivate_Text, MCUnifiedFrontendPrivate_Action, MCUnifiedFrontendPrivate_Picker, MCUnifiedFrontendPrivate_Address };
    struct MCUnifiedFrontendPrivate_FItem
    {
        EMCUnifiedFrontendPage Page=EMCUnifiedFrontendPage::Join;
        const TCHAR* Name;
        const TCHAR* Source;
        const TCHAR* Key;
        const TCHAR* Copy;
        uint8 Kind=MCUnifiedFrontendPrivate_Text;
        EMCMainMenuAction Command=EMCMainMenuAction::Back;
        bool bHeading=false;
    };
    // These are the five real source pages, not a second session/menu model.
    const MCUnifiedFrontendPrivate_FItem MCUnifiedFrontendPrivate_Items[]={
        {EMCUnifiedFrontendPage::Join,TEXT("Heading"),TEXT("Text1"),TEXT("UI.MainMenu.JoinHeading"),TEXT("ПОДКЛЮЧЕНИЕ"),MCUnifiedFrontendPrivate_Text,EMCMainMenuAction::Back,true},
        {EMCUnifiedFrontendPage::Join,TEXT("FindRooms"),TEXT(""),TEXT("UI.MainMenu.Action.FindRooms"),TEXT("НАЙТИ КОМНАТЫ STEAM"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::FindRooms},
        {EMCUnifiedFrontendPage::Join,TEXT("RoomLabel"),TEXT("RoomPickerLabel"),TEXT("UI.MainMenu.FriendRoom"),TEXT("Комната друга")},
        {EMCUnifiedFrontendPage::Join,TEXT("RoomPicker"),TEXT("RoomPicker"),TEXT(""),TEXT(""),MCUnifiedFrontendPrivate_Picker},
        {EMCUnifiedFrontendPage::Join,TEXT("JoinRoom"),TEXT(""),TEXT("UI.MainMenu.Action.JoinRoom"),TEXT("ПРИСОЕДИНИТЬСЯ К ВЫБРАННОЙ КОМНАТЕ"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::JoinRoom},
        {EMCUnifiedFrontendPage::Join,TEXT("ModeBody"),TEXT("ModeBody"),TEXT("UI.MainMenu.EditorJoinHint"),TEXT("В редакторе Steam отключён. Введи IP хоста и при необходимости порт: 192.168.1.10:7777.")},
        {EMCUnifiedFrontendPage::Join,TEXT("Address"),TEXT("AddressBox"),TEXT("UI.MainMenu.HostAddress"),TEXT("Адрес хоста"),MCUnifiedFrontendPrivate_Address},
        {EMCUnifiedFrontendPage::Join,TEXT("JoinAddress"),TEXT(""),TEXT("UI.MainMenu.Action.JoinAddress"),TEXT("ПРИСОЕДИНИТЬСЯ ПО IP"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::JoinAddress},
        {EMCUnifiedFrontendPage::Join,TEXT("Back"),TEXT(""),TEXT("UI.MainMenu.Action.Back"),TEXT("НАЗАД"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::Back},
        {EMCUnifiedFrontendPage::Connection,TEXT("AvailableHeading"),TEXT("AvailableHeading"),TEXT("UI.MainMenu.ConnectionHeading"),TEXT("ИГРАТЬ ВМЕСТЕ"),MCUnifiedFrontendPrivate_Text,EMCMainMenuAction::Back,true},
        {EMCUnifiedFrontendPage::Connection,TEXT("AvailableBody"),TEXT("AvailableBody"),TEXT("UI.MainMenu.ConnectionBody"),TEXT("Создай комнату для обычной смены или присоединись к друзьям.")},
        {EMCUnifiedFrontendPage::Connection,TEXT("CreateLobby"),TEXT(""),TEXT("UI.MainMenu.Action.PlayHub"),TEXT("ИГРАТЬ"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::CreateLobby},
        {EMCUnifiedFrontendPage::Connection,TEXT("JoinLobby"),TEXT(""),TEXT("UI.MainMenu.Action.JoinLobby"),TEXT("ПРИСОЕДИНИТЬСЯ К ДРУЗЬЯМ"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::JoinLobby},
        {EMCUnifiedFrontendPage::Connection,TEXT("UnavailableHeading"),TEXT("UnavailableHeading"),TEXT("UI.MainMenu.ComingSoon"),TEXT("СКОРО"),MCUnifiedFrontendPrivate_Text,EMCMainMenuAction::Back,true},
        {EMCUnifiedFrontendPage::Connection,TEXT("UnavailableBody"),TEXT("UnavailableBody"),TEXT("UI.MainMenu.DayUnavailableBody"),TEXT("Игровой контент этого дня ещё не подключён. Пока доступна игра в Day 01.")},
        {EMCUnifiedFrontendPage::Connection,TEXT("Back"),TEXT(""),TEXT("UI.MainMenu.Action.Back"),TEXT("НАЗАД"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::Back},
        {EMCUnifiedFrontendPage::Lobby,TEXT("Heading"),TEXT("Text1"),TEXT("UI.MainMenu.LobbyHeading"),TEXT("КОМНАТА"),MCUnifiedFrontendPrivate_Text,EMCMainMenuAction::Back,true},
        {EMCUnifiedFrontendPage::Lobby,TEXT("Players"),TEXT("Players"),TEXT("UI.MainMenu.LoadingPlayers"),TEXT("Загружаем участников…")},
        {EMCUnifiedFrontendPage::Lobby,TEXT("StartHint"),TEXT("Text3"),TEXT("UI.MainMenu.LobbyStartHint"),TEXT("Хост запускает обычную смену, когда команда собрана.")},
        {EMCUnifiedFrontendPage::Lobby,TEXT("Start"),TEXT(""),TEXT("UI.MainMenu.Action.StartLobby"),TEXT("НАЧАТЬ ИГРУ"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::StartLobby},
        {EMCUnifiedFrontendPage::Lobby,TEXT("Invite"),TEXT(""),TEXT("UI.MainMenu.Action.Invite"),TEXT("ПРИГЛАСИТЬ ДРУЗЕЙ STEAM"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::Invite},
        {EMCUnifiedFrontendPage::Lobby,TEXT("LocalHint"),TEXT("LocalHint"),TEXT("UI.MainMenu.LobbyLocalHint"),TEXT("Друзья: Join Lobby → IP этого компьютера:7777.")},
        {EMCUnifiedFrontendPage::Lobby,TEXT("Settings"),TEXT(""),TEXT("UI.MainMenu.Action.Settings"),TEXT("НАСТРОЙКИ"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::Settings},
        {EMCUnifiedFrontendPage::Lobby,TEXT("Leave"),TEXT(""),TEXT("UI.MainMenu.Action.LeaveLobby"),TEXT("ВЫЙТИ ИЗ КОМНАТЫ"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::LeaveLobby},
        {EMCUnifiedFrontendPage::Leave,TEXT("Heading"),TEXT("Text1"),TEXT("UI.Menu.LeaveHeading"),TEXT("Выйти из комнаты?"),MCUnifiedFrontendPrivate_Text,EMCMainMenuAction::Back,true},
        {EMCUnifiedFrontendPage::Leave,TEXT("Body"),TEXT("ModeBody"),TEXT("UI.Menu.LeaveClientBody"),TEXT("Ты вернёшься в главное меню.")},
        {EMCUnifiedFrontendPage::Leave,TEXT("Cancel"),TEXT(""),TEXT("UI.MainMenu.Action.Cancel"),TEXT("ОТМЕНА"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::Back},
        {EMCUnifiedFrontendPage::Leave,TEXT("Confirm"),TEXT(""),TEXT("UI.MainMenu.Action.ConfirmLeave"),TEXT("ВЫЙТИ В ГЛАВНОЕ МЕНЮ"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::ConfirmLeave},
        {EMCUnifiedFrontendPage::Exit,TEXT("Heading"),TEXT("Text1"),TEXT("UI.Menu.ExitHeading"),TEXT("Выйти из игры?"),MCUnifiedFrontendPrivate_Text,EMCMainMenuAction::Back,true},
        {EMCUnifiedFrontendPage::Exit,TEXT("Cancel"),TEXT(""),TEXT("UI.MainMenu.Action.Cancel"),TEXT("ОТМЕНА"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::Back},
        {EMCUnifiedFrontendPage::Exit,TEXT("Confirm"),TEXT(""),TEXT("UI.MainMenu.Action.ConfirmExit"),TEXT("ВЫЙТИ ИЗ ИГРЫ"),MCUnifiedFrontendPrivate_Action,EMCMainMenuAction::ConfirmExit},
    };
    const TCHAR* MCUnifiedFrontendPrivate_PageName(EMCUnifiedFrontendPage Page)
    {
        const TCHAR* Names[]={TEXT("Join"),TEXT("Connection"),TEXT("Lobby"),TEXT("Leave"),TEXT("Exit")};
        return Names[FMath::Clamp(int32(Page),0,4)];
    }
    bool MCUnifiedFrontendPrivate_ConvertPage(EMCMainMenuPage Source,EMCUnifiedFrontendPage& Out)
    {
        switch(Source) {
        case EMCMainMenuPage::Join:Out=EMCUnifiedFrontendPage::Join;return true;
        case EMCMainMenuPage::Connection:Out=EMCUnifiedFrontendPage::Connection;return true;
        case EMCMainMenuPage::Lobby:Out=EMCUnifiedFrontendPage::Lobby;return true;
        case EMCMainMenuPage::Leave:Out=EMCUnifiedFrontendPage::Leave;return true;
        case EMCMainMenuPage::Exit:Out=EMCUnifiedFrontendPage::Exit;return true;
        default:return false;
        }
    }
    FName MCUnifiedFrontendPrivate_ViewName(const MCUnifiedFrontendPrivate_FItem& Item) {return FName(*FString::Printf(TEXT("Frontend%s_%s"),MCUnifiedFrontendPrivate_PageName(Item.Page),Item.Name));}
    FName MCUnifiedFrontendPrivate_RowName(FName Name) {return FName(*(Name.ToString()+TEXT("Row")));}
    bool MCUnifiedFrontendPrivate_Within(const UWidget* Widget,const UWidget* Root)
    {
        for(const auto* Parent=Widget;Parent;Parent=Parent->GetParent()) if(Parent==Root) return true;
        return false;
    }
    UWidget* MCUnifiedFrontendPrivate_FocusTarget(UWidget* Widget)
    {
        if(auto* Button=Cast<UMCUniversalUIWidget>(Widget))
            return Button->WidgetTree?Button->WidgetTree->FindWidget(TEXT("UniversalSurface")):nullptr;
        return Widget;
    }
}

void UMCUnifiedFrontendBinding::Clicked() {if(Owner.IsValid()) Owner->ForwardAction(this);}
void UMCUnifiedFrontendBinding::PickerChanged(FString Value,ESelectInfo::Type) {if(Owner.IsValid()) Owner->ForwardPicker(this,Value);}
void UMCUnifiedFrontendBinding::AddressChanged(const FText& Value) {if(Owner.IsValid()) Owner->ForwardAddress(this,Value);}

UMCUnifiedFrontendWidget::UMCUnifiedFrontendWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;SetIsFocusable(true);
}

void UMCUnifiedFrontendWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {UpdateResponsiveLayout();BindControls();return;}
    auto Shape=[&](FName Name,FLinearColor Fill,FMargin ContentInset,bool bButton=false) {
        auto* Value=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
        Value->Kind=bButton?EMCUniversalUIKind::Button:EMCUniversalUIKind::Panel;Value->Label=FText::GetEmpty();
        Value->bCustomColors=true;Value->bUseThemeShape=false;Value->CustomFill=Fill;Value->CustomOutline=MCUnifiedFrontendPrivate_Navy;Value->CustomShadow=MCUnifiedFrontendPrivate_Navy;
        Value->CornerRadius=bButton?18:24;Value->OutlineWidth=3;Value->ShadowOffset=bButton?FVector2D(4,6):FVector2D(6,9);Value->ContentPadding=ContentInset;
        Value->Initialize();Value->BuildPresentationTree();Value->ApplyTheme();return Value;
    };
    auto Label=[&](FName Name,const FText& Copy,int32 FontSize,FLinearColor Tint) {
        auto* Value=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Value->SetText(Copy);
        Value->SetAutoWrapText(true);Value->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        Value->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);Value->SetJustification(ETextJustify::Center);
        Value->SetColorAndOpacity(Tint);if(GetResolvedTheme()) Value->SetFont(GetResolvedTheme()->MakeFont(FontSize,true));return Value;
    };
    auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("FrontendDesignBounds"));
    Bounds->SetMinDesiredWidth(660);Bounds->SetMaxDesiredWidth(1000);Bounds->SetMaxDesiredHeight(960);WidgetTree->RootWidget=Bounds;
    auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("FrontendColumn"));Bounds->SetContent(Stack);
    auto* Context=Shape(TEXT("FrontendContextBadge"),MCUnifiedFrontendPrivate_Paper,FMargin(22,9));
    Context->SetContentForSlot(TEXT("Content"),Label(TEXT("FrontendContextTitle"),Localized(TEXT("UI.MainMenu.BrandTitle"),LOCTEXT("Brand","MESS CONTROL")),24,MCUnifiedFrontendPrivate_Color(TEXT("AE466D"))));
    Stack->AddChildToVerticalBox(Context)->SetPadding(FMargin(0,0,8,15));
    auto* Outer=Shape(TEXT("FrontendOuter"),MCUnifiedFrontendPrivate_Blue,FMargin(20));Stack->AddChildToVerticalBox(Outer)->SetPadding(FMargin(0,0,8,12));
    auto* InnerPanel=Shape(TEXT("FrontendInner"),MCUnifiedFrontendPrivate_Inner,FMargin(24,18));InnerPanel->ShadowOffset=FVector2D::ZeroVector;InnerPanel->ApplyTheme();Outer->SetContentForSlot(TEXT("Content"),InnerPanel);
    auto* Viewport=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("FrontendScrollBounds"));Viewport->SetMinDesiredHeight(180);Viewport->SetMaxDesiredHeight(710);InnerPanel->SetContentForSlot(TEXT("Content"),Viewport);
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("FrontendScroll"));Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);Scroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);Viewport->SetContent(Scroll);
    auto* Pages=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("FrontendPages"));Scroll->AddChild(Pages);
    UVerticalBox* Bodies[5];
    for(int32 I=0;I<5;++I) {Bodies[I]=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),FName(*FString::Printf(TEXT("Frontend%sPage"),MCUnifiedFrontendPrivate_PageName(EMCUnifiedFrontendPage(I)))));Pages->AddChild(Bodies[I]);}
    for(const auto& Item:MCUnifiedFrontendPrivate_Items) {
        const FName Name=MCUnifiedFrontendPrivate_ViewName(Item);UWidget* Control=nullptr;
        if(Item.Kind==MCUnifiedFrontendPrivate_Text) {
            Control=Label(Name,Localized(Item.Key,FText::AsCultureInvariant(Item.Copy)),Item.bHeading?30:21,MCUnifiedFrontendPrivate_Paper);TextBindings.Add(Name,Item.Key);
        } else if(Item.Kind==MCUnifiedFrontendPrivate_Action) {
            auto* Button=Shape(Name,MCUnifiedFrontendPrivate_Blue,FMargin(22,13),true);
            auto* Caption=Label(FName(*(Name.ToString()+TEXT("Caption"))),Localized(Item.Key,FText::AsCultureInvariant(Item.Copy)),24,MCUnifiedFrontendPrivate_Paper);
            Caption->SetWrapTextAt(480);Button->SetContentForSlot(TEXT("Content"),Caption);TextBindings.Add(Caption->GetFName(),Item.Key);Control=Button;
        } else if(Item.Kind==MCUnifiedFrontendPrivate_Picker) {
            auto* MCUnifiedFrontendPrivate_Picker=WidgetTree->ConstructWidget<UMCMainMenuPicker>(UMCMainMenuPicker::StaticClass(),Name);
            if(GetResolvedTheme()) MCUnifiedFrontendPrivate_Picker->InitializePresentationFont(GetResolvedTheme()->MakeFont(22,true));MCUnifiedFrontendPrivate_Picker->InitializePresentationForeground(MCUnifiedFrontendPrivate_Navy);
            auto Style=MCUnifiedFrontendPrivate_Picker->GetWidgetStyle();auto Button=Style.ComboButtonStyle.ButtonStyle;
            Button.SetNormal(FSlateRoundedBoxBrush(MCUnifiedFrontendPrivate_Paper,14,MCUnifiedFrontendPrivate_Navy,3));Button.SetHovered(FSlateRoundedBoxBrush(MCUnifiedFrontendPrivate_Color(TEXT("E4EDFF")),14,MCUnifiedFrontendPrivate_Navy,3));
            Button.SetPressed(FSlateRoundedBoxBrush(MCUnifiedFrontendPrivate_Blue,14,MCUnifiedFrontendPrivate_Navy,3));Button.SetDisabled(FSlateRoundedBoxBrush(MCUnifiedFrontendPrivate_Paper.CopyWithNewOpacity(.45f),14,MCUnifiedFrontendPrivate_Navy,3));
            Button.SetNormalPadding(FMargin(10,7));Button.SetPressedPadding(FMargin(10,7));Style.ComboButtonStyle.SetButtonStyle(Button);
            Style.ComboButtonStyle.SetMenuBorderBrush(FSlateRoundedBoxBrush(MCUnifiedFrontendPrivate_Paper,14,MCUnifiedFrontendPrivate_Navy,3));Style.ComboButtonStyle.DownArrowImage.TintColor=MCUnifiedFrontendPrivate_Navy;MCUnifiedFrontendPrivate_Picker->SetWidgetStyle(Style);
            MCUnifiedFrontendPrivate_Picker->SetPresentationOptions({TEXT("AfterCoffee · 2 / 4")});Control=MCUnifiedFrontendPrivate_Picker;
        } else {
            auto* AddressBox=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),Name);
            auto Style=AddressBox->GetWidgetStyle();if(GetResolvedTheme()) Style.SetFont(GetResolvedTheme()->MakeFont(22,true));
            Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(MCUnifiedFrontendPrivate_Paper,14,MCUnifiedFrontendPrivate_Navy,3));Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(MCUnifiedFrontendPrivate_Paper,14,MCUnifiedFrontendPrivate_Navy,3));Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(MCUnifiedFrontendPrivate_Paper,14,MCUnifiedFrontendPrivate_Navy,3));
            AddressBox->SetWidgetStyle(Style);AddressBox->SetForegroundColor(MCUnifiedFrontendPrivate_Navy);AddressBox->SetHintText(Localized(Item.Key,FText::AsCultureInvariant(Item.Copy)));
            AddressBox->SetText(FText::AsCultureInvariant(TEXT("127.0.0.1:7777")));Control=AddressBox;
        }
        auto* Row=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedFrontendPrivate_RowName(Name));Row->SetMaxDesiredWidth(920);if(Item.Kind!=MCUnifiedFrontendPrivate_Text) Row->SetMinDesiredHeight(56);Row->SetContent(Control);
        Bodies[int32(Item.Page)]->AddChildToVerticalBox(Row)->SetPadding(FMargin(2,Item.bHeading?5:4,8,Item.bHeading?17:11));
    }
    auto* Status=Label(TEXT("FrontendStatus"),FText::GetEmpty(),20,MCUnifiedFrontendPrivate_Paper);Scroll->AddChild(Status);
    BindControls();SelectPage(PreviewPage);ApplyTheme();
}

void UMCUnifiedFrontendWidget::UnbindControls()
{
    for(UMCUnifiedFrontendBinding* Binding:Bindings) if(Binding && WidgetTree) {
        auto* View=WidgetTree->FindWidget(Binding->ViewName);
        if(auto* Button=Cast<UMCUniversalUIWidget>(View)) Button->OnClicked.RemoveDynamic(Binding,&UMCUnifiedFrontendBinding::Clicked);
        if(auto* MCUnifiedFrontendPrivate_Picker=Cast<UComboBoxString>(View)) MCUnifiedFrontendPrivate_Picker->OnSelectionChanged.RemoveDynamic(Binding,&UMCUnifiedFrontendBinding::PickerChanged);
        if(auto* AddressBox=Cast<UEditableTextBox>(View)) AddressBox->OnTextChanged.RemoveDynamic(Binding,&UMCUnifiedFrontendBinding::AddressChanged);
    }
    Bindings.Reset();
}
void UMCUnifiedFrontendWidget::BindControls()
{
    UnbindControls();if(!WidgetTree || IsDesignTime() || bEditorFixture) return;
    for(const auto& Item:MCUnifiedFrontendPrivate_Items) {
        auto* View=WidgetTree->FindWidget(MCUnifiedFrontendPrivate_ViewName(Item));if(!View) continue;
        auto* Binding=NewObject<UMCUnifiedFrontendBinding>(this);Bindings.Add(Binding);Binding->Owner=this;Binding->Page=Item.Page;
        Binding->ViewName=View->GetFName();Binding->Kind=Item.Kind;Binding->Action=Item.Command;
        Binding->SourceSuffix=Item.Kind==MCUnifiedFrontendPrivate_Action?FName(*FString::Printf(TEXT("Action%d"),int32(Item.Command))):FName(Item.Source);
        if(auto* Button=Cast<UMCUniversalUIWidget>(View)) Button->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedFrontendBinding::Clicked);
        if(auto* MCUnifiedFrontendPrivate_Picker=Cast<UComboBoxString>(View)) {MCUnifiedFrontendPrivate_Picker->OnSelectionChanged.AddUniqueDynamic(Binding,&UMCUnifiedFrontendBinding::PickerChanged);MCUnifiedFrontendPrivate_Picker->OnGenerateWidgetEvent.BindDynamic(this,&ThisClass::GeneratePickerItem);}
        if(auto* AddressBox=Cast<UEditableTextBox>(View)) AddressBox->OnTextChanged.AddUniqueDynamic(Binding,&UMCUnifiedFrontendBinding::AddressChanged);
    }
}
void UMCUnifiedFrontendWidget::SelectPage(EMCUnifiedFrontendPage Page)
{
    if(auto* Pages=WidgetTree?WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("FrontendPages")):nullptr) Pages->SetActiveWidgetIndex(FMath::Clamp(int32(Page),0,4));
}
bool UMCUnifiedFrontendWidget::HasBoundBackend() const
{
    EMCUnifiedFrontendPage Page=EMCUnifiedFrontendPage::Join;
    return !IsDesignTime() && !bEditorFixture && Backend.IsValid() && Backend->GetWorld()==GetWorld()
        && Backend->GetOwningPlayer()==GetOwningPlayer() && (Backend->IsInViewport() || Backend->GetParent())
        && Backend->IsVisible() && MCUnifiedFrontendPrivate_ConvertPage(Backend->GetCurrentMenuPage(),Page) && Backend->GetActiveMenuPageRoot();
}
void UMCUnifiedFrontendWidget::SetBackend(UMCMainMenuWidget* Menu)
{
    if(Backend.Get()!=Menu) {
        Backend=Menu;bEditorFixture=false;
        for(UMCUnifiedFrontendBinding* Binding:Bindings) if(Binding) {Binding->SourcePage.Reset();Binding->SourceControl.Reset();}
    }
    if(Menu) RefreshFromBackend();
}
UWidget* UMCUnifiedFrontendWidget::FindSource(EMCUnifiedFrontendPage Page,FName Suffix) const
{
    if(!HasBoundBackend() || !Backend->WidgetTree) return nullptr;
    EMCUnifiedFrontendPage ActivePage;if(!MCUnifiedFrontendPrivate_ConvertPage(Backend->GetCurrentMenuPage(),ActivePage) || ActivePage!=Page) return nullptr;
    auto* Source=Backend->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("Menu%s_%s"),MCUnifiedFrontendPrivate_PageName(Page),*Suffix.ToString())));
    return Source && MCUnifiedFrontendPrivate_Within(Source,Backend->GetActiveMenuPageRoot())?Source:nullptr;
}
bool UMCUnifiedFrontendWidget::IsSourceVisible(const UWidget* Source,const UWidget* Page) const
{
    if(!Source || !HasBoundBackend() || Page!=Backend->GetActiveMenuPageRoot() || !MCUnifiedFrontendPrivate_Within(Source,Page)) return false;
    const auto* HiddenSourceRoot=Backend->WidgetTree?Backend->WidgetTree->RootWidget.Get():nullptr;
    for(const auto* Parent=Source;Parent;Parent=Parent->GetParent()) {
        const auto SourceVisibility=Parent->GetVisibility();
        // Only this exact retained root is intentionally hidden by the presentation subsystem.
        if(SourceVisibility==ESlateVisibility::Collapsed || (SourceVisibility==ESlateVisibility::Hidden && Parent!=HiddenSourceRoot)) return false;
    }
    for(const auto* Parent=Backend->GetParent();Parent;Parent=Parent->GetParent()) if(!Parent->IsVisible()) return false;
    return true;
}
void UMCUnifiedFrontendWidget::RefreshFromBackend()
{
    if(!WidgetTree || !HasBoundBackend()) return;
    TGuardValue<bool> Guard(bSynchronizing,true);EMCUnifiedFrontendPage Page=EMCUnifiedFrontendPage::Join;MCUnifiedFrontendPrivate_ConvertPage(Backend->GetCurrentMenuPage(),Page);SelectPage(Page);
    auto* SourcePage=Backend->GetActiveMenuPageRoot();
    for(UMCUnifiedFrontendBinding* Binding:Bindings) if(Binding) {
        auto* View=WidgetTree->FindWidget(Binding->ViewName);if(!View) continue;
        auto* Source=FindSource(Binding->Page,Binding->SourceSuffix);Binding->SourcePage=Binding->Page==Page?SourcePage:nullptr;Binding->SourceControl=Source;
        auto* Row=WidgetTree->FindWidget(MCUnifiedFrontendPrivate_RowName(Binding->ViewName));if(!Row) Row=View;
        const bool Visible=Source && IsSourceVisible(Source,SourcePage);Row->SetVisibility(Visible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        bool Enabled=Visible;
        for(const auto* Parent=Source;Parent;Parent=Parent->GetParent()) Enabled&=Parent->GetIsEnabled();
        for(const UWidget* Parent=Backend.Get();Parent;Parent=Parent->GetParent()) Enabled&=Parent->GetIsEnabled();
        if(auto* MCUnifiedFrontendPrivate_Action=Cast<UMCMainMenuActionButton>(Source)) Enabled&=MCUnifiedFrontendPrivate_Action->Action==Binding->Action;
        View->SetIsEnabled(Enabled);
        if(auto* To=Cast<UTextBlock>(View)) if(auto* From=Cast<UTextBlock>(Source)) To->SetText(From->GetText());
        if(Cast<UMCUniversalUIWidget>(View)) if(auto* From=Cast<UMCMainMenuActionButton>(Source)) {
            auto* Caption=WidgetTree->FindWidget<UTextBlock>(FName(*(Binding->ViewName.ToString()+TEXT("Caption"))));
            if(auto* Original=Backend->WidgetTree->FindWidget<UTextBlock>(FName(*(Source->GetName()+TEXT("Label"))));Original && MCUnifiedFrontendPrivate_Within(Original,From) && Caption) Caption->SetText(Original->GetText());
        }
        if(auto* To=Cast<UComboBoxString>(View)) if(auto* From=Cast<UComboBoxString>(Source)) {
            bool Same=To->GetOptionCount()==From->GetOptionCount();
            if(Same) for(int32 I=0;I<From->GetOptionCount();++I) if(To->GetOptionAtIndex(I)!=From->GetOptionAtIndex(I)) {Same=false;break;}
            if(!Same) {To->ClearOptions();for(int32 I=0;I<From->GetOptionCount();++I) To->AddOption(From->GetOptionAtIndex(I));}
            if(To->GetSelectedIndex()!=From->GetSelectedIndex()) To->SetSelectedIndex(From->GetSelectedIndex());
        }
        if(auto* To=Cast<UEditableTextBox>(View)) if(auto* From=Cast<UEditableTextBox>(Source)) {
            if(!To->GetText().EqualTo(From->GetText())) To->SetText(From->GetText());To->SetHintText(From->GetHintText());
        }
    }
    if(auto* Context=WidgetTree->FindWidget<UTextBlock>(TEXT("FrontendContextTitle"))) {
        Context->SetText(Backend->GetFrontendTitleText());
        if(auto* Badge=WidgetTree->FindWidget(TEXT("FrontendContextBadge"))) Badge->SetVisibility(Context->GetText().IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    }
    if(auto* Status=WidgetTree->FindWidget<UTextBlock>(TEXT("FrontendStatus"))) {Status->SetText(Backend->GetFrontendStatusText());Status->SetVisibility(Status->GetText().IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
    RefreshTextBounds();
}
bool UMCUnifiedFrontendWidget::CanForward(const UMCUnifiedFrontendBinding* Binding) const
{
    if(bSynchronizing || !Binding || Binding->Owner.Get()!=this || !Bindings.ContainsByPredicate([Binding](const auto& Entry){return Entry.Get()==Binding;}) || !HasBoundBackend()) return false;
    EMCUnifiedFrontendPage Page=EMCUnifiedFrontendPage::Join;if(!MCUnifiedFrontendPrivate_ConvertPage(Backend->GetCurrentMenuPage(),Page) || Page!=Binding->Page) return false;
    auto* Source=Binding->SourceControl.Get();auto* SourcePage=Binding->SourcePage.Get();
    if(SourcePage!=Backend->GetActiveMenuPageRoot() || !Source || FindSource(Page,Binding->SourceSuffix)!=Source || !IsSourceVisible(Source,SourcePage)) return false;
    for(const auto* Parent=Source;Parent;Parent=Parent->GetParent()) if(!Parent->GetIsEnabled()) return false;
    for(const UWidget* Parent=Backend.Get();Parent;Parent=Parent->GetParent()) if(!Parent->GetIsEnabled()) return false;
    return true;
}
void UMCUnifiedFrontendWidget::ForwardAction(UMCUnifiedFrontendBinding* Binding)
{
    if(!CanForward(Binding)) return;
    auto* Source=Cast<UMCMainMenuActionButton>(Binding->SourceControl.Get());if(!Source || Source->Action!=Binding->Action) return;
    Source->OnClicked.Broadcast();RefreshFromBackend();
    if(HasBoundBackend() && GetOwningPlayer()) if(auto* Target=GetDefaultFocusTarget()) Target->SetUserFocus(GetOwningPlayer());
}
void UMCUnifiedFrontendWidget::ForwardPicker(UMCUnifiedFrontendBinding* Binding,const FString& Value)
{
    if(!CanForward(Binding)) return;
    if(auto* Source=Cast<UComboBoxString>(Binding->SourceControl.Get());Source && Source->FindOptionIndex(Value)!=INDEX_NONE) Source->SetSelectedOption(Value);
}
void UMCUnifiedFrontendWidget::ForwardAddress(UMCUnifiedFrontendBinding* Binding,const FText& Value)
{
    if(!CanForward(Binding)) return;
    // The original text box remains the only draft. Enter never initiates a second join path.
    if(auto* Source=Cast<UEditableTextBox>(Binding->SourceControl.Get())) Source->SetText(Value);
}
void UMCUnifiedFrontendWidget::RequestBack()
{
    if(!HasBoundBackend()) return;Backend->RequestBack();RefreshFromBackend();
    if(HasBoundBackend() && GetOwningPlayer()) if(auto* Target=GetDefaultFocusTarget()) Target->SetUserFocus(GetOwningPlayer());
}
UWidget* UMCUnifiedFrontendWidget::GetDefaultFocusTarget() const
{
    if(WidgetTree) for(const UMCUnifiedFrontendBinding* Binding:Bindings) if(Binding && Binding->Kind!=MCUnifiedFrontendPrivate_Text) {
        auto* View=WidgetTree->FindWidget(Binding->ViewName);auto* Row=WidgetTree->FindWidget(MCUnifiedFrontendPrivate_RowName(Binding->ViewName));
        auto* Pages=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("FrontendPages"));
        if(View && Row && Row->IsVisible() && View->GetIsEnabled() && Pages && MCUnifiedFrontendPrivate_Within(View,Pages->GetActiveWidget())) return MCUnifiedFrontendPrivate_FocusTarget(View);
    }
    return const_cast<UMCUnifiedFrontendWidget*>(this);
}
UWidget* UMCUnifiedFrontendWidget::GeneratePickerItem(FString Value)
{
    if(!WidgetTree) return nullptr;
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>();GeneratedPickerItems.Add(Label);Label->SetText(FText::FromString(Value));
    Label->SetAutoWrapText(true);Label->SetWrapTextAt(700);Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    Label->SetColorAndOpacity(MCUnifiedFrontendPrivate_Navy);if(GetResolvedTheme()) Label->SetFont(GetResolvedTheme()->MakeFont(22,true));return Label;
}
void UMCUnifiedFrontendWidget::UpdateResponsiveLayout(FVector2D AvailableSize)
{
    if(!WidgetTree) return;
    auto* RootBounds=WidgetTree->FindWidget<USizeBox>(TEXT("FrontendDesignBounds"));
    auto* Outer=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("FrontendOuter"));
    auto* Inner=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("FrontendInner"));
    auto* Viewport=WidgetTree->FindWidget<USizeBox>(TEXT("FrontendScrollBounds"));
    auto* Pages=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("FrontendPages"));
    if(!RootBounds || !Outer || !Inner || !Viewport || !Pages) return;
    if(WidgetTree->RootWidget==RootBounds) {
        auto* AlignmentRoot=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("FrontendAlignmentRoot"));
        WidgetTree->RootWidget=AlignmentRoot;auto* AlignedCell=AlignmentRoot->AddChildToOverlay(RootBounds);
        AlignedCell->SetHorizontalAlignment(HAlign_Center);AlignedCell->SetVerticalAlignment(VAlign_Center);
    }
    RootBounds->ClearMinDesiredWidth();RootBounds->ClearMinDesiredHeight();Viewport->ClearMinDesiredHeight();
    if(auto* StackCell=Cast<USizeBoxSlot>(RootBounds->GetContent()?RootBounds->GetContent()->Slot:nullptr)) StackCell->SetVerticalAlignment(VAlign_Center);
    if(auto* OuterCell=Cast<UVerticalBoxSlot>(Outer->Slot)) OuterCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* FixedContent=WidgetTree->FindWidget<UVerticalBox>(TEXT("FrontendFixedContent"));
    if(!FixedContent) {
        FixedContent=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("FrontendFixedContent"));
        Pages->RemoveFromParent();FixedContent->AddChildToVerticalBox(Pages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        if(auto* Status=WidgetTree->FindWidget(TEXT("FrontendStatus"))) {Status->RemoveFromParent();FixedContent->AddChildToVerticalBox(Status)->SetPadding(FMargin(0,12,0,0));}
        Viewport->SetContent(FixedContent); // Detach the old whole-modal Scroll without deleting it.
    }
    for(int32 Index=0;Index<5;++Index) {
        const auto Page=EMCUnifiedFrontendPage(Index);const FString Prefix=FString::Printf(TEXT("Frontend%sResponsive"),MCUnifiedFrontendPrivate_PageName(Page));
        auto* PageRoot=WidgetTree->FindWidget<UVerticalBox>(FName(*FString::Printf(TEXT("Frontend%sPage"),MCUnifiedFrontendPrivate_PageName(Page))));if(!PageRoot) continue;
        auto* BodyBounds=WidgetTree->FindWidget<USizeBox>(FName(*(Prefix+TEXT("BodyBounds"))));
        if(!BodyBounds) {
            BodyBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(Prefix+TEXT("BodyBounds"))));
            auto* BodyScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),FName(*(Prefix+TEXT("BodyScroll"))));
            BodyScroll->SetAllowOverscroll(false);BodyScroll->SetScrollBarVisibility(ESlateVisibility::Visible);BodyScroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);BodyScroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);
            auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),FName(*(Prefix+TEXT("Body"))));BodyScroll->AddChild(Body);BodyBounds->SetContent(BodyScroll);
            auto* Footer=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),FName(*(Prefix+TEXT("Footer"))));Footer->SetExplicitWrapSize(true);Footer->SetInnerSlotPadding({12,10});Footer->SetHorizontalAlignment(HAlign_Center);
            // Existing named row/control objects and their source-page bindings survive.
            for(const auto& Item:MCUnifiedFrontendPrivate_Items) if(Item.Page==Page)
                if(auto* Row=WidgetTree->FindWidget<USizeBox>(MCUnifiedFrontendPrivate_RowName(MCUnifiedFrontendPrivate_ViewName(Item)))) {
                    Row->RemoveFromParent();
                    if(Item.bHeading) PageRoot->AddChildToVerticalBox(Row)->SetPadding(FMargin(2,5,8,17));
                    else if(Item.Kind==MCUnifiedFrontendPrivate_Action) Footer->AddChildToWrapBox(Row);
                    else Body->AddChildToVerticalBox(Row)->SetPadding(FMargin(2,4,8,11));
                }
            PageRoot->AddChildToVerticalBox(BodyBounds)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            PageRoot->AddChildToVerticalBox(Footer)->SetPadding(FMargin(2,12,8,4));
        }
    }
    if(AvailableSize.X<=1 || AvailableSize.Y<=1) AvailableSize=GetPaintSpaceGeometry().GetLocalSize();
    if(AvailableSize.X<=1 || AvailableSize.Y<=1) return;
    const int32 ActivePage=Pages->GetActiveWidgetIndex();float PreferredWidth=ActivePage>=3?520.f:640.f;
    if(FSlateApplication::IsInitialized()) {
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        for(const auto& Item:MCUnifiedFrontendPrivate_Items) if(int32(Item.Page)==ActivePage && (Item.bHeading || Item.Kind==MCUnifiedFrontendPrivate_Action)) {
            const FName CopyName=Item.Kind==MCUnifiedFrontendPrivate_Action?FName(*(MCUnifiedFrontendPrivate_ViewName(Item).ToString()+TEXT("Caption"))):MCUnifiedFrontendPrivate_ViewName(Item);
            if(const auto* Caption=WidgetTree->FindWidget<UTextBlock>(CopyName)) PreferredWidth=FMath::Max(PreferredWidth,float(Measure->Measure(Caption->GetText(),Caption->GetFont()).X)+(Item.bHeading?112.f:156.f));
        }
    }
    const float ViewWidth=FMath::Max(1.f,FMath::Min(FMath::Min(1000.f,PreferredWidth),float(AvailableSize.X)-14.f)),ViewHeight=FMath::Max(1.f,float(AvailableSize.Y)-16.f);
    RootBounds->SetWidthOverride(ViewWidth);RootBounds->SetMaxDesiredWidth(ViewWidth);RootBounds->SetHeightOverride(ViewHeight);RootBounds->SetMaxDesiredHeight(ViewHeight);
    const float InnerWidth=FMath::Max(1.f,ViewWidth-Outer->ContentPadding.Left-Outer->ContentPadding.Right-Inner->ContentPadding.Left-Inner->ContentPadding.Right-8.f);
    float Chrome=Outer->ContentPadding.Top+Outer->ContentPadding.Bottom+Inner->ContentPadding.Top+Inner->ContentPadding.Bottom+27.f;
    if(auto* Context=WidgetTree->FindWidget(TEXT("FrontendContextBadge"));Context && Context->GetVisibility()!=ESlateVisibility::Collapsed) Chrome+=float(Context->GetDesiredSize().Y);
    if(auto* Status=WidgetTree->FindWidget<UTextBlock>(TEXT("FrontendStatus"))) {
        Status->SetAutoWrapText(false);Status->SetWrapTextAt(InnerWidth);Status->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
        if(Status->GetVisibility()!=ESlateVisibility::Collapsed) Chrome+=float(Status->GetDesiredSize().Y)+12.f;
    }
    Viewport->SetMaxDesiredHeight(FMath::Max(1.f,ViewHeight-Chrome+Inner->ContentPadding.Top+Inner->ContentPadding.Bottom));
    for(int32 Index=0;Index<5;++Index) {
        const auto Page=EMCUnifiedFrontendPage(Index);const FString Prefix=FString::Printf(TEXT("Frontend%sResponsive"),MCUnifiedFrontendPrivate_PageName(Page));
        auto* Footer=WidgetTree->FindWidget<UWrapBox>(FName(*(Prefix+TEXT("Footer"))));if(Footer) Footer->SetWrapSize(InnerWidth);
        float HeadingHeight=0;
        for(const auto& Item:MCUnifiedFrontendPrivate_Items) if(Item.Page==Page)
            if(auto* Row=WidgetTree->FindWidget<USizeBox>(MCUnifiedFrontendPrivate_RowName(MCUnifiedFrontendPrivate_ViewName(Item)))) {
                Row->SetMaxDesiredWidth(Item.Kind==MCUnifiedFrontendPrivate_Action?FMath::Min(440.f,InnerWidth):InnerWidth);
                if(Item.bHeading) HeadingHeight+=float(Row->GetDesiredSize().Y)+22.f;
                const FName CopyName=Item.Kind==MCUnifiedFrontendPrivate_Action?FName(*(MCUnifiedFrontendPrivate_ViewName(Item).ToString()+TEXT("Caption"))):MCUnifiedFrontendPrivate_ViewName(Item);
                if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(CopyName)) {
                    Caption->SetAutoWrapText(false);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
                    Caption->SetWrapTextAt(FMath::Max(1.f,(Item.Kind==MCUnifiedFrontendPrivate_Action?FMath::Min(440.f,InnerWidth)-44.f:InnerWidth)));
                }
            }
        if(auto* BodyBounds=WidgetTree->FindWidget<USizeBox>(FName(*(Prefix+TEXT("BodyBounds"))))) {
            const float FooterHeight=Footer?float(Footer->GetDesiredSize().Y)+16.f:16.f;
            BodyBounds->ClearMinDesiredHeight();BodyBounds->SetMaxDesiredHeight(FMath::Max(1.f,ViewHeight-Chrome-HeadingHeight-FooterHeight));
        }
    }
}
void UMCUnifiedFrontendWidget::RefreshTextBounds()
{
    if(!WidgetTree) return;
    UpdateResponsiveLayout();
    for(const auto& Item:MCUnifiedFrontendPrivate_Items) if(Item.Kind==MCUnifiedFrontendPrivate_Action) if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedFrontendPrivate_ViewName(Item)))
        if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(FName(*(MCUnifiedFrontendPrivate_ViewName(Item).ToString()+TEXT("Caption"))))) {
            const float Width=float(Button->GetCachedGeometry().GetLocalSize().X);
            if(Width>70) {const float Wrap=FMath::Max(100.f,Width-52.f);if(!FMath::IsNearlyEqual(Caption->GetWrapTextAt(),Wrap,1.f)) Caption->SetWrapTextAt(Wrap);}
        }
}
void UMCUnifiedFrontendWidget::ApplyTheme()
{
    Super::ApplyTheme();if(!WidgetTree || !GetResolvedTheme()) return;
    for(const auto& Item:MCUnifiedFrontendPrivate_Items) {
        const FName Name=Item.Kind==MCUnifiedFrontendPrivate_Action?FName(*(MCUnifiedFrontendPrivate_ViewName(Item).ToString()+TEXT("Caption"))):MCUnifiedFrontendPrivate_ViewName(Item);
        if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) Label->SetFont(GetResolvedTheme()->MakeFont(Item.bHeading?30:Item.Kind==MCUnifiedFrontendPrivate_Action?24:21,true));
    }
    if(HasBoundBackend()) RefreshFromBackend();UpdateResponsiveLayout();
}
void UMCUnifiedFrontendWidget::ApplyPreviewFixture()
{
    if(!WidgetTree) return;TGuardValue<bool> Guard(bSynchronizing,true);SelectPage(PreviewPage);
    if(auto* Context=WidgetTree->FindWidget(TEXT("FrontendContextBadge"))) Context->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    const bool Pending=PreviewScenario==EMCGameplayUIPreviewScenario::Pending;
    const bool Unavailable=PreviewScenario==EMCGameplayUIPreviewScenario::Empty || PreviewScenario==EMCGameplayUIPreviewScenario::Disabled;
    for(const auto& Item:MCUnifiedFrontendPrivate_Items) {
        const FName Name=MCUnifiedFrontendPrivate_ViewName(Item);auto* View=WidgetTree->FindWidget(Name);if(!View) continue;
        bool Visible=true;
        if(Item.Page==EMCUnifiedFrontendPage::Connection) {
            const bool Coming=FString(Item.Name).StartsWith(TEXT("Unavailable"));
            Visible=Coming?Unavailable:(Item.Command==EMCMainMenuAction::Back && Item.Kind==MCUnifiedFrontendPrivate_Action) || !Unavailable;
        }
        if(auto* Row=WidgetTree->FindWidget(MCUnifiedFrontendPrivate_RowName(Name))) Row->SetVisibility(Visible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        const bool BusyButton=Item.Kind==MCUnifiedFrontendPrivate_Action && (Item.Command==EMCMainMenuAction::CreateLobby || Item.Command==EMCMainMenuAction::JoinLobby
            || Item.Command==EMCMainMenuAction::FindRooms || Item.Command==EMCMainMenuAction::JoinRoom || Item.Command==EMCMainMenuAction::JoinAddress
            || Item.Command==EMCMainMenuAction::Invite || Item.Command==EMCMainMenuAction::StartLobby);
        View->SetIsEnabled(PreviewScenario!=EMCGameplayUIPreviewScenario::Disabled && (!Pending || !BusyButton));
        if(auto* MCUnifiedFrontendPrivate_Text=Cast<UTextBlock>(View)) MCUnifiedFrontendPrivate_Text->SetText(Localized(Item.Key,FText::AsCultureInvariant(Item.Copy)));
        if(Item.Kind==MCUnifiedFrontendPrivate_Action) if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(FName(*(Name.ToString()+TEXT("Caption"))))) Caption->SetText(Localized(Item.Key,FText::AsCultureInvariant(Item.Copy)));
        if(auto* MCUnifiedFrontendPrivate_Picker=Cast<UComboBoxString>(View)) {MCUnifiedFrontendPrivate_Picker->ClearOptions();if(!Unavailable) {MCUnifiedFrontendPrivate_Picker->AddOption(PreviewScenario==EMCGameplayUIPreviewScenario::LongText?TEXT("HASBULADO_WITH_A_VERY_LONG_ROOM_AND_PLAYER_NAME · 2 / 4"):TEXT("AfterCoffee · 2 / 4"));MCUnifiedFrontendPrivate_Picker->SetSelectedIndex(0);}}
        if(auto* AddressBox=Cast<UEditableTextBox>(View)) AddressBox->SetText(FText::AsCultureInvariant(TEXT("127.0.0.1:7777")));
    }
    if(auto* Players=WidgetTree->FindWidget<UTextBlock>(TEXT("FrontendLobby_Players"))) {
        const auto Count=Localized(TEXT("UI.MainMenu.LobbyCounts"),LOCTEXT("LobbyCounts","Игроков: {Count} / {Maximum}\nЗагрузились: {Loaded} / {Count}\n"));
        const FText Summary=FText::Format(Count,FFormatNamedArguments{{TEXT("Count"),2},{TEXT("Maximum"),4},{TEXT("Loaded"),Pending?1:2}});
        Players->SetText(FText::AsCultureInvariant(Summary.ToString()+(PreviewScenario==EMCGameplayUIPreviewScenario::LongText?
            TEXT("• HASBULADO_WITH_A_VERY_LONG_PLAYER_NAME_WITHOUT_SPACES\n• AfterCoffee"):TEXT("• HASBULADO\n• AfterCoffee"))));
    }
    if(auto* Context=WidgetTree->FindWidget<UTextBlock>(TEXT("FrontendContextTitle"))) Context->SetText(Localized(TEXT("UI.MainMenu.BrandTitle"),LOCTEXT("Brand","MESS CONTROL")));
    if(auto* Status=WidgetTree->FindWidget<UTextBlock>(TEXT("FrontendStatus"))) {Status->SetText(FText::GetEmpty());Status->SetVisibility(ESlateVisibility::Collapsed);}
    RefreshTextBounds();
}
void UMCUnifiedFrontendWidget::ApplyEditorPreview(float Time)
{
    // No backend/actors, settings, sessions, travel or RPCs are used for this fixture.
    Backend.Reset();bEditorFixture=true;BuildPresentationTree();Super::ApplyEditorPreview(Time);ApplyPreviewFixture();UpdateResponsiveLayout();
}
void UMCUnifiedFrontendWidget::NativeConstruct() {Super::NativeConstruct();BindControls();if(HasBoundBackend()) RefreshFromBackend();}
void UMCUnifiedFrontendWidget::NativeDestruct() {UnbindControls();Super::NativeDestruct();}
void UMCUnifiedFrontendWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);UpdateResponsiveLayout(Geometry.GetLocalSize());RefreshTextBounds();RefreshElapsed+=DeltaSeconds;
    if(RefreshElapsed>=.1f) {RefreshElapsed=0;if(HasBoundBackend()) RefreshFromBackend();}
}
void UMCUnifiedFrontendWidget::OnLocalizationChanged()
{
    Super::OnLocalizationChanged();if(HasBoundBackend()) RefreshFromBackend();else if(IsDesignTime() || bEditorFixture) ApplyPreviewFixture();
}
FReply UMCUnifiedFrontendWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(HasBoundBackend() && !Event.IsRepeat() && (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right)) {RequestBack();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

#undef LOCTEXT_NAMESPACE
