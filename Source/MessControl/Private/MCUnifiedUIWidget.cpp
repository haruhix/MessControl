#include "MCUnifiedUIWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCGameplayHUD.h"
#include "MCUnifiedSettingsWidget.h"
#include "MCUnifiedTeamWidget.h"
#include "MCUnifiedWardrobeWidget.h"
#include "MCUnifiedShopWidget.h"
#include "MCUnifiedHubWidget.h"
#include "MCUnifiedRewardWidget.h"
#include "MCUnifiedFrontendWidget.h"
#include "MCUnifiedGameplayMenuWidget.h"
#include "MCUnifiedCatchWidget.h"
#include "MCUnifiedHUDExtrasWidget.h"
#include "MCUnifiedLoadingWidget.h"
#include "MCUnifiedBossWidget.h"
#include "MCUnifiedAlertWidget.h"
#include "MCUnifiedTutorialWidget.h"
#include "MCUnifiedEmoteWidget.h"
#include "MCTutorialWidget.h"
#include "MCEmoteWidget.h"
#include "MCHubWidget.h"
#include "MCPerkChoiceWidget.h"
#include "MCGameplayMenuWidget.h"
#include "MCHubWardrobeWidget.h"
#include "MCHubShopWidget.h"
#include "MCScoreboardWidget.h"
#include "MCPlayerController.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SafeZone.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MCStaminaWidget.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"
#include "MCUIDataLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCUnifiedUI"

namespace
{
    FLinearColor UnifiedRootColor(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    UCanvasPanelSlot* UnifiedRootPlace(UCanvasPanel* Parent,UWidget* Child,FVector2D Position,FVector2D Size,FVector2D Anchor=FVector2D::ZeroVector,FVector2D Alignment=FVector2D::ZeroVector)
    {
        auto* Cell=Parent->AddChildToCanvas(Child);Cell->SetAnchors(FAnchors(Anchor.X,Anchor.Y));
        Cell->SetAlignment(Alignment);Cell->SetPosition(Position);Cell->SetSize(Size);return Cell;
    }
}

UMCUnifiedMenuButton::UMCUnifiedMenuButton() {Kind=EMCUniversalUIKind::Button;}
void UMCUnifiedMenuButton::BuildPresentationTree()
{
    Super::BuildPresentationTree();
    if(Action!=EMCMainMenuAction::CreateLobby || !WidgetTree || WidgetTree->FindWidget(TEXT("PlayGlyph"))) return;
    auto* Caption=WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"));
    auto* Stack=WidgetTree->FindWidget<UVerticalBox>(TEXT("UniversalDefaultContent"));
    auto* Fit=Caption?Cast<UScaleBox>(Caption->GetParent()):nullptr;
    if(!Stack || !Caption) return;
    UWidget* LabelContent=Fit?static_cast<UWidget*>(Fit):static_cast<UWidget*>(Caption);
    LabelContent->RemoveFromParent();
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("PlayLabelRow"));
    auto* Glyph=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("PlayGlyph"));
    Glyph->SetText(FText::AsCultureInvariant(TEXT("\u25B6")));
    auto* GlyphCell=Row->AddChildToHorizontalBox(Glyph);GlyphCell->SetVerticalAlignment(VAlign_Center);GlyphCell->SetPadding(FMargin(8,0,16,0));
    auto* LabelCell=Row->AddChildToHorizontalBox(LabelContent);LabelCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LabelCell->SetVerticalAlignment(VAlign_Center);
    Stack->AddChildToVerticalBox(Row);
}
void UMCUnifiedMenuButton::ApplyTheme()
{
    Super::ApplyTheme();
    if(WidgetTree) if(auto* Glyph=WidgetTree->FindWidget<UTextBlock>(TEXT("PlayGlyph"))) {
        if(auto* Resolved=GetResolvedTheme()) Glyph->SetFont(Resolved->MakeFont(FontSize,false));
        Glyph->SetColorAndOpacity(CustomText);
    }
}
void UMCUnifiedMenuButton::SetBackend(UMCMainMenuWidget* Menu)
{
    if(Backend.Get()==Menu) return;
    Backend=Menu;OnClicked.AddUniqueDynamic(this,&ThisClass::ForwardAction);
    if(Menu && Menu->WidgetTree) Menu->WidgetTree->ForEachWidget([this](UWidget* Widget){
        if(auto* Button=Cast<UMCMainMenuActionButton>(Widget);Button && Button->Action==Action) SetIsEnabled(Button->GetIsEnabled());
    });
}
void UMCUnifiedMenuButton::ForwardAction() {if(!IsDesignTime() && Backend.IsValid()) Backend->HandleAction(Action);}

UMCUnifiedUIWidget::UMCUnifiedUIWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;
    Logo=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/V2/Art/T_UI_PSD_MenuLogo.T_UI_PSD_MenuLogo")));
    SetIsFocusable(true);
}

