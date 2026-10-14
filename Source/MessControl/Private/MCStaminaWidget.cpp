#include "MCStaminaWidget.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothMovementComponent.h"
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
    const auto* Move=Hero?Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()):nullptr;
    const bool Deep=bShowSwimEnergy && Move && Move->IsInDeepWater();
    const bool Available=Hero && Hero->Status && Hero->Status->IsAlive()
        && (!bShowSwimEnergy || (Move && (Deep || Move->GetSwimEnergy()<Move->GetMaxSwimEnergy()-.01f)));
    SetVisibility(Available?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(!Available) return;
    const float NewMaximum=bShowSwimEnergy?Move->GetMaxSwimEnergy():FMath::Max(1.f,Hero->GetMaxStamina());
    const float NewCurrent=FMath::Clamp(bShowSwimEnergy?Move->GetSwimEnergy():Hero->GetStamina(),0.f,NewMaximum);
    const float NewNormalized=FMath::Clamp(NewCurrent/NewMaximum,0.f,1.f);
    const bool Exhausted=bShowSwimEnergy?Move->IsDrowning():Hero->IsStaminaExhausted();
    const bool Low=bShowSwimEnergy && Move->IsSwimFatigued();
    const int32 LabelMode=!bShowSwimEnergy?0:Exhausted?4:Low?3:!Deep?2:1;
    const int32 DrowningTenths=Exhausted && bShowSwimEnergy?FMath::CeilToInt(Move->GetDrowningSecondsRemaining()*10.f):INDEX_NONE;
    const FLinearColor PlayerColor=bShowSwimEnergy?Exhausted?FLinearColor(1.f,.18f,.16f)
        :Low?FLinearColor(1.f,.64f,.23f):FLinearColor(.25f,.76f,1.f):Hero->GetPlayerColor();
    if(bHasSample && FMath::IsNearlyEqual(Current,NewCurrent,.01f)
        && FMath::IsNearlyEqual(Maximum,NewMaximum,.01f) && bExhausted==Exhausted && LastColor.Equals(PlayerColor)
        && LastLabelMode==LabelMode && bLastDeepWater==Deep && LastDrowningTenths==DrowningTenths) return;
    const int32 DisplayCurrent=FMath::RoundToInt(NewCurrent),DisplayMaximum=FMath::RoundToInt(NewMaximum);
    const bool TextChanged=!bHasSample || FMath::RoundToInt(Current)!=DisplayCurrent || FMath::RoundToInt(Maximum)!=DisplayMaximum
        || LastLabelMode!=LabelMode || bLastDeepWater!=Deep || LastDrowningTenths!=DrowningTenths;
    Current=NewCurrent; Maximum=NewMaximum; Normalized=NewNormalized; bExhausted=Exhausted; bHasSample=true;
    LastColor=PlayerColor;
    LastLabelMode=LabelMode; bLastDeepWater=Deep; LastDrowningTenths=DrowningTenths;
    if(StaminaProgress)
    {
        const float Grace=bShowSwimEnergy && FMath::IsFinite(Move->DrowningGraceSeconds)?FMath::Max(3.f,Move->DrowningGraceSeconds):6.f;
        StaminaProgress->SetPercent(bShowSwimEnergy && Exhausted?FMath::Clamp(Move->GetDrowningSecondsRemaining()/Grace,0.f,1.f):Normalized);
        StaminaProgress->SetFillColorAndOpacity(!bShowSwimEnergy && Normalized<.2f?FLinearColor(1,.64f,.23f):PlayerColor);
    }
    if(StaminaValue && TextChanged) {
        const FString Prefix=LabelMode==3?TEXT("СИЛЫ ЗАКАНЧИВАЮТСЯ"):LabelMode==2?TEXT("ВОССТАНОВЛЕНИЕ")
            :bShowSwimEnergy?TEXT("ПЛАВАНИЕ"):TEXT("ВЫНОСЛИВОСТЬ");
        const FString Label=LabelMode==4?FString::Printf(TEXT("ТОНЕШЬ · ДО ГИБЕЛИ %.1f С"),DrowningTenths/10.f)
            :Prefix+FString::Printf(TEXT("  %d / %d"),DisplayCurrent,DisplayMaximum);
        StaminaValue->SetText(FText::FromString(Label));
        StaminaValue->SetColorAndOpacity(FSlateColor(bShowSwimEnergy && Exhausted?PlayerColor:FLinearColor(.98f,.97f,.93f)));
    }
    OnStaminaChanged(Current,Maximum,Normalized,bExhausted);
}
