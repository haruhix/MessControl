#include "MCFishingRewardWidget.h"
#include "MCFishingRewardArt.h"

#include "MCHubGameState.h"
#include "MCPlayerController.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "GameFramework/Character.h"
#include "Widgets/InvalidateWidgetReason.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "MCFishingReward"

namespace MCFishingFeedback
{
    constexpr float PopSeconds=.42f;
    constexpr float FlightAt=1.02f;
    constexpr float FlightSeconds=.60f;
    constexpr float ArrivalAt=FlightAt+FlightSeconds;
    constexpr float PulseSeconds=.38f;
    constexpr float FullSeconds=1.5f;
    constexpr int32 MaximumPending=5; // Plus the current receipt: at most six.
    const FVector2D BadgeSize(356,124);
    const FVector2D TokenOffset(-114,0);

    float Smooth(float T) {T=FMath::Clamp(T,0.f,1.f);return T*T*(3.f-2.f*T);}
    float Pop(float T)
    {
        T=FMath::Clamp(T,0.f,1.f);
        return T<.68f?FMath::Lerp(.85f,1.03f,Smooth(T/.68f)):FMath::Lerp(1.03f,1.f,Smooth((T-.68f)/.32f));
    }
    FVector2D TokenPop(float Age)
    {
        if(Age<.07f) return FMath::Lerp(FVector2D(.66,.36),FVector2D(1.12,.62),Smooth(Age/.07f));
        if(Age<.23f) return FMath::Lerp(FVector2D(1.12,.62),FVector2D(.88,1.20),Smooth((Age-.07f)/.16f));
        if(Age<.34f) return FMath::Lerp(FVector2D(.88,1.20),FVector2D(1.06,.96),Smooth((Age-.23f)/.11f));
        return FMath::Lerp(FVector2D(1.06,.96),FVector2D(1,1),Smooth((Age-.34f)/.12f));
    }
    float TokenLift(float Age)
    {
        if(Age<.07f) return FMath::Lerp(22.f,13.f,Smooth(Age/.07f));
        if(Age<.23f) return FMath::Lerp(13.f,-16.f,Smooth((Age-.07f)/.16f));
        return FMath::Lerp(-16.f,0.f,Smooth((Age-.23f)/.23f));
    }
    FVector2D CounterPop(float Age)
    {
        if(Age<.055f) return FMath::Lerp(FVector2D(1,1),FVector2D(1.18,.86),Smooth(Age/.055f));
        if(Age<.14f) return FMath::Lerp(FVector2D(1.18,.86),FVector2D(.97,1.13),Smooth((Age-.055f)/.085f));
        if(Age<.24f) return FMath::Lerp(FVector2D(.97,1.13),FVector2D(1.045,.98),Smooth((Age-.14f)/.10f));
        return FMath::Lerp(FVector2D(1.045,.98),FVector2D(1,1),Smooth((Age-.24f)/.14f));
    }
    FVector2D Arc(FVector2D Start,FVector2D End,float T)
    {
        const FVector2D Bend((Start.X+End.X)*.5,FMath::Min(Start.Y,End.Y)-FMath::Clamp(FVector2D::Distance(Start,End)*.22,60.,170.));
        return Start*((1.-T)*(1.-T))+Bend*(2.*(1.-T)*T)+End*(T*T);
    }
    void Fish(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer,const FLinearColor& Color,float LineWidth=3.f)
    {
        const FVector2D Size=Geometry.GetLocalSize();
        auto P=[&](double X,double Y) {return FVector2D(X*Size.X,Y*Size.Y);};
        auto Stroke=[&](const TArray<FVector2D>& Points,float Width=1.f) {
            FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width*LineWidth);
        };
        auto Ellipse=[&](double X,double Y,double RX,double RY) {
            TArray<FVector2D> Points;Points.Reserve(33);
            for(int32 I=0;I<=32;++I) {const double A=I*2.*PI/32.;Points.Add(P(X+RX*FMath::Cos(A),Y+RY*FMath::Sin(A)));}
            // Slate owns the draw payload after this call; no particle actors or retained arrays.
            FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,LineWidth);
        };
        Ellipse(.57,.5,.28,.21);
        Stroke({P(.31,.49),P(.1,.27),P(.1,.73),P(.31,.51)});
        Ellipse(.69,.45,.018,.018);Stroke({P(.49,.34),P(.43,.24),P(.62,.3)},.8f);
    }
}