void UMCUnifiedUIWidget::ShowScreen(EMCUnifiedUIScreen Screen)
{
    ActiveScreen=Screen;
    if(WidgetTree) {
        if(WidgetTree->RootWidget) WidgetTree->RootWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        if(auto* Layer=WidgetTree->FindWidget(TEXT("InterfaceLayers"))) Layer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        if(auto* Pages=WidgetTree->FindWidget(TEXT("InterfaceScreens"))) Pages->SetVisibility(Screen==EMCUnifiedUIScreen::Hub?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Visible);
        if(auto* Page=WidgetTree->FindWidget(TEXT("Screen_Hub"))) Page->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    }
    if(WidgetTree) if(auto* Team=WidgetTree->FindWidget<UMCUnifiedTeamWidget>(TEXT("UnifiedTeam")))
        Team->SetVisibility((IsDesignTime()?Screen==EMCUnifiedUIScreen::Team:TeamBackend.IsValid())?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(WidgetTree) if(auto* Layer=WidgetTree->FindWidget(TEXT("TeamViewport")))
        Layer->SetVisibility((IsDesignTime()?Screen==EMCUnifiedUIScreen::Team:TeamBackend.IsValid())?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedCatchWidget>(TEXT("UnifiedCatch")))
        View->SetVisibility((IsDesignTime()?Screen==EMCUnifiedUIScreen::Catch:CatchBackend.IsValid())?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedHUDExtrasWidget>(TEXT("UnifiedHUDExtras"))) {
        const bool Visible=IsDesignTime()?Screen==EMCUnifiedUIScreen::HUDExtras:HUDBackend.IsValid() && Screen==EMCUnifiedUIScreen::Gameplay;
        View->SetVisibility(Visible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* Source=HUDBackend.Get()) Source->SetUnifiedExtrasActive(Visible && View->IsPresentationPrepared());
    }
    if(WidgetTree) {
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedBossWidget>(TEXT("UnifiedBoss")))
            View->SetVisibility((IsDesignTime()?Screen==EMCUnifiedUIScreen::Boss:BossBackend.IsValid() && View->IsPresentationPrepared())?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedAlertWidget>(TEXT("UnifiedAlert")))
            View->SetVisibility((IsDesignTime()?Screen==EMCUnifiedUIScreen::Alert:AlertBackend.IsValid() && View->IsPresentationPrepared())?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(WidgetTree) {
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedTutorialWidget>(TEXT("UnifiedTutorial")))
            View->SetVisibility((IsDesignTime()?Screen==EMCUnifiedUIScreen::Tutorial:HasTutorialPresentation() && Screen==EMCUnifiedUIScreen::Gameplay)?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes")))
            View->SetVisibility((IsDesignTime()?Screen==EMCUnifiedUIScreen::Emotes:Screen==EMCUnifiedUIScreen::Emotes && EmoteBackend.IsValid())?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(WidgetTree) if(auto* Pages=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("InterfaceScreens"))) Pages->SetActiveWidgetIndex(int32(Screen));
}

void UMCUnifiedUIWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {EnsureGameplayControls();EnsureAdditionalScreens();ShowScreen(IsDesignTime()?PreviewScreen:ActiveScreen);return;}
    auto* Safe=WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass(),TEXT("InterfaceSafeArea"));WidgetTree->RootWidget=Safe;
    auto* Pages=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("InterfaceScreens"));Safe->SetContent(Pages);
    UCanvasPanel* Screens[7];
    const TCHAR* Names[]={TEXT("Menu"),TEXT("Gameplay"),TEXT("Hub"),TEXT("Wardrobe"),TEXT("Boosters"),TEXT("Tools"),TEXT("Perks")};
    for(int32 I=0;I<7;++I) {Screens[I]=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),FName(*(FString(TEXT("Screen_"))+Names[I])));Pages->AddChild(Screens[I]);}
    auto Text=[&](UCanvasPanel* Parent,FName Name,const FText& Value,FVector2D Pos,FVector2D Size,int32 FontSize)
    {
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Label->SetText(Value);Label->SetAutoWrapText(true);Label->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        UnifiedRootPlace(Parent,Label,Pos,Size);BindStyle(Name,EMCUIColorToken::Cream,EMCUIStyleTarget::Text,FontSize,true);return Label;
    };
    auto Panel=[&](UCanvasPanel* Parent,FName Name,FVector2D Pos,FVector2D Size,FVector2D Anchor=FVector2D::ZeroVector,FVector2D Alignment=FVector2D::ZeroVector)
    {
        auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
        Shape->Label=FText::GetEmpty();Shape->bCustomColors=true;Shape->CustomFill=UnifiedRootColor(TEXT("30333F"));Shape->CustomOutline=UnifiedRootColor(TEXT("718080"));Shape->CustomShadow=UnifiedRootColor(TEXT("879094"));
        Shape->bUseThemeShape=false;Shape->CornerRadius=12;Shape->OutlineWidth=4;Shape->ShadowOffset={10,12};Shape->ContentPadding=FMargin(10);
        if(Name==TEXT("ReserveBlock")) {Shape->ContourPoints={{0,0},{.66,0},{.66,.5},{1,.5},{1,1},{0,1}};Shape->ShadowOffset={16,18};}
        if(Name==TEXT("ToolResourceBlock")) Shape->ContourPoints={{0,0},{.30,0},{.30,.12},{1,.12},{1,1},{0,1}};
        if(Name==TEXT("NextEventBlock") || Name.ToString().StartsWith(TEXT("V2Player"))) Shape->ShadowOffset={-12,12};
        if(Name.ToString().StartsWith(TEXT("V2Skill"))) Shape->ContentPadding=FMargin(8);
        Shape->Initialize();Shape->BuildPresentationTree();UnifiedRootPlace(Parent,Shape,Pos,Size,Anchor,Alignment);
        auto* Body=WidgetTree->ConstructWidget<UCanvasPanel>();Shape->SetContentForSlot(TEXT("Content"),Body);return Body;
    };
    auto Bar=[&](UCanvasPanel* Parent,FName Name,FVector2D Pos,FVector2D Size)
    {
        auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
        Shape->Kind=EMCUniversalUIKind::Progress;Shape->Label=FText::GetEmpty();Shape->Progress=.8f;Shape->ProgressFill=EMCUIColorToken::BossHealth;
        Shape->bCustomColors=true;Shape->CustomFill=UnifiedRootColor(TEXT("45404F"));Shape->CustomOutline=UnifiedRootColor(TEXT("FFF9F8"));Shape->CustomShadow=FLinearColor::Transparent;
        Shape->bUseThemeShape=false;Shape->CornerRadius=6;Shape->OutlineWidth=4;Shape->ShadowOffset=FVector2D::ZeroVector;Shape->ContentPadding=FMargin(5,-5,5,5);
        Shape->Initialize();Shape->BuildPresentationTree();UnifiedRootPlace(Parent,Shape,Pos,Size);return Shape;
    };
    // MENU: separate PSD composition, not a palette applied to the saved sidebar.
    auto* Mark=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("MenuLogo"));Mark->SetBrushFromTexture(Logo.LoadSynchronous(),true);
    auto* MarkFit=WidgetTree->ConstructWidget<UScaleBox>();MarkFit->SetStretch(EStretch::ScaleToFit);MarkFit->SetContent(Mark);
    auto* LogoCell=Screens[0]->AddChildToCanvas(MarkFit);LogoCell->SetAnchors(FAnchors(.0011f,.0306f,.332f,.354f));LogoCell->SetOffsets(FMargin(0));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("MenuActionsScroll"));
    auto* MenuCell=Screens[0]->AddChildToCanvas(Scroll);MenuCell->SetAnchors(FAnchors(.077f,.429f,.267f,.89f));MenuCell->SetOffsets(FMargin(0));Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
    auto* Actions=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("MenuActions"));Scroll->AddChild(Actions);
    const EMCMainMenuAction Commands[]={EMCMainMenuAction::CreateLobby,EMCMainMenuAction::JoinLobby,EMCMainMenuAction::Settings,EMCMainMenuAction::Credits,EMCMainMenuAction::Exit};
    const TCHAR* Keys[]={TEXT("UI.MainMenu.Action.PlayHub"),TEXT("UI.WashMenu.JoinFriends"),TEXT("UI.MainMenu.Action.Settings"),TEXT("UI.MainMenu.Action.Credits"),TEXT("UI.MainMenu.Action.Exit")};
    const FText Copies[]={LOCTEXT("Play","ИГРАТЬ"),LOCTEXT("Friends","С ДРУЗЬЯМИ"),LOCTEXT("Settings","НАСТРОЙКИ"),LOCTEXT("Credits","АВТОРЫ"),LOCTEXT("Exit","ВЫХОД")};
    for(int32 I=0;I<5;++I)
    {
        auto* Button=WidgetTree->ConstructWidget<UMCUnifiedMenuButton>(UMCUnifiedMenuButton::StaticClass(),FName(*FString::Printf(TEXT("MenuAction_%d"),I)));
        Button->Action=Commands[I];Button->LabelKey=Keys[I];Button->Label=Copies[I];Button->FontSize=I==0?36:28;
        Button->bCustomColors=true;Button->CustomFill=UnifiedRootColor(I==0?TEXT("1B4450"):TEXT("B63F81"));Button->CustomText=UnifiedRootColor(TEXT("FFF9F8"));
        Button->CustomOutline=UnifiedRootColor(I==0?TEXT("FFF9F8"):TEXT("B0FCF3"));Button->CustomShadow=UnifiedRootColor(I==0?TEXT("FFF9F8"):TEXT("B0FCF3"));
        Button->bUseThemeShape=false;Button->CornerRadius=18;Button->OutlineWidth=4;Button->ContentPadding=FMargin(12,8);Button->ShadowOffset={10,12};
        Button->Initialize();Button->BuildPresentationTree();
        auto* Minimum=WidgetTree->ConstructWidget<USizeBox>();Minimum->SetMinDesiredHeight(76);Minimum->SetContent(Button);
        Actions->AddChildToVerticalBox(Minimum)->SetPadding(FMargin(0,0,14,18));
    }
    // GAMEPLAY: compact tooth counter and cleanliness, player portraits/event,
    // tool/player resources, and four active slots are independent anchored groups.
    auto* Reserve=Panel(Screens[1],TEXT("ReserveBlock"),{50,25},{340,152});
    auto* Tooth=WidgetTree->ConstructWidget<UMCHUDIcon>();Tooth->bReserveTooth=true;Tooth->Tint=FLinearColor::White;UnifiedRootPlace(Reserve,Tooth,{8,0},{70,70});
    Text(Reserve,TEXT("V2ReserveCount"),FText::AsNumber(12),{84,-4},{190,72},58);
    auto* Clean=Text(Reserve,TEXT("V2CleanLabel"),LOCTEXT("Clean","ЧИСТОТА РТА"),{4,68},{305,30},26);TextBindings.Add(Clean->GetFName(),TEXT("UI.HUD.Cleanliness"));
    Bar(Reserve,TEXT("V2CleanBar"),{4,105},{305,28});
    auto* Players=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("PlayerCards"));UnifiedRootPlace(Screens[1],Players,{-50,30},{410,142},{1,0},{1,0});
    for(int32 I=0;I<4;++I)
    {
        auto* Holder=WidgetTree->ConstructWidget<UCanvasPanel>();auto* Cell=Players->AddChildToHorizontalBox(Holder);Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Cell->SetPadding(FMargin(0,0,10,10));
        auto* Card=Panel(Holder,FName(*FString::Printf(TEXT("V2Player%d"),I+1)),{0,0},{88,130});
        auto* Face=WidgetTree->ConstructWidget<UMCHUDIcon>();Face->bReserveTooth=true;Face->Tint=FLinearColor::White;UnifiedRootPlace(Card,Face,{3,0},{62,83});
        Bar(Card,FName(*FString::Printf(TEXT("V2PlayerHP%d"),I+1)),{0,87},{68,20});
    }
    auto* Event=Panel(Screens[1],TEXT("NextEventBlock"),{-50,188},{395,110},{1,0},{1,0});
    auto* EventLabel=Text(Event,TEXT("V2NextEventLabel"),LOCTEXT("NextEvent","СЛЕДУЮЩЕЕ СОБЫТИЕ"),{5,0},{365,30},22);TextBindings.Add(EventLabel->GetFName(),TEXT("UI.HUD.NextEvent"));
    Text(Event,TEXT("V2NextEvent"),LOCTEXT("EventSample","ОРЕХОВОЕ СОБЫТИЕ"),{5,32},{365,56},28);
    auto* Tool=Panel(Screens[1],TEXT("ToolResourceBlock"),{50,-36},{380,145},{0,1},{0,1});
    auto* ToolIcon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("V2ToolIcon"));ToolIcon->ToolSlot=0;ToolIcon->Tint=FLinearColor::White;UnifiedRootPlace(Tool,ToolIcon,{3,7},{100,100});
    Text(Tool,TEXT("V2ToolName"),LOCTEXT("ToolSample","КИРКА"),{4,-47},{160,35},30);
    Text(Tool,TEXT("V2PlayerName"),LOCTEXT("PlayerSample","ИГРОК"),{126,-47},{220,35},28);
    Bar(Tool,TEXT("V2HealthBar"),{120,15},{225,32});Bar(Tool,TEXT("V2StaminaBar"),{120,63},{225,29});
    auto* Skills=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("SkillCards"));UnifiedRootPlace(Screens[1],Skills,{-50,-25},{420,140},{1,1},{1,1});
    for(int32 I=0;I<4;++I)
    {
        auto* Holder=WidgetTree->ConstructWidget<UCanvasPanel>();auto* Cell=Skills->AddChildToHorizontalBox(Holder);Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Cell->SetPadding(FMargin(0,0,10,10));
        Text(Holder,FName(*FString::Printf(TEXT("V2SkillKey%d"),I+1)),FText::AsNumber(I+1),{40,0},{45,30},30);
        auto* Card=Panel(Holder,FName(*FString::Printf(TEXT("V2Skill%d"),I+1)),{0,36},{94,94});
        Text(Card,FName(*FString::Printf(TEXT("V2SkillLabel%d"),I+1)),LOCTEXT("EmptySkill","Пусто"),{0,17},{79,55},20);
    }
    EnsureGameplayControls();
    EnsureAdditionalScreens();
    ShowScreen(IsDesignTime()?PreviewScreen:ActiveScreen);
}

