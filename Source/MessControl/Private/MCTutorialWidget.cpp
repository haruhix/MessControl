#include "MCTutorialWidget.h"
#include "MCTutorialDirector.h"
#include "MCTutorialTypes.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
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
    const FLinearColor Cream(.98f,.97f,.92f), Mint(.30f,.91f,.73f), Muted(.66f,.75f,.79f);

    UTextBlock* Label(UWidgetTree* Tree,int32 Size,FLinearColor Color=Cream)
    {
        auto* Text=Tree->ConstructWidget<UTextBlock>();
        Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Size));
        Text->SetColorAndOpacity(FSlateColor(Color));
        Text->SetAutoWrapText(true);
        return Text;
    }

    UButton* Button(UWidgetTree* Tree,UTextBlock*& OutLabel,const TCHAR* Text)
    {
        auto* Result=Tree->ConstructWidget<UButton>();
        FButtonStyle Style=Result->GetStyle();
        Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.035f,.10f,.11f),10.f,Mint.CopyWithNewOpacity(.55f),1.f));
        Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.065f,.18f,.17f),10.f,Mint,2.f));
        Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.025f,.075f,.075f),10.f,Mint,2.f));
        Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.035f,.065f,.075f),10.f,Muted.CopyWithNewOpacity(.3f),1.f));
        Style.SetNormalPadding(FMargin(18,10));
        Style.SetPressedPadding(FMargin(18,10));
        Result->SetStyle(Style);
        OutLabel=Label(Tree,16,Mint);
        OutLabel->SetText(FText::FromString(Text));
        OutLabel->SetJustification(ETextJustify::Center);
        Result->SetContent(OutLabel);
        return Result;
    }
}

UMCTutorialWidget::UMCTutorialWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // A reflected hard reference on the native CDO also makes the portrait discoverable by cooking.
    // Quiet optional loading retains the existing fallback while the new asset is being authored.
    static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Portrait(
        TEXT("/Game/Art/UI/T_ToothFairyPortrait_v1.T_ToothFairyPortrait_v1"),LOAD_Quiet|LOAD_NoWarn);
    FairyPortrait=Portrait.Get();
}

void UMCTutorialWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget=Root;
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);
    Scale->SetStretchDirection(EStretchDirection::DownOnly);
    CardSlot=Root->AddChildToCanvas(Scale);
    CardSlot->SetAnchors(FAnchors(.5f,1.f));
    CardSlot->SetAlignment(FVector2D(.5f,1.f));
    CardSlot->SetPosition(FVector2D(0,-290));
    CardSlot->SetSize(FVector2D(980,230));
    CardSize=WidgetTree->ConstructWidget<USizeBox>();
    CardSize->SetWidthOverride(980);
    CardSize->SetHeightOverride(230);
    Scale->SetContent(CardSize);
    Card=WidgetTree->ConstructWidget<UBorder>();
    Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.012f,.025f,.037f,.96f),18.f,MCTutorialWidgetPrivate::Mint.CopyWithNewOpacity(.5f),2.f));
    Card->SetPadding(FMargin(20,16));
    Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Card->SetClipping(EWidgetClipping::ClipToBounds);
    CardSize->SetContent(Card);
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
    Card->SetContent(Row);

    auto* PortraitSize=WidgetTree->ConstructWidget<USizeBox>();
    PortraitSize->SetWidthOverride(102); PortraitSize->SetHeightOverride(102);
    auto* PortraitBorder=WidgetTree->ConstructWidget<UBorder>();
    PortraitBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.04f,.12f,.13f),20.f));
    PortraitBorder->SetPadding(FMargin(6)); PortraitSize->SetContent(PortraitBorder);
    auto* PortraitOverlay=WidgetTree->ConstructWidget<UOverlay>(); PortraitBorder->SetContent(PortraitOverlay);
    PortraitImage=WidgetTree->ConstructWidget<UImage>();
    auto* ImageSlot=PortraitOverlay->AddChildToOverlay(PortraitImage);
    ImageSlot->SetHorizontalAlignment(HAlign_Fill);
    ImageSlot->SetVerticalAlignment(VAlign_Fill);
    PortraitFallback=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* FallbackSlot=PortraitOverlay->AddChildToOverlay(PortraitFallback);
    FallbackSlot->SetHorizontalAlignment(HAlign_Center); FallbackSlot->SetVerticalAlignment(VAlign_Center);
    auto* Star=MCTutorialWidgetPrivate::Label(WidgetTree,38,MCTutorialWidgetPrivate::Mint);
    Star->SetText(FText::FromString(TEXT("✦"))); Star->SetJustification(ETextJustify::Center);
    PortraitFallback->AddChildToVerticalBox(Star);
    auto* Fairy=MCTutorialWidgetPrivate::Label(WidgetTree,14,MCTutorialWidgetPrivate::Cream);
    Fairy->SetText(FText::FromString(TEXT("ФЕЯ"))); Fairy->SetJustification(ETextJustify::Center);
    PortraitFallback->AddChildToVerticalBox(Fairy);
    auto* PortraitSlot=Row->AddChildToHorizontalBox(PortraitSize);
    PortraitSlot->SetVerticalAlignment(VAlign_Center); PortraitSlot->SetPadding(FMargin(0,0,20,0));

    auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();
    Body->SetClipping(EWidgetClipping::ClipToBounds);
    Row->AddChildToHorizontalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    StageTitle=MCTutorialWidgetPrivate::Label(WidgetTree,15,MCTutorialWidgetPrivate::Mint);
    Body->AddChildToVerticalBox(StageTitle)->SetPadding(FMargin(0,0,0,6));
    Dialogue=MCTutorialWidgetPrivate::Label(WidgetTree,18);
    Body->AddChildToVerticalBox(Dialogue)->SetPadding(FMargin(0,0,0,8));
    Action=MCTutorialWidgetPrivate::Label(WidgetTree,19);
    Body->AddChildToVerticalBox(Action)->SetPadding(FMargin(0,0,0,8));

    auto* StatusRow=WidgetTree->ConstructWidget<UHorizontalBox>();
    Body->AddChildToVerticalBox(StatusRow)->SetPadding(FMargin(0,0,0,6));
    OwnProgress=MCTutorialWidgetPrivate::Label(WidgetTree,14,MCTutorialWidgetPrivate::Muted);
    OwnProgress->SetAutoWrapText(false);
    OwnProgress->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    OwnProgress->SetClipping(EWidgetClipping::ClipToBounds);
    StatusRow->AddChildToHorizontalBox(OwnProgress)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    TeamProgress=MCTutorialWidgetPrivate::Label(WidgetTree,14,MCTutorialWidgetPrivate::Mint);
    TeamProgress->SetAutoWrapText(false);
    auto* TeamSlot=StatusRow->AddChildToHorizontalBox(TeamProgress);
    TeamSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    TeamSlot->SetPadding(FMargin(20,0,0,0));
    Timer=MCTutorialWidgetPrivate::Label(WidgetTree,14,MCTutorialWidgetPrivate::Muted);
    Timer->SetAutoWrapText(false);
    auto* TimerSlot=StatusRow->AddChildToHorizontalBox(Timer);
    TimerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    TimerSlot->SetPadding(FMargin(20,0,0,0));
    Progress=WidgetTree->ConstructWidget<UProgressBar>();
    Progress->SetFillColorAndOpacity(MCTutorialWidgetPrivate::Mint);
    auto* BarSize=WidgetTree->ConstructWidget<USizeBox>(); BarSize->SetHeightOverride(6); BarSize->SetContent(Progress);
    Body->AddChildToVerticalBox(BarSize)->SetPadding(FMargin(0,0,0,4));

    auto* Buttons=WidgetTree->ConstructWidget<UHorizontalBox>();
    Body->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0,10,0,0));
    UTextBlock* ReadyText=nullptr;
    ReadyButton=MCTutorialWidgetPrivate::Button(WidgetTree,ReadyText,TEXT("Готов к дню 1  ·  Enter")); ReadyLabel=ReadyText;
    ReadyButton->OnClicked.AddUniqueDynamic(this,&UMCTutorialWidget::ReadyClicked);
    Buttons->AddChildToHorizontalBox(ReadyButton)->SetPadding(FMargin(0,0,12,0));
    UTextBlock* MenuText=nullptr;
    MenuButton=MCTutorialWidgetPrivate::Button(WidgetTree,MenuText,TEXT("В главное меню"));
    MenuButton->OnClicked.AddUniqueDynamic(this,&UMCTutorialWidget::MenuClicked);
    Buttons->AddChildToHorizontalBox(MenuButton);
    UpdatePortrait();
}

