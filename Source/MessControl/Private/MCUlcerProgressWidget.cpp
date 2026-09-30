#include "MCUlcerProgressWidget.h"
#include "MCMouthSurface.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"

int32 UMCUlcerProgressWidget::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& CullingRect,FSlateWindowElementList& E,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    const auto* Patch=Source.Get(); if(!Patch || !Patch->bUlcer) return Layer;
    const FVector2D Center=G.GetLocalSize()*.5;
    const float Radius=FMath::Min(Center.X,Center.Y)-RingThickness;
    const FSlateRoundedBoxBrush Back(FLinearColor::White,Radius);
    FSlateDrawElement::MakeBox(E,Layer,G.ToPaintGeometry(FVector2D(Radius*2),FSlateLayoutTransform(Center-FVector2D(Radius))),&Back,ESlateDrawEffect::None,FLinearColor(.015f,.025f,.04f,.92f));
    auto Arc=[&](float Value,FLinearColor Color,int32 L) {
        TArray<FVector2D> Points; const int32 Steps=FMath::Max(1,FMath::CeilToInt(Value*96));
        for(int32 I=0;I<=Steps;++I) { const float A=-PI*.5f+2*PI*Value*I/Steps; Points.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius); }
        FSlateDrawElement::MakeLines(E,L,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,RingThickness);
    };
    Arc(1,FLinearColor(.25f,.32f,.38f,.85f),Layer+1);
    if(Patch->Healing>0) Arc(FMath::Clamp(Patch->Healing,0.f,1.f),ProgressColor,Layer+2);
    const FString Text=FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Patch->Healing*100));
    const auto Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18);
    const FVector2D Size=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text,Font);
    FSlateDrawElement::MakeText(E,Layer+3,G.ToPaintGeometry(Size,FSlateLayoutTransform(Center-Size*.5)),Text,Font,ESlateDrawEffect::None,FLinearColor::White);
    return Layer+3;
}
