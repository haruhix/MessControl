#include "MCHubShopShowcaseWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Layout/Clipping.h"
#include "MCHubEconomyComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "MCHubShopShowcase"

namespace
{
constexpr int32 OffersPerPage = 4;
constexpr double HoldSeconds = 4.8;
constexpr double TransitionSeconds = .66;
constexpr float Stagger[] = {0.f, .068f, .155f, .222f};

const FLinearColor Navy = FLinearColor(FColor::FromHex(TEXT("0A1D3D")));
const FLinearColor Blue = FLinearColor(FColor::FromHex(TEXT("183C70")));
const FLinearColor DarkBlue = FLinearColor(FColor::FromHex(TEXT("0C2855")));
const FLinearColor Cream = FLinearColor(FColor::FromHex(TEXT("F6E9CD")));
const FLinearColor Rose = FLinearColor(FColor::FromHex(TEXT("A64260")));
const FLinearColor Gold = FLinearColor(FColor::FromHex(TEXT("F7C653")));
const FLinearColor GoldEdge = FLinearColor(FColor::FromHex(TEXT("9C621E")));
const FSlateRoundedBoxBrush Screen(FLinearColor::White, 18.f);
const FSlateRoundedBoxBrush Tile(FLinearColor::White, 14.f);
const FSlateRoundedBoxBrush Plate(FLinearColor::White, 8.f);
const FSlateRoundedBoxBrush Coin(FLinearColor::White, 11.f, GoldEdge, 2.f);

void DrawBox(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry,
    FVector2D Position, FVector2D Size, const FSlateBrush& Brush, FLinearColor Color)
{
    FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)),
        &Brush, ESlateDrawEffect::None, Color);
}

void DrawCenteredText(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry,
    FVector2D Center, const FText& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
{
    const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
    FSlateDrawElement::MakeText(Elements, Layer,
        Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Center - TextSize * .5)),
        Text, Font, ESlateDrawEffect::None, Color);
}

void FitText(FSlateFontInfo& Font, const FText& Text, double Width)
{
    const double Advance = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
    if (Advance > Width) Font.Size *= Width / Advance;
}
}

void UMCHubShopShowcaseWidget::BuildPresentationTree()
{
    if (!WidgetTree || WidgetTree->RootWidget) return;
    auto* Bounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ShowcaseBounds"));
    Bounds->SetWidthOverride(DisplayWidth);
    Bounds->SetHeightOverride(DisplayHeight);
    WidgetTree->RootWidget = Bounds;
}

void UMCHubShopShowcaseWidget::ApplyTheme()
{
    BuildPresentationTree();
    Super::ApplyTheme();
    if (const auto* ResolvedTheme = GetResolvedTheme())
    {
        HeadingFont = ResolvedTheme->MakeFont(34, true);
        PriceFont = ResolvedTheme->MakeGameplayFont(28, true);
    }
    Heading = Localized(TEXT("UI.Shop.Title"), LOCTEXT("Heading", "SHOP"));
    EmptyStock = Localized(TEXT("UI.Shop.Empty"), LOCTEXT("Empty", "OUT OF STOCK"));
    DisplayedEconomy.Reset(); // Rebuild the localized offer labels on the next tick.
    Invalidate(EInvalidateWidgetReason::Paint);
}

