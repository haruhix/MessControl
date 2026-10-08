#include "MCFreezeBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

TSharedRef<SWidget> UMCFreezeBarWidget::RebuildWidget()
{
    // Match the base class's rebuild recovery before authoring the native tree.
    Initialize();
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    BuildDefaultWidgetTree();
    TSharedRef<SWidget> Widget = Super::RebuildWidget();
    ApplyFreeze();
    return Widget;
}

void UMCFreezeBarWidget::BuildDefaultWidgetTree()
{
    // Blueprint subclasses can supply a tree with the optional named controls.
    if (!WidgetTree || WidgetTree->RootWidget)
    {
        return;
    }

    USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>();
    Root->SetWidthOverride(220.f);
    Root->SetHeightOverride(52.f);
    WidgetTree->RootWidget = Root;

    UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 8.f));
    Background->SetBrushColor(FLinearColor(.012f, .027f, .04f, .94f));
    Background->SetPadding(FMargin(10.f, 6.f));
    Root->SetContent(Background);

    UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
    Background->SetContent(Box);

    FreezeCaption = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FreezeCaption"));
    FreezeCaption->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13));
    FreezeCaption->SetJustification(ETextJustify::Center);
    FreezeCaption->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .8f));
    FreezeCaption->SetShadowOffset(FVector2D(1.f, 1.f));
    Box->AddChildToVerticalBox(FreezeCaption)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

    USizeBox* BarHeight = WidgetTree->ConstructWidget<USizeBox>();
    BarHeight->SetHeightOverride(12.f);
    Box->AddChildToVerticalBox(BarHeight);

    FreezeProgress = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("FreezeProgress"));
    FProgressBarStyle Style;
    Style.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(.045f, .095f, .13f), 4.f));
    Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.f));
    Style.SetMarqueeImage(FSlateNoResource());
    FreezeProgress->SetWidgetStyle(Style);
    FreezeProgress->SetBarFillType(EProgressBarFillType::LeftToRight);
    FreezeProgress->SetBarFillStyle(EProgressBarFillStyle::Scale);
    BarHeight->SetContent(FreezeProgress);
}

void UMCFreezeBarWidget::SetFreeze(float Amount, bool bSafe)
{
    FreezeAmount = FMath::IsFinite(Amount) ? FMath::Clamp(Amount, 0.f, 1.f) : 0.f;
    bInSafeZone = bSafe;
    ApplyFreeze();
}

void UMCFreezeBarWidget::ApplyFreeze()
{
    const FLinearColor Cold(.18f, .82f, 1.f);
    const FLinearColor Warning(1.f, .69f, .20f);
    const FLinearColor Danger(1.f, .14f, .12f);
    const FLinearColor FillColor = FreezeAmount <= .7f
        ? FMath::Lerp(Cold, Warning, FreezeAmount / .7f)
        : FMath::Lerp(Warning, Danger, (FreezeAmount - .7f) / .3f);

    if (FreezeProgress)
    {
        FreezeProgress->SetPercent(FreezeAmount);
        FreezeProgress->SetFillColorAndOpacity(FillColor);
    }

    if (FreezeCaption)
    {
        FreezeCaption->SetText(FText::Format(
            bInSafeZone
                ? NSLOCTEXT("IceEvent", "FreezeWarmingCaption", "Заморозка {0}% · Отогрев")
                : NSLOCTEXT("IceEvent", "FreezeCaption", "Заморозка {0}%"),
            FText::AsNumber(FMath::RoundToInt(FreezeAmount * 100.f))));
        FreezeCaption->SetColorAndOpacity(FSlateColor(bInSafeZone
            ? FLinearColor(.40f, 1.f, .76f)
            : FreezeAmount >= .8f ? Danger : FLinearColor(.88f, .96f, 1.f)));
    }
}
