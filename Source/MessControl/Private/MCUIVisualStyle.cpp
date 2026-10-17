#include "MCUIVisualStyle.h"

#include "MCGameplayUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"

namespace MCUIVisualStyle
{
FVector2D SizeFor(const UWidget* Widget,FVector2D Fallback)
{
    if(!Widget) return Fallback;
    FVector2D Size=Widget->GetCachedGeometry().GetLocalSize();
    if(Size.X>0 && Size.Y>0) return Size;
    if(const auto* Canvas=Cast<UCanvasPanelSlot>(Widget->Slot))
    {
        Size=Canvas->GetSize();
        if(Size.X>0 && Size.Y>0) return Size;
    }
    if(const auto* Box=Cast<USizeBox>(Widget->GetParent()))
    {
        if(Box->GetWidthOverride()>1) Fallback.X=Box->GetWidthOverride();
        if(Box->GetHeightOverride()>1) Fallback.Y=Box->GetHeightOverride();
    }
    return Fallback;
}

FSlateBrush Surface(UObject* Owner,const FSlateBrush& Previous,const UMCGameplayUITheme* Theme,
    FLinearColor Color,FVector2D Size,float Radius,bool bFill,float Segments,bool bDiamond,bool bEmphasized)
{
    if(!Theme) return FSlateRoundedBoxBrush(Color,Radius);
    const TCHAR* Path=bDiamond?TEXT("/Game/Gameplay/UI/Materials/M_UI_Phase1Tool.M_UI_Phase1Tool"):
        bFill?TEXT("/Game/Gameplay/UI/Materials/M_UI_Phase1Fill.M_UI_Phase1Fill"):
        TEXT("/Game/Gameplay/UI/Materials/M_UI_Phase1Panel.M_UI_Phase1Panel");
    auto* Material=LoadObject<UMaterialInterface>(nullptr,Path,nullptr,LOAD_NoWarn);
    if(!Material) return FSlateRoundedBoxBrush(Color,Radius,Theme->Palette.Border,Theme->OutlineWidth);
    auto* MID=Cast<UMaterialInstanceDynamic>(Previous.GetResourceObject());
    if(!MID || MID->GetMaterial()!=Material->GetMaterial()) MID=UMaterialInstanceDynamic::Create(Material,Owner);
    const auto& P=Theme->Palette;
    const bool Light=Color.GetLuminance()>.35f;
    const FLinearColor Edge=bEmphasized?P.Focus:Light?P.Border:P.Accent;
    MID->SetVectorParameterValue(TEXT("SurfaceColor"),Color);
    MID->SetVectorParameterValue(TEXT("EdgeColor"),Edge.CopyWithNewOpacity(Edge.A*Color.A));
    MID->SetVectorParameterValue(TEXT("HighlightColor"),P.Cream.CopyWithNewOpacity((Light?.26f:.32f)*Color.A*P.Cream.A));
    MID->SetVectorParameterValue(TEXT("PanelSize"),FLinearColor(FMath::Max(1.,Size.X),FMath::Max(1.,Size.Y),0,0));
    MID->SetScalarParameterValue(TEXT("Radius"),Radius);
    MID->SetScalarParameterValue(TEXT("BorderWidth"),bFill?.6f:bEmphasized?2.2f:Theme->OutlineWidth);
    MID->SetScalarParameterValue(TEXT("Glow"),bFill?.08f:bEmphasized?1.f:Light?0.f:.18f);
    MID->SetScalarParameterValue(TEXT("Segments"),Segments);
    MID->SetScalarParameterValue(TEXT("Shape"),bDiamond?1.f:0.f);
    MID->SetScalarParameterValue(TEXT("Selected"),bEmphasized?1.f:0.f);
    FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageSize=Size;
    Brush.TintColor=FLinearColor::White;Brush.SetResourceObject(MID);
    return Brush;
}

void Panel(UBorder* Widget,const UMCGameplayUITheme* Theme,FLinearColor Color)
{
    if(!Widget || !Theme) return;
    Widget->SetBrush(Surface(Widget,Widget->Background,Theme,Color,SizeFor(Widget),Theme->CornerRadius));
    Widget->SetBrushColor(FLinearColor::White);
}

void Image(UImage* Widget,const UMCGameplayUITheme* Theme,FLinearColor Color,bool bDiamond,bool bEmphasized)
{
    if(!Widget || !Theme) return;
    const auto Size=SizeFor(Widget);
    if(Size.X<8 || Size.Y<8) {Widget->SetBrush(FSlateColorBrush(FLinearColor::White));Widget->SetColorAndOpacity(Color);return;}
    Widget->SetBrush(Surface(Widget,Widget->GetBrush(),Theme,Color,Size,Theme->CornerRadius,false,0,bDiamond,bEmphasized));
    Widget->SetColorAndOpacity(FLinearColor::White);
}

void Progress(UProgressBar* Widget,const UMCGameplayUITheme* Theme,FLinearColor Color,float Segments)
{
    if(!Widget || !Theme) return;
    const auto Size=SizeFor(Widget,FVector2D(240,12));const auto& P=Theme->Palette;
    auto Style=Widget->GetWidgetStyle();
    Style.SetBackgroundImage(Surface(Widget,Style.BackgroundImage,Theme,P.Ink.CopyWithNewOpacity(Color.A),Size,4,true,Segments));
    Style.SetFillImage(Surface(Widget,Style.FillImage,Theme,Color,Size,4,true,Segments));
    FSlateBrush Marquee=FSlateNoResource();Marquee.ImageSize=Size;Style.SetMarqueeImage(Marquee);
    Widget->SetWidgetStyle(Style);Widget->SetBarFillStyle(EProgressBarFillStyle::Mask);
    Widget->SetFillColorAndOpacity(FLinearColor::White);
}

void Button(UButton* Widget,const UMCGameplayUITheme* Theme)
{
    if(Theme) Button(Widget,Theme,Theme->Palette.Button);
}

void Button(UButton* Widget,const UMCGameplayUITheme* Theme,FLinearColor NormalColor)
{
    if(!Widget || !Theme) return;
    const auto Size=SizeFor(Widget,FVector2D(320,54));const auto& P=Theme->Palette;
    auto Style=Widget->GetStyle();
    Style.SetNormal(Surface(Widget,Style.Normal,Theme,NormalColor,Size,Theme->CornerRadius));
    Style.SetHovered(Surface(Widget,Style.Hovered,Theme,P.ButtonHover,Size,Theme->CornerRadius,false,0,false,true));
    Style.SetPressed(Surface(Widget,Style.Pressed,Theme,P.ButtonPressed,Size,Theme->CornerRadius,false,0,false,true));
    Style.SetDisabled(Surface(Widget,Style.Disabled,Theme,P.ButtonDisabled,Size,Theme->CornerRadius));
    Widget->SetStyle(Style);Widget->SetBackgroundColor(FLinearColor::White);
}

void RefreshGeometry(UWidgetTree* Tree)
{
    if(!Tree) return;
    Tree->ForEachWidgetAndDescendants([](UWidget* Widget)
    {
        const auto Size=Widget->GetCachedGeometry().GetLocalSize();if(Size.X<=1 || Size.Y<=1) return;
        auto Update=[&](const FSlateBrush& Brush)
        {
            auto* MID=Cast<UMaterialInstanceDynamic>(Brush.GetResourceObject());
            if(!MID || !MID->GetMaterial()->GetName().StartsWith(TEXT("M_UI_Phase1"))) return;
            FLinearColor Old;
            if(MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("PanelSize")),Old)
                && FMath::IsNearlyEqual(Old.R,Size.X,.5) && FMath::IsNearlyEqual(Old.G,Size.Y,.5)) return;
            MID->SetVectorParameterValue(TEXT("PanelSize"),FLinearColor(Size.X,Size.Y,0,0));
        };
        if(auto* Border=Cast<UBorder>(Widget)) Update(Border->Background);
        if(auto* ImageWidget=Cast<UImage>(Widget)) Update(ImageWidget->GetBrush());
        if(auto* ProgressWidget=Cast<UProgressBar>(Widget)) {Update(ProgressWidget->GetWidgetStyle().FillImage);Update(ProgressWidget->GetWidgetStyle().BackgroundImage);}
        if(auto* ButtonWidget=Cast<UButton>(Widget)) {const auto& S=ButtonWidget->GetStyle();Update(S.Normal);Update(S.Hovered);Update(S.Pressed);Update(S.Disabled);}
    });
}
}
