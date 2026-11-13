#include "MCRaidLoadoutWidget.h"

#include "Algo/Compare.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "MCHubEconomyComponent.h"
#include "MCHubGameState.h"
#include "MCHubWidget.h"
#include "MCGameplayHUD.h"
#include "MCPerkTypes.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"

#define LOCTEXT_NAMESPACE "MCRaidLoadout"

namespace
{
constexpr int32 RaidSlotsPerCategory=FMCHubRaidLoadout::SlotsPerKind;

FLinearColor RaidColor(const TCHAR* Hex)
{
    return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
}

UTextBlock* RaidLabel(UWidgetTree* Tree,FName Name,const UMCGameplayUITheme* Theme,int32 Size,bool bDark=false)
{
    auto* Label=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    if(Theme) Label->SetFont(Theme->MakeGameplayFont(Size,true));
    Label->SetColorAndOpacity(RaidColor(bDark?TEXT("182537"):TEXT("FFF5E5")));
    Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Label;
}

UMCUniversalUIWidget* RaidSurface(UWidgetTree* Tree,FName Name,const TCHAR* Fill,const TCHAR* Edge,
    bool bButton=false,const FMargin& Padding=FMargin(9.f,5.f))
{
    auto* Surface=Tree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Surface->Kind=bButton?EMCUniversalUIKind::Button:EMCUniversalUIKind::Panel;
    Surface->bUseThemeShape=false;
    Surface->bCustomColors=true;
    Surface->CustomFill=RaidColor(Fill);
    Surface->CustomOutline=RaidColor(Edge);
    Surface->CustomText=RaidColor(TEXT("FFF5E5"));
    Surface->CustomShadow=FLinearColor::Transparent;
    Surface->ShadowOffset=FVector2D::ZeroVector;
    Surface->CornerRadius=bButton?7.f:11.f;
    Surface->OutlineWidth=1.4f;
    Surface->ContentPadding=Padding;
    Surface->FontSize=17;
    Surface->SetVisibility(bButton?ESlateVisibility::Visible:ESlateVisibility::SelfHitTestInvisible);
    Surface->BuildPresentationTree();
    return Surface;
}

void RaidSetText(UWidgetTree* Tree,FName Name,const FText& Text)
{
    if(auto* Label=Tree->FindWidget<UTextBlock>(Name)) Label->SetText(Text);
}

void RaidSetButton(UMCUniversalUIWidget* Button,const FText& Text,bool bEnabled)
{
    if(!Button) return;
    Button->SetIsEnabled(bEnabled);
    Button->SetLabel(NAME_None,Text);
}
}

UMCRaidPerkRowWidget::UMCRaidPerkRowWidget()
{
    bUseNativeLayout=false;
    ActiveFill=RaidColor(TEXT("442962"));ActiveOutline=RaidColor(TEXT("B978DF"));
    PassiveFill=RaidColor(TEXT("20485E"));PassiveOutline=RaidColor(TEXT("6FBED7"));
    StockFill=RaidColor(TEXT("263B55"));StockOutline=RaidColor(TEXT("526B87"));
    SelectedOutline=RaidColor(TEXT("EBC371"));
}

void UMCRaidPerkRowWidget::BuildPresentationTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    auto* Surface=RaidSurface(WidgetTree,TEXT("RaidRowSurface"),TEXT("263B55"),TEXT("526B87"),true,FMargin(9,5));
    WidgetTree->RootWidget=Surface;
    auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("RaidRowContent"));
    Line->SetVisibility(ESlateVisibility::HitTestInvisible);
    Surface->SetContentForSlot(TEXT("Content"),Line);
    auto* IconFrame=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RaidRowIconFrame"));
    IconFrame->SetWidthOverride(27);IconFrame->SetHeightOverride(25);
    auto* ArtLayers=WidgetTree->ConstructWidget<UOverlay>();
    IconFrame->AddChild(ArtLayers);
    auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("RaidRowIcon"));
    auto* IconLayer=ArtLayers->AddChildToOverlay(Icon);
    IconLayer->SetHorizontalAlignment(HAlign_Fill);IconLayer->SetVerticalAlignment(VAlign_Fill);
    auto* Tooth=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("RaidRowTooth"));
    Tooth->bTooth=true;Tooth->SetVisibility(ESlateVisibility::Collapsed);
    auto* ToothLayer=ArtLayers->AddChildToOverlay(Tooth);
    ToothLayer->SetHorizontalAlignment(HAlign_Fill);ToothLayer->SetVerticalAlignment(VAlign_Fill);
    auto* ArtSlot=Line->AddChildToHorizontalBox(IconFrame);
    ArtSlot->SetPadding(FMargin(0,0,8,0));ArtSlot->SetVerticalAlignment(VAlign_Center);
    const auto* Style=GetResolvedTheme();
    auto* Name=RaidLabel(WidgetTree,TEXT("RaidRowName"),Style,17);
    auto* NameSlot=Line->AddChildToHorizontalBox(Name);
    NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameSlot->SetVerticalAlignment(VAlign_Center);
    auto* Detail=RaidLabel(WidgetTree,TEXT("RaidRowDetail"),Style,14);
    auto* DetailSlot=Line->AddChildToHorizontalBox(Detail);
    DetailSlot->SetPadding(FMargin(7,0,0,0));DetailSlot->SetVerticalAlignment(VAlign_Center);
}