void UMCUnifiedUIWidget::SetMenuBackend(UMCMainMenuWidget* Backend)
{
    MenuBackend=Backend;
    if(WidgetTree) if(auto* Settings=WidgetTree->FindWidget<UMCUnifiedSettingsWidget>(TEXT("UnifiedSettings"))) Settings->SetBackend(Backend);
    if(WidgetTree) if(auto* Frontend=WidgetTree->FindWidget<UMCUnifiedFrontendWidget>(TEXT("UnifiedFrontend"))) Frontend->SetBackend(Backend);
    if(WidgetTree) WidgetTree->ForEachWidget([Backend](UWidget* Widget){if(auto* Button=Cast<UMCUnifiedMenuButton>(Widget)) Button->SetBackend(Backend);});
}
void UMCUnifiedUIWidget::SetHUDBackend(UMCGameplayHUD* Backend)
{
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedHUDExtrasWidget>(TEXT("UnifiedHUDExtras"))) View->SetBackend(Backend);
    if(HUDBackend.Get()==Backend) return;
    HUDBackend=Backend;HUDControls.Reset();
    if(Backend && Backend->WidgetTree) {
        // ForEachWidgetAndDescendants replaces user widgets with their inner
        // trees. Cache the owning controls first (stamina and portraits).
        Backend->WidgetTree->ForEachWidget([this](UWidget* Item){HUDControls.Add(Item->GetFName(),Item);});
        Backend->WidgetTree->ForEachWidgetAndDescendants([this](UWidget* Item){if(!HUDControls.Contains(Item->GetFName())) HUDControls.Add(Item->GetFName(),Item);});
    }
    if(Backend) RefreshRuntimePresentation();
}
UWidget* UMCUnifiedUIWidget::GetDefaultFocusTarget() const
{
    if(!WidgetTree) return nullptr;
    if(ActiveScreen==EMCUnifiedUIScreen::Emotes && HasEmotePresentation())
        return WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes"));
    if(RequiresTutorialMenuInput()) if(auto* Tutorial=WidgetTree->FindWidget<UMCUnifiedTutorialWidget>(TEXT("UnifiedTutorial"))) return Tutorial->GetDefaultFocusTarget();
    if(TeamBackend.IsValid()) if(auto* Team=WidgetTree->FindWidget<UMCUnifiedTeamWidget>(TEXT("UnifiedTeam"))) return Team->GetDefaultFocusTarget();
    if(ActiveScreen==EMCUnifiedUIScreen::Hub) if(auto* Hub=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"))) return Hub->GetDefaultFocusTarget();
    if(ActiveScreen==EMCUnifiedUIScreen::Reward) if(auto* Reward=WidgetTree->FindWidget<UMCUnifiedRewardWidget>(TEXT("UnifiedReward"))) return Reward->GetDefaultFocusTarget();
    if(ActiveScreen==EMCUnifiedUIScreen::Frontend) if(auto* Frontend=WidgetTree->FindWidget<UMCUnifiedFrontendWidget>(TEXT("UnifiedFrontend"))) return Frontend->GetDefaultFocusTarget();
    if(ActiveScreen==EMCUnifiedUIScreen::GameplayMenu) if(auto* Menu=WidgetTree->FindWidget<UMCUnifiedGameplayMenuWidget>(TEXT("UnifiedGameplayMenu"))) return Menu->GetDefaultFocusTarget();
    if(ActiveScreen==EMCUnifiedUIScreen::Wardrobe) if(auto* Wardrobe=WidgetTree->FindWidget<UMCUnifiedWardrobeWidget>(TEXT("UnifiedWardrobe"))) return Wardrobe->GetDefaultFocusTarget();
    if(ActiveScreen==EMCUnifiedUIScreen::Boosters) if(auto* Shop=WidgetTree->FindWidget<UMCUnifiedShopWidget>(TEXT("UnifiedShop"))) return Shop->GetDefaultFocusTarget();
    if(ActiveScreen==EMCUnifiedUIScreen::Settings) if(auto* Settings=WidgetTree->FindWidget<UMCUnifiedSettingsWidget>(TEXT("UnifiedSettings"))) return Settings->GetDefaultFocusTarget();
    if(auto* Button=WidgetTree->FindWidget<UMCUnifiedMenuButton>(TEXT("MenuAction_0")))
        if(Button->WidgetTree) return Button->WidgetTree->FindWidget(TEXT("UniversalSurface"));
    return nullptr;
}
void UMCUnifiedUIWidget::ApplyTheme() {Super::ApplyTheme();if(!IsDesignTime()) RefreshRuntimePresentation();RefreshAdaptiveLayout();}
void UMCUnifiedUIWidget::ApplyEditorPreview(float Time)
{
    const int64 Screen=StaticEnum<EMCUnifiedUIScreen>()->GetValueByNameString(PreviewState.ToString());
    if(Screen>=0 && Screen<=int64(EMCUnifiedUIScreen::Emotes)) PreviewScreen=EMCUnifiedUIScreen(Screen);
    if(WidgetTree) WidgetTree->ForEachWidget([this](UWidget* Item) {
        if(auto* View=Cast<UMCGameplayUIWidget>(Item)) {View->PreviewCulture=PreviewCulture;View->PreviewScenario=PreviewScenario;}
    });
    ShowScreen(PreviewScreen);Super::ApplyEditorPreview(Time);
    if(!WidgetTree) return;
    if(PreviewScreen==EMCUnifiedUIScreen::Settings) if(auto* Settings=WidgetTree->FindWidget<UMCUnifiedSettingsWidget>(TEXT("UnifiedSettings"))) {
        Settings->PreviewSettingsTab=PreviewSettingsTab;Settings->ApplyEditorPreview(Time);
    }
    if(PreviewScreen==EMCUnifiedUIScreen::Team) if(auto* Team=WidgetTree->FindWidget<UMCUnifiedTeamWidget>(TEXT("UnifiedTeam"))) Team->ApplyEditorPreview(Time);
    if(PreviewScreen==EMCUnifiedUIScreen::Wardrobe) if(auto* Wardrobe=WidgetTree->FindWidget<UMCUnifiedWardrobeWidget>(TEXT("UnifiedWardrobe"))) Wardrobe->ApplyEditorPreview(Time);
    if(PreviewScreen==EMCUnifiedUIScreen::Boosters) if(auto* Shop=WidgetTree->FindWidget<UMCUnifiedShopWidget>(TEXT("UnifiedShop"))) Shop->ApplyEditorPreview(Time);
    if(PreviewScreen==EMCUnifiedUIScreen::Hub) if(auto* Hub=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"))) {Hub->PreviewState=PreviewHubMode;Hub->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::Reward) if(auto* Reward=WidgetTree->FindWidget<UMCUnifiedRewardWidget>(TEXT("UnifiedReward"))) {Reward->PreviewState=PreviewRewardMode;Reward->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::Frontend) if(auto* Frontend=WidgetTree->FindWidget<UMCUnifiedFrontendWidget>(TEXT("UnifiedFrontend"))) {Frontend->PreviewPage=EMCUnifiedFrontendPage(FMath::Clamp(PreviewFrontendPage,0,4));Frontend->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::GameplayMenu) if(auto* Menu=WidgetTree->FindWidget<UMCUnifiedGameplayMenuWidget>(TEXT("UnifiedGameplayMenu"))) {Menu->PreviewState=PreviewGameplayMenuMode;Menu->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::Catch) if(auto* View=WidgetTree->FindWidget<UMCUnifiedCatchWidget>(TEXT("UnifiedCatch"))) {View->PreviewState=PreviewCatchMode;View->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::HUDExtras) if(auto* View=WidgetTree->FindWidget<UMCUnifiedHUDExtrasWidget>(TEXT("UnifiedHUDExtras"))) {View->PreviewState=PreviewHUDExtrasMode;View->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::Loading) if(auto* View=WidgetTree->FindWidget<UMCUnifiedLoadingWidget>(TEXT("UnifiedLoading"))) {View->PreviewState=PreviewLoadingMode;View->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::Boss) if(auto* View=WidgetTree->FindWidget<UMCUnifiedBossWidget>(TEXT("UnifiedBoss"))) {View->PreviewState=PreviewBossMode;View->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::Alert) if(auto* View=WidgetTree->FindWidget<UMCUnifiedAlertWidget>(TEXT("UnifiedAlert"))) {View->PreviewState=PreviewAlertMode;View->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::Tutorial) if(auto* View=WidgetTree->FindWidget<UMCUnifiedTutorialWidget>(TEXT("UnifiedTutorial"))) {View->PreviewState=PreviewTutorialMode;View->PreviewLesson=PreviewTutorialLesson;View->ApplyEditorPreview(Time);}
    if(PreviewScreen==EMCUnifiedUIScreen::Emotes) if(auto* View=WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes"))) {View->PreviewState=PreviewEmoteMode;View->PreviewCategory=PreviewEmoteCategory;View->ApplyEditorPreview(Time);}
    RefreshAdaptiveLayout();
    if(PreviewScreen!=EMCUnifiedUIScreen::Gameplay) return;
    if(auto* Image=WidgetTree->FindWidget<UImage>(TEXT("V2ToolArt"))) {
        auto* Texture=ResolveToolArt(TEXT("Tool.Pickaxe"));Image->SetBrushFromTexture(Texture,true);
        if(auto* Fallback=WidgetTree->FindWidget(TEXT("V2ToolIcon"))) Fallback->SetVisibility(Texture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    if(auto* Summary=WidgetTree->FindWidget<UTextBlock>(TEXT("V2RunSummary"))) Summary->SetText(Localized(TEXT("UI.V2.Preview.RunSummary"),LOCTEXT("RunSample","ДО СОБЫТИЯ 02:48 · ЗАДАЧИ 420 / 1440")));
    if(auto* Current=WidgetTree->FindWidget<UTextBlock>(TEXT("V2CurrentEvent"))) Current->SetText(Localized(TEXT("UI.V2.Preview.CurrentEvent"),LOCTEXT("CurrentSample","СЕЙЧАС: УБОРКА")));
    if(auto* Hint=WidgetTree->FindWidget<UTextBlock>(TEXT("V2ActionHint"))) Hint->SetText(Localized(TEXT("UI.HUD.Hint.Pickaxe"),LOCTEXT("PickHint","ЛКМ · ДРОБИТЬ ЕДУ")));
}
void UMCUnifiedUIWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(IsDesignTime()) return;
    RefreshDelay-=DeltaSeconds;if(RefreshDelay<=0) {RefreshDelay=.05f;RefreshRuntimePresentation();RefreshAdaptiveLayout();}
}
void UMCUnifiedUIWidget::NativeDestruct()
{
    SetEmoteFocusLease(false);
    if(auto* Source=BossBackend.Get()) Source->SetUnifiedViewActive(false);
    if(auto* Source=AlertBackend.Get()) Source->SetUnifiedViewActive(false);
    Super::NativeDestruct();
}
FReply UMCUnifiedUIWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    // One physical Back press owns one navigation transition, including source routes and fallbacks.
    const bool bBackNavigationKey=Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right;
    if(bBackNavigationKey && Event.IsRepeat()) return FReply::Handled();
    if(ActiveScreen==EMCUnifiedUIScreen::Settings && WidgetTree)
        if(auto* Settings=WidgetTree->FindWidget<UMCUnifiedSettingsWidget>(TEXT("UnifiedSettings"));Settings && Settings->RouteRebindKey(Event.GetKey())) return FReply::Handled();
    if(WidgetTree) {
        if(ActiveScreen==EMCUnifiedUIScreen::Emotes && HasEmotePresentation())
            if(auto* View=WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes"));View && View->RouteSourceKeyDown(Event)) return FReply::Handled();
        if(RequiresTutorialMenuInput())
            if(auto* View=WidgetTree->FindWidget<UMCUnifiedTutorialWidget>(TEXT("UnifiedTutorial"));View && View->RouteSourceKey(Event)) return FReply::Handled();
        if(ActiveScreen==EMCUnifiedUIScreen::GameplayMenu) if(auto* Menu=WidgetTree->FindWidget<UMCUnifiedGameplayMenuWidget>(TEXT("UnifiedGameplayMenu"));Menu && Menu->RouteSourceKey(Event)) return FReply::Handled();
        if(ActiveScreen==EMCUnifiedUIScreen::Reward) if(auto* Reward=WidgetTree->FindWidget<UMCUnifiedRewardWidget>(TEXT("UnifiedReward"));Reward && Reward->RouteSourceKey(Event)) return FReply::Handled();
        if(ActiveScreen==EMCUnifiedUIScreen::Hub && !TeamBackend.IsValid()) if(auto* Hub=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"));Hub && Hub->RouteSourceKey(Event)) return FReply::Handled();
        if(ActiveScreen==EMCUnifiedUIScreen::Frontend && (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) && !Event.IsRepeat()) if(auto* Frontend=WidgetTree->FindWidget<UMCUnifiedFrontendWidget>(TEXT("UnifiedFrontend"))) {Frontend->RequestBack();return FReply::Handled();}
    }
    if((Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) && !Event.IsRepeat() && WidgetTree) {
        if(WardrobeBackend.IsValid()) if(auto* Wardrobe=WidgetTree->FindWidget<UMCUnifiedWardrobeWidget>(TEXT("UnifiedWardrobe"))) {Wardrobe->RequestBack();return FReply::Handled();}
        if(ShopBackend.IsValid()) if(auto* Shop=WidgetTree->FindWidget<UMCUnifiedShopWidget>(TEXT("UnifiedShop"))) {Shop->RequestBack();return FReply::Handled();}
    }
    if(TeamBackend.IsValid()) {
        const FKey Key=Event.GetKey();
        if(Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right) {TeamBackend->RequestUnifiedClose();return FReply::Handled();}
        if(Key==EKeys::Tab || Key==EKeys::Gamepad_Special_Left) {
            if(auto* PC=GetOwningPlayer<AMCPlayerController>();PC && PC->IsHubWorld() && !Event.IsRepeat()) TeamBackend->RequestUnifiedClose();
            return FReply::Handled();
        }
    }
    if(MenuBackend.IsValid() && (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right))
    {MenuBackend->RequestBack();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCUnifiedUIWidget::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(WidgetTree && ActiveScreen==EMCUnifiedUIScreen::Hub && !TeamBackend.IsValid())
        if(auto* Hub=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"));Hub && Hub->RouteSourceMouseWheel(Event)) return FReply::Handled();
    return Super::NativeOnMouseWheel(Geometry,Event);
}
FReply UMCUnifiedUIWidget::NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event)
{
    if(WidgetTree) {
        if(ActiveScreen==EMCUnifiedUIScreen::Reward) if(auto* Reward=WidgetTree->FindWidget<UMCUnifiedRewardWidget>(TEXT("UnifiedReward"));Reward && Reward->RouteSourceAnalog(Event)) return FReply::Handled();
        if(ActiveScreen==EMCUnifiedUIScreen::GameplayMenu) if(auto* Menu=WidgetTree->FindWidget<UMCUnifiedGameplayMenuWidget>(TEXT("UnifiedGameplayMenu"));Menu && Menu->RouteSourceAnalog(Event)) return FReply::Handled();
    }
    return Super::NativeOnAnalogValueChanged(Geometry,Event);
}
FReply UMCUnifiedUIWidget::NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(WidgetTree && ActiveScreen==EMCUnifiedUIScreen::Emotes && HasEmotePresentation())
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes"));View && View->RouteSourceKeyUp(Event)) return FReply::Handled();
    if(TeamBackend.IsValid() && (Event.GetKey()==EKeys::Tab || Event.GetKey()==EKeys::Gamepad_Special_Left)) {
        if(auto* PC=GetOwningPlayer<AMCPlayerController>();PC && !PC->IsHubWorld()) TeamBackend->RequestUnifiedClose();
        return FReply::Handled();
    }
    return Super::NativeOnKeyUp(Geometry,Event);
}
FReply UMCUnifiedUIWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(ActiveScreen==EMCUnifiedUIScreen::Settings && WidgetTree)
        if(auto* Settings=WidgetTree->FindWidget<UMCUnifiedSettingsWidget>(TEXT("UnifiedSettings"));Settings && Settings->RouteRebindKey(Event.GetEffectingButton())) return FReply::Handled();
    if(ActiveScreen==EMCUnifiedUIScreen::GameplayMenu && WidgetTree)
        if(auto* Menu=WidgetTree->FindWidget<UMCUnifiedGameplayMenuWidget>(TEXT("UnifiedGameplayMenu"));Menu && Menu->RouteSourceMouseButton(Event)) return FReply::Handled();
    return Super::NativeOnPreviewMouseButtonDown(Geometry,Event);
}
void UMCUnifiedUIWidget::SetTeamBackend(UMCScoreboardWidget* Backend)
{
    TeamBackend=Backend;
    if(WidgetTree) if(auto* Team=WidgetTree->FindWidget<UMCUnifiedTeamWidget>(TEXT("UnifiedTeam"))) {
        Team->SetBackend(Backend);
        Team->SetVisibility(Backend?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
}
void UMCUnifiedUIWidget::SetWardrobeBackend(UMCHubWardrobeWidget* Backend)
{
    WardrobeBackend=Backend;
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedWardrobeWidget>(TEXT("UnifiedWardrobe"))) View->SetBackend(Backend);
}
void UMCUnifiedUIWidget::SetShopBackend(UMCHubShopWidget* Backend)
{
    ShopBackend=Backend;
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedShopWidget>(TEXT("UnifiedShop"))) View->SetBackend(Backend);
}
void UMCUnifiedUIWidget::SetHubBackend(UMCHubWidget* Backend)
{
    HubBackend=Backend;if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"))) View->SetBackend(Backend);
}
void UMCUnifiedUIWidget::SetRewardBackend(UMCPerkChoiceWidget* Backend)
{
    RewardBackend=Backend;if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedRewardWidget>(TEXT("UnifiedReward"))) View->SetBackend(Backend);
}
void UMCUnifiedUIWidget::SetGameplayMenuBackend(UMCGameplayMenuWidget* Backend)
{
    GameplayMenuBackend=Backend;if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedGameplayMenuWidget>(TEXT("UnifiedGameplayMenu"))) View->SetBackend(Backend);
}
bool UMCUnifiedUIWidget::PrepareEncounterPresentation()
{
    if(!WidgetTree) return false;
    auto* Boss=WidgetTree->FindWidget<UMCUnifiedBossWidget>(TEXT("UnifiedBoss"));
    auto* Alert=WidgetTree->FindWidget<UMCUnifiedAlertWidget>(TEXT("UnifiedAlert"));
    if(!Boss || !Alert) return false;
    if(!Boss->IsPresentationPrepared()) Boss->PreparePresentation();if(!Alert->IsPresentationPrepared()) Alert->PreparePresentation();return Boss->IsPresentationPrepared() && Alert->IsPresentationPrepared();
}
void UMCUnifiedUIWidget::SetBossBackend(UMCBossHealthWidget* Source)
{
    if(BossBackend.Get()!=Source) if(auto* Previous=BossBackend.Get()) Previous->SetUnifiedViewActive(false);
    BossBackend=Source;
    auto* View=WidgetTree?WidgetTree->FindWidget<UMCUnifiedBossWidget>(TEXT("UnifiedBoss")):nullptr;
    if(View) {View->SetBackend(Source);if(!View->IsPresentationPrepared()) View->PreparePresentation();}
    if(Source) Source->SetUnifiedViewActive(View && View->IsPresentationPrepared());
}
void UMCUnifiedUIWidget::SetAlertBackend(UMCGameplayAlertWidget* Source)
{
    if(AlertBackend.Get()!=Source) if(auto* Previous=AlertBackend.Get()) Previous->SetUnifiedViewActive(false);
    AlertBackend=Source;
    auto* View=WidgetTree?WidgetTree->FindWidget<UMCUnifiedAlertWidget>(TEXT("UnifiedAlert")):nullptr;
    if(View) {View->SetBackend(Source);if(!View->IsPresentationPrepared()) View->PreparePresentation();}
    if(Source) Source->SetUnifiedViewActive(View && View->IsPresentationPrepared());
}
bool UMCUnifiedUIWidget::PrepareOverlayPresentation(UMCTutorialWidget* TutorialSource,UMCEmoteWidget* EmoteSource)
{
    if(!WidgetTree) return false;
    SetTutorialBackend(TutorialSource);SetEmoteBackend(EmoteSource);
    auto* Tutorial=WidgetTree->FindWidget<UMCUnifiedTutorialWidget>(TEXT("UnifiedTutorial"));
    auto* Emotes=WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes"));
    if(!Tutorial || !Emotes) return false;
    Tutorial->PreparePresentation();Emotes->PreparePresentation();
    return Tutorial->IsPresentationPrepared() && Emotes->IsPresentationPrepared();
}
void UMCUnifiedUIWidget::SetTutorialBackend(UMCTutorialWidget* Source)
{
    TutorialBackend=Source;
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedTutorialWidget>(TEXT("UnifiedTutorial"))) View->SetBackend(Source);
}
void UMCUnifiedUIWidget::SetEmoteBackend(UMCEmoteWidget* Source)
{
    if(EmoteBackend.Get()!=Source) {SetEmoteFocusLease(false);PreparedEmoteCorpus.Reset();}
    EmoteBackend=Source;
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes"))) {
        View->SetBackend(Source);
        if(Source) {
            if(View->GetResolvedTranslationTable()!=Source->GetResolvedTranslationTable()) View->SetTranslationTable(Source->GetResolvedTranslationTable());
            const auto Snapshot=Source->GetUnifiedPresentationSnapshot();FString Corpus;
            for(const auto& Copy:Snapshot.KnownChoiceLabels) Corpus+=Copy.ToString()+TEXT("\n");
            // A preloading call can precede possession/library resolution. Warm the actual corpus when it arrives.
            if(Corpus!=PreparedEmoteCorpus) {PreparedEmoteCorpus=MoveTemp(Corpus);View->ApplyTheme();}
        }
        if(!View->IsPresentationPrepared()) View->PreparePresentation();
    }
}
bool UMCUnifiedUIWidget::HasTutorialPresentation() const
{
    const auto* Source=TutorialBackend.Get();const auto* View=WidgetTree?WidgetTree->FindWidget<UMCUnifiedTutorialWidget>(TEXT("UnifiedTutorial")):nullptr;
    return Source && Source->GetWorld()==GetWorld() && Source->GetOwningPlayer()==GetOwningPlayer()
        && Source->IsInViewport() && Source->IsVisible() && Source->WidgetTree && Source->WidgetTree->FindWidget(TEXT("TutorialRoot"))
        && Source->WidgetTree->FindWidget(TEXT("TutorialRoot"))!=Source->WidgetTree->FindWidget(TEXT("TutorialCard"))
        && View && View->IsPresentationPrepared() && Source->GetUnifiedPresentationSnapshot().bVisible;
}
bool UMCUnifiedUIWidget::RequiresTutorialMenuInput() const
{
    return ActiveScreen==EMCUnifiedUIScreen::Gameplay && HasTutorialPresentation() && TutorialBackend->GetUnifiedPresentationSnapshot().bRequiresMenuInput;
}
bool UMCUnifiedUIWidget::HasEmotePresentation() const
{
    const auto* Source=EmoteBackend.Get();const auto* View=WidgetTree?WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes")):nullptr;
    return Source && Source->GetWorld()==GetWorld() && Source->GetOwningPlayer()==GetOwningPlayer()
        && Source->IsInViewport() && Source->IsVisible() && View && View->IsPresentationPrepared() && View->HasRuntimePresentation();
}
void UMCUnifiedUIWidget::SetEmoteFocusLease(bool bActive)
{
    auto* Source=EmoteBackend.Get();auto* View=WidgetTree?WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes")):nullptr;
    if(!Source || !View) return;
    if(bActive && ActiveScreen==EMCUnifiedUIScreen::Emotes && HasEmotePresentation()) Source->SetUnifiedFocusView(View);
    else Source->ClearUnifiedFocusView(View);
}

