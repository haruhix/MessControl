#include "MCGameplayAlertWidget.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "MCGameplayAlert"

namespace MCGameplayAlertPrivate
{
    constexpr float EnterSeconds=.28f,ExitSeconds=.24f;
    constexpr int32 MaxPending=8;
    constexpr double MaxQueueAge=20.;
}

void UMCGameplayAlertWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(false);BuildPresentationTree();SetVisibility(ESlateVisibility::Collapsed);
}

void UMCGameplayAlertWidget::BuildPresentationTree()
{
    if(!WidgetTree || AlertPanel) return;
    if(WidgetTree->RootWidget)
    {
        AlertPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("GameplayAlertPanel")));
        AlertText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("GameplayAlertText")));
        if(AlertPanel) AlertSlot=Cast<UCanvasPanelSlot>(AlertPanel->Slot);
        return;
    }
    // Share the boss stack's logical viewport and project DPI so its count-based
    // vertical offset stays aligned at every viewport size.
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("AlertDesignRoot"));WidgetTree->RootWidget=Root;
    AlertPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("GameplayAlertPanel"));AlertPanel->SetPadding(FMargin(14,10));AlertPanel->SetRenderTransformPivot(FVector2D(.5f,.5f));AlertPanel->SetClipping(EWidgetClipping::ClipToBounds);
    AlertSlot=Root->AddChildToCanvas(AlertPanel);AlertSlot->SetAnchors(FAnchors(.5f,0));AlertSlot->SetAlignment(FVector2D(.5f,0));AlertSlot->SetPosition(FVector2D(0,24));AlertSlot->SetSize(FVector2D(800,66));
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();AlertPanel->SetContent(Row);
    auto Stripe=[&](FName Name)
    {
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Text->SetText(FText::AsCultureInvariant(TEXT("//")));Row->AddChildToHorizontalBox(Text)->SetVerticalAlignment(VAlign_Center);BindStyle(Name,EMCUIColorToken::Warning,EMCUIStyleTarget::Text,29,true);
    };
    Stripe(TEXT("AlertStripeLeft"));
    AlertText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("GameplayAlertText"));AlertText->SetJustification(ETextJustify::Center);AlertText->SetAutoWrapText(true);AlertText->SetWrapTextAt(660);AlertText->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    auto* TextSlot=Row->AddChildToHorizontalBox(AlertText);TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TextSlot->SetVerticalAlignment(VAlign_Center);TextSlot->SetPadding(FMargin(12,0));
    Stripe(TEXT("AlertStripeRight"));
    BindStyle(TEXT("GameplayAlertPanel"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);BindStyle(TEXT("GameplayAlertText"),EMCUIColorToken::BossHealth,EMCUIStyleTarget::Text,19,true);ApplyTheme();
}

void UMCGameplayAlertWidget::ApplyTheme()
{
    Super::ApplyTheme();PresentCurrent();ApplyReveal();
}

void UMCGameplayAlertWidget::QueueAlert(const FText& Message,float HoldSeconds,FName EventId)
{
    QueueOwnedAlert(Message,FText::GetEmpty(),NAME_None,HoldSeconds,EventId);
}

void UMCGameplayAlertWidget::QueueOwnedAlert(const FText& Message,const FText& OwnerName,FName OwnerID,float HoldSeconds,FName EventId)
{
    const FText Clean=FText::TrimPrecedingAndTrailing(Message);
    if(Clean.IsEmpty() || !FMath::IsFinite(HoldSeconds) || !GetWorld() || IsDesignTime()) return;
    const FString Bounded=Clean.ToString().Left(180);
    if(EventId!=NAME_None && ((bPresenting && Current.EventId==EventId) || Pending.ContainsByPredicate([EventId](const FAlert& Alert){return Alert.EventId==EventId;}))) return;
    FAlert Alert;Alert.Message=Clean.ToString().Len()<=180?Clean:FText::FromString(Bounded);Alert.OwnerName=OwnerName;Alert.OwnerID=OwnerID;Alert.EventId=EventId;
    Alert.HoldSeconds=FMath::Clamp(FMath::Max(HoldSeconds,1.8f+Bounded.Len()*.035f),2.5f,7.f);Alert.QueuedAt=GetWorld()->GetTimeSeconds();
    if(Pending.Num()>=MCGameplayAlertPrivate::MaxPending) Pending.RemoveAt(0);
    Pending.Add(MoveTemp(Alert));if(!bPresenting) PresentNext();
}