UMCFishingRewardWidget::UMCFishingRewardWidget()
{
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMCFishingRewardWidget::BuildPresentationTree()
{
    if(WidgetTree && !WidgetTree->RootWidget)
        WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("FishingRewardCanvas"));
    // Cache every analytic brush and all font sizes even while idle, so the
    // retained instance can be prepared by the location readiness barrier.
    ApplyTheme();
}

void UMCFishingRewardWidget::ApplyTheme()
{
    Super::ApplyTheme();
    const auto* Resolved=GetResolvedTheme();
    Colors=Resolved?Resolved->Palette:FMCUIThemePalette();
    AmountFont=Resolved?Resolved->MakeFont(52,true):FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),52);
    FullFont=Resolved?Resolved->MakeFont(32,true):FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),32);
    DetailFont=Resolved?Resolved->MakeFont(18,true):FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18);
    BadgeBrush=FSlateRoundedBoxBrush(FLinearColor::White,27.f,FLinearColor::White.CopyWithNewOpacity(.8f),1.5f);
    ShadowBrush=FSlateRoundedBoxBrush(FLinearColor::Black,26.f);
    FishDiscBrush=FSlateRoundedBoxBrush(FLinearColor::White,32.f);
    DotBrush=FSlateRoundedBoxBrush(FLinearColor::White,4.f);
    DepthBrush=FSlateRoundedBoxBrush(FLinearColor::White,27.f);
    SheenBrush=FSlateRoundedBoxBrush(FLinearColor::White,20.f);
    InsetBrush=FSlateRoundedBoxBrush(FLinearColor::White,12.f);
    RingBrush=FSlateRoundedBoxBrush(FLinearColor::Transparent,60.f,FLinearColor::White,2.f);
    MCFishingRewardArt::Prime();
    PrimeTextMetrics();
    RefreshCopy();
    Invalidate(EInvalidateWidgetReason::Paint);
}

