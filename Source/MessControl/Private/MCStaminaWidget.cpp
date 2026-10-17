#include "MCStaminaWidget.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothMovementComponent.h"
#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"

#define LOCTEXT_NAMESPACE "MCStamina"

void UMCStaminaWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetVisibility(ESlateVisibility::HitTestInvisible);
    BuildPresentationTree();
}
void UMCStaminaWidget::BuildPresentationTree()
{
    if(!WidgetTree || (StaminaValue && StaminaProgress)) return;
    if(WidgetTree->RootWidget) {
        StaminaValue=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("StaminaValue")));StaminaProgress=Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("StaminaProgress")));StaminaCaption=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("StaminaCaption")));return;
    }
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("StaminaSize"));
    Size->SetWidthOverride(336);Size->SetMinDesiredHeight(56);WidgetTree->RootWidget=Size;
    auto* Background=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("StaminaBackground"));
    Background->SetPadding(FMargin(12,8));Size->SetContent(Background);
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); Background->SetContent(Box);
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,5));
    StaminaCaption=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("StaminaCaption"));
    StaminaCaption->SetAutoWrapText(true);StaminaCaption->SetWrapTextAt(188);StaminaCaption->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    StaminaCaption->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    auto* CaptionSlot=Header->AddChildToHorizontalBox(StaminaCaption);CaptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));CaptionSlot->SetVerticalAlignment(VAlign_Center);
    StaminaValue=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("StaminaValue"));
    StaminaValue->SetAutoWrapText(true);StaminaValue->SetWrapTextAt(116);StaminaValue->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    StaminaValue->SetJustification(ETextJustify::Right);StaminaValue->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    auto* ValueWidth=WidgetTree->ConstructWidget<USizeBox>();ValueWidth->SetWidthOverride(116);ValueWidth->SetContent(StaminaValue);
    auto* ValueSlot=Header->AddChildToHorizontalBox(ValueWidth);ValueSlot->SetPadding(FMargin(8,0,0,0));ValueSlot->SetVerticalAlignment(VAlign_Center);
    StaminaProgress=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("StaminaProgress"));
    auto* Height=WidgetTree->ConstructWidget<USizeBox>();Height->SetHeightOverride(12);Height->SetContent(StaminaProgress);Box->AddChildToVerticalBox(Height);
    BindStyle(TEXT("StaminaBackground"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    BindStyle(TEXT("StaminaCaption"),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,16,true);
    BindStyle(TEXT("StaminaValue"),EMCUIColorToken::Accent,EMCUIStyleTarget::Text,18,true);
    BindStyle(TEXT("StaminaProgress"),EMCUIColorToken::Stamina,EMCUIStyleTarget::Progress);
    ApplyTheme();
}
void UMCStaminaWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt);if(IsDesignTime()) return; RefreshElapsed+=Dt;
    if(RefreshElapsed>=.1f) {RefreshElapsed=0;Refresh();}
}
void UMCStaminaWidget::Refresh()
{
    if(IsDesignTime()) return;BuildPresentationTree();
    const auto* Hero=GetOwningPlayerPawn<AMCToothCharacter>();
    const auto* Move=Hero?Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()):nullptr;
    const bool Deep=bShowSwimEnergy && Move && Move->IsInDeepWater();
    const bool Available=Hero && Hero->Status && Hero->Status->IsAlive()
        && (!bShowSwimEnergy || (Move && (Deep || Move->GetSwimEnergy()<Move->GetMaxSwimEnergy()-.01f)));
    SetVisibility(Available?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(!Available) return;
    const float NewMaximum=FMath::Max(1.f,bShowSwimEnergy?Move->GetMaxSwimEnergy():Hero->GetMaxStamina());
    const float NewCurrent=FMath::Clamp(bShowSwimEnergy?Move->GetSwimEnergy():Hero->GetStamina(),0.f,NewMaximum);
    const float NewNormalized=FMath::Clamp(NewCurrent/NewMaximum,0.f,1.f);
    const bool Exhausted=bShowSwimEnergy?Move->IsDrowning():Hero->IsStaminaExhausted();
    const bool Low=bShowSwimEnergy && Move->IsSwimFatigued();
    const int32 LabelMode=!bShowSwimEnergy?0:Exhausted?4:Low?3:!Deep?2:1;
    const int32 DrowningTenths=Exhausted && bShowSwimEnergy?FMath::CeilToInt(Move->GetDrowningSecondsRemaining()*10.f):INDEX_NONE;
    const auto& Palette=GetResolvedTheme()->Palette;
    const FLinearColor PlayerColor=bShowSwimEnergy?Exhausted?Palette.Cost:Low?Palette.Warning:Palette.Cleanliness:Palette.Stamina;
    if(bHasSample && FMath::IsNearlyEqual(Current,NewCurrent,.01f)
        && FMath::IsNearlyEqual(Maximum,NewMaximum,.01f) && bExhausted==Exhausted && LastColor.Equals(PlayerColor)
        && LastLabelMode==LabelMode && bLastDeepWater==Deep && LastDrowningTenths==DrowningTenths) return;
    Current=NewCurrent; Maximum=NewMaximum; Normalized=NewNormalized; bExhausted=Exhausted; bHasSample=true;
    LastColor=PlayerColor;
    LastLabelMode=LabelMode; bLastDeepWater=Deep; LastDrowningTenths=DrowningTenths;
    if(StaminaProgress)
    {
        const float Grace=bShowSwimEnergy && FMath::IsFinite(Move->DrowningGraceSeconds)?FMath::Max(3.f,Move->DrowningGraceSeconds):6.f;
        StaminaProgress->SetPercent(bShowSwimEnergy && Exhausted?FMath::Clamp(Move->GetDrowningSecondsRemaining()/Grace,0.f,1.f):Normalized);
    }
    PresentSample();
    OnStaminaChanged(Current,Maximum,Normalized,bExhausted);
}
void UMCStaminaWidget::PresentSample()
{
    const FString Culture=IsDesignTime()?PreviewCulture:FString();const auto* ThemeStyle=GetResolvedTheme();const auto& Palette=ThemeStyle->Palette;
    const FText Caption=LastLabelMode==4?Localized(TEXT("UI.Swim.Drowning"),LOCTEXT("Drowning","ТОНЕШЬ")):LastLabelMode==3?Localized(TEXT("UI.Swim.Low"),LOCTEXT("SwimLow","СИЛЫ ЗАКАНЧИВАЮТСЯ")):LastLabelMode==2?Localized(TEXT("UI.Swim.Recovery"),LOCTEXT("Recovery","ВОССТАНОВЛЕНИЕ")):bShowSwimEnergy || LastLabelMode==1?Localized(TEXT("UI.Swim.Title"),LOCTEXT("Swim","ПЛАВАНИЕ")):Localized(TEXT("UI.Stamina.Title"),LOCTEXT("Stamina","ВЫНОСЛИВОСТЬ"));
    const FText Value=LastLabelMode==4?UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Swim.TimeToDeath"),LOCTEXT("TimeToDeath","ДО ГИБЕЛИ {Seconds} С")),FFormatNamedArguments{{TEXT("Seconds"),UMCUIDataLibrary::DecimalForCulture(FMath::Max(0,LastDrowningTenths)/10.f,1,Culture)}},Culture):UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Stamina.Value"),LOCTEXT("Value","{Current} / {Maximum}")),FFormatNamedArguments{{TEXT("Current"),UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt(Current),Culture)},{TEXT("Maximum"),UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt(Maximum),Culture)}},Culture);
    if(StaminaCaption) StaminaCaption->SetText(Caption);
    if(StaminaValue) {StaminaValue->SetText(StaminaCaption?Value:FText::Format(LOCTEXT("LegacyValue","{Caption} · {Value}"),FFormatNamedArguments{{TEXT("Caption"),Caption},{TEXT("Value"),Value}}));StaminaValue->SetColorAndOpacity(LastLabelMode==4?Palette.Cost:Palette.Accent);}
    if(StaminaProgress) MCUIVisualStyle::Progress(StaminaProgress,ThemeStyle,LastLabelMode==4?Palette.Cost:LastLabelMode==3 || (!bShowSwimEnergy && LastLabelMode==0 && Normalized<.2f)?Palette.Warning:bShowSwimEnergy || LastLabelMode>0?Palette.Cleanliness:Palette.Stamina);
}
void UMCStaminaWidget::ApplyTheme() {Super::ApplyTheme();PresentSample();}
void UMCStaminaWidget::ApplyEditorPreview(float Time)
{
    PreviewTime=FMath::Max(0.f,Time);BuildPresentationTree();SetVisibility(PreviewScenario==EMCGameplayUIPreviewScenario::Empty?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    const bool Swim=bShowSwimEnergy || PreviewState==TEXT("Swim") || PreviewState==TEXT("Recovery") || PreviewState==TEXT("Drowning");
    Maximum=100;Current=PreviewScenario==EMCGameplayUIPreviewScenario::LowResources?14.f:72.f;Normalized=Current/Maximum;bExhausted=PreviewState==TEXT("Drowning");
    LastLabelMode=bExhausted?4:PreviewState==TEXT("Recovery")?2:Swim?PreviewScenario==EMCGameplayUIPreviewScenario::LowResources?3:1:0;
    LastDrowningTenths=FMath::RoundToInt(FMath::Max(0.f,3.4f-Time)*10.f);if(bExhausted) {Current=0;Normalized=0;}
    if(StaminaProgress) StaminaProgress->SetPercent(bExhausted?FMath::Clamp((3.4f-Time)/6.f,0.f,1.f):Normalized);
    ApplyTheme();
}
#undef LOCTEXT_NAMESPACE