bool UMCUnifiedUIWidget::PrepareHUDExtrasPresentation()
{
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedHUDExtrasWidget>(TEXT("UnifiedHUDExtras"))) {
        View->PreparePresentation();return View->IsPresentationPrepared();
    }
    return false;
}
bool UMCUnifiedUIWidget::PrepareCatchPresentation(UMCFishingRewardWidget* Backend)
{
    if(!Backend || !WidgetTree) return false;
    if(auto* View=WidgetTree->FindWidget<UMCUnifiedCatchWidget>(TEXT("UnifiedCatch"))) {
        CatchBackend=Backend;View->SetBackend(Backend);View->PreparePresentation();
        return View->IsPresentationPrepared();
    }
    return false;
}
void UMCUnifiedUIWidget::SetCatchBackend(UMCFishingRewardWidget* Backend)
{
    CatchBackend=Backend;
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedCatchWidget>(TEXT("UnifiedCatch"))) {
        View->SetBackend(Backend);View->SetVisibility(Backend?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
}
bool UMCUnifiedUIWidget::RequiresHubMenuInput() const
{
    if(WidgetTree) if(auto* View=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"))) return View->RequiresMenuInput();return false;
}
bool UMCUnifiedUIWidget::GetHubFishCounterTargets(UWidget*& Icon,UWidget*& Card) const
{
    Icon=nullptr;Card=nullptr;if(!HubBackend.IsValid() || ActiveScreen!=EMCUnifiedUIScreen::Hub || !WidgetTree) return false;
    if(auto* View=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"))) {Icon=View->GetFishCounterIcon();Card=View->GetFishCounterCard();}
    return Icon && Card;
}
void UMCUnifiedUIWidget::EnsureAdditionalScreens()
{
    auto* Pages=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("InterfaceScreens"));
    auto* Safe=Cast<USafeZone>(WidgetTree->RootWidget);if(!Pages || !Safe) return;
    if(!WidgetTree->FindWidget(TEXT("MenuActionsBounds")))
        if(auto* Scroll=WidgetTree->FindWidget<UScrollBox>(TEXT("MenuActionsScroll")))
            if(auto* Page=Cast<UCanvasPanel>(Scroll->GetParent())) {
                const auto* Original=Cast<UCanvasPanelSlot>(Scroll->Slot);
                const FVector2D Anchor=Original?Original->GetAnchors().Minimum:FVector2D(.077,.429);
                Scroll->RemoveFromParent();
                auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("MenuActionsBounds"));
                Bounds->SetMinDesiredWidth(360);Bounds->SetMaxDesiredWidth(900);Bounds->SetMaxDesiredHeight(520);Bounds->SetContent(Scroll);
                auto* Cell=Page->AddChildToCanvas(Bounds);Cell->SetAnchors(FAnchors(Anchor.X,Anchor.Y));Cell->SetPosition(FVector2D::ZeroVector);Cell->SetAutoSize(true);
            }
    auto* Layers=WidgetTree->FindWidget<UOverlay>(TEXT("InterfaceLayers"));
    if(!Layers) {
        Safe->SetContent(nullptr);
        Layers=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("InterfaceLayers"));
        Safe->SetContent(Layers);
        auto* PageCell=Layers->AddChildToOverlay(Pages);PageCell->SetHorizontalAlignment(HAlign_Fill);PageCell->SetVerticalAlignment(VAlign_Fill);
    }
    for(const TCHAR* Name:{TEXT("Screen_Settings"),TEXT("Screen_Team"),TEXT("Screen_Reward"),TEXT("Screen_Frontend"),TEXT("Screen_GameplayMenu"),TEXT("Screen_Catch"),TEXT("Screen_HUDExtras"),TEXT("Screen_Loading"),TEXT("Screen_Boss"),TEXT("Screen_Alert"),TEXT("Screen_Tutorial"),TEXT("Screen_Emotes")})
        if(!WidgetTree->FindWidget(Name)) Pages->AddChild(WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),FName(Name)));
    auto* Settings=WidgetTree->FindWidget<UMCUnifiedSettingsWidget>(TEXT("UnifiedSettings"));
    if(!Settings) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedSettings"),UMCUnifiedSettingsWidget::StaticClass(),GetResolvedTheme());
        Settings=WidgetTree->ConstructWidget<UMCUnifiedSettingsWidget>(Class,TEXT("UnifiedSettings"));
        Settings->Initialize();Settings->BuildPresentationTree();
    }
    auto* Team=WidgetTree->FindWidget<UMCUnifiedTeamWidget>(TEXT("UnifiedTeam"));
    if(!Team) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedTeam"),UMCUnifiedTeamWidget::StaticClass(),GetResolvedTheme());
        Team=WidgetTree->ConstructWidget<UMCUnifiedTeamWidget>(Class,TEXT("UnifiedTeam"));
        Team->Initialize();Team->BuildPresentationTree();Team->SetVisibility(ESlateVisibility::Collapsed);
    }
    auto* Hub=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"));
    if(!Hub) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedHub"),UMCUnifiedHubWidget::StaticClass(),GetResolvedTheme());
        Hub=WidgetTree->ConstructWidget<UMCUnifiedHubWidget>(Class,TEXT("UnifiedHub"));Hub->Initialize();Hub->BuildPresentationTree();
        if(auto* Page=WidgetTree->FindWidget<UCanvasPanel>(TEXT("Screen_Hub"))) {
            auto* Cell=Page->AddChildToCanvas(Hub);Cell->SetAnchors(FAnchors(0,0,1,1));Cell->SetOffsets(FMargin(0));
            Hub->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        }
    }
    auto* Reward=WidgetTree->FindWidget<UMCUnifiedRewardWidget>(TEXT("UnifiedReward"));
    if(!Reward) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedReward"),UMCUnifiedRewardWidget::StaticClass(),GetResolvedTheme());
        Reward=WidgetTree->ConstructWidget<UMCUnifiedRewardWidget>(Class,TEXT("UnifiedReward"));Reward->Initialize();Reward->BuildPresentationTree();
    }
    auto* Frontend=WidgetTree->FindWidget<UMCUnifiedFrontendWidget>(TEXT("UnifiedFrontend"));
    if(!Frontend) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedFrontend"),UMCUnifiedFrontendWidget::StaticClass(),GetResolvedTheme());
        Frontend=WidgetTree->ConstructWidget<UMCUnifiedFrontendWidget>(Class,TEXT("UnifiedFrontend"));Frontend->Initialize();Frontend->BuildPresentationTree();
    }
    auto* GameplayMenu=WidgetTree->FindWidget<UMCUnifiedGameplayMenuWidget>(TEXT("UnifiedGameplayMenu"));
    if(!GameplayMenu) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedGameplayMenu"),UMCUnifiedGameplayMenuWidget::StaticClass(),GetResolvedTheme());
        GameplayMenu=WidgetTree->ConstructWidget<UMCUnifiedGameplayMenuWidget>(Class,TEXT("UnifiedGameplayMenu"));GameplayMenu->Initialize();GameplayMenu->BuildPresentationTree();
    }
    if(!WidgetTree->FindWidget(TEXT("UnifiedCatch"))) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedCatch"),UMCUnifiedCatchWidget::StaticClass(),GetResolvedTheme());
        auto* View=WidgetTree->ConstructWidget<UMCUnifiedCatchWidget>(Class,TEXT("UnifiedCatch"));
        View->Initialize();View->BuildPresentationTree();View->SetVisibility(ESlateVisibility::Collapsed);
        auto* Cell=Layers->AddChildToOverlay(View);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
    }
    if(!WidgetTree->FindWidget(TEXT("UnifiedHUDExtras"))) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedHUDExtras"),UMCUnifiedHUDExtrasWidget::StaticClass(),GetResolvedTheme());
        auto* View=WidgetTree->ConstructWidget<UMCUnifiedHUDExtrasWidget>(Class,TEXT("UnifiedHUDExtras"));
        View->Initialize();View->BuildPresentationTree();View->SetVisibility(ESlateVisibility::Collapsed);
        auto* Cell=Layers->AddChildToOverlay(View);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
    }
    if(!WidgetTree->FindWidget(TEXT("UnifiedLoading"))) if(auto* Page=WidgetTree->FindWidget<UCanvasPanel>(TEXT("Screen_Loading"))) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedLoading"),UMCUnifiedLoadingWidget::StaticClass(),GetResolvedTheme());
        auto* View=WidgetTree->ConstructWidget<UMCUnifiedLoadingWidget>(Class,TEXT("UnifiedLoading"));
        View->Initialize();View->BuildPresentationTree();
        auto* Cell=Page->AddChildToCanvas(View);Cell->SetAnchors(FAnchors(0,0,1,1));Cell->SetOffsets(FMargin(0));
    }
    if(!WidgetTree->FindWidget(TEXT("UnifiedBoss"))) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedBoss"),UMCUnifiedBossWidget::StaticClass(),GetResolvedTheme());
        auto* View=WidgetTree->ConstructWidget<UMCUnifiedBossWidget>(Class,TEXT("UnifiedBoss"));View->Initialize();View->BuildPresentationTree();View->SetVisibility(ESlateVisibility::Collapsed);
        auto* Cell=Layers->AddChildToOverlay(View);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
    }
    if(!WidgetTree->FindWidget(TEXT("UnifiedAlert"))) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedAlert"),UMCUnifiedAlertWidget::StaticClass(),GetResolvedTheme());
        auto* View=WidgetTree->ConstructWidget<UMCUnifiedAlertWidget>(Class,TEXT("UnifiedAlert"));View->Initialize();View->BuildPresentationTree();View->SetVisibility(ESlateVisibility::Collapsed);
        auto* Cell=Layers->AddChildToOverlay(View);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
    }
    if(!WidgetTree->FindWidget(TEXT("UnifiedTutorial"))) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedTutorial"),UMCUnifiedTutorialWidget::StaticClass(),GetResolvedTheme());
        auto* View=WidgetTree->ConstructWidget<UMCUnifiedTutorialWidget>(Class,TEXT("UnifiedTutorial"));View->Initialize();View->BuildPresentationTree();View->SetVisibility(ESlateVisibility::Collapsed);
        auto* Cell=Layers->AddChildToOverlay(View);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
    }
    if(!WidgetTree->FindWidget(TEXT("UnifiedEmotes"))) if(auto* Page=WidgetTree->FindWidget<UCanvasPanel>(TEXT("Screen_Emotes"))) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedEmotes"),UMCUnifiedEmoteWidget::StaticClass(),GetResolvedTheme());
        auto* View=WidgetTree->ConstructWidget<UMCUnifiedEmoteWidget>(Class,TEXT("UnifiedEmotes"));View->Initialize();View->BuildPresentationTree();View->SetVisibility(ESlateVisibility::Collapsed);
        auto* Cell=Page->AddChildToCanvas(View);Cell->SetAnchors(FAnchors(0,0,1,1));Cell->SetOffsets(FMargin(0));
    }
    // The host constrains the available screen and scrolls overflow. It never
    // scales text down when a translation makes the component larger.
    auto Host=[this](UWidget* View,FName Name,FName FrameName) {
        auto* Scroll=WidgetTree->FindWidget<UScrollBox>(Name);
        if(Scroll) return Scroll;
        if(auto* Old=View->GetParent();Old && Old->GetFName()==TEXT("SettingsScale")) Old->RemoveFromParent();
        View->RemoveFromParent();
        Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),Name);
        auto* Frame=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FrameName);
        Frame->SetMinDesiredHeight(1016);
        auto* Cell=CastChecked<UScrollBoxSlot>(Scroll->AddChild(Frame));Cell->SetHorizontalAlignment(HAlign_Fill);
        Frame->SetContent(View);
        if(auto* ContentCell=Cast<USizeBoxSlot>(View->Slot)) {ContentCell->SetHorizontalAlignment(HAlign_Center);ContentCell->SetVerticalAlignment(VAlign_Center);}
        return Scroll;
    };
    // Commerce occupies the existing numbered pages; append editable hosts only.
    auto* Wardrobe=WidgetTree->FindWidget<UMCUnifiedWardrobeWidget>(TEXT("UnifiedWardrobe"));
    if(!Wardrobe) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedWardrobe"),UMCUnifiedWardrobeWidget::StaticClass(),GetResolvedTheme());
        Wardrobe=WidgetTree->ConstructWidget<UMCUnifiedWardrobeWidget>(Class,TEXT("UnifiedWardrobe"));
        Wardrobe->Initialize();Wardrobe->BuildPresentationTree();
    }
    auto* Shop=WidgetTree->FindWidget<UMCUnifiedShopWidget>(TEXT("UnifiedShop"));
    if(!Shop) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedShop"),UMCUnifiedShopWidget::StaticClass(),GetResolvedTheme());
        Shop=WidgetTree->ConstructWidget<UMCUnifiedShopWidget>(Class,TEXT("UnifiedShop"));
        Shop->Initialize();Shop->BuildPresentationTree();
    }
    auto CommerceHost=[&](UWidget* View,FName Viewport,FName Frame,FName PageName) {
        if(WidgetTree->FindWidget(Viewport)) return;
        auto* Page=WidgetTree->FindWidget<UCanvasPanel>(PageName);if(!Page) return;
        auto* Scroll=Host(View,Viewport,Frame);
        // A narrow logical viewport must scroll overflow rather than clip or
        // shrink text. Centre smaller views inside a minimum viewport width.
        if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(Frame)) {
            View->RemoveFromParent();
            auto* Across=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),FName(*(Viewport.ToString()+TEXT("Horizontal"))));
            Across->SetOrientation(Orient_Horizontal);Across->SetScrollBarVisibility(ESlateVisibility::Collapsed);
            Across->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
            auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(Viewport.ToString()+TEXT("Content"))));
            Width->SetMinDesiredWidth(1856);Width->SetContent(View);
            if(auto* Cell=Cast<USizeBoxSlot>(View->Slot)) {Cell->SetHorizontalAlignment((View==Reward || View==GameplayMenu)?HAlign_Center:HAlign_Left);Cell->SetVerticalAlignment(VAlign_Center);}
            Across->AddChild(Width);Bounds->SetContent(Across);
        }
        Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
        auto* Cell=Page->AddChildToCanvas(Scroll);Cell->SetAnchors(FAnchors(0,0,1,1));Cell->SetOffsets(FMargin(32));
    };
    CommerceHost(Wardrobe,TEXT("WardrobeViewport"),TEXT("WardrobeViewportFrame"),TEXT("Screen_Wardrobe"));
    CommerceHost(Shop,TEXT("ShopViewport"),TEXT("ShopViewportFrame"),TEXT("Screen_Boosters"));
    CommerceHost(Reward,TEXT("RewardViewport"),TEXT("RewardViewportFrame"),TEXT("Screen_Reward"));
    CommerceHost(Frontend,TEXT("FrontendViewport"),TEXT("FrontendViewportFrame"),TEXT("Screen_Frontend"));
    CommerceHost(GameplayMenu,TEXT("GameplayMenuViewport"),TEXT("GameplayMenuViewportFrame"),TEXT("Screen_GameplayMenu"));
    if(!WidgetTree->FindWidget(TEXT("SettingsViewport"))) {
        auto* Scroll=Host(Settings,TEXT("SettingsViewport"),TEXT("SettingsViewportFrame"));
        auto* Page=WidgetTree->FindWidget<UCanvasPanel>(TEXT("Screen_Settings"));
        auto* Cell=Page->AddChildToCanvas(Scroll);Cell->SetAnchors(FAnchors(0,0,1,1));Cell->SetOffsets(FMargin(32));
    }
    if(!WidgetTree->FindWidget(TEXT("TeamViewport"))) {
        auto* Scroll=Host(Team,TEXT("TeamViewport"),TEXT("TeamViewportFrame"));
        Scroll->SetVisibility(ESlateVisibility::Collapsed);
        auto* Cell=Layers->AddChildToOverlay(Scroll);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);Cell->SetPadding(FMargin(32));
    }
}
UTexture2D* UMCUnifiedUIWidget::ResolveToolArt(FName Key) const
{
    const auto* Asset=ToolIconArt.Find(Key);return Asset?(IsDesignTime()?Asset->LoadSynchronous():Asset->Get()):nullptr;
}

