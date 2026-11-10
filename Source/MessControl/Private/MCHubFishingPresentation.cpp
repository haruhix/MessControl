#include "MCHubFishingPresentation.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Layout/Clipping.h"
#include "Layout/Geometry.h"
#include "MCFishingRodComponent.h"
#include "MCUIDataLibrary.h"
#include "Rendering/DrawElements.h"

namespace MCHubFishingPresentation
{
    namespace
    {
        struct FHintCopy
        {
            const TCHAR* Key;
            const TCHAR* Russian;
            const TCHAR* English;
        };
        constexpr FHintCopy Hints[]={
            {TEXT("hub.fishing.cast.charge"),TEXT("Зажми ЛКМ для дальности. Отпусти, чтобы забросить."),
                TEXT("Hold LMB to set cast distance. Release to cast.")},
            {TEXT("hub.fishing.cast.charge.gamepad"),TEXT("Зажми RT для дальности. Отпусти, чтобы забросить."),
                TEXT("Hold RT to set cast distance. Release to cast.")},
            {TEXT("hub.fishing.cast.wait"),TEXT("Ждём поклёвку…"),TEXT("Waiting for a bite…")},
            {TEXT("hub.fishing.challenge"),TEXT("Кликай ЛКМ, чтобы удержать крючок в зоне."),
                TEXT("Click LMB to keep the hook in the zone.")},
            {TEXT("hub.fishing.challenge.gamepad"),TEXT("Нажимай RT, чтобы удержать крючок в зоне."),
                TEXT("Tap RT to keep the hook in the zone.")},
            {TEXT("hub.fishing.cast.reel"),TEXT("Сматываем леску…"),TEXT("Reeling in the line…")},
        };
        static_assert(UE_ARRAY_COUNT(Hints)==static_cast<int32>(EHint::Count));

        // The meter is drawn with the same resource-free Slate boxes as the Hub panels.
        const FSlateRoundedBoxBrush Housing{FLinearColor::White,25.f};
        const FSlateRoundedBoxBrush HousingFace{FLinearColor::White,19.f};
        const FSlateRoundedBoxBrush TrackBorder{FLinearColor::White,13.f};
        const FSlateRoundedBoxBrush Track{FLinearColor::White,8.f};
        const FSlateRoundedBoxBrush Mouse{FLinearColor::White,21.f};
        const FSlateRoundedBoxBrush MouseFace{FLinearColor::White,18.f};
        const FSlateRoundedBoxBrush MouseButton{FLinearColor::White,FVector4(16,0,0,0)};
        const FSlateRoundedBoxBrush Progress{FLinearColor::White,2.f};
        const FLinearColor Blue{FColor(0x4C,0x76,0xAE)};
        const FLinearColor Ink{FColor(0x1A,0x43,0x4E)};
        const FLinearColor White{FColor(0xFD,0xFD,0xFD)};
        const FLinearColor Green{FColor(0x1E,0xB2,0x2F)};
        const FLinearColor Rose{FColor(0xAE,0x4C,0x6C)};
    }

    FText ResolveHint(UDataTable* Table,EHint Hint,const FString& Culture)
    {
        const auto& Copy=Hints[static_cast<int32>(Hint)];
        const bool bRussian=Culture.IsEmpty()
            ? FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName()==TEXT("ru")
            : Culture==TEXT("ru") || Culture.StartsWith(TEXT("ru-"));
        return UMCUIDataLibrary::ResolveText(Table,FName(Copy.Key),
            FText::AsCultureInvariant(bRussian?Copy.Russian:Copy.English),Culture);
    }

    void AppendHints(TArray<FText>& Copies,UDataTable* Table,const FString& Culture)
    {
        for(int32 Index=0;Index<static_cast<int32>(EHint::Count);++Index)
            Copies.Add(ResolveHint(Table,static_cast<EHint>(Index),Culture));
    }

