#include "MCStaminaWidget.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

void UMCStaminaWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetVisibility(ESlateVisibility::HitTestInvisible);
    // A Blueprint may provide its own tree and optional named controls.
    if(WidgetTree->RootWidget) return;
    auto* Background=WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,10));
    Background->SetBrushColor(FLinearColor(.012f,.020f,.033f,.88f));
    Background->SetPadding(FMargin(14,8));
    WidgetTree->RootWidget=Background;
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); Background->SetContent(Box);
    StaminaValue=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("StaminaValue"));
    StaminaValue->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13));
    StaminaValue->SetColorAndOpacity(FSlateColor(FLinearColor(.98f,.97f,.93f)));
    Box->AddChildToVerticalBox(StaminaValue)->SetPadding(FMargin(0,0,0,5));
    StaminaProgress=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("StaminaProgress"));
    StaminaProgress->SetFillColorAndOpacity(FLinearColor(.26f,.91f,.71f));
    Box->AddChildToVerticalBox(StaminaProgress);
}
void UMCStaminaWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt); RefreshElapsed+=Dt;
    if(RefreshElapsed>=.1f) {RefreshElapsed=0;Refresh();}
}
void UMCStaminaWidget::Refresh()
{
    const auto* Hero=GetOwningPlayerPawn<AMCToothCharacter>();
    const bool Available=Hero && Hero->Status && Hero->Status->IsAlive();
    SetVisibility(Available?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(!Available) return;
    const float NewMaximum=FMath::Max(1.f,Hero->GetMaxStamina());
    const float NewCurrent=FMath::Clamp(Hero->GetStamina(),0.f,NewMaximum);
    const float NewNormalized=FMath::Clamp(Hero->GetStaminaNormalized(),0.f,1.f);
    const bool Exhausted=Hero->IsStaminaExhausted();
    const FLinearColor PlayerColor=Hero->GetPlayerColor();
    if(bHasSample && FMath::IsNearlyEqual(Current,NewCurrent,.01f)
        && FMath::IsNearlyEqual(Maximum,NewMaximum,.01f) && bExhausted==Exhausted && LastColor.Equals(PlayerColor)) return;
    const int32 DisplayCurrent=FMath::RoundToInt(NewCurrent),DisplayMaximum=FMath::RoundToInt(NewMaximum);
    const bool TextChanged=!bHasSample || FMath::RoundToInt(Current)!=DisplayCurrent || FMath::RoundToInt(Maximum)!=DisplayMaximum;
    Current=NewCurrent; Maximum=NewMaximum; Normalized=NewNormalized; bExhausted=Exhausted; bHasSample=true;
    LastColor=PlayerColor;
    if(StaminaProgress)
    {
        StaminaProgress->SetPercent(Normalized);
        StaminaProgress->SetFillColorAndOpacity(Normalized<.2f?FLinearColor(1,.64f,.23f):PlayerColor);
    }
    if(StaminaValue && TextChanged) StaminaValue->SetText(FText::FromString(FString::Printf(TEXT("ВЫНОСЛИВОСТЬ  %d / %d"),DisplayCurrent,DisplayMaximum)));
    OnStaminaChanged(Current,Maximum,Normalized,bExhausted);
}
