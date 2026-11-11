#pragma once

#include "MCGameplayUIWidget.h"
#include "MCHubShopShowcaseWidget.generated.h"

class UMCHubEconomyComponent;
class UTexture2D;

/** The passive display inside the Hub vending machine. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCHubShopShowcaseWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()

public:
    static constexpr int32 DisplayWidth = 256;
    static constexpr int32 DisplayHeight = 528;

    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;

    UFUNCTION(BlueprintPure, Category="Shop|Showcase") TArray<FName> GetVisibleOfferIds() const;
    UFUNCTION(BlueprintPure, Category="Shop|Showcase") TArray<int32> GetVisibleOfferPrices() const;

protected:
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
    struct FOffer
    {
        FName Id;
        int32 Price = 0;
        FText PriceLabel;
        FText Caption;
        TWeakObjectPtr<UTexture2D> Icon;
    };

    void RefreshOffers(const UMCHubEconomyComponent* Economy, double Now);
    void DrawOffer(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Cell,
        const FOffer& Offer, double Offset, float Opacity, const FLinearColor& Tint) const;
    const FOffer* FindOffer(int32 Page, int32 Cell) const;

    TWeakObjectPtr<const UMCHubEconomyComponent> DisplayedEconomy;
    TArray<FName> DisplayedStock;
    TArray<FOffer> Offers;
    FSlateFontInfo HeadingFont;
    FSlateFontInfo PriceFont;
    FText Heading;
    FText EmptyStock;
    double CycleStart = 0.;
    int32 ActivePage = 0;
    int32 PreviousPage = 0;
    float Transition = 0.f;
};
