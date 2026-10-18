#include "MCTutorialWidget.h"
#include "MCGamepadHints.h"
#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "MCTutorialDirector.h"
#include "MCTutorialTypes.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace MCTutorialWidgetPrivate
{
    constexpr float CardWidth=920.f, LessonHeight=180.f, ExpandedHeight=260.f, MaximumHeight=400.f, BottomInset=200.f;
    UTextBlock* Label(UWidgetTree* Tree,int32 Size,FLinearColor Color=FLinearColor::White,FName Name=NAME_None)
    {
        auto* Text=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        if(const auto* Owner=Tree->GetTypedOuter<UMCGameplayUIWidget>()) Text->SetFont(Owner->GetResolvedTheme()->MakeGameplayFont(Size,false));
        Text->SetColorAndOpacity(Color);Text->SetAutoWrapText(true);return Text;
    }
    UButton* Button(UWidgetTree* Tree,UTextBlock*& OutLabel,const TCHAR* Text,FName Name,FName LabelName)
    {
        auto* Result=Tree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
        OutLabel=Label(Tree,16,FLinearColor::White,LabelName);OutLabel->SetText(FText::AsCultureInvariant(Text));OutLabel->SetJustification(ETextJustify::Center);
        Result->SetContent(OutLabel);return Result;
    }
}

#define LOCTEXT_NAMESPACE "MCTutorialUI"
UMCTutorialWidget::UMCTutorialWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // A reflected hard reference on the native CDO also makes the portrait discoverable by cooking.
    // Quiet optional loading retains the existing fallback while the new asset is being authored.
    static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Portrait(
        TEXT("/Game/Art/UI/T_ToothFairyPortrait_v1.T_ToothFairyPortrait_v1"),LOAD_Quiet|LOAD_NoWarn);
    FairyPortrait=Portrait.Get();
}

void UMCTutorialWidget::NativeOnInitialized() {Super::NativeOnInitialized();SetIsFocusable(true);}

void UMCTutorialWidget::BindPresentationTree()
{
    CardSize=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("TutorialCardSize")));
    Card=Cast<UBorder>(WidgetTree->FindWidget(TEXT("TutorialCard")));
    PortraitImage=Cast<UImage>(WidgetTree->FindWidget(TEXT("TutorialPortrait")));
    PortraitFallback=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("TutorialPortraitFallback")));
    PortraitBorder=Cast<UBorder>(WidgetTree->FindWidget(TEXT("TutorialPortraitBorder")));
    PortraitStar=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialPortraitStar")));
    PortraitName=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialPortraitName")));
    StageTitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialStageTitle")));
    Dialogue=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialDialogue")));
    Action=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialAction")));
    OwnProgress=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialOwnProgress")));
    TeamProgress=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialTeamProgress")));
    Timer=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialTimer")));
    Progress=Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("TutorialProgress")));
    ReadyButton=Cast<UButton>(WidgetTree->FindWidget(TEXT("TutorialReadyButton")));
    ReadyLabel=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialReadyLabel")));
    MenuButton=Cast<UButton>(WidgetTree->FindWidget(TEXT("TutorialMenuButton")));
    MenuLabel=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TutorialMenuLabel")));
    CardSlot=nullptr;
    if(auto* LegacyScale=Cast<UScaleBox>(WidgetTree->FindWidget(TEXT("TutorialScale")))) {LegacyScale->SetStretch(EStretch::None);CardSlot=Cast<UCanvasPanelSlot>(LegacyScale->Slot);}
    else if(CardSize) CardSlot=Cast<UCanvasPanelSlot>(CardSize->Slot);
    if(CardSize) CardSize->SetMaxDesiredHeight(MCTutorialWidgetPrivate::MaximumHeight);
    if(CardSlot) CardSlot->SetAutoSize(true);
    if(ReadyButton) ReadyButton->OnClicked.AddUniqueDynamic(this,&UMCTutorialWidget::ReadyClicked);
    if(MenuButton) MenuButton->OnClicked.AddUniqueDynamic(this,&UMCTutorialWidget::MenuClicked);
    NormalizePresentationLayout();
}

