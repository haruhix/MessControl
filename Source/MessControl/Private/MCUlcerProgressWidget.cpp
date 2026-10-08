#include "MCUlcerProgressWidget.h"
#include "MCMouthSurface.h"
#include "MCToothpick.h"
#include "Engine/World.h"
#include <initializer_list>
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"

int32 UMCUlcerProgressWidget::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& CullingRect,FSlateWindowElementList& E,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    const auto* Pick=ToothpickSource.Get(); const auto* Patch=Source.Get();
    if(Pick?Pick->IsBroken():(!Patch || !Patch->bUlcer || Patch->bTreatmentBlocked || Patch->IsHealed())) return Layer;
    const bool Pull=Pick && Pick->State==EMCToothpickState::Impaled;
    const float Value=Pick?(Pull?Pick->PullProgress:0.f):Patch->Healing;
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
    if(Value>0) Arc(FMath::Clamp(Value,0.f,1.f),ProgressColor,Layer+2);
    const float Pulse=.75f+.25f*FMath::Sin(GetWorld()->GetTimeSeconds()*5.f);
    const float Scale=1.f+.10f*Pulse;
    const FLinearColor IconColor=(Pick?FLinearColor(1,.77f,.30f):ProgressColor).CopyWithNewOpacity(Pulse);
    auto Stroke=[&](std::initializer_list<FVector2D> Shape) {
        TArray<FVector2D> Points; for(const auto& Point:Shape) Points.Add(Center+FVector2D(0,-9)+Point*Scale);
        FSlateDrawElement::MakeLines(E,Layer+3,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,IconColor,true,3.f);
    };
    if(Pull) {Stroke({FVector2D(0,10),FVector2D(0,-12)});Stroke({FVector2D(-8,-4),FVector2D(0,-12),FVector2D(8,-4)});Stroke({FVector2D(-9,10),FVector2D(9,10)});}
    else if(Pick) {Stroke({FVector2D(-7,12),FVector2D(5,-9)});Stroke({FVector2D(-10,-3),FVector2D(0,-11),FVector2D(12,-7)});}
    else {Stroke({FVector2D(-7,12),FVector2D(-7,-3),FVector2D(6,-3),FVector2D(6,12),FVector2D(-7,12)});Stroke({FVector2D(-2,-3),FVector2D(-2,-9),FVector2D(7,-9)});Stroke({FVector2D(10,-9),FVector2D(15,-12)});}
    const FString Text=Value>0?FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Value*100)):Pull?TEXT("E"):Pick?TEXT("2"):TEXT("4");
    const auto Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14);
    const FVector2D Size=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text,Font);
    FSlateDrawElement::MakeText(E,Layer+4,G.ToPaintGeometry(Size,FSlateLayoutTransform(Center+FVector2D(0,16)-Size*.5)),Text,Font,ESlateDrawEffect::None,FLinearColor::White);
    return Layer+4;
}