void UMCTutorialWidget::NativeConstruct()
{
    Super::NativeConstruct();
    RefreshState();
}

bool UMCTutorialWidget::IsTutorialVisible() const
{
    const auto* Director=AMCTutorialDirector::Find(GetWorld());
    return Director && Director->Stage!=EMCTutorialStage::Finished;
}

bool UMCTutorialWidget::RequiresMenuInput() const
{
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

void UMCTutorialWidget::RefreshState()
{
    if (!Card) return;
    const auto* Director=AMCTutorialDirector::Find(GetWorld());
    const bool Visible=Director && Director->Stage!=EMCTutorialStage::Finished;
    Card->SetVisibility(Visible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if (!Visible) return;
    UpdatePortrait();
    const bool Complete=Director->IsComplete();
    const bool Expanded=Complete || Director->Stage==EMCTutorialStage::Intro || Director->Stage==EMCTutorialStage::Loading;
    DesiredCardHeight=Expanded?310.f:230.f;
    CardSize->SetHeightOverride(DesiredCardHeight);
    StageTitle->SetText(FText::FromString(FString::Printf(TEXT("ОБУЧЕНИЕ  ·  ЗУБНАЯ ФЕЯ  ·  %s"),*Director->Title.ToString())));
    Dialogue->SetText(Director->FairyLine);
    Dialogue->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Expanded?18:16));
    Dialogue->SetAutoWrapText(true);
    Dialogue->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    Dialogue->SetToolTipText(Director->FairyLine);
    Dialogue->SetVisibility(ESlateVisibility::HitTestInvisible);
    Action->SetText(Director->Instruction);
    ReadyButton->SetVisibility(Complete && !Director->UsesShortFlow()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    MenuButton->SetVisibility(Complete && !Director->UsesShortFlow()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const auto* Personal=Director->GetPlayerProgress(PC?PC->GetPlayerState<APlayerState>():nullptr);
    const int32 Required=Personal?Personal->StageRequired:0;
    const int32 Done=Personal?FMath::Clamp(Personal->StageProgress,0,Required):0;
    const bool Ready=Personal && Personal->bReady;
    FString PersonalText;
    if (Complete) PersonalText=Director->UsesShortFlow()?TEXT("Обучение пройдено. Получаем первый личный перк"):Ready?TEXT("Ты готов. Ждём команду"):TEXT("Обучение пройдено. Подтверди готовность");
    else if (Required>0) PersonalText=FString::Printf(TEXT("Твой прогресс: %d/%d%s"),Done,Required,Done>=Required?TEXT("  ·  Ждём команду"):TEXT(""));
    else if (Director->Stage==EMCTutorialStage::Loading) PersonalText=Personal && Personal->bLoaded?TEXT("Ты на арене. Ждём загрузку команды"):TEXT("Загружаем арену…");
    else PersonalText=FMCTutorialProgressRules::IsSharedStage(Director->Stage)?TEXT("Одна общая задача для всей команды"):TEXT("Работайте вместе");
    OwnProgress->SetText(FText::FromString(PersonalText));
    FString TeamText;
    if (Complete || Director->Stage==EMCTutorialStage::Loading)
    {
        int32 TeamDone=0;
        for (const auto& Player:Director->Players) TeamDone+=Complete?Player.bReady:Player.bLoaded;
        TeamText=Complete?FString::Printf(TEXT("Готовы: %d/%d"),TeamDone,Director->GetRequiredPlayers())
            :FString::Printf(TEXT("На арене: %d/%d"),TeamDone,Director->GetRequiredPlayers());
    }
    else if (Required>0) TeamText=FString::Printf(TEXT("Команда: %d/%d"),Director->GetCompletedPlayers(),Director->GetRequiredPlayers());
    else if (Director->TeamTasksTotal>0) TeamText=FString::Printf(TEXT("Общая задача: %d/%d"),FMath::Max(0,Director->TeamTasksTotal-Director->TeamTasksLeft),Director->TeamTasksTotal);
    else TeamText=FString::Printf(TEXT("Участников: %d"),Director->GetRequiredPlayers());
    TeamProgress->SetText(FText::FromString(TeamText));
    const float SharedProgress=Director->TeamTasksTotal>0?FMath::Clamp(1.f-float(Director->TeamTasksLeft)/Director->TeamTasksTotal,0.f,1.f):0.f;
    Progress->SetPercent(Complete?1.f:Required>0?float(Done)/Required:SharedProgress);
    if(FMCTutorialProgressRules::IsSharedStage(Director->Stage)) {
        const float Pulse=.75f+.25f*FMath::Sin(GetWorld()->GetTimeSeconds()*5.f);
        Action->SetColorAndOpacity(FSlateColor(FLinearColor(1,.88f,.48f,Pulse)));
    } else Action->SetColorAndOpacity(FSlateColor(MCTutorialWidgetPrivate::Cream));
    Progress->SetVisibility(Required>0 || Complete || Director->TeamTasksTotal>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    ReadyButton->SetIsEnabled(Complete && Personal && !Ready);
    ReadyLabel->SetText(FText::FromString(Ready?TEXT("Ты готов ✓"):TEXT("Готов к дню 1  ·  Enter")));
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    const double Remaining=Director->StageEndsAt>0 && GS?FMath::Max(0.,Director->StageEndsAt-GS->GetServerWorldTimeSeconds()):0.;
    Timer->SetVisibility(Director->StageEndsAt>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    Timer->SetText(FText::FromString(FString::Printf(TEXT("%02d с"),FMath::CeilToInt(Remaining))));
}

void UMCTutorialWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    const FVector2D Area=Geometry.GetLocalSize();
    if (CardSlot && Area.X>0 && Area.Y>0)
    {
        const float Width=FMath::Min(980.f,FMath::Max(280.f,float(Area.X)-32.f));
        const float Scale=Width/980.f;
        // Leave the lower 278 px for the inventory, stamina and action hints.
        const float BottomInset=FMath::Min(290.f,float(Area.Y)*.55f);
        CardSlot->SetPosition(FVector2D(0,-BottomInset));
        CardSlot->SetSize(FVector2D(Width,DesiredCardHeight*Scale));
    }
    RefreshElapsed+=DeltaSeconds;
    if (RefreshElapsed>=.1f) { RefreshElapsed=0; RefreshState(); }
}

void UMCTutorialWidget::ReadyClicked()
{
    if (!RequiresMenuInput() || !ReadyButton || !ReadyButton->GetIsEnabled()) return;
    if (auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->ServerSetTutorialReady(true);
}

void UMCTutorialWidget::MenuClicked()
{
    if (auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->ReturnToMainMenu();
}

FReply UMCTutorialWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if (!Event.IsRepeat() && RequiresMenuInput() && (Event.GetKey()==EKeys::Enter || Event.GetKey()==EKeys::Virtual_Gamepad_Accept.GetVirtualKey()))
    { ReadyClicked(); return FReply::Handled(); }
    if (!Event.IsRepeat() && Event.GetKey()==EKeys::Escape)
    { MenuClicked(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry,Event);
}