void UMCUnifiedUIWidget::EnsureGameplayControls()
{
    if(!WidgetTree) return;
    auto* Screen=WidgetTree->FindWidget<UCanvasPanel>(TEXT("Screen_Gameplay"));if(!Screen) return;
    auto Body=[this](FName Name)->UCanvasPanel* {
        auto* Shape=WidgetTree->FindWidget<UMCUniversalUIWidget>(Name);
        return Shape?Cast<UCanvasPanel>(Shape->GetContentForSlot(TEXT("Content"))):nullptr;
    };
    auto Label=[this](UCanvasPanel* Parent,FName Name,FVector2D Pos,FVector2D Size,int32 FontSize) {
        if(!Parent || WidgetTree->FindWidget(Name)) return;
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Text->SetAutoWrapText(false);Text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        UnifiedRootPlace(Parent,Text,Pos,Size);BindStyle(Name,EMCUIColorToken::Cream,EMCUIStyleTarget::Text,FontSize,true);
    };
    if(!WidgetTree->FindWidget(TEXT("V2RunSummary"))) {
        Label(Screen,TEXT("V2RunSummary"),{50,194},{340,46},14);
        if(auto* Text=WidgetTree->FindWidget<UTextBlock>(TEXT("V2RunSummary"))) Text->SetAutoWrapText(true);
    }
    Label(Body(TEXT("NextEventBlock")),TEXT("V2CurrentEvent"),{5,72},{365,22},14);
    Label(Body(TEXT("ToolResourceBlock")),TEXT("V2ToolCooldown"),{120,108},{225,20},12);
    if(!WidgetTree->FindWidget(TEXT("V2ToolArt"))) if(auto* Content=Body(TEXT("ToolResourceBlock"))) {
        auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("V2ToolArtFit"));Fit->SetStretch(EStretch::ScaleToFit);
        auto* Image=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("V2ToolArt"));Fit->SetContent(Image);
        UnifiedRootPlace(Content,Fit,{3,7},{100,100});
    }
    for(int32 I=1;I<=4;++I) {
        auto* Content=Body(FName(*FString::Printf(TEXT("V2Skill%d"),I)));if(!Content) continue;
        const FName Name(*FString::Printf(TEXT("V2SkillIcon%d"),I));
        if(!WidgetTree->FindWidget(Name)) {
            auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name);Icon->SetVisibility(ESlateVisibility::Collapsed);
            auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),FName(*(Name.ToString()+TEXT("Fit"))));Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(Icon);UnifiedRootPlace(Content,Fit,{9,0},{60,60});
        }
        Label(Content,FName(*FString::Printf(TEXT("V2SkillStatus%d"),I)),{0,60},{78,18},12);
    }
    if(!WidgetTree->FindWidget(TEXT("V2ActionHint"))) {
        auto* Hint=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("V2ActionHint"));
        Hint->SetAutoWrapText(true);Hint->SetJustification(ETextJustify::Center);
        UnifiedRootPlace(Screen,Hint,{0,-190},{520,64},{.5,1},{.5,1});BindStyle(Hint->GetFName(),EMCUIColorToken::Cream,EMCUIStyleTarget::Text,18,true);
        auto* Contact=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("V2ContactProgress"));
        FProgressBarStyle Style;Style.SetBackgroundImage(FSlateRoundedBoxBrush(UnifiedRootColor(TEXT("30333F")),2.f));Style.SetFillImage(FSlateRoundedBoxBrush(UnifiedRootColor(TEXT("B0FCF3")),2.f));
        Contact->SetWidgetStyle(Style);Contact->SetPercent(0);UnifiedRootPlace(Screen,Contact,{0,-182},{460,5},{.5,1},{.5,1});
    }
}