void UMCRaidPerkRowWidget::Present(EMCRaidPerkRowStyle Style,const FText& Name,const FText& Detail,UTexture2D* Icon,
    const FText& Tooltip,bool bAction,bool bSelected,bool bReady)
{
    BuildPresentationTree();
    if(!WidgetTree) return;
    if(auto* Surface=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidRowSurface"))) {
        Surface->Kind=bAction?EMCUniversalUIKind::Button:EMCUniversalUIKind::Panel;
        Surface->CustomFill=Style==EMCRaidPerkRowStyle::Active?ActiveFill:Style==EMCRaidPerkRowStyle::Passive?PassiveFill:StockFill;
        Surface->CustomOutline=bSelected?SelectedOutline:Style==EMCRaidPerkRowStyle::Active?ActiveOutline:
            Style==EMCRaidPerkRowStyle::Passive?PassiveOutline:StockOutline;
        Surface->SetVisibility(bAction?ESlateVisibility::Visible:ESlateVisibility::SelfHitTestInvisible);
        Surface->SetToolTipText(Tooltip);
        Surface->OnClicked.AddUniqueDynamic(this,&ThisClass::Pressed);
        Surface->BuildPresentationTree();Surface->ApplyTheme();
    }
    const bool bShowTooth=Style==EMCRaidPerkRowStyle::Team && !Detail.IsEmpty();
    if(auto* Art=WidgetTree->FindWidget<UImage>(TEXT("RaidRowIcon"))) {
        Art->SetBrushFromTexture(Icon,false);
        Art->SetVisibility(Icon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(auto* Tooth=WidgetTree->FindWidget<UMCHUDIcon>(TEXT("RaidRowTooth"))) {
        Tooth->Tint=RaidColor(bReady?TEXT("9CE0B4"):TEXT("C5D4E4"));
        Tooth->SetVisibility(bShowTooth?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(auto* Frame=WidgetTree->FindWidget(TEXT("RaidRowIconFrame")))
        Frame->SetVisibility(Icon || bShowTooth?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    RaidSetText(WidgetTree,TEXT("RaidRowName"),Name);
    if(auto* Label=WidgetTree->FindWidget<UTextBlock>(TEXT("RaidRowDetail"))) {
        Label->SetText(Detail);
        Label->SetColorAndOpacity(RaidColor(bReady?TEXT("90E6AE"):TEXT("DFE9F3")));
        Label->SetVisibility(Detail.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
}

void UMCRaidPerkRowWidget::Pressed() {OnPressed.Broadcast();}

void UMCRaidPerkRowWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    if(!IsDesignTime()) return;
    const bool bStock=PreviewState==TEXT("Stock"),bAdd=PreviewState==TEXT("Add");
    const bool bDisabled=PreviewState==TEXT("Disabled");
    Present(bStock?EMCRaidPerkRowStyle::Stock:EMCRaidPerkRowStyle::Active,
        bAdd?LOCTEXT("SampleAdd","+ ADD"):LOCTEXT("SampleName","Rocket"),
        bStock?FText::AsCultureInvariant(TEXT("×3")):FText::GetEmpty(),nullptr,FText::GetEmpty(),!bDisabled,bAdd);
    if(auto* Surface=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidRowSurface"))) {
        Surface->PreviewScenario=bDisabled?EMCGameplayUIPreviewScenario::Disabled:EMCGameplayUIPreviewScenario::Default;
        Surface->ApplyTheme();
    }
}

UMCRaidLoadoutWidget::UMCRaidLoadoutWidget()
{
    bUseNativeLayout=false;
    bOverridePreviewUIVersion=true;
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    RowClass=TSoftClassPtr<UMCRaidPerkRowWidget>(FSoftObjectPath(
        TEXT("/Game/Gameplay/UI/V2/WBP_UIRaidPerkRow.WBP_UIRaidPerkRow_C")));
}

void UMCRaidLoadoutWidget::BuildPresentationTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("RaidRoot"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Root;
    const auto* Style=GetResolvedTheme();
    auto Rail=[&](bool bRight) {
        auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),bRight?TEXT("RaidRightFit"):TEXT("RaidLeftFit"));
        Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
        Fit->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* Cell=Root->AddChildToCanvas(Fit);
        Cell->SetAnchors(bRight?FAnchors(.778f,.068f,.984f,.96f):FAnchors(.016f,.068f,.201f,.96f));
        Cell->SetOffsets(FMargin(0));
        auto* Gutter=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),bRight?TEXT("RaidRightGutter"):TEXT("RaidLeftGutter"));
        Gutter->SetWidthOverride(bRight?330.f:315.f);
        Gutter->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* FitSlot=CastChecked<UScaleBoxSlot>(Fit->AddChild(Gutter));
        FitSlot->SetHorizontalAlignment(bRight?HAlign_Right:HAlign_Left);FitSlot->SetVerticalAlignment(VAlign_Top);
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),bRight?TEXT("RaidRightColumn"):TEXT("RaidLeftColumn"));
        Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Gutter->AddChild(Column);
        return Column;
    };
    auto MakePanel=[&](UVerticalBox* RailBody,FName PanelName,FName TitleName,FName DetailName,UWidget* Extra=nullptr) {
        auto* Panel=RaidSurface(WidgetTree,PanelName,TEXT("17283D"),TEXT("52647D"),false,FMargin(0));
        RailBody->AddChildToVerticalBox(Panel)->SetPadding(FMargin(0,0,0,10));
        auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>();
        Stack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Panel->SetContentForSlot(TEXT("Content"),Stack);
        auto* Cap=RaidSurface(WidgetTree,NAME_None,TEXT("F6F0E2"),TEXT("F6F0E2"),false,FMargin(10,7));
        Cap->CornerRadius=8;
        Stack->AddChildToVerticalBox(Cap)->SetPadding(FMargin(1,1,1,5));
        auto* Heading=WidgetTree->ConstructWidget<UHorizontalBox>();
        Heading->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Cap->SetContentForSlot(TEXT("Content"),Heading);
        auto* Title=RaidLabel(WidgetTree,TitleName,Style,18,true);
        auto* TitleSlot=Heading->AddChildToHorizontalBox(Title);
        TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TitleSlot->SetVerticalAlignment(VAlign_Center);
        if(!DetailName.IsNone()) {
            auto* Count=RaidLabel(WidgetTree,DetailName,Style,17,true);
            Heading->AddChildToHorizontalBox(Count)->SetVerticalAlignment(VAlign_Center);
        }
        if(Extra) Heading->AddChildToHorizontalBox(Extra)->SetVerticalAlignment(VAlign_Center);
        return Stack;
    };
    auto Rows=[&](UVerticalBox* Stack,FName Name) {
        auto* List=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),Name);
        List->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Stack->AddChildToVerticalBox(List)->SetPadding(FMargin(8,0,8,4));return List;
    };
    auto* Left=Rail(false);
    auto* Team=MakePanel(Left,TEXT("RaidTeamPanel"),TEXT("RaidTeamTitle"),NAME_None);
    Rows(Team,TEXT("RaidTeamRows"));
    auto* Status=RaidLabel(WidgetTree,TEXT("RaidStatus"),Style,14);
    Status->SetAutoWrapText(true);
    Team->AddChildToVerticalBox(Status)->SetPadding(FMargin(8,1,8,4));
    auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("RaidActions"));
    Actions->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Team->AddChildToVerticalBox(Actions)->SetPadding(FMargin(8,3,8,8));
    auto* Ready=RaidSurface(WidgetTree,TEXT("RaidReady"),TEXT("238754"),TEXT("CBECD5"),true);
    Actions->AddChildToHorizontalBox(Ready)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Back=RaidSurface(WidgetTree,TEXT("RaidBack"),TEXT("A34F6B"),TEXT("F9E9EE"),true);
    Back->SetLabel(NAME_None,FText::AsCultureInvariant(TEXT("Esc")));
    Actions->AddChildToHorizontalBox(Back)->SetPadding(FMargin(6,0,0,0));
    auto* StartButton=RaidSurface(WidgetTree,TEXT("RaidStart"),TEXT("238754"),TEXT("CBECD5"),true);
    Team->AddChildToVerticalBox(StartButton)->SetPadding(FMargin(8,0,8,8));
    auto* Close=RaidSurface(WidgetTree,TEXT("RaidStockClose"),TEXT("263B55"),TEXT("526B87"),true,FMargin(7,2));
    Close->SetLabel(NAME_None,FText::AsCultureInvariant(TEXT("×")));
    auto* Stock=MakePanel(Left,TEXT("RaidStockPanel"),TEXT("RaidStockTitle"),NAME_None,Close);
    auto* Context=RaidLabel(WidgetTree,TEXT("RaidStockContext"),Style,14);
    Stock->AddChildToVerticalBox(Context)->SetPadding(FMargin(9,1,9,5));
    auto* StockSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RaidStockBounds"));
    StockSize->SetMaxDesiredHeight(310);
    Stock->AddChildToVerticalBox(StockSize)->SetPadding(FMargin(8,0,8,6));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("RaidStockScroll"));
    Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
    StockSize->AddChild(Scroll);
    auto* StockRows=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("RaidStockRows"));
    StockRows->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Scroll->AddChild(StockRows);
    auto* Right=Rail(true);
    Rows(MakePanel(Right,TEXT("RaidActivePanel"),TEXT("RaidActiveTitle"),TEXT("RaidActiveCount")),TEXT("RaidActiveRows"));
    Rows(MakePanel(Right,TEXT("RaidPassivePanel"),TEXT("RaidPassiveTitle"),TEXT("RaidPassiveCount")),TEXT("RaidPassiveRows"));
}

