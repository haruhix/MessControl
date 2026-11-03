#pragma once

#include "MCGameplayUIWidget.h"
#include "MCUnifiedShopWidget.generated.h"

class UMCHubShopWidget;
class UMCUnifiedShopWidget;
class UMCUniversalUIWidget;
class UImage;
class UTextBlock;
struct FMCUnifiedShopProduct;

/** Stable catalog/control identity; only the existing shop can accept a purchase. */
UCLASS()
class MESSCONTROL_API UMCUnifiedShopBinding : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<UMCUnifiedShopWidget> Owner;
    UPROPERTY(Transient) FName ItemID;
    UPROPERTY(Transient) FName SourceControl;
    UFUNCTION() void Clicked();
};

USTRUCT()
struct FMCUnifiedShopCard
{
    GENERATED_BODY()
    UPROPERTY(Transient) FName ItemID;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> Surface;
    UPROPERTY(Transient) TObjectPtr<UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Name;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Price;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> State;
};

/** PSD boosters shop view. Stock, prices, queued state and actions remain source-owned. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedShopWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedShopWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void SetBackend(UMCHubShopWidget* Source);
    bool HasBoundBackend() const { return Backend.IsValid(); }
    void RefreshFromBackend();
    UWidget* GetDefaultFocusTarget() const;
    void RequestBack();
    void ForwardItem(FName ItemID);
    void ForwardControl(FName Name);
    /** Retain a matching stepped frame as its header wraps; disable for hand-authored points. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") bool bAdaptHeaderContour=true;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    void BindStaticControls();
    void PresentProducts(const TArray<FMCUnifiedShopProduct>& Products,FName Selected);
    void PresentPreview();
    void UpdateHeaderContour();
    UMCUniversalUIWidget* MakeShape(FName Name,FLinearColor Fill,FVector2D Shadow,bool bButton=false);
    UTextBlock* MakeText(FName Name,int32 FontSize);
    UWidget* SourceControl(FName Name) const;
    TWeakObjectPtr<UMCHubShopWidget> Backend;
    UPROPERTY(Transient) TArray<FMCUnifiedShopCard> Cards;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUnifiedShopBinding>> CardBindings;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUnifiedShopBinding>> StaticBindings;
    float RefreshElapsed=0.f;
};
