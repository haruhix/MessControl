#include "MCGameplayAlertWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace MCGameplayAlertPrivate
{
    constexpr float EnterSeconds=.28f;
    constexpr float ExitSeconds=.24f;
    constexpr int32 MaxPending=8;
    constexpr double MaxQueueAge=20.;

    TArray<FString> WrapMessage(FString Message,const FSlateFontInfo& Font,float Width)
    {
        Message.ReplaceInline(TEXT("\r"),TEXT(" ")); Message.ReplaceInline(TEXT("\n"),TEXT(" "));
        TArray<FString> Words,Lines;
        Message.ParseIntoArray(Words,TEXT(" "),true);
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        FString Line;
        for(int32 I=0;I<Words.Num();++I) {
            const auto& Word=Words[I];
            const FString Candidate=Line.IsEmpty()?Word:Line+TEXT(" ")+Word;
            if(!Line.IsEmpty() && Measure->Measure(Candidate,Font).X>Width) {
                Lines.Add(Line); Line=Word;
                if(Lines.Num()==2) {
                    for(++I;I<Words.Num();++I) Line+=TEXT(" ")+Words[I];
                    break;
                }
            }
            else Line=Candidate;
        }
        if(!Line.IsEmpty()) Lines.Add(Line);
        // User-supplied messages are bounded and never draw outside the warning band.
        for(auto& Value:Lines) if(Measure->Measure(Value,Font).X>Width) {
            while(Value.Len()>1 && Measure->Measure(Value+TEXT("…"),Font).X>Width) Value.LeftChopInline(1);
            Value+=TEXT("…");
        }
        return Lines;
    }
}

void UMCGameplayAlertWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false); ForceVolatile(true);
    if(WidgetTree && !WidgetTree->RootWidget) WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>();
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMCGameplayAlertWidget::QueueAlert(const FText& Message,float HoldSeconds,FName EventId)
{
    FString Clean=Message.ToString().TrimStartAndEnd();
    if(Clean.IsEmpty() || !FMath::IsFinite(HoldSeconds) || !GetWorld()) return;
    Clean=Clean.Left(180);
    if(EventId!=NAME_None && ((bPresenting && Current.EventId==EventId)
        || Pending.ContainsByPredicate([EventId](const FAlert& Alert){return Alert.EventId==EventId;}))) return;
    FAlert Alert;
    Alert.Message=FText::FromString(Clean); Alert.EventId=EventId;
    // Reading time scales with the message, with a bounded hold even for long custom text.
    Alert.HoldSeconds=FMath::Clamp(FMath::Max(HoldSeconds,1.8f+Clean.Len()*.035f),2.5f,7.f);
    Alert.QueuedAt=GetWorld()->GetTimeSeconds();
    if(Pending.Num()>=MCGameplayAlertPrivate::MaxPending) Pending.RemoveAt(0);
    Pending.Add(MoveTemp(Alert));
    if(!bPresenting) PresentNext();
}

void UMCGameplayAlertWidget::ClearAlerts()
{
    Pending.Reset(); Current=FAlert(); Elapsed=0; bPresenting=false;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMCGameplayAlertWidget::PresentNext()
{
    const double Now=GetWorld()?GetWorld()->GetTimeSeconds():0.;
    while(!Pending.IsEmpty()) {
        Current=MoveTemp(Pending[0]); Pending.RemoveAt(0);
        if(Now-Current.QueuedAt>MCGameplayAlertPrivate::MaxQueueAge) continue;
        bPresenting=true; Elapsed=0; SetVisibility(ESlateVisibility::HitTestInvisible);
        return;
    }
    ClearAlerts();
}

void UMCGameplayAlertWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(!bPresenting) return;
    Elapsed+=FMath::Max(0.f,DeltaSeconds);
    if(Elapsed>=MCGameplayAlertPrivate::EnterSeconds+Current.HoldSeconds+MCGameplayAlertPrivate::ExitSeconds) PresentNext();
}