void UMCFishingRewardWidget::PrimeTextMetrics(float FontScale)
{
    if(!FSlateApplication::IsInitialized()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();
    if(!Renderer) return;
    const auto Measure=Renderer->GetFontMeasureService();
    Measure->Measure(FText::AsCultureInvariant(TEXT("+0123456789 ,.")),AmountFont,FontScale);
    Measure->Measure(Localized(TEXT("hub.fishing.reward.full"),LOCTEXT("Full","Запас полон")),FullFont,FontScale);
    const FText Currency=Localized(TEXT("hub.fishing.reward.currency"),LOCTEXT("Currency","Рыбные монеты"));
    Measure->Measure(Currency,DetailFont,FontScale);
    const FText Labels[]={
        Localized(TEXT("hub.fishing.reward.Common"),LOCTEXT("Common","Обычный улов")),
        Localized(TEXT("hub.fishing.reward.Rare"),LOCTEXT("Rare","Редкий улов")),
        Localized(TEXT("hub.fishing.reward.Epic"),LOCTEXT("Epic","Эпический улов")),
        Localized(TEXT("hub.fishing.reward.Legendary"),LOCTEXT("Legendary","Легендарный улов"))};
    for(const FText& Label:Labels) Measure->Measure(Label,DetailFont,FontScale);
    FFormatNamedArguments Args;Args.Add(TEXT("Amount"),FText::AsNumber(1234567890));
    Measure->Measure(FText::Format(Localized(TEXT("hub.fishing.reward.amount"),LOCTEXT("Amount","+{Amount}")),Args),AmountFont,FontScale);
    PrimedFontScale=FontScale;
}

void UMCFishingRewardWidget::SetCounterTarget(UWidget* Icon,UWidget* Card)
{
    if(CounterCard.Get()!=Card) RestoreCounterScale();
    CounterIcon=Icon;CounterCard=Card;
}

void UMCFishingRewardWidget::PresentCatch(EMCHubItemRarity Rarity,int32 FishCoins,int32 WalletAfter)
{
    if(IsDesignTime() || !GetWorld() || FishCoins<0 || WalletAfter<0 || FishCoins>WalletAfter
        || uint8(Rarity)>uint8(EMCHubItemRarity::Legendary)) return;
    if(PresentationWorld.IsValid() && PresentationWorld.Get()!=GetWorld()) ClearPresentation();
    PresentationWorld=GetWorld();
    FCatchReceipt Receipt;Receipt.Rarity=Rarity;Receipt.Amount=FishCoins;Receipt.WalletAfter=WalletAfter;
    if(Pending.Num()<MCFishingFeedback::MaximumPending) Pending.Add(Receipt);
    else {
        auto& Tail=Pending.Last();
        if(Tail.Amount>0 && FishCoins>0) {
            Tail.Amount=int32(FMath::Min<int64>(int64(Tail.Amount)+FishCoins,MAX_int32));
            Tail.WalletAfter=WalletAfter;Tail.bMerged=true;
        } else Tail=Receipt; // Never turn a full-wallet receipt into a credited flight.
    }
    if(!bActive) StartNext();
}

void UMCFishingRewardWidget::StartNext()
{
    RestoreCounterScale();
    if(Pending.IsEmpty()) {bActive=false;return;}
    Active=Pending[0];Pending.RemoveAt(0,EAllowShrinking::No);
    AnimationAge=0;bActive=true;bSuppressed=true;bFlightStarted=false;bHasCounterEndpoint=false;
    RefreshCopy();
}

bool UMCFishingRewardWidget::IsAnimating() const {return bActive || !Pending.IsEmpty();}

void UMCFishingRewardWidget::RefreshCopy()
{
    if(!bActive) return;
    AmountText=Active.Amount==0?Localized(TEXT("hub.fishing.reward.full"),LOCTEXT("Full","Запас полон")):
        FText::Format(Localized(TEXT("hub.fishing.reward.amount"),LOCTEXT("Amount","+{Amount}")),FFormatNamedArguments{{TEXT("Amount"),FText::AsNumber(Active.Amount)}});
    const FText Currency=Localized(TEXT("hub.fishing.reward.currency"),LOCTEXT("Currency","Рыбные монеты"));
    FText Rarity;
    switch(Active.Rarity) {
    case EMCHubItemRarity::Rare:Rarity=Localized(TEXT("hub.fishing.reward.Rare"),LOCTEXT("Rare","Редкий улов"));FishColor=Colors.Accent;break;
    case EMCHubItemRarity::Epic:Rarity=Localized(TEXT("hub.fishing.reward.Epic"),LOCTEXT("Epic","Эпический улов"));FishColor=FMath::Lerp(Colors.Accent,Colors.Cost,.5f);break;
    case EMCHubItemRarity::Legendary:Rarity=Localized(TEXT("hub.fishing.reward.Legendary"),LOCTEXT("Legendary","Легендарный улов"));FishColor=Colors.Warning;break;
    default:Rarity=Localized(TEXT("hub.fishing.reward.Common"),LOCTEXT("Common","Обычный улов"));FishColor=Colors.Mint;break;
    }
    // Keep one short line so the rarity remains readable at 720p.
    DetailText=Active.bMerged || Active.Amount==0?Currency:Rarity;
    AmountTextScale=1;DetailTextScale=1;
    if(FSlateApplication::IsInitialized()) if(const auto Renderer=FSlateApplication::Get().GetRenderer()) {
        const auto Measure=Renderer->GetFontMeasureService();
        AmountTextScale=FMath::Min(1.f,208.f/FMath::Max(1.f,float(Measure->Measure(AmountText,Active.Amount==0?FullFont:AmountFont).X)));
        DetailTextScale=FMath::Min(1.f,202.f/FMath::Max(1.f,float(Measure->Measure(DetailText,DetailFont).X)));
    }
}

void UMCFishingRewardWidget::OnLocalizationChanged() {Super::OnLocalizationChanged();PrimeTextMetrics();RefreshCopy();Invalidate(EInvalidateWidgetReason::Paint);}

void UMCFishingRewardWidget::RestoreCounterScale()
{
    if(bCounterPulsing) if(auto* Card=PulsedCard.Get()) {
        Card->SetRenderScale(OriginalCounterScale);Card->SetRenderTransformPivot(OriginalCounterPivot);
    }
    bCounterPulsing=false;PulsedCard.Reset();
}

void UMCFishingRewardWidget::ClearPresentation()
{
    RestoreCounterScale();Pending.Reset();bActive=false;bSuppressed=true;bFlightStarted=false;bHasCounterEndpoint=false;
    AnimationAge=0;CounterIcon.Reset();CounterCard.Reset();PresentationWorld.Reset();
    Invalidate(EInvalidateWidgetReason::Paint);
}

void UMCFishingRewardWidget::NativeDestruct() {ClearPresentation();Super::NativeDestruct();}

bool UMCFishingRewardWidget::ShouldSuppress() const
{
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const auto* Hub=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    return !PC || !PC->IsLocalController() || !PC->GetPawn() || PC->bShowMouseCursor || PC->IsWardrobeOpen()
        || PC->IsHubShopOpen() || PC->IsRewardMenuOpen() || !Hub || Hub->Phase!=EMCHubPhase::Gathering
        || UGameplayStatics::IsGamePaused(this);
}

float UMCFishingRewardWidget::ResponsiveScale(const FGeometry& Geometry) const
{
    const auto Size=Geometry.GetLocalSize();
    return float(FMath::Clamp(FMath::Min(Size.X/1280.,Size.Y/720.),.72,1.));
}

FVector2D UMCFishingRewardWidget::HeroAnchor(const FGeometry& Geometry) const
{
    const FVector2D View=Geometry.GetLocalSize();
    FVector2D Center(View.X*.5,View.Y*.35);
    auto* PC=GetOwningPlayer();
    const APawn* Pawn=PC?PC->GetPawn():nullptr;
    if(Pawn) {
        float Height=82.f;
        if(const auto* Character=Cast<ACharacter>(Pawn)) Height=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+28.f;
        FVector2D Pixel;
        if(PC->ProjectWorldLocationToScreen(Pawn->GetActorLocation()+FVector(0,0,Height),Pixel,false))
            USlateBlueprintLibrary::ScreenToWidgetLocal(this,Geometry,Pixel,Center);
        // Leave a clear nameplate row between the receipt and the hero.
        Center.Y-=164.f*ResponsiveScale(Geometry);
    }
    const FVector2D Half=MCFishingFeedback::BadgeSize*ResponsiveScale(Geometry)*.52;
    Center.X=FMath::Clamp(Center.X,Half.X+16.,FMath::Max(Half.X+16.,View.X-Half.X-16.));
    Center.Y=FMath::Clamp(Center.Y,Half.Y+36.,FMath::Max(Half.Y+36.,View.Y-Half.Y-100.));
    return Center;
}

bool UMCFishingRewardWidget::CounterAnchor(const FGeometry& Geometry,FVector2D& Out) const
{
    const auto* Icon=CounterIcon.Get();
    if(!Icon || Icon->GetWorld()!=GetWorld() || !Icon->IsVisible()) return false;
    const FGeometry& Target=Icon->GetCachedGeometry();
    if(Target.GetLocalSize().X<=0 || Target.GetLocalSize().Y<=0) return false;
    Out=Geometry.AbsoluteToLocal(Target.LocalToAbsolute(Target.GetLocalSize()*.5));
    const auto Size=Geometry.GetLocalSize();
    return FMath::IsFinite(Out.X) && FMath::IsFinite(Out.Y) && Out.X>=0 && Out.Y>=0 && Out.X<=Size.X && Out.Y<=Size.Y;
}

void UMCFishingRewardWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    // DPI is stable between resizes. Warm its glyph atlas while the retained
    // response widget is idle behind the location's readiness cover.
    const float FontScale=Geometry.GetAccumulatedLayoutTransform().GetScale();
    if(!FMath::IsNearlyEqual(PrimedFontScale,FontScale)) PrimeTextMetrics(FontScale);
    bViewportFontsPrepared=PrimedFontScale>0 && Geometry.GetLocalSize().X>0 && Geometry.GetLocalSize().Y>0;
    if(!bActive) return;
    // Animation changes only draw data, never the viewport's layout/prepass.
    Invalidate(EInvalidateWidgetReason::Paint);
    if(PresentationWorld.Get()!=GetWorld()) {ClearPresentation();return;}
    bSuppressed=ShouldSuppress() || Geometry.GetLocalSize().X<1 || Geometry.GetLocalSize().Y<1;
    if(bSuppressed) {RestoreCounterScale();return;}
    if(!bFlightStarted) BadgeCenter=HeroAnchor(Geometry);
    const float NextAge=AnimationAge+FMath::Max(0.f,DeltaSeconds);
    if(Active.Amount>0 && !bFlightStarted && NextAge>=MCFishingFeedback::FlightAt) {
        const FVector2D Size=Geometry.GetLocalSize();
        const float Scale=ResponsiveScale(Geometry);
        FVector2D Start=BadgeCenter+MCFishingFeedback::TokenOffset*Scale;
        FVector2D End;
        if(CounterAnchor(Geometry,End)) Start-=(End-Start).GetSafeNormal()*14.f*Scale;
        FlightStartNormalized=FVector2D(Start.X/Size.X,Start.Y/Size.Y);bFlightStarted=true;
    }
    AnimationAge=NextAge;
    bHasCounterEndpoint=CounterAnchor(Geometry,FlightEnd);
    if(Active.Amount>0 && AnimationAge>=MCFishingFeedback::ArrivalAt && bHasCounterEndpoint) {
        if(!bCounterPulsing) if(auto* Card=CounterCard.Get()) {
            PulsedCard=Card;OriginalCounterScale=Card->GetRenderTransform().Scale;
            OriginalCounterPivot=Card->GetRenderTransformPivot();Card->SetRenderTransformPivot(FVector2D(.5,.5));bCounterPulsing=true;
        }
        if(auto* Card=PulsedCard.Get();Card && bCounterPulsing) {
            Card->SetRenderScale(OriginalCounterScale*MCFishingFeedback::CounterPop(AnimationAge-MCFishingFeedback::ArrivalAt));
        }
    }
    const float End=Active.Amount>0?MCFishingFeedback::ArrivalAt+MCFishingFeedback::PulseSeconds:MCFishingFeedback::FullSeconds;
    if(AnimationAge>=End) {RestoreCounterScale();bActive=false;StartNext();}
}

