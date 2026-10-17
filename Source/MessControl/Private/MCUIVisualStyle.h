#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

class UWidget;
class UWidgetTree;
class UBorder;
class UImage;
class UProgressBar;
class UButton;
class UMCGameplayUITheme;

/** Shared procedural brushes for phase-one gameplay UI. No screen art or baked text. */
namespace MCUIVisualStyle
{
    FVector2D SizeFor(const UWidget* Widget,FVector2D Fallback=FVector2D(320,64));
    FSlateBrush Surface(UObject* Owner,const FSlateBrush& Previous,const UMCGameplayUITheme* Theme,
        FLinearColor Color,FVector2D Size,float Radius=8.f,bool bFill=false,float Segments=0.f,
        bool bDiamond=false,bool bEmphasized=false);
    void Panel(UBorder* Widget,const UMCGameplayUITheme* Theme,FLinearColor Color);
    void Image(UImage* Widget,const UMCGameplayUITheme* Theme,FLinearColor Color,bool bDiamond=false,bool bEmphasized=false);
    void Progress(UProgressBar* Widget,const UMCGameplayUITheme* Theme,FLinearColor Color,float Segments=0.f);
    void Button(UButton* Widget,const UMCGameplayUITheme* Theme);
    void Button(UButton* Widget,const UMCGameplayUITheme* Theme,FLinearColor NormalColor);
    void RefreshGeometry(UWidgetTree* Tree);
}
