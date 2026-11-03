#pragma once

#include "MCGameplayUIWidget.h"
#include "MCPerkChoiceWidget.h"
#include "MCUnifiedRewardWidget.generated.h"

class UMCUnifiedRewardWidget;
class UMCUniversalUIWidget;
class UImage;
class UTextBlock;
class UScrollBox;
class USizeBox;

/** One exact source control, never an index that could target a newer offer. */
UCLASS()
class MESSCONTROL_API UMCUnifiedRewardBinding : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<UMCUnifiedRewardWidget> Owner;
    UPROPERTY(Transient) TWeakObjectPtr<UMCPerkCardButton> SourceButton;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> View;
    UFUNCTION() void Clicked();
};

USTRUCT()
struct FMCUnifiedRewardCard
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UWidget> Host;
    UPROPERTY(Transient) TObjectPtr<USizeBox> Width;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> Surface;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> Type;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> Rarity;
    UPROPERTY(Transient) TObjectPtr<UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Name;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Description;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Stats;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Metadata;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> BodyScroll;
    UPROPERTY(Transient) TWeakObjectPtr<UMCPerkCardButton> SourceButton;
    FName PerkID;
};

/** Editable PSD REWARD view; the existing presenter/controller owns all decisions. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedRewardWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedRewardWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void SetBackend(UMCPerkChoiceWidget* Source);
    void RefreshFromBackend();
    bool HasBoundBackend() const { return Backend.IsValid(); }
    UWidget* GetDefaultFocusTarget() const;
    void RequestBack();
    bool RouteSourceKey(const FKeyEvent& Event);
    bool RouteSourceAnalog(const FAnalogInputEvent& Event);
    void ActivateSourceButton(UMCPerkCardButton* Button);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Rewards") TArray<FName> PreviewPerkIDs;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") bool bAdaptHeaderContour=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") bool bAdaptCardLayout=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout",meta=(ClampMin="240")) float MinimumCardWidth=310.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout",meta=(ClampMin="310")) float MaximumCardWidth=480.f;
    /** Baseline copy cap at1080p; a taller parent viewport may expand it without scaling text. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout",meta=(ClampMin="240")) float MaximumCardCopyHeight=360.f;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event) override;
    virtual void OnLocalizationChanged() override;
private:
    void ResolvePresentationTree();
    void Present(const FMCPerkChoicePresentationSnapshot& Snapshot);
    void PresentPreview();
    void BindSourceControl(UMCUniversalUIWidget* View,UMCPerkCardButton* Source);
    void RefreshFocusFromSource();
    void UpdateHeaderContour();
    float AdaptiveCardWidth(const FMCPerkChoicePresentationSnapshot& Snapshot) const;
    void UpdateCardCopyHeight();
    void RefreshTextBounds();
    FText TypeLabel(EMCPerkKind Kind) const;
    FText RarityLabel(EMCPerkRarity Rarity) const;
    UMCUniversalUIWidget* MakeShape(FName Name,FLinearColor Fill,bool bButton=false);
    UTextBlock* MakeText(FName Name,int32 FontSize,FLinearColor Color);
    TWeakObjectPtr<UMCPerkChoiceWidget> Backend;
    UPROPERTY(Transient) TArray<FMCUnifiedRewardCard> Cards;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUniversalUIWidget>> ActionViews;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUnifiedRewardBinding>> Bindings;
    TWeakObjectPtr<UMCPerkCardButton> SelectedSource;
    bool bShowingContract=false;
    float RefreshElapsed=0.f;
};