int32 UMCGameplayAlertWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    const int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    if(!bPresenting || !FSlateApplication::IsInitialized()) return Top;
    using namespace MCGameplayAlertPrivate;
    const FVector2D Size=Geometry.GetLocalSize();
    if(Size.X<100 || Size.Y<100) return Top;
    const float Scale=FMath::Clamp(float(FMath::Min(Size.X/1920.,Size.Y/1080.)),.55f,1.5f);
    const float Enter=FMath::Clamp(Elapsed/EnterSeconds,0.f,1.f);
    const float Exit=FMath::Clamp((Elapsed-EnterSeconds-Current.HoldSeconds)/ExitSeconds,0.f,1.f);
    const float Reveal=FMath::SmoothStep(0.f,1.f,Enter)*(1.f-FMath::SmoothStep(0.f,1.f,Exit));
    const float Opacity=Reveal*Style.GetColorAndOpacityTint().A;
    const float Width=FMath::Min(float(Size.X)*.78f,1120.f*Scale);
    const auto Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),FMath::Max(14,FMath::RoundToInt(27.f*Scale)));
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const auto Lines=WrapMessage(Current.Message.ToString(),Font,Width-100.f*Scale);
    const float LineHeight=32.f*Scale;
    const float Height=FMath::Max(58.f*Scale,Lines.Num()*LineHeight+24.f*Scale);
    // Near the centre, above the aim point; the warning remains readable during movement.
    const float Y=float(Size.Y)*.39f-(1.f-Reveal)*24.f*Scale;
    const FLinearColor Red(.78f,.022f,.043f,.94f*Opacity),Bright(1.f,.08f,.1f,Opacity),White(1.f,.97f,.95f,Opacity);
    auto Box=[&](float X,float TopY,float W,float H,FLinearColor Color) {
        const FSlateRoundedBoxBrush Brush(FLinearColor::White,0);
        FSlateDrawElement::MakeBox(Elements,Top+1,Geometry.ToPaintGeometry(FVector2D(W,H),
            FSlateLayoutTransform(FVector2D(X,TopY))),&Brush,ESlateDrawEffect::None,Color);
    };
    auto Line=[&](TArray<FVector2D> Points,FLinearColor Color,float Thickness=2.f) {
        FSlateDrawElement::MakeLines(Elements,Top+2,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness*Scale);
    };
    const float BandWidth=Width*(.72f+.28f*Reveal),BandLeft=(float(Size.X)-BandWidth)*.5f;
    Box(BandLeft,Y,BandWidth,Height,Red);
    Box(BandLeft,Y-12.f*Scale,BandWidth,2.f*Scale,Bright);
    Box(BandLeft,Y+Height+10.f*Scale,BandWidth,2.f*Scale,Bright);
    const float StripeHeight=9.f*Scale,Step=22.f*Scale;
    const float Offset=FMath::Fmod(Elapsed*18.f*Scale,Step);
    for(float X=BandLeft+Offset;X<BandLeft+BandWidth-StripeHeight;X+=Step) {
        Line({FVector2D(X,Y-10.f*Scale),FVector2D(X+StripeHeight,Y-StripeHeight-10.f*Scale)},Bright,4);
        Line({FVector2D(X,Y+Height+19.f*Scale),FVector2D(X+StripeHeight,Y+Height+10.f*Scale)},Bright,4);
    }
    if(Reveal>.7f) for(int32 I=0;I<Lines.Num();++I) {
        const FVector2D Extent=Measure->Measure(Lines[I],Font);
        const FVector2D Position((Size.X-Extent.X)*.5f,Y+(Height-Lines.Num()*LineHeight)*.5f+I*LineHeight);
        FSlateDrawElement::MakeText(Elements,Top+3,Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(Position)),
            Lines[I],Font,ESlateDrawEffect::None,White);
    }
    const FVector2D Icon(Size.X*.5f,Y+Height+51.f*Scale);
    const float Radius=22.f*Scale;
    Line({Icon+FVector2D(0,-Radius),Icon+FVector2D(Radius,0),Icon+FVector2D(0,Radius),
        Icon+FVector2D(-Radius,0),Icon+FVector2D(0,-Radius)},Bright,3);
    Line({Icon+FVector2D(0,-10.f*Scale),Icon+FVector2D(0,3.f*Scale)},White,4);
    Box(float(Icon.X)-2.f*Scale,float(Icon.Y)+8.f*Scale,4.f*Scale,4.f*Scale,White);
    return Top+3;
}