void UMCTutorialWidget::NormalizePresentationLayout()
{
    if(!WidgetTree || !ReadyButton) return;
    auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("TutorialBodyScroll")));
    auto* Actions=Cast<UHorizontalBox>(ReadyButton->GetParent());
    if(!Scroll || !Actions) return;
    if(!WidgetTree->FindWidget(TEXT("TutorialContentColumn"))) if(auto* Row=Cast<UHorizontalBox>(Scroll->GetParent())) {
        // Pin completion actions below the scrolling instructions. This also
        // upgrades the existing authored tree without replacing its content.
        Scroll->RemoveFromParent();Actions->RemoveFromParent();
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TutorialContentColumn"));
        Row->AddChildToHorizontalBox(Column)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Column->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,10,0,0));
    }
    Scroll->SetAllowOverscroll(false);Scroll->SetScrollbarThickness(FVector2D(5,5));
    for(UButton* Button:{ReadyButton.Get(),MenuButton.Get()}) if(Button) if(auto* ContentSlot=Cast<UButtonSlot>(Button->GetContentSlot())) {
        ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Center);ContentSlot->SetPadding(FMargin(0));
    }
}

void UMCTutorialWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {BindPresentationTree();return;}
    const auto& P=GetResolvedTheme()->Palette;
    SetIsFocusable(true);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("TutorialRoot"));
    WidgetTree->RootWidget=Root;
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    CardSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TutorialCardSize"));
    CardSlot=Root->AddChildToCanvas(CardSize);
    CardSlot->SetAnchors(FAnchors(.5f,1.f));
    CardSlot->SetAlignment(FVector2D(.5f,1.f));
    DesiredCardHeight=MCTutorialWidgetPrivate::LessonHeight;
    CardSlot->SetPosition(FVector2D(0,-MCTutorialWidgetPrivate::BottomInset));
    CardSlot->SetSize(FVector2D(MCTutorialWidgetPrivate::CardWidth,DesiredCardHeight));
    CardSlot->SetAutoSize(true);
    CardSize->SetWidthOverride(MCTutorialWidgetPrivate::CardWidth);
    CardSize->SetMinDesiredHeight(DesiredCardHeight);
    CardSize->SetMaxDesiredHeight(MCTutorialWidgetPrivate::MaximumHeight);
    Card=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TutorialCard"));
    Card->SetBrush(FSlateRoundedBoxBrush(P.Surface,GetResolvedTheme()->CornerRadius,P.Border,GetResolvedTheme()->OutlineWidth));
    Card->SetPadding(FMargin(14,10));
    Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Card->SetClipping(EWidgetClipping::Inherit);
    CardSize->SetContent(Card);
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
    Card->SetContent(Row);

    auto* PortraitSize=WidgetTree->ConstructWidget<USizeBox>();
    PortraitSize->SetWidthOverride(74); PortraitSize->SetHeightOverride(74);
    PortraitBorder=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TutorialPortraitBorder"));
    PortraitBorder->SetBrush(FSlateRoundedBoxBrush(P.SurfaceAlt,GetResolvedTheme()->CornerRadius));
    PortraitBorder->SetPadding(FMargin(6)); PortraitSize->SetContent(PortraitBorder);
    auto* PortraitOverlay=WidgetTree->ConstructWidget<UOverlay>(); PortraitBorder->SetContent(PortraitOverlay);
    PortraitImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("TutorialPortrait"));
    auto* PortraitScale=WidgetTree->ConstructWidget<UScaleBox>();PortraitScale->SetStretch(EStretch::ScaleToFit);PortraitScale->SetStretchDirection(EStretchDirection::DownOnly);PortraitScale->SetContent(PortraitImage);
    auto* ImageSlot=PortraitOverlay->AddChildToOverlay(PortraitScale);
    ImageSlot->SetHorizontalAlignment(HAlign_Fill);
    ImageSlot->SetVerticalAlignment(VAlign_Fill);
    PortraitFallback=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TutorialPortraitFallback"));
    auto* FallbackSlot=PortraitOverlay->AddChildToOverlay(PortraitFallback);
    FallbackSlot->SetHorizontalAlignment(HAlign_Center); FallbackSlot->SetVerticalAlignment(VAlign_Center);
    auto* Star=MCTutorialWidgetPrivate::Label(WidgetTree,30,P.Mint,TEXT("TutorialPortraitStar"));PortraitStar=Star;
    Star->SetText(FText::FromString(TEXT("✦"))); Star->SetJustification(ETextJustify::Center);
    PortraitFallback->AddChildToVerticalBox(Star);
    auto* Fairy=MCTutorialWidgetPrivate::Label(WidgetTree,11,P.Ink,TEXT("TutorialPortraitName"));PortraitName=Fairy;
    Fairy->SetText(FText::FromString(TEXT("ФЕЯ"))); Fairy->SetJustification(ETextJustify::Center);
    PortraitFallback->AddChildToVerticalBox(Fairy);
    auto* PortraitSlot=Row->AddChildToHorizontalBox(PortraitSize);
    PortraitSlot->SetVerticalAlignment(VAlign_Center); PortraitSlot->SetPadding(FMargin(0,0,14,0));

    auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();
    Body->SetClipping(EWidgetClipping::Inherit);
    auto* BodyScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("TutorialBodyScroll"));
    BodyScroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);BodyScroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);BodyScroll->SetScrollbarPadding(FMargin(6,0,0,0));BodyScroll->AddChild(Body);
    Row->AddChildToHorizontalBox(BodyScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    StageTitle=MCTutorialWidgetPrivate::Label(WidgetTree,16,P.Ink,TEXT("TutorialStageTitle"));
    auto* Header=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TutorialHeader"));Header->SetPadding(FMargin(10,6));Header->SetContent(StageTitle);
    Body->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,8));
    Dialogue=MCTutorialWidgetPrivate::Label(WidgetTree,14,P.Ink,TEXT("TutorialDialogue"));
    Body->AddChildToVerticalBox(Dialogue)->SetPadding(FMargin(0,0,0,6));
    Action=MCTutorialWidgetPrivate::Label(WidgetTree,15,P.Ink,TEXT("TutorialAction"));
    Body->AddChildToVerticalBox(Action)->SetPadding(FMargin(0,0,0,6));

    auto* StatusRow=WidgetTree->ConstructWidget<UHorizontalBox>();
    Body->AddChildToVerticalBox(StatusRow)->SetPadding(FMargin(0,0,0,4));
    OwnProgress=MCTutorialWidgetPrivate::Label(WidgetTree,12,P.Muted,TEXT("TutorialOwnProgress"));
    OwnProgress->SetAutoWrapText(true);
    OwnProgress->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    OwnProgress->SetClipping(EWidgetClipping::ClipToBounds);
    StatusRow->AddChildToHorizontalBox(OwnProgress)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    TeamProgress=MCTutorialWidgetPrivate::Label(WidgetTree,12,P.Marine,TEXT("TutorialTeamProgress"));
    TeamProgress->SetAutoWrapText(true);
    auto* TeamSlot=StatusRow->AddChildToHorizontalBox(TeamProgress);
    TeamSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    TeamSlot->SetPadding(FMargin(20,0,0,0));
    Timer=MCTutorialWidgetPrivate::Label(WidgetTree,12,P.Muted,TEXT("TutorialTimer"));
    Timer->SetAutoWrapText(false);
    auto* TimerSlot=StatusRow->AddChildToHorizontalBox(Timer);
    TimerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    TimerSlot->SetPadding(FMargin(20,0,0,0));
    Progress=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("TutorialProgress"));
    Progress->SetFillColorAndOpacity(P.Mint);
    auto* BarSize=WidgetTree->ConstructWidget<USizeBox>(); BarSize->SetHeightOverride(6); BarSize->SetContent(Progress);
    Body->AddChildToVerticalBox(BarSize)->SetPadding(FMargin(0,0,0,4));

    auto* Buttons=WidgetTree->ConstructWidget<UHorizontalBox>();
    Body->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0,10,0,0));
    UTextBlock* ReadyText=nullptr;
    ReadyButton=MCTutorialWidgetPrivate::Button(WidgetTree,ReadyText,TEXT("Готов"),TEXT("TutorialReadyButton"),TEXT("TutorialReadyLabel")); ReadyLabel=ReadyText;
    ReadyButton->OnClicked.AddUniqueDynamic(this,&UMCTutorialWidget::ReadyClicked);
    auto* ReadySlot=Buttons->AddChildToHorizontalBox(ReadyButton);ReadySlot->SetPadding(FMargin(0,0,12,0));ReadySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    UTextBlock* MenuText=nullptr;
    MenuButton=MCTutorialWidgetPrivate::Button(WidgetTree,MenuText,TEXT("В главное меню"),TEXT("TutorialMenuButton"),TEXT("TutorialMenuLabel"));MenuLabel=MenuText;
    MenuButton->OnClicked.AddUniqueDynamic(this,&UMCTutorialWidget::MenuClicked);
    Buttons->AddChildToHorizontalBox(MenuButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    NormalizePresentationLayout();UpdatePortrait();ApplyTheme();
}

void UMCTutorialWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(!IsDesignTime() && !bEditorFixture) RefreshState();
}

bool UMCTutorialWidget::IsTutorialVisible() const
{
    if(IsDesignTime() || bEditorFixture) return PreviewState!=TEXT("Empty") && PreviewScenario!=EMCGameplayUIPreviewScenario::Empty;
    const auto* Director=AMCTutorialDirector::Find(GetWorld());
    return Director && Director->Stage!=EMCTutorialStage::Finished;
}

bool UMCTutorialWidget::RequiresMenuInput() const
{
    if(IsDesignTime() || bEditorFixture) return bFixtureComplete && !bFixtureShort;
    const auto* Director=AMCTutorialDirector::Find(GetWorld());
    return Director && Director->IsComplete() && !Director->UsesShortFlow();
}

UWidget* UMCTutorialWidget::GetReadyFocusTarget() const
{
    return ReadyButton && ReadyButton->GetIsEnabled()?static_cast<UWidget*>(ReadyButton.Get()):static_cast<UWidget*>(MenuButton.Get());
}

void UMCTutorialWidget::UpdatePortrait()
{
    if (!PortraitImage || !PortraitFallback) return;
    if (DisplayedPortrait.Get()!=FairyPortrait.Get())
    {
        PortraitImage->SetBrushFromTexture(FairyPortrait,true);
        DisplayedPortrait=FairyPortrait;
    }
    PortraitImage->SetVisibility(FairyPortrait?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    PortraitFallback->SetVisibility(FairyPortrait?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}

FString UMCTutorialWidget::DisplayCulture() const {return IsDesignTime() || bEditorFixture?PreviewCulture:FString();}
FText UMCTutorialWidget::Copy(FName Key,const FText& Fallback) const
{
    const auto* Style=GetResolvedTheme();UDataTable* Table=Style?Style->LocalizationTable.LoadSynchronous():nullptr;
    if(!Table) Table=UMCUIDataLibrary::GetDefaultLocalizationTable();return UMCUIDataLibrary::ResolveText(Table,Key,Fallback,DisplayCulture());
}
FText UMCTutorialWidget::DirectorCopy(const FText& Text) const
{
    const auto Namespace=FTextInspector::GetNamespace(Text);const auto Key=FTextInspector::GetKey(Text);
    return Namespace.IsSet() && Namespace.GetValue()==TEXT("MCTutorial") && Key.IsSet()
        ? Copy(FName(*(TEXT("UI.Tutorial.Copy.")+Key.GetValue())),Text):Text;
}
void UMCTutorialWidget::RefreshState()
{
    if(IsDesignTime() || bEditorFixture) return;
    if(!Card) BuildPresentationTree();if(!Card) return;
    if(CardSlot) CardSlot->SetAutoSize(true);
    const auto* Director=AMCTutorialDirector::Find(GetWorld());const bool Visible=Director && Director->Stage!=EMCTutorialStage::Finished;
    Card->SetVisibility(Visible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);if(!Visible) return;
    UpdatePortrait();
    const bool Complete=Director->IsComplete();const bool Expanded=Complete || Director->Stage==EMCTutorialStage::Intro || Director->Stage==EMCTutorialStage::Loading;
    DesiredCardHeight=Expanded?MCTutorialWidgetPrivate::ExpandedHeight:MCTutorialWidgetPrivate::LessonHeight;
    if(CardSize) {CardSize->ClearHeightOverride();CardSize->SetMinDesiredHeight(DesiredCardHeight);}
    auto Format=[&](FName Key,const FText& Fallback,const FFormatNamedArguments& Args){return UMCUIDataLibrary::FormatTextForCulture(Copy(Key,Fallback),Args,DisplayCulture());};
    if(StageTitle) StageTitle->SetText(Format(TEXT("UI.Tutorial.StageHeading"),LOCTEXT("StageHeading","Обучение · Зубная фея · {StageTitle}"),{{TEXT("StageTitle"),DirectorCopy(Director->Title)}}));
    if(Dialogue) {Dialogue->SetText(DirectorCopy(Director->FairyLine));Dialogue->SetToolTipText(DirectorCopy(Director->FairyLine));}
    const auto* PC=GetOwningPlayer<AMCPlayerController>();const FText Instruction=DirectorCopy(Director->Instruction);
    if(Action) Action->SetText(PC && PC->IsUsingGamepad()?FText::AsCultureInvariant(MCGamepadHints::Adapt(Instruction.ToString())):Instruction);
    const bool FullComplete=Complete && !Director->UsesShortFlow();
    if(ReadyButton) ReadyButton->SetVisibility(FullComplete?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(MenuButton) MenuButton->SetVisibility(FullComplete?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(ReadyButton) if(auto* Actions=Cast<UHorizontalBox>(ReadyButton->GetParent())) Actions->SetVisibility(FullComplete?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    const auto* Personal=Director->GetPlayerProgress(PC?PC->GetPlayerState<APlayerState>():nullptr);
    const int32 Required=Personal?Personal->StageRequired:0,Done=Personal?FMath::Clamp(Personal->StageProgress,0,Required):0;const bool Ready=Personal && Personal->bReady;
    FText PersonalText;
    if(Complete) PersonalText=Director->UsesShortFlow()?Copy(TEXT("UI.Tutorial.ShortComplete"),LOCTEXT("ShortComplete","Обучение пройдено. Получаем первый личный перк")):
        Ready?Copy(TEXT("UI.Tutorial.CompleteReady"),LOCTEXT("CompleteReady","Ты готов. Ждём команду")):Copy(TEXT("UI.Tutorial.CompleteNotReady"),LOCTEXT("CompleteNotReady","Обучение пройдено. Подтверди готовность"));
    else if(Required>0) PersonalText=Format(Done>=Required?TEXT("UI.Tutorial.PersonalCompleteWaiting"):TEXT("UI.Tutorial.PersonalProgress"),
        Done>=Required?LOCTEXT("PersonalWaiting","Твой прогресс: {Current}/{Required} · Ждём команду"):LOCTEXT("PersonalProgress","Твой прогресс: {Current}/{Required}"),{{TEXT("Current"),Done},{TEXT("Required"),Required}});
    else if(Director->Stage==EMCTutorialStage::Loading) PersonalText=Personal && Personal->bLoaded?Copy(TEXT("UI.Tutorial.LoadedWaiting"),LOCTEXT("LoadedWaiting","Ты на арене. Ждём загрузку команды")):Copy(TEXT("UI.Tutorial.Loading"),LOCTEXT("Loading","Загружаем арену…"));
    else PersonalText=FMCTutorialProgressRules::IsSharedStage(Director->Stage)?Copy(TEXT("UI.Tutorial.SharedTask"),LOCTEXT("SharedTask","Одна общая задача для всей команды")):Copy(TEXT("UI.Tutorial.WorkTogether"),LOCTEXT("WorkTogether","Работайте вместе"));
    if(OwnProgress) OwnProgress->SetText(PersonalText);
    FText TeamText;
    if(Complete || Director->Stage==EMCTutorialStage::Loading) {
        int32 TeamDone=0;for(const auto& Player:Director->Players) TeamDone+=Complete?Player.bReady:Player.bLoaded;
        TeamText=Format(Complete?TEXT("UI.Tutorial.TeamReady"):TEXT("UI.Tutorial.TeamLoaded"),
            Complete?LOCTEXT("TeamReady","Готовы: {Current}/{Total}"):LOCTEXT("TeamLoaded","На арене: {Current}/{Total}"),{{TEXT("Current"),TeamDone},{TEXT("Total"),Director->GetRequiredPlayers()}});
    } else if(Required>0) TeamText=Format(TEXT("UI.Tutorial.TeamComplete"),LOCTEXT("TeamComplete","Команда: {Current}/{Total}"),{{TEXT("Current"),Director->GetCompletedPlayers()},{TEXT("Total"),Director->GetRequiredPlayers()}});
    else if(Director->TeamTasksTotal>0) TeamText=Format(TEXT("UI.Tutorial.SharedProgress"),LOCTEXT("SharedProgress","Общая задача: {Current}/{Total}"),{{TEXT("Current"),FMath::Max(0,Director->TeamTasksTotal-Director->TeamTasksLeft)},{TEXT("Total"),Director->TeamTasksTotal}});
    else TeamText=Format(TEXT("UI.Tutorial.ParticipantCount"),LOCTEXT("Participants","Участников: {Count}"),{{TEXT("Count"),Director->GetRequiredPlayers()}});
    if(TeamProgress) TeamProgress->SetText(TeamText);
    const float SharedProgress=Director->TeamTasksTotal>0?FMath::Clamp(1.f-float(Director->TeamTasksLeft)/Director->TeamTasksTotal,0.f,1.f):0;
    if(Progress) {Progress->SetPercent(Complete?1:Required>0?float(Done)/Required:SharedProgress);Progress->SetVisibility(Required>0 || Complete || Director->TeamTasksTotal>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
    if(ReadyButton) ReadyButton->SetIsEnabled(FullComplete && Personal && !Ready);
    if(ReadyLabel) ReadyLabel->SetText(Ready?Copy(TEXT("UI.Tutorial.ReadyConfirmed"),LOCTEXT("ReadyConfirmed","Ты готов ✓")):Copy(TEXT("UI.Tutorial.ReadyAction"),LOCTEXT("ReadyAction","Готов")));
    // Ordinary work lessons have no UI timer. The server's existing stage deadlines and transitions are unchanged.
    const bool ShowTimer=Director->Stage==EMCTutorialStage::Intro && Director->StageEndsAt>0;
    if(Timer) {
        Timer->SetVisibility(ShowTimer?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(ShowTimer) {const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;const int32 Remaining=GS?FMath::CeilToInt(FMath::Max(0.,Director->StageEndsAt-GS->GetServerWorldTimeSeconds())):0;
            Timer->SetText(Format(TEXT("UI.Tutorial.StageSeconds"),LOCTEXT("StageSeconds","{Seconds} с"),{{TEXT("Seconds"),Remaining}}));}
    }
    ApplyTheme();
}

void UMCTutorialWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    const FVector2D Area=Geometry.GetLocalSize();
    if (CardSlot && Area.X>0 && Area.Y>0)
    {
        const float Width=FMath::Min(MCTutorialWidgetPrivate::CardWidth,FMath::Max(280.f,float(Area.X)-32.f));
        // Reflow the text at the actual logical width; shrinking the whole lesson
        // would make translated instructions progressively smaller.
        if(CardSize && !FMath::IsNearlyEqual(CardSize->GetWidthOverride(),Width,.5f)) CardSize->SetWidthOverride(Width);
        const float TextWidth=FMath::Max(140.f,Width-116.f);
        if(StageTitle) {StageTitle->SetWrapTextAt(TextWidth-28.f);StageTitle->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
        for(UTextBlock* Text:{Dialogue.Get(),Action.Get()}) if(Text) {Text->SetWrapTextAt(TextWidth-8.f);Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
        for(UTextBlock* Label:{ReadyLabel.Get(),MenuLabel.Get()}) if(Label) {Label->SetWrapTextAt(FMath::Max(80.f,(TextWidth-12.f)*.5f-32.f));Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
        if(OwnProgress) OwnProgress->SetWrapTextAt(FMath::Max(70.f,TextWidth*.5f-20.f));
        if(TeamProgress) {TeamProgress->SetAutoWrapText(true);TeamProgress->SetWrapTextAt(FMath::Max(70.f,TextWidth*.5f-20.f));}
        // The compact lesson sits just above stamina and controls, below the player.
        const float BottomInset=FMath::Min(MCTutorialWidgetPrivate::BottomInset,float(Area.Y)*.55f);
        if(CardSize) {const float MaxHeight=FMath::Min(MCTutorialWidgetPrivate::MaximumHeight,FMath::Max(120.f,float(Area.Y)-BottomInset-24.f));CardSize->SetMaxDesiredHeight(MaxHeight);CardSize->SetMinDesiredHeight(FMath::Min(DesiredCardHeight,MaxHeight));}
        CardSlot->SetPosition(FVector2D(0,-BottomInset));
        CardSlot->SetAutoSize(true);CardSlot->SetSize(FVector2D(Width,DesiredCardHeight));
    }
    if(IsDesignTime() || bEditorFixture) return;
    RefreshElapsed+=DeltaSeconds;
    if (RefreshElapsed>=.1f) { RefreshElapsed=0; RefreshState(); }
}

void UMCTutorialWidget::ReadyClicked()
{
    if (IsDesignTime() || bEditorFixture) return;
    if (!RequiresMenuInput() || !ReadyButton || !ReadyButton->GetIsEnabled()) return;
    if (auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->ServerSetTutorialReady(true);
}

void UMCTutorialWidget::MenuClicked()
{
    if(IsDesignTime() || bEditorFixture) return;
    if (auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->ReturnToMainMenu();
}

FReply UMCTutorialWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(IsDesignTime() || bEditorFixture) return Super::NativeOnKeyDown(Geometry,Event);
    if(!Event.IsRepeat() && RequiresMenuInput() && (Event.GetKey()==EKeys::Left || Event.GetKey()==EKeys::Right || Event.GetKey()==EKeys::Tab || Event.GetKey()==EKeys::Gamepad_DPad_Left || Event.GetKey()==EKeys::Gamepad_DPad_Right)) {
        const bool ReadyFocused=ReadyButton && (ReadyButton->HasUserFocus(GetOwningPlayer()) || ReadyButton->HasFocusedDescendants());
        UButton* Target=ReadyFocused || !ReadyButton || !ReadyButton->GetIsEnabled()?MenuButton.Get():ReadyButton.Get();
        if(Target && GetOwningPlayer()) Target->SetUserFocus(GetOwningPlayer());
        return FReply::Handled();
    }
    if (!Event.IsRepeat() && RequiresMenuInput() && (Event.GetKey()==EKeys::Enter || Event.GetKey()==EKeys::Gamepad_FaceButton_Bottom || Event.GetKey()==EKeys::Virtual_Gamepad_Accept.GetVirtualKey()))
    { if(MenuButton && (MenuButton->HasUserFocus(GetOwningPlayer()) || MenuButton->HasFocusedDescendants())) MenuClicked();else ReadyClicked();return FReply::Handled(); }
    if (!Event.IsRepeat() && (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right))
    { MenuClicked(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry,Event);
}

FReply UMCTutorialWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    // Handle directional navigation before a focused button consumes Enter/A.
    const FKey Key=Event.GetKey();
    const bool NavigationKey=Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Tab || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Right ||
        Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom || Key==EKeys::Virtual_Gamepad_Accept.GetVirtualKey() || Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right;
    return RequiresMenuInput() && NavigationKey?NativeOnKeyDown(Geometry,Event):Super::NativeOnPreviewKeyDown(Geometry,Event);
}


void UMCTutorialWidget::ApplyTheme()
{
    const auto* Style=GetResolvedTheme();if(!Style) return;const auto& P=Style->Palette;
    auto Text=[&](UTextBlock* Label,int32 Size,FLinearColor Color,bool Bold=false){if(Label){Label->SetFont(Style->MakeGameplayFont(Size,Bold));Label->SetColorAndOpacity(Color);}};
    BindStyle(TEXT("TutorialCard"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    BindStyle(TEXT("TutorialHeader"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    BindStyle(TEXT("TutorialPortraitBorder"),EMCUIColorToken::SurfaceAlt,EMCUIStyleTarget::Background);
    BindStyle(TEXT("TutorialProgress"),EMCUIColorToken::Accent,EMCUIStyleTarget::Progress);
    Text(StageTitle,16,WidgetTree && WidgetTree->FindWidget(TEXT("TutorialHeader"))?P.Ink:P.OnMarine,true);Text(Dialogue,16,P.OnMarine);Text(Action,17,P.OnMarine,true);Text(OwnProgress,13,P.OnMarine);Text(TeamProgress,13,P.Mint);Text(Timer,13,P.OnMarine);
    Text(PortraitStar,30,P.Mint,true);Text(PortraitName,11,P.Ink,true);
    if(PortraitName) PortraitName->SetText(Copy(TEXT("UI.Tutorial.FairyShortName"),LOCTEXT("Fairy","Фея")));
    MCUIVisualStyle::Button(ReadyButton,Style,P.Mint);MCUIVisualStyle::Button(MenuButton,Style,P.Button);
    for(UButton* Button:{ReadyButton.Get(),MenuButton.Get()}) if(Button) {auto ButtonStyle=Button->GetStyle();ButtonStyle.SetNormalPadding(FMargin(16,9));ButtonStyle.SetPressedPadding(FMargin(16,9));Button->SetStyle(ButtonStyle);}
    Text(ReadyLabel,17,P.ButtonText,true);Text(MenuLabel,17,P.ButtonText,true);
    if(MenuLabel) MenuLabel->SetText(Copy(TEXT("UI.Tutorial.ReturnToMenu"),LOCTEXT("ReturnToMenu","В главное меню")));
    Super::ApplyTheme();
}
void UMCTutorialWidget::ApplyEditorPreview(float Time)
{
    BuildPresentationTree();bEditorFixture=true;
    if(!Card) return;Card->SetVisibility(PreviewState==TEXT("Empty") || PreviewScenario==EMCGameplayUIPreviewScenario::Empty?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    bFixtureComplete=PreviewState==TEXT("Complete") || PreviewState==TEXT("FullComplete") || PreviewState==TEXT("ShortComplete") || PreviewState==TEXT("Ready") || PreviewScenario==EMCGameplayUIPreviewScenario::Victory;
    const bool Short=bPreviewShortFlow || PreviewState==TEXT("ShortComplete");
    bFixtureShort=Short;
    const bool Ready=bPreviewReady || PreviewState==TEXT("Ready");const FString Culture=DisplayCulture();
    auto Format=[&](FName Key,const FText& Fallback,const FFormatNamedArguments& Args){return UMCUIDataLibrary::FormatTextForCulture(Copy(Key,Fallback),Args,Culture);};
    FText TitleText=Copy(TEXT("UI.Tutorial.Copy.BrushTitle"),LOCTEXT("FixtureTitle","01 · ОСВОЕНИЕ ЩЁТКИ"));
    FText DialogueText=Copy(TEXT("UI.Tutorial.Copy.BrushFairy"),LOCTEXT("FixtureDialogue","Щётка всегда в первом слоте. Выбери её и очисти свой подсвеченный зуб. Каждый пробует сам — я подожду!"));
    FText InstructionText=Copy(TEXT("UI.Tutorial.Copy.BrushHint"),LOCTEXT("FixtureHint","1 — щётка. Держи ЛКМ у своего подсвеченного зуба до полной очистки. Без таймера."));
    if(bFixtureComplete) {
        TitleText=Copy(TEXT("UI.Tutorial.Copy.CompleteTitle"),LOCTEXT("FixtureCompleteTitle","ОБУЧЕНИЕ ПРОЙДЕНО"));
        DialogueText=Short?Copy(TEXT("UI.Tutorial.Copy.ShortCompleteFairy"),LOCTEXT("FixtureShortDialogue","Обучение пройдено! Переходим на основную карту: там каждый выберет первый перк.")):
            Copy(TEXT("UI.Tutorial.Copy.CompleteFairy"),LOCTEXT("FixtureFullDialogue","Он просыпается — скоро настоящий завтрак. Когда вся команда готова, начинаем день 1."));
        InstructionText=Short?Copy(TEXT("UI.Tutorial.Copy.ShortCompleteHint"),LOCTEXT("FixtureShortHint","После готовности команды загрузится основная карта.")):
            Copy(TEXT("UI.Tutorial.Copy.CompleteHint"),LOCTEXT("FixtureFullHint","Нажми «Готов к дню 1». Переход начнётся после готовности всей команды."));
    }
    if(!PreviewTitle.IsEmpty()) TitleText=PreviewTitle;
    if(!PreviewDialogue.IsEmpty()) DialogueText=PreviewDialogue;
    if(!PreviewInstruction.IsEmpty()) InstructionText=PreviewInstruction;
    if(StageTitle) StageTitle->SetText(Format(TEXT("UI.Tutorial.StageHeading"),LOCTEXT("FixtureHeading","Обучение · Зубная фея · {StageTitle}"),{{TEXT("StageTitle"),TitleText}}));
    if(Dialogue) {Dialogue->SetText(DialogueText);Dialogue->SetToolTipText(DialogueText);}
    if(Action) Action->SetText(InstructionText);
    if(OwnProgress) OwnProgress->SetText(bFixtureComplete?(Short?Copy(TEXT("UI.Tutorial.ShortComplete"),LOCTEXT("FixtureShort","Обучение пройдено. Получаем первый личный перк")):Ready?Copy(TEXT("UI.Tutorial.CompleteReady"),LOCTEXT("FixtureReady","Ты готов. Ждём команду")):Copy(TEXT("UI.Tutorial.CompleteNotReady"),LOCTEXT("FixtureNotReady","Обучение пройдено. Подтверди готовность"))):
        Format(TEXT("UI.Tutorial.PersonalProgress"),LOCTEXT("FixtureProgress","Твой прогресс: {Current}/{Required}"),{{TEXT("Current"),PreviewCurrent},{TEXT("Required"),PreviewRequired}}));
    if(TeamProgress) TeamProgress->SetText(Format(TEXT("UI.Tutorial.TeamReady"),LOCTEXT("FixtureTeam","Готовы: {Current}/{Total}"),{{TEXT("Current"),2},{TEXT("Total"),4}}));
    if(Progress){Progress->SetPercent(bFixtureComplete?1:PreviewRequired>0?FMath::Clamp(float(PreviewCurrent)/PreviewRequired,0.f,1.f):0);Progress->SetVisibility(ESlateVisibility::HitTestInvisible);}
    if(ReadyButton){ReadyButton->SetVisibility(bFixtureComplete && !Short?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ReadyButton->SetIsEnabled(!Ready);}
    if(MenuButton) MenuButton->SetVisibility(bFixtureComplete && !Short?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(ReadyButton) if(auto* Actions=Cast<UHorizontalBox>(ReadyButton->GetParent())) Actions->SetVisibility(bFixtureComplete && !Short?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(ReadyLabel) ReadyLabel->SetText(Ready?Copy(TEXT("UI.Tutorial.ReadyConfirmed"),LOCTEXT("FixtureReadyConfirmed","Ты готов ✓")):Copy(TEXT("UI.Tutorial.ReadyAction"),LOCTEXT("FixtureReadyAction","Готов")));
    if(Timer) Timer->SetVisibility(ESlateVisibility::Collapsed);
    DesiredCardHeight=bFixtureComplete || PreviewState==TEXT("LongText")?MCTutorialWidgetPrivate::ExpandedHeight:MCTutorialWidgetPrivate::LessonHeight;
    if(CardSize){CardSize->ClearHeightOverride();CardSize->SetMinDesiredHeight(DesiredCardHeight);}
    if(CardSlot) {CardSlot->SetAutoSize(true);CardSlot->SetSize(FVector2D(MCTutorialWidgetPrivate::CardWidth,DesiredCardHeight));}
    UpdatePortrait();ApplyTheme();SetRenderOpacity(Time<=0?1.f:FMath::Clamp(Time*6.f,0.f,1.f));
}
void UMCTutorialWidget::ResetEditorPreview(){bEditorFixture=false;SetRenderOpacity(1);}
#undef LOCTEXT_NAMESPACE