void UMCGameplayAlertWidget::ClearAlerts()
{
    Pending.Reset();Current=FAlert();Elapsed=0;bPresenting=false;SetVisibility(ESlateVisibility::Collapsed);
}
void UMCGameplayAlertWidget::PresentNext()
{
    BuildPresentationTree();const double Now=GetWorld()?GetWorld()->GetTimeSeconds():0.;
    while(!Pending.IsEmpty())
    {
        Current=MoveTemp(Pending[0]);Pending.RemoveAt(0);
        if(Now-Current.QueuedAt>MCGameplayAlertPrivate::MaxQueueAge) continue;
        bPresenting=true;Elapsed=0;SetVisibility(ESlateVisibility::HitTestInvisible);PresentCurrent();ApplyReveal();return;
    }
    ClearAlerts();
}
void UMCGameplayAlertWidget::SetBossStackCount(int32 Count,float StackHeight)
{
    BossStackCount=FMath::Clamp(Count,0,3);
    BossStackHeight=StackHeight;
    const float Height=BossStackCount>0?(StackHeight>=0.f?StackHeight+5.f:BossStackCount*64.3f+20.f):0.f;
    if(AlertSlot) AlertSlot->SetPosition(FVector2D(0,18.f+Height));
}
void UMCGameplayAlertWidget::PresentCurrent()
{
    if(AlertText)
    {
        AlertText->SetText(Current.OwnerName.IsEmpty()?Current.Message:UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Alert.OwnedMessage"),LOCTEXT("OwnedMessage","{Owner} · {Message}")),FFormatNamedArguments{{TEXT("Owner"),Current.OwnerName},{TEXT("Message"),Current.Message}},IsDesignTime()?PreviewCulture:FString()));
        AlertText->SetWrapTextAt(660.f);AlertText->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);AlertText->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        AlertText->TakeWidget()->SlatePrepass();
        if(AlertSlot) AlertSlot->SetSize(FVector2D(800,FMath::Max(66.f,AlertText->GetDesiredSize().Y+20.f)));
    }
    SetBossStackCount(BossStackCount,BossStackHeight);
}
void UMCGameplayAlertWidget::ApplyReveal()
{
    if(!AlertPanel) return;
    const float Enter=FMath::Clamp(Elapsed/MCGameplayAlertPrivate::EnterSeconds,0.f,1.f);
    const float Exit=FMath::Clamp((Elapsed-MCGameplayAlertPrivate::EnterSeconds-Current.HoldSeconds)/MCGameplayAlertPrivate::ExitSeconds,0.f,1.f);
    const float Reveal=FMath::SmoothStep(0.f,1.f,Enter)*(1.f-FMath::SmoothStep(0.f,1.f,Exit));
    AlertPanel->SetRenderOpacity(Reveal);AlertPanel->SetRenderScale(FVector2D(.72f+.28f*Reveal,1));AlertPanel->SetRenderTranslation(FVector2D(0,-24.f*(1-Reveal)));
}
void UMCGameplayAlertWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);if(IsDesignTime() || !bPresenting) return;
    Elapsed+=FMath::Max(0.f,DeltaSeconds);
    if(Elapsed>=MCGameplayAlertPrivate::EnterSeconds+Current.HoldSeconds+MCGameplayAlertPrivate::ExitSeconds) PresentNext();
    else ApplyReveal();
}
void UMCGameplayAlertWidget::ApplyEditorPreview(float Time)
{
    PreviewTime=FMath::Max(0.f,Time);BuildPresentationTree();bPresenting=PreviewScenario!=EMCGameplayUIPreviewScenario::Empty;
    Current.HoldSeconds=3.5f;Current.OwnerID=TEXT("RottenGuardian");Current.OwnerName=Localized(TEXT("UI.Boss.Zombie.Name"),LOCTEXT("BossName","Гнилой страж"));
    Current.Message=PreviewScenario==EMCGameplayUIPreviewScenario::LongText?Localized(TEXT("UI.Alert.LongPreview"),LOCTEXT("LongPreview","Берегись удара! Держись рядом с опорой и помоги товарищу выбраться из опасной зоны.")):Localized(TEXT("UI.Alert.BossWarningPreview"),LOCTEXT("BossWarning","БЕРЕГИСЬ УДАРА!"));
    SetBossStackCount(PreviewScenario==EMCGameplayUIPreviewScenario::ThreeBosses?3:PreviewScenario==EMCGameplayUIPreviewScenario::TwoBosses?2:PreviewScenario==EMCGameplayUIPreviewScenario::OneBoss?1:0);
    Elapsed=Time<=0.f?MCGameplayAlertPrivate::EnterSeconds:FMath::Fmod(Time,MCGameplayAlertPrivate::EnterSeconds+Current.HoldSeconds+MCGameplayAlertPrivate::ExitSeconds);
    SetVisibility(bPresenting?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);ApplyTheme();
}

#undef LOCTEXT_NAMESPACE