int32 UMCFishingRewardWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    const int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    if(!bActive || bSuppressed) return Top;
    using namespace MCFishingFeedback;
    const float Scale=ResponsiveScale(Geometry);
    const FLinearColor Inherited=Style.GetColorAndOpacityTint();
    auto Tint=[&](FLinearColor Color,float Alpha=1.f) {Color.A*=Alpha;return Color*Inherited;};
    auto Pose=[&](FVector2D Size,FVector2D Center,FVector2D Stretch,float Angle=0.f) {
        const auto Render=Concatenate(FSlateRenderTransform(FScale2D(FVector2f(float(Stretch.X),float(Stretch.Y)))),
            FSlateRenderTransform(FQuat2D(Angle)));
        return Geometry.MakeChild(Size,FSlateLayoutTransform(Center-Size*.5),Render,FVector2D(.5,.5));
    };
    auto Box=[&](const FGeometry& Geo,int32 Z,const FSlateBrush& Brush,FLinearColor Color) {
        FSlateDrawElement::MakeBox(Elements,Z,Geo.ToPaintGeometry(),&Brush,ESlateDrawEffect::None,Color);
    };
    auto Spark=[&](FVector2D Center,float Size,float Angle,FLinearColor Color,int32 Z) {
        Box(Pose(FVector2D(3.5,Size),Center,FVector2D(Scale,Scale),Angle),Z,DotBrush,Color);
        Box(Pose(FVector2D(Size,3.5),Center,FVector2D(Scale,Scale),Angle),Z,DotBrush,Color);
    };
    const float End=Active.Amount>0?FlightAt:FullSeconds;
    const float Entry=Smooth(AnimationAge/.10f);
    const float Exit=Smooth((End-AnimationAge)/.16f);
    const float CardEntry=Smooth((AnimationAge-.065f)/.28f);
    const float Back=1.f+2.2f*FMath::Pow(CardEntry-1.f,3.f)+1.2f*FMath::Square(CardEntry-1.f);
    const float Fade=Smooth((AnimationAge-.065f)/.14f)*Exit;
    const FVector2D CardStretch(Scale*FMath::Lerp(.72f,1.f,Back),Scale*FMath::Lerp(.65f,1.f,Back));
    const FVector2D CardCenter=BadgeCenter+FVector2D(0,(1.f-CardEntry)*12.f+(1.f-Exit)*8.f)*Scale;
    const FLinearColor Face=FMath::Lerp(Colors.Cream,FishColor,.09f);
    if(AnimationAge<End) {
        const float Tilt=FMath::DegreesToRadians(-3.5f*(1.f-CardEntry));
        for(int32 I=3;I>=0;--I)
            Box(Pose(BadgeSize+FVector2D(4+I*4,4+I*3),CardCenter+FVector2D(0,11+I*2)*Scale,CardStretch,Tilt),
                Top+1,ShadowBrush,Tint(Colors.Ink,Fade*(.035f-I*.006f)));
        Box(Pose(BadgeSize,CardCenter+FVector2D(0,7)*Scale,CardStretch,Tilt),Top+2,DepthBrush,Tint(FMath::Lerp(Colors.Marine,Face,.66f),Fade));
        // Animated transforms stay in render space: glyph atlas size is fixed.
        const FGeometry Card=Pose(BadgeSize,CardCenter,CardStretch,Tilt);
        Box(Card,Top+4,BadgeBrush,Tint(Face,Fade));
        Box(Card.MakeChild(FVector2D(336,22),FSlateLayoutTransform(FVector2D(10,7))),Top+5,SheenBrush,Tint(FLinearColor::White,Fade*.38f));
        Box(Card.MakeChild(FVector2D(210,29),FSlateLayoutTransform(FVector2D(127,81))),Top+6,InsetBrush,Tint(FishColor,Fade*.12f));
        const float NumberEntry=Smooth((AnimationAge-.12f)/.18f);
        const float NumberPop=1.f+.13f*FMath::Sin(NumberEntry*PI)*(1.f-NumberEntry);
        const FGeometry Number=Card.MakeChild(FVector2D(210,64),FSlateLayoutTransform(FVector2D(126,Active.Amount==0?27:10)),
            FSlateRenderTransform(FScale2D(AmountTextScale*NumberPop)),FVector2D::ZeroVector);
        const FGeometry NumberShadow=Number.MakeChild(FVector2D(210,64),FSlateLayoutTransform(FVector2D(0,2)));
        FSlateDrawElement::MakeText(Elements,Top+8,NumberShadow.ToPaintGeometry(),AmountText,Active.Amount==0?FullFont:AmountFont,ESlateDrawEffect::None,Tint(Colors.Ink,Fade*NumberEntry*.16f));
        FSlateDrawElement::MakeText(Elements,Top+9,Number.ToPaintGeometry(),AmountText,Active.Amount==0?FullFont:AmountFont,ESlateDrawEffect::None,Tint(Colors.Ink,Fade*NumberEntry));
        const FGeometry Detail=Card.MakeChild(FVector2D(202,25),FSlateLayoutTransform(FVector2D(133,83)),FSlateRenderTransform(FScale2D(DetailTextScale)),FVector2D::ZeroVector);
        FSlateDrawElement::MakeText(Elements,Top+9,Detail.ToPaintGeometry(),DetailText,DetailFont,ESlateDrawEffect::None,Tint(Colors.Ink,Fade*NumberEntry*.8f));
    }
    const FVector2D MedalCenter=BadgeCenter+TokenOffset*Scale;
    if(Active.Amount==0 || AnimationAge<FlightAt) {
        FVector2D Position=MedalCenter+FVector2D(0,TokenLift(AnimationAge))*Scale;
        FVector2D Stretch=TokenPop(AnimationAge);
        float Angle=FMath::DegreesToRadians(-12.f*(1.f-Smooth(AnimationAge/.25f))+8.f*FMath::Sin(Smooth(AnimationAge/.42f)*PI));
        if(Active.Amount>0 && AnimationAge>.86f) {
            const float Pull=Smooth((AnimationAge-.86f)/.16f);
            FVector2D Target;
            if(CounterAnchor(Geometry,Target)) Position-=(Target-MedalCenter).GetSafeNormal()*14.f*Scale*Pull;
            Stretch*=FVector2D(FMath::Lerp(1.,1.10,double(Pull)),FMath::Lerp(1.,.88,double(Pull)));
            Angle-=FMath::DegreesToRadians(8.f*Pull);
        }
        const float TokenOpacity=Entry*(Active.Amount==0?Exit:1.f)*Inherited.A;
        MCFishingRewardArt::DrawToken(Pose(FVector2D(100,100),Position,Stretch*Scale,Angle),Elements,Top+15,FishColor,Colors.Ink,Colors.Cream,TokenOpacity);
        if(Active.Amount>0 && AnimationAge>.09f && AnimationAge<.47f) {
            const float T=Smooth((AnimationAge-.09f)/.38f);
            for(int32 I=0;I<4;++I) {
                const float A=(I*.5f+.20f)*PI;
                const FVector2D At=MedalCenter+FVector2D(FMath::Cos(A),FMath::Sin(A))*(54.f+16.f*T)*Scale;
                Spark(At,10.f*(1.f-.45f*T),A,Tint(FMath::Lerp(Colors.Cream,FishColor,.3f),(1.f-T)*.7f),Top+30);
            }
        }
    } else if(bHasCounterEndpoint) {
        const FVector2D Size=Geometry.GetLocalSize();
        const FVector2D Start(FlightStartNormalized.X*Size.X,FlightStartNormalized.Y*Size.Y);
        const int32 Count=Active.Amount>=5?3:Active.Amount>=2?2:1;
        for(int32 Coin=Count-1;Coin>=0;--Coin) {
            const float Progress=(AnimationAge-FlightAt-Coin*.055f)/FlightSeconds;
            if(Progress<0.f || Progress>=1.f) continue;
            const float T=FMath::Pow(Progress,1.5f);
            const FVector2D Launch=Start+FVector2D(Coin*8,-Coin*7)*Scale;
            const FVector2D Center=Arc(Launch,FlightEnd,T);
            for(int32 I=1;I<=5;++I) {
                const float TrailT=T-I*.024f;if(TrailT<=0) continue;
                const FVector2D At=Arc(Launch,FlightEnd,TrailT);
                const float Radius=(5.f-I*.55f)*Scale;
                Box(Pose(FVector2D(Radius*2),At,FVector2D(1,1)),Top+32,DotBrush,Tint(FMath::Lerp(FishColor,Colors.Cream,.45f),(1.f-Progress)*(.48f-I*.055f)));
            }
            const float Suction=Smooth((Progress-.65f)/.35f);
            const float CoinScale=Scale*FMath::Lerp(1.f,.28f,Suction)*(1.f-Coin*.08f);
            const float Release=Smooth(Progress/.10f);
            const float Spawn=Coin==0?1.f:Smooth(Progress/.08f);
            const float EmitScale=Coin==0?1.f:FMath::Lerp(.70f,1.f,Spawn);
            const FVector2D Stretch=FVector2D(FMath::Lerp(1.10f,.86f+.14f*FMath::Cos(T*2.f*PI),Release),
                FMath::Lerp(.88f,1.f+.10f*FMath::Sin(T*PI),Release))*(CoinScale*EmitScale);
            const float Angle=FMath::DegreesToRadians(-8.f+32.f*FMath::Sin(T*PI));
            const float Opacity=Spawn*Smooth((1.f-Progress)/.12f)*Inherited.A;
            MCFishingRewardArt::DrawToken(Pose(FVector2D(100,100),Center,Stretch,Angle),Elements,Top+35+Coin*14,FishColor,Colors.Ink,Colors.Cream,Opacity);
        }
    }
    if(Active.Amount>0 && bHasCounterEndpoint && AnimationAge>=ArrivalAt && AnimationAge<ArrivalAt+PulseSeconds) {
        const float T=Smooth((AnimationAge-ArrivalAt)/PulseSeconds);
        const float Radius=FMath::Lerp(13.f,38.f,T)*Scale;
        Box(Pose(FVector2D(Radius*2),FlightEnd,FVector2D(1,1)),Top+80,RingBrush,Tint(FishColor,(1.f-T)*.55f));
        for(int32 I=0;I<4;++I) {
            const float Angle=(I*.5f+.18f)*PI;
            const FVector2D Position=FlightEnd+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*(17.f+27.f*T)*Scale;
            Spark(Position,9.f*(1.f-T)+2.f,Angle,Tint(FMath::Lerp(FishColor,Colors.Cream,.35f),(1.f-T)*.8f),Top+81);
        }
    }
    return Top+90;
}

#undef LOCTEXT_NAMESPACE