void UMCRaidLoadoutWidget::BindControls()
{
    if(!WidgetTree) return;
    if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidReady"))) Button->OnClicked.AddUniqueDynamic(this,&ThisClass::ToggleReady);
    if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidBack"))) Button->OnClicked.AddUniqueDynamic(this,&ThisClass::Back);
    if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidStart"))) Button->OnClicked.AddUniqueDynamic(this,&ThisClass::Start);
    if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidStockClose"))) Button->OnClicked.AddUniqueDynamic(this,&ThisClass::DismissStock);
}

void UMCRaidLoadoutWidget::SetBackend(UMCHubWidget* Source)
{
    Backend=Source;
    // Resolve the authored row while the Hub widget is prepared, before scene ACK.
    PreparedRowClass=RowClass.LoadSynchronous();
    LastSignature.Reset();
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMCRaidLoadoutWidget::NativeConstruct()
{
    Super::NativeConstruct();
    BindControls();
    RefreshFromHub();
}

void UMCRaidLoadoutWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    FitGutters(Geometry.GetLocalSize());
}

void UMCRaidLoadoutWidget::FitGutters(FVector2D Size)
{
    if(!WidgetTree || !bFitMouthGutters || Size.X<=1 || Size.Y<=1 || LastViewportSize.Equals(Size,.5)) return;
    LastViewportSize=Size;
    const double Ratio=Size.Y/Size.X;
    if(auto* Fit=WidgetTree->FindWidget(TEXT("RaidLeftFit")))
        if(auto* Cell=Cast<UCanvasPanelSlot>(Fit->Slot)) {
            FAnchors Anchors=Cell->GetAnchors();
            Anchors.Maximum.X=FMath::Clamp(.5-.61*Ratio,Anchors.Minimum.X+.018,.201);
            Cell->SetAnchors(Anchors);
        }
    if(auto* Fit=WidgetTree->FindWidget(TEXT("RaidRightFit")))
        if(auto* Cell=Cast<UCanvasPanelSlot>(Fit->Slot)) {
            FAnchors Anchors=Cell->GetAnchors();
            Anchors.Minimum.X=FMath::Clamp(.5+.522*Ratio,.778,Anchors.Maximum.X-.018);
            Cell->SetAnchors(Anchors);
        }
}