void UMCHubShopShowcaseWidget::RefreshOffers(const UMCHubEconomyComponent* Economy, double Now)
{
    if (!Economy || !Economy->bShopStockGenerated || !Economy->CatalogTable.Get()) return;
    if (DisplayedEconomy.Get() == Economy && DisplayedStock == Economy->ShopStockIDs) return;

    DisplayedEconomy = Economy;
    DisplayedStock = Economy->ShopStockIDs;
    Offers.Reset();
    for (const FMCHubEconomyCatalogEntry& Entry : Economy->GetShopCatalog())
    {
        if (!Entry.Definition.bEnabled || !Entry.Definition.bInShop) continue;
        FOffer& Offer = Offers.AddDefaulted_GetRef();
        Offer.Id = Entry.ItemID;
        Offer.Price = Entry.Definition.Price;
        Offer.PriceLabel = FText::AsNumber(Offer.Price);
        Offer.Caption = Localized(Entry.Definition.DisplayNameKey, Entry.Definition.DisplayName);
        Offer.Icon = Entry.Definition.Icon.Get();
    }
    CycleStart = Now;
    ActivePage = PreviousPage = 0;
    Transition = 0.f;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void UMCHubShopShowcaseWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    UWorld* World = GetWorld();
    const auto* Controller = World ? Cast<AMCPlayerController>(World->GetFirstPlayerController()) : nullptr;
    if (!Controller || !Controller->IsLocalController() || !Controller->IsWorldPresentationReady()) return;
    const auto* Player = Controller->GetPlayerState<AMCPlayerState>();
    const double Now = World->GetTimeSeconds();
    RefreshOffers(Player ? Player->Economy.Get() : nullptr, Now);
    if (Offers.Num() <= OffersPerPage) return;

    const int32 PageCount = FMath::DivideAndRoundUp(Offers.Num(), OffersPerPage);
    const double Elapsed = FMath::Max(0., Now - CycleStart);
    const double PageSeconds = HoldSeconds + TransitionSeconds;
    const double Cycle = FMath::Fmod(Elapsed, PageSeconds * PageCount);
    const int32 OldPage = FMath::FloorToInt(Cycle / PageSeconds);
    const float Slide = float(FMath::Max(0., FMath::Fmod(Cycle, PageSeconds) - HoldSeconds));
    const int32 NextPage = Slide > 0.f ? (OldPage + 1) % PageCount : OldPage;
    if (ActivePage != NextPage || Transition != Slide)
    {
        ActivePage = NextPage;
        PreviousPage = OldPage;
        Transition = Slide;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

const UMCHubShopShowcaseWidget::FOffer* UMCHubShopShowcaseWidget::FindOffer(int32 Page, int32 Cell) const
{
    if (Cell >= Offers.Num()) return nullptr;
    return &Offers[(Page * OffersPerPage + Cell) % Offers.Num()];
}

TArray<FName> UMCHubShopShowcaseWidget::GetVisibleOfferIds() const
{
    TArray<FName> Ids;
    for (int32 Cell = 0; Cell < OffersPerPage; ++Cell)
        if (const FOffer* Offer = FindOffer(ActivePage, Cell)) Ids.Add(Offer->Id);
    return Ids;
}

TArray<int32> UMCHubShopShowcaseWidget::GetVisibleOfferPrices() const
{
    TArray<int32> Prices;
    for (int32 Cell = 0; Cell < OffersPerPage; ++Cell)
        if (const FOffer* Offer = FindOffer(ActivePage, Cell)) Prices.Add(Offer->Price);
    return Prices;
}

void UMCHubShopShowcaseWidget::DrawOffer(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Cell,
    const FOffer& Offer, double Offset, float Opacity, const FLinearColor& Tint) const
{
    if (Opacity <= 0.f) return;
    const FLinearColor ContentTint = Tint.CopyWithNewOpacity(Tint.A * Opacity);
    const FVector2D Bounds = Cell.GetLocalSize();
    if (auto* Texture = Offer.Icon.Get())
    {
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.DrawAs = ESlateBrushDrawType::Image;
        const FVector2D SourceSize(FMath::Max(1, Texture->GetSizeX()), FMath::Max(1, Texture->GetSizeY()));
        const FVector2D Limit(Bounds.X - 22., Bounds.Y - 61.);
        const FVector2D Extent = SourceSize * FMath::Min(Limit.X / SourceSize.X, Limit.Y / SourceSize.Y);
        DrawBox(Elements, Layer, Cell, {(Bounds.X - Extent.X) * .5, 13. + (Limit.Y - Extent.Y) * .5 + Offset},
            Extent, Brush, ContentTint);
    }
    else
    {
        FSlateFontInfo Font = PriceFont.HasValidFont() ? PriceFont : FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 22);
        Font.Size = 22.f;
        FitText(Font, Offer.Caption, Bounds.X - 18.);
        DrawCenteredText(Elements, Layer, Cell, {Bounds.X * .5, (Bounds.Y - 45.) * .5 + Offset},
            Offer.Caption, Font, Cream * ContentTint);
    }
    const double PriceY = Bounds.Y - 47. + Offset;
    DrawBox(Elements, Layer, Cell, {8., PriceY}, {Bounds.X - 16., 36.}, Plate, Cream * ContentTint);
    FSlateFontInfo Font = PriceFont.HasValidFont() ? PriceFont : FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 28);
    FitText(Font, Offer.PriceLabel, Bounds.X - 52.);
    const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Offer.PriceLabel, Font);
    const double GroupLeft = (Bounds.X - (22. + 6. + TextSize.X)) * .5;
    DrawBox(Elements, Layer + 1, Cell, {GroupLeft, PriceY + 7.}, {22., 22.}, Coin, Gold * ContentTint);
    DrawCenteredText(Elements, Layer + 1, Cell, {GroupLeft + 28. + TextSize.X * .5, PriceY + 18.},
        Offer.PriceLabel, Font, Rose * ContentTint);
}

int32 UMCHubShopShowcaseWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
    const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style,
    bool bParentEnabled) const
{
    const int32 Top = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled);
    const FVector2D Size(DisplayWidth, DisplayHeight);
    const FVector2D Available = Geometry.GetLocalSize();
    const float Scale = float(FMath::Min(Available.X / Size.X, Available.Y / Size.Y));
    if (Scale <= 0.f) return Top;
    const auto Panel = Geometry.MakeChild(Size, FSlateLayoutTransform(Scale, (Available - Size * Scale) * .5));
    const auto Tint = Style.GetColorAndOpacityTint();
    DrawBox(Elements, Top + 1, Panel, FVector2D::ZeroVector, Size, Screen, Navy * Tint);
    DrawBox(Elements, Top + 2, Panel, {15., 15.}, {226., 53.}, Plate, Cream * Tint);
    FSlateFontInfo Title = HeadingFont.HasValidFont() ? HeadingFont : FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 31);
    FitText(Title, Heading, 210.);
    DrawCenteredText(Elements, Top + 3, Panel, {128., 41.5}, Heading, Title, Rose * Tint);
    if (Offers.IsEmpty())
    {
        if (DisplayedEconomy.IsValid())
        {
            FSlateFontInfo Font = PriceFont.HasValidFont() ? PriceFont : FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 28);
            FitText(Font, EmptyStock, 212.);
            DrawCenteredText(Elements, Top + 3, Panel, {128., 290.}, EmptyStock, Font, Cream * Tint);
        }
        return Top + 3;
    }

    constexpr double CellHeight = 209.;
    for (int32 Index = 0; Index < OffersPerPage; ++Index)
    {
        const FVector2D Position(12. + (Index % 2) * 120., 81. + (Index / 2) * 224.);
        const FVector2D CellSize(112., CellHeight);
        const auto Cell = Panel.MakeChild(CellSize, FSlateLayoutTransform(Position));
        FSlateDrawElement::MakeGradient(Elements, Top + 2, Cell.ToPaintGeometry(),
            {FSlateGradientStop(FVector2D(0., 0.), Blue * Tint),
             FSlateGradientStop(FVector2D(0., CellHeight), DarkBlue * Tint)},
            Orient_Horizontal, ESlateDrawEffect::None, FVector4f(14.f));
        Elements.PushClip(FSlateClippingZone(Cell));
        const FOffer* Incoming = FindOffer(ActivePage, Index);
        if (Transition > 0.f)
        {
            const float Time = Transition - Stagger[Index];
            const float Out = FMath::SmoothStep(0.f, .172f, Time);
            const float In = FMath::SmoothStep(.161f, .438f, Time);
            if (const FOffer* Outgoing = FindOffer(PreviousPage, Index))
                DrawOffer(Elements, Top + 3, Cell, *Outgoing, Out * 36., 1.f - Out, Tint);
            if (Incoming) DrawOffer(Elements, Top + 3, Cell, *Incoming, (In - 1.f) * 46., In, Tint);
        }
        else if (Incoming)
        {
            DrawOffer(Elements, Top + 3, Cell, *Incoming, 0., 1.f, Tint);
        }
        Elements.PopClip();
    }

    if (Transition > 0.f)
    {
        const FVector2D GridSize(232., 433.);
        const auto Grid = Panel.MakeChild(GridSize, FSlateLayoutTransform(FVector2D(12., 81.)));
        const float Progress = Transition / TransitionSeconds;
        const double SweepY = FMath::Lerp(-74., GridSize.Y, double(Progress));
        const FLinearColor Sweep = FLinearColor(.35f, .68f, 1.f, .21f * FMath::Sin(PI * Progress)) * Tint;
        Elements.PushClip(FSlateClippingZone(Grid));
        FSlateDrawElement::MakeGradient(Elements, Top + 5,
            Grid.ToPaintGeometry(FVector2D(GridSize.X, 74.), FSlateLayoutTransform(FVector2D(0., SweepY))),
            {FSlateGradientStop(FVector2D(0., 0.), Sweep.CopyWithNewOpacity(0.f)),
             FSlateGradientStop(FVector2D(0., 53.), Sweep),
             FSlateGradientStop(FVector2D(0., 74.), Sweep.CopyWithNewOpacity(0.f))},
            Orient_Horizontal, ESlateDrawEffect::None);
        Elements.PopClip();
    }
    return Top + 5;
}

#undef LOCTEXT_NAMESPACE
