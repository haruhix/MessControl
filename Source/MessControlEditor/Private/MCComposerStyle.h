#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class SWidget;

/** Shared private Slate presentation for the SFX and VFX Composer workspaces. */
namespace MCComposerStyle
{
    FLinearColor UIColor(const TCHAR* Hex);

    extern const FLinearColor WorkspaceMint;
    extern const FLinearColor Ink;
    extern const FLinearColor Surface;
    extern const FLinearColor Muted;
    extern const FLinearColor AppFill;
    extern const FLinearColor GraphFill;
    extern const FLinearColor PanelFill;
    extern const FLinearColor HeadingFill;
    extern const FLinearColor ButtonFill;
    extern const FLinearColor ButtonHover;
    extern const FLinearColor BorderFill;
    extern const FLinearColor TextColor;

    const FSlateRoundedBoxBrush& PanelBrush();
    const FSlateRoundedBoxBrush& SelectedPanelBrush();
    const FButtonStyle& SolidStyle();
    const FButtonStyle& NeutralStyle();

    TSharedRef<SWidget> Label(const FString& Value, int32 Size = 12, FLinearColor Color = TextColor);
    TSharedRef<SWidget> Heading(const FString& Text);
    TSharedRef<SWidget> Command(const FString& Text, TFunction<void()> Action, bool Primary = false);
    TSharedRef<SWidget> SmallCommand(const FString& Text, TFunction<void()> Action);
}
