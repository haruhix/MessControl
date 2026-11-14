#include "MCComposerStyle.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

namespace MCComposerStyle
{
    FLinearColor UIColor(const TCHAR* Hex)
    {
        return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
    }

    const FLinearColor WorkspaceMint = UIColor(TEXT("#8FE3CF"));
    const FLinearColor Ink = UIColor(TEXT("#123136"));
    const FLinearColor Surface = UIColor(TEXT("#1C2A36"));
    const FLinearColor Muted = UIColor(TEXT("#9EB3C3"));
    const FLinearColor AppFill = UIColor(TEXT("#0F141B"));
    const FLinearColor GraphFill = UIColor(TEXT("#151C25"));
    const FLinearColor PanelFill = UIColor(TEXT("#171F2A"));
    const FLinearColor HeadingFill = UIColor(TEXT("#1D2835"));
    const FLinearColor ButtonFill = UIColor(TEXT("#253443"));
    const FLinearColor ButtonHover = UIColor(TEXT("#314859"));
    const FLinearColor BorderFill = UIColor(TEXT("#324454"));
    const FLinearColor TextColor = UIColor(TEXT("#EFF4F8"));

    const FSlateRoundedBoxBrush& PanelBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 7.f);
        return Brush;
    }

    const FSlateRoundedBoxBrush& SelectedPanelBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 7.f, WorkspaceMint, 1.2f);
        return Brush;
    }

    const FButtonStyle& SolidStyle()
    {
        static const FButtonStyle Style = FButtonStyle()
            .SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 6.f))
            .SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.08f, 1.08f, 1.08f), 6.f))
            .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.87f, .87f, .87f), 6.f))
            .SetDisabled(FSlateRoundedBoxBrush(FLinearColor::White, 6.f))
            .SetDisabledForeground(Muted)
            .SetNormalPadding(FMargin(0))
            .SetPressedPadding(FMargin(0));
        return Style;
    }

    const FButtonStyle& NeutralStyle()
    {
        static const FButtonStyle Style = FButtonStyle()
            .SetNormal(FSlateRoundedBoxBrush(ButtonFill, 6.f))
            .SetHovered(FSlateRoundedBoxBrush(ButtonHover, 6.f))
            .SetPressed(FSlateRoundedBoxBrush(HeadingFill, 6.f))
            .SetDisabled(FSlateRoundedBoxBrush(HeadingFill, 6.f))
            .SetNormalForeground(TextColor)
            .SetHoveredForeground(TextColor)
            .SetPressedForeground(TextColor)
            .SetDisabledForeground(Muted)
            .SetNormalPadding(FMargin(0))
            .SetPressedPadding(FMargin(0));
        return Style;
    }

    TSharedRef<SWidget> Label(const FString& Value, int32 Size, FLinearColor Color)
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Value))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size))
            .ColorAndOpacity(Color)
            .AutoWrapText(true);
    }

    TSharedRef<SWidget> Heading(const FString& Text)
    {
        return SNew(SBorder)
            .Padding(10, 7)
            .BorderImage(&PanelBrush())
            .BorderBackgroundColor(HeadingFill)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Text))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))
                .ColorAndOpacity(TextColor)
            ];
    }

    TSharedRef<SWidget> Command(const FString& Text, TFunction<void()> Action, bool Primary)
    {
        return SNew(SButton)
            .ButtonStyle(Primary ? &SolidStyle() : &NeutralStyle())
            .ContentPadding(FMargin(12, 9))
            .ButtonColorAndOpacity(Primary ? WorkspaceMint : FLinearColor::White)
            .ForegroundColor(Primary ? Ink : TextColor)
            .OnClicked_Lambda([Action = MoveTemp(Action)] { Action(); return FReply::Handled(); })
            [
                SNew(STextBlock)
                .Text(FText::FromString(Text))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))
                .AutoWrapText(true)
            ];
    }

    TSharedRef<SWidget> SmallCommand(const FString& Text, TFunction<void()> Action)
    {
        return SNew(SButton)
            .ButtonStyle(&NeutralStyle())
            .ContentPadding(FMargin(7, 4))
            .ButtonColorAndOpacity(FLinearColor::White)
            .ForegroundColor(TextColor)
            .OnClicked_Lambda([Action = MoveTemp(Action)] { Action(); return FReply::Handled(); })
            [
                SNew(STextBlock)
                .Text(FText::FromString(Text))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
            ];
    }
}