void UMCRaidLoadoutWidget::OnLocalizationChanged()
{
    Super::OnLocalizationChanged();
    LastSignature.Reset();
    if(!IsDesignTime()) RefreshFromHub();
}

void UMCRaidLoadoutWidget::RefreshFromHub()
{
    if(IsDesignTime() || !Backend.IsValid()) return;
    const auto* Hub=Backend->GetUnifiedState();
    if(!Hub || Hub->Phase==EMCHubPhase::Gathering) {
        SetVisibility(ESlateVisibility::Collapsed);
        EditSlot=INDEX_NONE;EditingID=NAME_None;LastSignature.Reset();
        return;
    }
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    const auto* Player=GetOwningPlayerState<AMCPlayerState>();
    const auto* Economy=Player?Player->Economy.Get():nullptr;
    const bool bSelecting=Hub->Phase==EMCHubPhase::Selecting;
    const auto* Destination=Hub->FindDestination(Hub->SelectedDay);
    const bool bDestinationAvailable=Destination && Destination->bEnabled && Destination->HasValidMapPath();
    const bool bCanEdit=bSelecting && Economy && Hub->HasLoadedScene(Player);
    const bool bCanReady=bCanEdit && bDestinationAvailable;
    const bool bCanStart=Backend->CanLaunchUnifiedDestination();
    const bool bReady=Hub->IsPlayerReady(Player);
    FText Status=Hub->Notice;
    if(Status.IsEmpty()) {
        if(!bSelecting || !Hub->AreAllLoaded()) Status=Localized(TEXT("UI.Raid.Loading"),LOCTEXT("Loading","Loading…"));
        else if(!bDestinationAvailable) Status=Localized(TEXT("hub.selection.unavailable"),LOCTEXT("Unavailable","Not available yet"));
    }
    FString Signature=FString::Printf(TEXT("%p:%p:%d:%d:%d:%d:%d:%d:%s"),Hub,Economy,Hub->SceneGeneration,Hub->SelectedDay,
        bCanEdit,bCanReady,bCanStart,bReady,*Status.ToString());
    if(Economy) {
        for(FName ID:Economy->RaidLoadout.ActivePerks) Signature+=TEXT("|A:")+ID.ToString();
        for(FName ID:Economy->RaidLoadout.PassivePerks) Signature+=TEXT("|P:")+ID.ToString();
        for(const auto& Entry:Economy->RaidPerkStock) Signature+=FString::Printf(TEXT("|S:%s:%d"),*Entry.PerkID.ToString(),Entry.Copies);
    }
    for(const auto& Entry:Hub->Players)
        if(Entry.Player) Signature+=FString::Printf(TEXT("|T:%p:%d:%s"),Entry.Player.Get(),Entry.bReady,*Entry.Player->GetPlayerName());
    if(LastSignature==Signature) return;
    const bool bFirstPresentation=LastSignature.IsEmpty();
    LastSignature=MoveTemp(Signature);

    FView Previous=MoveTemp(View);
    View=FView();
    View.bCanEdit=bCanEdit;View.bCanReady=bCanReady;View.bCanStart=bCanStart;View.bReady=bReady;View.Status=Status;
    if(Economy) {
        View.Active=Economy->RaidLoadout.ActivePerks;
        View.Passive=Economy->RaidLoadout.PassivePerks;
        for(const auto& Entry:Economy->RaidPerkStock) {
            FMCPerkDefinition Definition;
            if(!Economy->GetRaidPerkDefinition(Entry.PerkID,Definition)) continue;
            auto& Item=View.Stock.AddDefaulted_GetRef();
            Item.ID=Entry.PerkID;Item.Copies=Entry.Copies;
            Item.Name=Localized(Definition.DisplayNameKey,Definition.DisplayName.IsEmpty()?FText::FromName(Entry.PerkID):Definition.DisplayName);
            Item.Description=Localized(Definition.DescriptionKey,Definition.Description);
            Item.Icon=Definition.Icon;
            Item.bPassive=Definition.Kind==EMCPerkKind::Passive;
        }
    }
    for(const auto& Entry:Hub->Players) {
        if(!Entry.Player) continue;
        View.Team.Add({FText::AsCultureInvariant(Entry.Player->GetPlayerName()),Entry.bReady});
    }
    const auto& Edited=bEditPassive?View.Passive:View.Active;
    const bool bNewScene=LastDay!=Hub->SelectedDay || LastSceneGeneration!=Hub->SceneGeneration;
    const bool bSlotChanged=EditSlot!=INDEX_NONE && (Edited.IsValidIndex(EditSlot)?Edited[EditSlot]!=EditingID:
        EditSlot!=Edited.Num() || !EditingID.IsNone());
    if(!bCanEdit || bSlotChanged || bNewScene) {
        EditSlot=INDEX_NONE;EditingID=NAME_None;
    }
    LastDay=Hub->SelectedDay;LastSceneGeneration=Hub->SceneGeneration;
    const bool bStockChanged=!Algo::Compare(Previous.Stock,View.Stock,[](const FPerkView& A,const FPerkView& B) {
        return A.ID==B.ID && A.Copies==B.Copies && A.bPassive==B.bPassive && A.Icon==B.Icon
            && A.Name.EqualTo(B.Name) && A.Description.EqualTo(B.Description);
    });
    if(bFirstPresentation || bNewScene || bSlotChanged || bStockChanged || Previous.Active!=View.Active
        || Previous.Passive!=View.Passive || Previous.bCanEdit!=View.bCanEdit) {
        PresentCategory(false);PresentCategory(true);PresentStock();
    }
    PresentTeam();
}