void UMCUnifiedUIWidget::RefreshAdaptiveLayout()
{
    if(!WidgetTree || !FSlateApplication::IsInitialized()) return;
    const FVector2D Geometry=GetPaintSpaceGeometry().GetLocalSize();
    const float AreaX=Geometry.X>100?Geometry.X:1920.f,AreaY=Geometry.Y>100?Geometry.Y:1080.f;
    if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("MenuActionsBounds"))) {
        Bounds->SetMaxDesiredWidth(FMath::Max(280.f,FMath::Min(900.f,AreaX*.85f)));
        Bounds->SetMaxDesiredHeight(FMath::Max(160.f,AreaY*.51f));
    }
    for(const TCHAR* Name:{TEXT("SettingsViewportFrame"),TEXT("TeamViewportFrame"),TEXT("WardrobeViewportFrame"),TEXT("ShopViewportFrame"),TEXT("RewardViewportFrame"),TEXT("FrontendViewportFrame"),TEXT("GameplayMenuViewportFrame")})
        if(auto* Frame=WidgetTree->FindWidget<USizeBox>(Name)) Frame->SetMinDesiredHeight(FMath::Max(0.f,AreaY-64));
    for(const TCHAR* Name:{TEXT("WardrobeViewportContent"),TEXT("ShopViewportContent"),TEXT("RewardViewportContent"),TEXT("FrontendViewportContent"),TEXT("GameplayMenuViewportContent")})
        if(auto* Width=WidgetTree->FindWidget<USizeBox>(Name)) Width->SetMinDesiredWidth(FMath::Max(0.f,AreaX-64));
    if(ActiveScreen!=EMCUnifiedUIScreen::Gameplay && (!IsDesignTime() || PreviewScreen!=EMCUnifiedUIScreen::Gameplay)) return;
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    auto Label=[this](FName Name) {return WidgetTree->FindWidget<UTextBlock>(Name);};
    auto Natural=[&](UTextBlock* Text) -> FVector2D {if(Text) {const FVector2D Size=Measure->Measure(Text->GetText(),Text->GetFont());return Size;}return FVector2D::ZeroVector;};
    auto Reflow=[&](UTextBlock* Text,float Width,FVector2D Position) {
        if(!Text) return 0.f;
        Width=FMath::Max(40.f,Width);
        const FVector2D TextSize=Natural(Text);
        const float Line=FMath::Max(1.f,TextSize.Y);
        const float Height=FMath::Max(Line*FMath::Max(1,FMath::CeilToInt((TextSize.X+10)/Width)),static_cast<float>(Text->GetDesiredSize().Y))+6;
        Text->SetAutoWrapText(true);Text->SetWrapTextAt(Width);
        Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        Text->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
        if(auto* Cell=Cast<UCanvasPanelSlot>(Text->Slot)) {Cell->SetPosition(Position);Cell->SetSize({Width,Height});}
        return Height;
    };
    const float LeftLimit=FMath::Max(340.f,FMath::Min(900.f,AreaX*.45f));
    if(auto* Shape=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ReserveBlock"))) {
        auto* Clean=Label(TEXT("V2CleanLabel"));
        const float Width=FMath::Clamp(static_cast<float>(Natural(Clean).X)+40.f,340.f,LeftLimit);
        const float Height=Reflow(Clean,Width-28,{4,68});
        if(auto* Cell=Cast<UCanvasPanelSlot>(Shape->Slot)) Cell->SetSize({Width,68+Height+44});
        if(auto* Bar=WidgetTree->FindWidget(TEXT("V2CleanBar"))) if(auto* Cell=Cast<UCanvasPanelSlot>(Bar->Slot)) {Cell->SetPosition({4,68+Height+5});Cell->SetSize({Width-28,28});}
        if(auto* Summary=Label(TEXT("V2RunSummary"))) Reflow(Summary,Width,{50,25+68+Height+64});
    }
    if(auto* Shape=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ToolResourceBlock"))) {
        auto* Tool=Label(TEXT("V2ToolName"));auto* Player=Label(TEXT("V2PlayerName"));
        const float ToolWidth=FMath::Clamp(static_cast<float>(Natural(Tool).X)+20.f,90.f,LeftLimit*.6f);
        const float Width=FMath::Clamp(ToolWidth+static_cast<float>(Natural(Player).X)+54.f,380.f,LeftLimit);
        const float PlayerWidth=Width-ToolWidth-34.f;
        const float ToolHeight=Reflow(Tool,ToolWidth,{4,0}),PlayerHeight=Reflow(Player,PlayerWidth,{ToolWidth+18,0});
        const float Top=-FMath::Max(ToolHeight,PlayerHeight)-12.f;
        Reflow(Tool,ToolWidth,{4,Top});Reflow(Player,PlayerWidth,{ToolWidth+18,Top});
        if(auto* Cell=Cast<UCanvasPanelSlot>(Shape->Slot)) Cell->SetSize({Width,145});
        for(const TCHAR* Name:{TEXT("V2HealthBar"),TEXT("V2StaminaBar")})
            if(auto* Bar=WidgetTree->FindWidget(Name)) if(auto* Cell=Cast<UCanvasPanelSlot>(Bar->Slot)) Cell->SetSize({Width-155,Cell->GetSize().Y});
    }
    if(auto* Shape=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("NextEventBlock"))) {
        auto* Heading=Label(TEXT("V2NextEventLabel"));auto* Event=Label(TEXT("V2NextEvent"));auto* Current=Label(TEXT("V2CurrentEvent"));
        const float NaturalWidth=static_cast<float>(FMath::Max3(Natural(Heading).X,Natural(Event).X,Natural(Current).X))+40.f;
        const float Width=FMath::Clamp(NaturalWidth,395.f,FMath::Max(395.f,FMath::Min(700.f,AreaX*.36f)));
        float Y=0;Y+=Reflow(Heading,Width-30,{5,Y})+6;
        if(Event && Event->IsVisible()) Y+=Reflow(Event,Width-30,{5,Y})+6;
        Y+=Reflow(Current,Width-30,{5,Y})+18;
        if(auto* Cell=Cast<UCanvasPanelSlot>(Shape->Slot)) Cell->SetSize({Width,FMath::Max(110.f,Y)});
    }
}

