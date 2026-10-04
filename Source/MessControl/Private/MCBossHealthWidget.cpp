#include "MCBossHealthWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

void UMCBossHealthWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget=Root;
    HealthPanel=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* PanelSlot=Root->AddChildToCanvas(HealthPanel);
    PanelSlot->SetAnchors(FAnchors(.16f,.73f,.84f,.73f));
    PanelSlot->SetOffsets(FMargin(0,0,0,70));
    auto* Name=WidgetTree->ConstructWidget<UTextBlock>();
    Name->SetText(NSLOCTEXT("Boss", "ZombieName", "Гнилой страж"));
    Name->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),22));
    Name->SetColorAndOpacity(FSlateColor(FLinearColor(.88f,.84f,.72f)));
    Name->SetShadowColorAndOpacity(FLinearColor(0,0,0,.9f));
    Name->SetShadowOffset(FVector2D(1,2));
    HealthPanel->AddChildToVerticalBox(Name)->SetPadding(FMargin(1,0,0,10));
    auto* Frame=WidgetTree->ConstructWidget<UBorder>();
    Frame->SetBrushColor(FLinearColor(.30f,.26f,.19f,.94f));
    Frame->SetPadding(FMargin(2));
    auto* Height=WidgetTree->ConstructWidget<USizeBox>();
    Height->SetHeightOverride(14);
    auto* Layers=WidgetTree->ConstructWidget<UOverlay>();
    Height->SetContent(Layers); Frame->SetContent(Height);
    HealthPanel->AddChildToVerticalBox(Frame);
    FProgressBarStyle Style;
    Style.SetBackgroundImage(FSlateColorBrush(FLinearColor(.025f,.016f,.015f,.95f)));
    Style.SetFillImage(FSlateColorBrush(FLinearColor::White));
    Style.SetMarqueeImage(FSlateNoResource());
    LossBar=WidgetTree->ConstructWidget<UProgressBar>();
    LossBar->SetWidgetStyle(Style); LossBar->SetFillColorAndOpacity(FLinearColor(.63f,.48f,.26f));
    auto* LossSlot=Layers->AddChildToOverlay(LossBar);
    LossSlot->SetHorizontalAlignment(HAlign_Fill); LossSlot->SetVerticalAlignment(VAlign_Fill);
    Style.SetBackgroundImage(FSlateNoResource());
    CurrentBar=WidgetTree->ConstructWidget<UProgressBar>();
    CurrentBar->SetWidgetStyle(Style); CurrentBar->SetFillColorAndOpacity(FLinearColor(.48f,.055f,.045f));
    auto* CurrentSlot=Layers->AddChildToOverlay(CurrentBar);
    CurrentSlot->SetHorizontalAlignment(HAlign_Fill); CurrentSlot->SetVerticalAlignment(VAlign_Fill);
    for (int32 Index=0;Index<2;++Index)
    {
        auto* Bar=WidgetTree->ConstructWidget<UBorder>();
        Bar->SetBrushColor(FLinearColor::Black);
        auto* BarSlot=Root->AddChildToCanvas(Bar);
        BarSlot->SetAnchors(Index==0?FAnchors(0,0,1,.12f):FAnchors(0,.88f,1,1));
        BarSlot->SetOffsets(FMargin(0)); BarSlot->SetZOrder(5);
        if (Index==0) TopBar=Bar; else BottomBar=Bar;
        Bar->SetVisibility(ESlateVisibility::Collapsed);
    }
    HealthPanel->SetVisibility(ESlateVisibility::Collapsed);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMCBossHealthWidget::ShowHealth(float Health,float MaxHealth,float DeltaSeconds)
{
    const float Fraction=FMath::Clamp(Health/FMath::Max(1.f,MaxHealth),0.f,1.f);
    if (!bShowingHealth || Fraction>LastFraction)
    { DelayedFraction=Fraction; LossHold=0; }
    else if (Fraction<LastFraction-KINDA_SMALL_NUMBER) LossHold=.45f;
    else if (LossHold>0) LossHold=FMath::Max(0.f,LossHold-DeltaSeconds);
    else DelayedFraction=FMath::FInterpConstantTo(DelayedFraction,Fraction,DeltaSeconds,.7f);
    LastFraction=Fraction;
    bShowingHealth=true;
    CurrentBar->SetPercent(Fraction);
    LossBar->SetPercent(FMath::Max(Fraction,DelayedFraction));
    HealthPanel->SetVisibility(bLetterbox?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}

void UMCBossHealthWidget::HideHealth()
{
    bShowingHealth=false;
    if (HealthPanel) HealthPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UMCBossHealthWidget::SetLetterbox(bool bShow)
{
    bLetterbox=bShow;
    TopBar->SetVisibility(bShow?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    BottomBar->SetVisibility(bShow?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if (bShow) HealthPanel->SetVisibility(ESlateVisibility::Collapsed);
}