const UMCRaidLoadoutWidget::FPerkView* UMCRaidLoadoutWidget::FindPerk(FName ID) const
{
    return View.Stock.FindByPredicate([ID](const FPerkView& Item){return Item.ID==ID;});
}

bool UMCRaidLoadoutWidget::IsAvailable(const FPerkView& Item,bool bPassive) const
{
    return Item.Copies>0 && Item.bPassive==bPassive && !(bPassive?View.Passive:View.Active).Contains(Item.ID);
}

UMCRaidPerkRowWidget* UMCRaidLoadoutWidget::AddRow(UVerticalBox* Host,EMCRaidPerkRowStyle Style,
    const FText& Name,const FText& Detail,UTexture2D* Icon,const FText& Tooltip,bool bAction,bool bSelected,bool bReady)
{
    if(!Host || !WidgetTree) return nullptr;
    UClass* Class=PreparedRowClass;
    if(!Class && IsDesignTime()) Class=RowClass.LoadSynchronous();
    if(!Class) Class=UMCRaidPerkRowWidget::StaticClass();
    auto* Row=WidgetTree->ConstructWidget<UMCRaidPerkRowWidget>(Class);
    if(!Row) return nullptr;
#if WITH_EDITOR
    if(IsDesignTime()) Row->SetDesignerFlags(EWidgetDesignFlags::Designing);
#endif
    Row->Theme=GetResolvedTheme();Row->PreviewCulture=PreviewCulture;
    Row->Present(Style,Name,Detail,Icon,Tooltip,bAction,bSelected,bReady);
    Host->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,4));
    return Row;
}