    int32 DrawMeter(const UMCFishingRodComponent& Rod,bool bGamepad,const FGeometry& Geometry,
        FSlateWindowElementList& Elements,int32 Layer,const FLinearColor& Tint)
    {
        const FVector2D Screen=Geometry.GetLocalSize();
        if(Screen.X<100 || Screen.Y<100) return Layer;
        const float Scale=FMath::Clamp(float(Screen.Y/1080.),.36f,1.f);
        const FVector2D Origin(FMath::Min(Screen.X*.67,Screen.X-159.*Scale),Screen.Y*.5-110.*Scale);
        const FGeometry Panel=Geometry.MakeChild(FVector2D(142,263),FSlateLayoutTransform(Scale,Origin));
        auto Box=[&](int32 Depth,const FSlateBrush& Brush,FVector2D Position,FVector2D Size,FLinearColor Color)
        {
            FSlateDrawElement::MakeBox(Elements,Layer+Depth,Panel.ToPaintGeometry(Size,FSlateLayoutTransform(Position)),
                &Brush,ESlateDrawEffect::None,Color*Tint);
        };
        auto Stroke=[&](int32 Depth,const TArray<FVector2D>& Points,float Width,FLinearColor Color)
        {
            const float LineWidth=Width*Panel.GetAccumulatedLayoutTransform().GetScale();
            FSlateDrawElement::MakeLines(Elements,Layer+Depth,Panel.ToPaintGeometry(),Points,
                ESlateDrawEffect::None,Color*Tint,true,FMath::Max(1.f,LineWidth));
        };

        // Overlap the stem and foot to form the rounded L-shaped housing from the concept.
        Box(1,Housing,{8,8},{72,263},Ink);
        Box(1,Housing,{8,185},{134,86},Ink);
        Box(2,Housing,{0,0},{72,263},Ink);
        Box(2,Housing,{0,177},{134,86},Ink);
        Box(3,HousingFace,{6,6},{60,251},Blue);
        Box(3,HousingFace,{6,183},{122,74},Blue);
        Box(4,TrackBorder,{14,11},{39,241},White);
        constexpr double TrackLeft=19,TrackTop=16,TrackWidth=29,TrackHeight=231;
        Box(5,Track,{TrackLeft,TrackTop},{TrackWidth,TrackHeight},Blue);

        // Clip the same rounded track to either the charge fill or the moving catch window.
        auto FillTrack=[&](double Top,double Bottom,FLinearColor Color)
        {
            if(Bottom<=Top) return;
            const FPaintGeometry Clip=Panel.ToPaintGeometry(FVector2D(TrackWidth,Bottom-Top),
                FSlateLayoutTransform(FVector2D(TrackLeft,Top)));
            Elements.PushClip(FSlateClippingZone(Clip));
            Box(6,Track,{TrackLeft,TrackTop},{TrackWidth,TrackHeight},Color);
            Elements.PopClip();
        };
        if(Rod.IsCatchChallengeActive())
        {
            // The hook's tip and both window bounds use the same vertical coordinate.
            const auto HookY=[](float Height) {return FMath::Lerp(TrackTop+TrackHeight-9.,TrackTop+9.,Height);};
            FillTrack(HookY(Rod.GetCatchZoneTop()),HookY(Rod.GetCatchZoneBottom()),
                Rod.IsHookInCatchZone()?Green:Rose);
            const double X=TrackLeft+TrackWidth*.5,Y=HookY(Rod.GetHookHeight());
            Stroke(7,{{X,-48},{X,Y-21},{X-7,Y-8},{X,Y},{X+9,Y-9}},4.8f,FLinearColor::White);
            Box(5,Progress,{72,248},{45,4},Ink);
            if(const float Amount=Rod.GetCatchProgress();Amount>0.f)
                Box(6,Progress,{72,248},{45.*Amount,4},White);
        }
        else
        {
            const double TrackBottom=TrackTop+TrackHeight;
            FillTrack(FMath::Lerp(TrackBottom,TrackTop,Rod.GetCastCharge()),TrackBottom,White);
        }

        if(bGamepad)
        {
            Box(4,TrackBorder,{69,198},{51,35},White);
            Box(5,Track,{72,201},{45,29},Blue);
            Stroke(6,{{81,223},{81,208},{88,208},{91,211},{91,214},{88,216},{81,216}},2.6f,White);
            Stroke(6,{{86,216},{92,223}},2.6f,White);
            Stroke(6,{{97,208},{109,208}},2.6f,White);
            Stroke(6,{{103,208},{103,223}},2.6f,White);
        }
        else
        {
            Box(4,Mouse,{69,185},{51,60},White);
            Box(5,MouseFace,{72,188},{45,54},Blue);
            Box(6,MouseButton,{72,188},{21,24},White);
            Stroke(6,{{71,212},{118,212}},3.f,White);
            Stroke(6,{{94,187},{94,212}},3.f,White);
        }
        return Layer+7;
    }
}
