#pragma once

#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

/** Ordinary editable UMG shapes. No textures, shader variants or dynamic materials are allocated. */
namespace MCWorldUIShapes
{
inline UMCUniversalUIWidget* Panel(UWidgetTree* Tree,FName Name,UMCGameplayUITheme* NewTheme,
    EMCUIColorToken Fill,FMargin ContentInset,float Radius=12.f)
{
    auto* Shape=Tree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Shape->Theme=NewTheme;Shape->Kind=EMCUniversalUIKind::Panel;Shape->Label=FText::GetEmpty();
    Shape->Fill=Fill;Shape->bUseThemeShape=false;Shape->CornerRadius=Radius;Shape->OutlineWidth=2.f;
    Shape->ShadowOffset=FVector2D(0,3);Shape->ContentPadding=ContentInset;
    Shape->SetIsFocusable(false);Shape->Initialize();Shape->BuildPresentationTree();
    Shape->SetVisibility(ESlateVisibility::HitTestInvisible);return Shape;
}
inline UTextBlock* Text(UWidgetTree* Tree,FName Name,float BoundedWidth)
{
    auto* Caption=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Caption->SetJustification(ETextJustify::Center);Caption->SetAutoWrapText(true);Caption->SetWrapTextAt(BoundedWidth);
    Caption->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    Caption->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);return Caption;
}
/** UI-copy preparation only. No source widget/actor is constructed or changed. */
inline bool PrimeText(const FSlateFontInfo& ActualFont,const TArray<FText>& KnownCopy,float ActualScale)
{
    if(!ActualFont.HasValidFont() || !FSlateApplication::IsInitialized()) return false;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return false;
    const auto Measure=Renderer->GetFontMeasureService();
    const float FontScale=FMath::Max(.001f,ActualScale);
    // This finite baseline is also used by late-created widgets at their actual paint scale.
    Measure->Measure(FText::AsCultureInvariant(TEXT("0123456789 %.,/+\u00A0\u202F\u2191\u00AB\u00BB")),ActualFont,FontScale);
    for(const FText& Copy:KnownCopy) Measure->Measure(Copy,ActualFont,FontScale);
    return true;
}
inline void TextStyle(UTextBlock* Caption,const UMCGameplayUITheme* NewTheme,int32 FontSize,EMCUIColorToken Ink)
{
    if(!Caption || !NewTheme) return;
    Caption->SetFont(NewTheme->MakeFont(FontSize));Caption->SetColorAndOpacity(NewTheme->GetColor(Ink));
    // Covers the actual styled size/scale even for a dynamically created late source view.
    // Future localized captions are warmed by the existing location UI barrier below.
    PrimeText(Caption->GetFont(),{},FMath::Max(.001f,Caption->GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale()));
}
inline void ProgressStyle(UProgressBar* Bar,const UMCGameplayUITheme* NewTheme,FLinearColor Fill)
{
    if(!Bar || !NewTheme) return;
    FProgressBarStyle ShapeStyle;
    ShapeStyle.SetBackgroundImage(FSlateRoundedBoxBrush(NewTheme->Palette.LCDBackground,5.f,NewTheme->Palette.Border,1.5f));
    ShapeStyle.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,4.f));ShapeStyle.SetMarqueeImage(FSlateNoResource());
    Bar->SetWidgetStyle(ShapeStyle);Bar->SetBarFillStyle(EProgressBarFillStyle::Scale);Bar->SetFillColorAndOpacity(Fill);
}
inline USizeBox* Bounds(UWidgetTree* Tree,FName Name,float Width,float MinHeight=0.f)
{
    auto* Size=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);
    Size->SetWidthOverride(Width);Size->SetMinDesiredHeight(MinHeight);return Size;
}
}