void UMCRaidLoadoutWidget::PresentCategory(bool bPassive)
{
    auto* Host=WidgetTree->FindWidget<UVerticalBox>(bPassive?TEXT("RaidPassiveRows"):TEXT("RaidActiveRows"));
    if(!Host) return;
    const auto& Equipped=bPassive?View.Passive:View.Active;
    const auto Style=bPassive?EMCRaidPerkRowStyle::Passive:EMCRaidPerkRowStyle::Active;
    RaidSetText(WidgetTree,bPassive?TEXT("RaidPassiveTitle"):TEXT("RaidActiveTitle"),
        bPassive?Localized(TEXT("UI.Raid.Passive"),LOCTEXT("Passive","PASSIVE PERKS")):Localized(TEXT("UI.Raid.Active"),LOCTEXT("Active","ACTIVE PERKS")));
    RaidSetText(WidgetTree,bPassive?TEXT("RaidPassiveCount"):TEXT("RaidActiveCount"),
        FText::Format(LOCTEXT("SlotCount","{0}/{1}"),Equipped.Num(),RaidSlotsPerCategory));
    Host->ClearChildren();
    for(int32 Index=0;Index<Equipped.Num();++Index) {
        const FName ID=Equipped[Index];
        const auto* Item=FindPerk(ID);
        auto* Row=AddRow(Host,Style,Item?Item->Name:FText::FromName(ID),FText::GetEmpty(),
            Item?Item->Icon.Get():nullptr,Item?Item->Description:FText::GetEmpty(),View.bCanEdit,bEditPassive==bPassive && EditSlot==Index);
        if(Row) Row->OnPressed.AddWeakLambda(this,[this,bPassive,Index,ID]{Edit(bPassive,Index,ID);});
    }
    if(Equipped.Num()<RaidSlotsPerCategory && View.Stock.ContainsByPredicate([this,bPassive](const FPerkView& Item){return IsAvailable(Item,bPassive);})) {
        const int32 Index=Equipped.Num();
        const FText Copy=FText::Format(LOCTEXT("Plus","+ {0}"),Localized(TEXT("UI.Raid.Add"),LOCTEXT("Add","ADD")));
        auto* Row=AddRow(Host,Style,Copy,FText::GetEmpty(),nullptr,FText::GetEmpty(),View.bCanEdit,bEditPassive==bPassive && EditSlot==Index);
        if(Row) Row->OnPressed.AddWeakLambda(this,[this,bPassive,Index]{Edit(bPassive,Index,NAME_None);});
    } else if(Equipped.IsEmpty()) {
        AddRow(Host,EMCRaidPerkRowStyle::Team,Localized(TEXT("UI.Raid.NoStock"),LOCTEXT("Empty","No perks in stock")));
    }
}

