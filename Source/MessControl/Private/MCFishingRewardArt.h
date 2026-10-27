#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Layout/Geometry.h"
#include "Math/TransformCalculus2D.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderTransform.h"

/** Small retained analytic art, shared by the badge token and its flight. */
namespace MCFishingRewardArt
{
    namespace Private
    {
        struct FTokenBrushes
        {
            // Half-height rounding makes circles for square boxes and soft
            // capsules for the body/shadow. These brushes have no resources.
            FSlateRoundedBoxBrush Round{FLinearColor::White};
            FSlateRoundedBoxBrush SoftCorner{FLinearColor::White,3.f};
            TArray<FVector2D> TopGlint;
            TArray<FVector2D> LowerInset;

            FTokenBrushes()
            {
                TopGlint.Reserve(19);LowerInset.Reserve(19);
                for(int32 Index=0;Index<=18;++Index)
                {
                    const float TopAngle=FMath::DegreesToRadians(211.f+Index*(91.f/18.f));
                    const float BottomAngle=FMath::DegreesToRadians(28.f+Index*(124.f/18.f));
                    TopGlint.Add(FVector2D(50.+35.*FMath::Cos(TopAngle),45.+35.*FMath::Sin(TopAngle)));
                    LowerInset.Add(FVector2D(50.+35.*FMath::Cos(BottomAngle),45.+35.*FMath::Sin(BottomAngle)));
                }
            }
        };

        inline const FTokenBrushes& Brushes()
        {
            static const FTokenBrushes Cached;
            return Cached;
        }
    }

    /** Call while the retained widget is prepared under its loading barrier. */
    inline void Prime() { (void)Private::Brushes(); }

    /** Geometry has a 100x100 local canvas; the caller owns render-only motion. */
    inline int32 DrawToken(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 BaseLayer,
        FLinearColor Hue,FLinearColor Ink,FLinearColor Cream,float Alpha)
    {
        const float Opacity=FMath::Clamp(Alpha,0.f,1.f);
        if(Opacity<=.001f) return BaseLayer;
        const auto& Art=Private::Brushes();
        auto Tint=[&](FLinearColor Color,float Weight=1.f)
        {
            Color.A*=Opacity*Weight;
            return Color;
        };
        auto Box=[&](int32 Layer,const FSlateBrush& Brush,FVector2D Position,FVector2D Size,
            FLinearColor Color,float Degrees=0.f)
        {
            const FGeometry Shape=Geometry.MakeChild(Size,FSlateLayoutTransform(Position),
                FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(Degrees))),FVector2D(.5f,.5f));
            FSlateDrawElement::MakeBox(Elements,BaseLayer+Layer,Shape.ToPaintGeometry(),&Brush,
                ESlateDrawEffect::None,Color);
        };

        // A soft contact shadow and a six-unit colored sidewall make the
        // object read as a small toy medal, rather than a flat line icon.
        const FLinearColor Shadow=FMath::Lerp(Ink,Hue,.2f);
        Box(1,Art.Round,FVector2D(7,84),FVector2D(86,16),Tint(Shadow,.045f));
        Box(1,Art.Round,FVector2D(13,87),FVector2D(74,12),Tint(Shadow,.065f));
        Box(1,Art.Round,FVector2D(21,89),FVector2D(58,8),Tint(Shadow,.09f));
        Box(2,Art.Round,FVector2D(4,7),FVector2D(92,92),Tint(FMath::Lerp(Hue,Ink,.38f)));
        Box(3,Art.Round,FVector2D(4,1),FVector2D(92,92),Tint(FMath::Lerp(Hue,Ink,.13f)));
        Box(4,Art.Round,FVector2D(7,3),FVector2D(86,86),Tint(FMath::Lerp(Hue,Cream,.58f)));
        Box(5,Art.Round,FVector2D(10,6),FVector2D(80,80),Tint(FMath::Lerp(Hue,Cream,.14f)));
        FSlateDrawElement::MakeLines(Elements,BaseLayer+6,Geometry.ToPaintGeometry(),Art.LowerInset,
            ESlateDrawEffect::None,Tint(FMath::Lerp(Hue,Ink,.24f),.4f),true,2.3f);
        FSlateDrawElement::MakeLines(Elements,BaseLayer+7,Geometry.ToPaintGeometry(),Art.TopGlint,
            ESlateDrawEffect::None,Tint(Cream,.65f),true,3.5f);
        Box(7,Art.Round,FVector2D(20,20),FVector2D(25,7),Tint(Cream,.25f),-29.f);

        // Filled cream silhouette: rounded diamond tail, a small dorsal fin,
        // broad pill body and one expressive eye. The colored offset beneath
        // it is shallow embossing, not a harsh black outline.
        const FLinearColor Emboss=Tint(FMath::Lerp(Hue,Ink,.38f),.38f);
        Box(8,Art.SoftCorner,FVector2D(20,42),FVector2D(20,20),Emboss,45.f);
        Box(8,Art.SoftCorner,FVector2D(48,33),FVector2D(14,9),Emboss,-22.f);
        Box(8,Art.Round,FVector2D(33,37),FVector2D(48,30),Emboss);
        Box(9,Art.SoftCorner,FVector2D(20,40),FVector2D(20,20),Tint(Cream),45.f);
        Box(9,Art.SoftCorner,FVector2D(48,31),FVector2D(14,9),Tint(FMath::Lerp(Cream,Hue,.12f)),-22.f);
        Box(9,Art.Round,FVector2D(33,35),FVector2D(48,30),Tint(Cream));
        Box(10,Art.Round,FVector2D(42,38),FVector2D(21,4),Tint(FLinearColor::White,.5f));
        Box(11,Art.Round,FVector2D(67,42),FVector2D(6,7),Tint(Ink,.94f));
        Box(12,Art.Round,FVector2D(68,42.5),FVector2D(2.2,2.2),Tint(Cream));
        return BaseLayer+12;
    }
}