void UMCUnifiedUIWidget::RefreshRuntimePresentation()
{
    if(WidgetTree) {
        if(auto* Settings=WidgetTree->FindWidget<UMCUnifiedSettingsWidget>(TEXT("UnifiedSettings"))) Settings->RefreshFromBackend();
        if(auto* Team=WidgetTree->FindWidget<UMCUnifiedTeamWidget>(TEXT("UnifiedTeam"))) Team->RefreshFromBackend();
        if(auto* Wardrobe=WidgetTree->FindWidget<UMCUnifiedWardrobeWidget>(TEXT("UnifiedWardrobe"))) Wardrobe->RefreshFromBackend();
        if(auto* Shop=WidgetTree->FindWidget<UMCUnifiedShopWidget>(TEXT("UnifiedShop"))) Shop->RefreshFromBackend();
        if(auto* Hub=WidgetTree->FindWidget<UMCUnifiedHubWidget>(TEXT("UnifiedHub"))) Hub->RefreshFromBackend();
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedTutorialWidget>(TEXT("UnifiedTutorial"))) View->RefreshFromBackend();
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedEmoteWidget>(TEXT("UnifiedEmotes"))) View->RefreshFromBackend();
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedCatchWidget>(TEXT("UnifiedCatch"))) View->RefreshFromBackend();
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedHUDExtrasWidget>(TEXT("UnifiedHUDExtras"))) View->RefreshFromBackend();
        auto* Boss=WidgetTree->FindWidget<UMCUnifiedBossWidget>(TEXT("UnifiedBoss"));if(Boss) Boss->RefreshFromBackend();
        if(auto* View=WidgetTree->FindWidget<UMCUnifiedAlertWidget>(TEXT("UnifiedAlert"))) {
            View->SetBossStackPresentation(BossBackend.IsValid() && Boss?Boss->GetDisplayedBossCount():-1,Boss?Boss->GetDisplayedStackHeight():0);
            View->RefreshFromBackend();
        }
        if(auto* Reward=WidgetTree->FindWidget<UMCUnifiedRewardWidget>(TEXT("UnifiedReward"))) Reward->RefreshFromBackend();
        if(auto* Frontend=WidgetTree->FindWidget<UMCUnifiedFrontendWidget>(TEXT("UnifiedFrontend"))) Frontend->RefreshFromBackend();
        if(auto* Menu=WidgetTree->FindWidget<UMCUnifiedGameplayMenuWidget>(TEXT("UnifiedGameplayMenu"))) Menu->RefreshFromBackend();
    }
    auto* Source=HUDBackend.Get();if(!Source || !WidgetTree || !Source->WidgetTree) return;
    auto Find=[this](FName Name)->UWidget* {const auto* Item=HUDControls.Find(Name);return Item?Item->Get():nullptr;};
    auto Set=[this](FName Name,const FText& Value) {if(auto* Text=WidgetTree->FindWidget<UTextBlock>(Name)) Text->SetText(Value);};
    auto Text=[&Find](FName Name)->FText {if(auto* Label=Cast<UTextBlock>(Find(Name))) return Label->GetText();return FText::GetEmpty();};
    auto CopyBar=[&](FName Target,FName Name) {
        auto* To=WidgetTree->FindWidget<UMCUniversalUIWidget>(Target);auto* From=Cast<UProgressBar>(Find(Name));if(To && From) To->SetProgressValue(From->GetPercent());
    };
    Set(TEXT("V2ReserveCount"),Text(TEXT("ReserveValue")));CopyBar(TEXT("V2CleanBar"),TEXT("CleanProgress"));
    // The existing presenter owns timeline selection, including the legacy DayPlan path.
    Set(TEXT("V2NextEvent"),Source->GetUnifiedNextEventTitle());
    Set(TEXT("V2NextEventLabel"),Localized(TEXT("UI.V2.HUD.NextEvent"),LOCTEXT("NextEvent","СЛЕДУЮЩЕЕ СОБЫТИЕ")));
    if(auto* Next=WidgetTree->FindWidget(TEXT("V2NextEvent"))) Next->SetVisibility(Source->HasUnifiedNextEvent()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    Set(TEXT("V2CurrentEvent"),FText::Format(FText::AsCultureInvariant(TEXT("{0}: {1}")),Localized(TEXT("UI.HUD.Timeline.Now"),LOCTEXT("Now","СЕЙЧАС")),Source->GetUnifiedCurrentEventTitle()));
    Set(TEXT("V2RunSummary"),FText::Format(FText::AsCultureInvariant(TEXT("{0} {1} · {2} {3}")),Text(TEXT("TimerLabel")),Text(TEXT("TimerValue")),Text(TEXT("TaskLabel")),Text(TEXT("TaskValue"))));
    Set(TEXT("V2ToolName"),Source->GetUnifiedToolName());
    if(auto* Count=WidgetTree->FindWidget<UTextBlock>(TEXT("V2ReserveCount"))) {
        FString Left,Right;if(Count->GetText().ToString().Split(TEXT("/"),&Left,&Right)) Count->SetText(FText::FromString(Left.TrimStartAndEnd()));
    }
    for(int32 I=1;I<=4;++I) {
        auto* Card=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(*FString::Printf(TEXT("V2Player%d"),I)));
        auto* Original=Find(FName(*FString::Printf(TEXT("Player%d"),I)));
        if(Card && Original) {
            if(auto* Holder=Card->GetParent()) Holder->SetVisibility(Original->IsVisible()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            auto* Face=Cast<UMCHUDIcon>(Find(FName(*FString::Printf(TEXT("Player%dFace"),I))));
            if(auto* Content=Cast<UCanvasPanel>(Card->GetContentForSlot(TEXT("Content")));Content && Face)
                for(UWidget* Child:Content->GetAllChildren()) if(auto* To=Cast<UMCHUDIcon>(Child)) {
                    To->bReserveTooth=false;To->bTooth=true;To->Tint=Face->Tint;To->bHost=Face->bHost;To->bDead=Face->bDead;To->bSad=Face->bSad;To->bHappy=Face->bHappy;
                    To->InvalidateLayoutAndVolatility();
                }
        }
        CopyBar(FName(*FString::Printf(TEXT("V2PlayerHP%d"),I)),FName(*FString::Printf(TEXT("Player%dHealth"),I)));
    }
    if(auto* Stamina=Cast<UMCStaminaWidget>(Find(TEXT("StaminaWidget"))))
        if(auto* Bar=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("V2StaminaBar"))) Bar->SetProgressValue(Stamina->Normalized);
    auto* PC=GetOwningPlayer();auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    if(auto* Bar=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("V2HealthBar"))) {
        const auto* Status=Hero?Hero->Status.Get():nullptr;Bar->SetProgressValue(Status?Status->State.Health/FMath::Max(1.f,Status->State.MaxHealth):0);
    }
    if(Hero && Hero->Inventory) {
        const auto* Inventory=Hero->Inventory.Get();
        const auto Upgrade=Inventory->SelectedUpgrade();
        const FString Key=Upgrade!=EMCToolUpgrade::None?TEXT("ToolUpgrade.")+StaticEnum<EMCToolUpgrade>()->GetNameStringByValue(int64(Upgrade)):
            TEXT("Tool.")+StaticEnum<EMCToolSlot>()->GetNameStringByValue(int64(Inventory->Selected));
        auto* Texture=ResolveToolArt(FName(Key));
        if(auto* Image=WidgetTree->FindWidget<UImage>(TEXT("V2ToolArt"))) {Image->SetBrushFromTexture(Texture,true);Image->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
        if(auto* Icon=WidgetTree->FindWidget<UMCHUDIcon>(TEXT("V2ToolIcon"))) {
            Icon->ToolSlot=int32(Inventory->Selected);Icon->SetVisibility(Texture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);Icon->InvalidateLayoutAndVolatility();
        }
    }
    Set(TEXT("V2ToolCooldown"),Text(TEXT("CooldownValue")));
    if(auto* Value=WidgetTree->FindWidget(TEXT("V2ToolCooldown"))) {
        auto* Original=Find(TEXT("SprayCooldown"));Value->SetVisibility(Original && Original->IsVisible()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(PC && PC->PlayerState) Set(TEXT("V2PlayerName"),FText::AsCultureInvariant(PC->PlayerState->GetPlayerName()));
    Set(TEXT("V2ActionHint"),Text(TEXT("ActionHint")));
    if(auto* Contact=WidgetTree->FindWidget<UProgressBar>(TEXT("V2ContactProgress")))
        if(auto* Original=Cast<UProgressBar>(Find(TEXT("ContactProgress")))) {
            Contact->SetPercent(Original->GetPercent());Contact->SetVisibility(Original->GetPercent()>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        }
    const auto* Perks=UMCPerkComponent::ForActor(Hero);
    for(int32 I=0;I<4;++I) {
        const auto* SlotState=Perks && Perks->ActiveSkillSlots.IsValidIndex(I)?&Perks->ActiveSkillSlots[I]:nullptr;
        const bool Filled=SlotState && !SlotState->PerkID.IsNone();FMCPerkDefinition Definition;
        const bool Defined=Filled && Perks->GetPerkDefinition(SlotState->PerkID,Definition);
        const FText Name=Filled?(Defined?Localized(Definition.DisplayNameKey,Definition.DisplayName):FText::FromName(SlotState->PerkID)):Localized(TEXT("UI.Skill.Empty"),LOCTEXT("EmptySkill","Пусто"));
        UTexture2D* Texture=Defined?Definition.Icon.Get():nullptr;
        FText Status;
        if(Filled) {
            const float Cooldown=Perks->GetSkillCooldownRemaining(I);
            if(Cooldown>0) Status=FText::Format(Localized(TEXT("UI.Skill.Cooldown"),LOCTEXT("Cooldown","{0} с")),FText::AsNumber(FMath::CeilToInt(Cooldown)));
            else if(Perks->GetPerkKind(SlotState->PerkID)==EMCPerkKind::Consumable) Status=FText::AsNumber(SlotState->Charges);
            else Status=Localized(TEXT("UI.Skill.Ready"),LOCTEXT("Ready","Готово"));
        }
        Set(FName(*FString::Printf(TEXT("V2SkillLabel%d"),I+1)),Name);Set(FName(*FString::Printf(TEXT("V2SkillStatus%d"),I+1)),Status);
        if(auto* Label=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("V2SkillLabel%d"),I+1)))) Label->SetVisibility(Texture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
        if(auto* Icon=WidgetTree->FindWidget<UImage>(FName(*FString::Printf(TEXT("V2SkillIcon%d"),I+1)))) {
            Icon->SetBrushFromTexture(Texture);Icon->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            Icon->SetToolTipText(FText::Format(FText::AsCultureInvariant(TEXT("{0}\n{1}")),Name,Status));
        }
    }
}

#undef LOCTEXT_NAMESPACE