void UMCRaidLoadoutWidget::PresentStock()
{
    auto* Panel=WidgetTree->FindWidget(TEXT("RaidStockPanel"));
    auto* Rows=WidgetTree->FindWidget<UVerticalBox>(TEXT("RaidStockRows"));
    if(!Panel || !Rows) return;
    Panel->SetVisibility(EditSlot==INDEX_NONE?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    Rows->ClearChildren();
    if(EditSlot==INDEX_NONE) return;
    RaidSetText(WidgetTree,TEXT("RaidStockTitle"),Localized(TEXT("UI.Raid.Stock"),LOCTEXT("Stock","YOUR STOCK")));
    RaidSetText(WidgetTree,TEXT("RaidStockContext"),bEditPassive?Localized(TEXT("UI.Raid.Passive"),LOCTEXT("Passive","PASSIVE PERKS")):
        Localized(TEXT("UI.Raid.Active"),LOCTEXT("Active","ACTIVE PERKS")));
    if(auto* Scroll=WidgetTree->FindWidget<UScrollBox>(TEXT("RaidStockScroll"))) Scroll->ScrollToStart();
    for(const auto& Item:View.Stock) {
        if(!IsAvailable(Item,bEditPassive)) continue;
        auto* Row=AddRow(Rows,EMCRaidPerkRowStyle::Stock,Item.Name,FText::Format(LOCTEXT("Copies","×{0}"),Item.Copies),
            Item.Icon.Get(),Item.Description,View.bCanEdit);
        if(Row) Row->OnPressed.AddWeakLambda(this,[this,ID=Item.ID]{RequestPerk(ID);});
    }
    if(!EditingID.IsNone()) {
        auto* Row=AddRow(Rows,EMCRaidPerkRowStyle::Stock,Localized(TEXT("UI.Raid.Remove"),LOCTEXT("Remove","REMOVE")),
            FText::GetEmpty(),nullptr,FText::GetEmpty(),View.bCanEdit);
        if(Row) Row->OnPressed.AddWeakLambda(this,[this]{RequestPerk(NAME_None);});
    }
}

void UMCRaidLoadoutWidget::PresentTeam()
{
    auto* Rows=WidgetTree->FindWidget<UVerticalBox>(TEXT("RaidTeamRows"));
    if(!Rows) return;
    const int32 Ready=View.Team.FilterByPredicate([](const FMemberView& Member){return Member.bReady;}).Num();
    const FFormatNamedArguments Count={{TEXT("Ready"),Ready},{TEXT("Total"),View.Team.Num()}};
    RaidSetText(WidgetTree,TEXT("RaidTeamTitle"),FText::Format(Localized(TEXT("UI.Raid.Team"),LOCTEXT("Team","TEAM {Ready}/{Total}")),Count));
    Rows->ClearChildren();
    for(const auto& Member:View.Team)
        AddRow(Rows,EMCRaidPerkRowStyle::Team,Member.Name,
            Member.bReady?Localized(TEXT("UI.Raid.Ready"),LOCTEXT("Ready","READY")):
                Localized(TEXT("UI.Raid.Choosing"),LOCTEXT("Choosing","CHOOSING")),nullptr,FText::GetEmpty(),false,false,Member.bReady);
    if(auto* Status=WidgetTree->FindWidget<UTextBlock>(TEXT("RaidStatus"))) {
        Status->SetText(View.Status);
        Status->SetVisibility(View.Status.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    auto* ReadyButton=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidReady"));
    RaidSetButton(ReadyButton,View.bReady?Localized(TEXT("UI.Raid.Unready"),LOCTEXT("Unready","UNREADY")):
        Localized(TEXT("UI.Raid.Ready"),LOCTEXT("Ready","READY")),View.bCanReady);
    if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidStart"))) {
        Button->SetVisibility(View.bCanStart?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        RaidSetButton(Button,Localized(TEXT("UI.Raid.Start"),LOCTEXT("Start","START")),View.bCanStart);
    }
    if(auto* BackButton=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RaidBack"))) BackButton->SetIsEnabled(View.bCanEdit);
}

void UMCRaidLoadoutWidget::Present()
{
    BuildPresentationTree();
    if(!WidgetTree) return;
    PresentCategory(false);PresentCategory(true);PresentStock();PresentTeam();
}

void UMCRaidLoadoutWidget::Edit(bool bPassive,int32 SlotIndex,FName ID)
{
    if(!View.bCanEdit) return;
    if(EditSlot==SlotIndex && bEditPassive==bPassive) {CloseStock();return;}
    EditSlot=SlotIndex;bEditPassive=bPassive;EditingID=ID;
    PresentCategory(false);PresentCategory(true);PresentStock();
}

bool UMCRaidLoadoutWidget::CloseStock()
{
    if(EditSlot==INDEX_NONE) return false;
    EditSlot=INDEX_NONE;EditingID=NAME_None;
    PresentCategory(false);PresentCategory(true);PresentStock();
    if(auto* Source=Backend.Get()) Source->SetUserFocus(GetOwningPlayer());
    return true;
}

void UMCRaidLoadoutWidget::RequestPerk(FName ID)
{
    if(EditSlot==INDEX_NONE || !View.bCanEdit) return;
    const int32 SlotIndex=EditSlot;const bool bPassive=bEditPassive;
    CloseStock();
    if(auto* Controller=GetOwningPlayer<AMCPlayerController>()) Controller->RequestHubRaidPerk(ID,SlotIndex,bPassive);
}

void UMCRaidLoadoutWidget::ToggleReady()
{
    if(View.bCanReady) if(auto* Controller=GetOwningPlayer<AMCPlayerController>()) Controller->RequestHubRaidReady(!View.bReady);
}
void UMCRaidLoadoutWidget::Start() {if(auto* Source=Backend.Get()) Source->HandleAction(EMCHubMenuAction::Launch);}
void UMCRaidLoadoutWidget::Back() {if(auto* Source=Backend.Get()) Source->RequestUnifiedBack();}
void UMCRaidLoadoutWidget::DismissStock() {CloseStock();}
void UMCRaidLoadoutWidget::Confirm() {if(View.bCanStart) Start();else ToggleReady();}

void UMCRaidLoadoutWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    if(!IsDesignTime()) return;
    View=FView();
    const bool bEmpty=PreviewState==TEXT("RaidEmpty");
    const bool bFull=PreviewState==TEXT("RaidFull");
    const bool bGuest=PreviewState==TEXT("RaidGuest");
    const bool bAllReady=PreviewState==TEXT("RaidAllReady") || bGuest;
    View.bCanEdit=true;View.bCanReady=true;
    View.bReady=bAllReady || PreviewState==TEXT("RaidReady");
    View.bCanStart=bAllReady && !bGuest;
    View.Team={{LOCTEXT("Molar","Molar"),!bEmpty},{LOCTEXT("Mint","Mint"),!bEmpty},
        {LOCTEXT("Crown","Crown"),bAllReady},{LOCTEXT("You","You"),View.bReady}};
    if(!bEmpty) {
        const FText Active[]={LOCTEXT("Rocket","Rocket"),LOCTEXT("Sponge","Sponge"),LOCTEXT("Hook","Hook"),
            LOCTEXT("Bubble","Bubble"),LOCTEXT("Coffee","Coffee")};
        const FText Passive[]={LOCTEXT("LightFoot","Light Foot"),LOCTEXT("HeavyHit","Heavy Hit"),LOCTEXT("DeepBreath","Deep Breath"),
            LOCTEXT("QuickStep","Quick Step"),LOCTEXT("HardShell","Hard Shell")};
        const FName ActiveExamples[]={TEXT("Consumable_Rocket"),TEXT("Active_Sponge"),TEXT("Active_Hook"),
            TEXT("Consumable_Air"),TEXT("Consumable_Grenade")};
        const FName PassiveExamples[]={TEXT("B1_LightFoot"),TEXT("B1_HeavyHit"),TEXT("B1_DeepBreath"),
            TEXT("B1_ConfidentThrow"),TEXT("B1_BigPortion")};
        const auto* PerkTable=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
        for(bool bPassive:{false,true}) for(int32 Index=0;Index<UE_ARRAY_COUNT(Active);++Index) {
            const FName ID(*FString::Printf(TEXT("RaidPreview_%c%d"),bPassive?TEXT('P'):TEXT('A'),Index));
            auto& Item=View.Stock.AddDefaulted_GetRef();
            Item.ID=ID;Item.Name=bPassive?Passive[Index]:Active[Index];Item.Copies=1+Index%3;Item.bPassive=bPassive;
            if(const auto* Definition=PerkTable?PerkTable->FindRow<FMCPerkDefinition>(
                bPassive?PassiveExamples[Index]:ActiveExamples[Index],TEXT("Raid preview"),false):nullptr) {
                Item.Icon=Definition->Icon;
                Item.Icon.LoadSynchronous();
            }
            if(Index<(bFull?4:2)) (bPassive?View.Passive:View.Active).Add(ID);
        }
    }
    EditSlot=!bEmpty && (PreviewState==TEXT("Default") || PreviewState==TEXT("RaidStock"))?View.Passive.Num():INDEX_NONE;
    bEditPassive=true;EditingID=NAME_None;
    Present();
}

#undef LOCTEXT_NAMESPACE

