#pragma once

#include "MCTutorialWidget.h"
#include "MCTutorialTypes.h"
#include "MCUnifiedTutorialWidget.generated.h"

class UMCUniversalUIWidget;
class UTextBlock;

/** Editable New view of the original tutorial owner; actor-free lesson fixtures are Designer-only. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedTutorialWidget : public UMCTutorialWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedTutorialWidget(const FObjectInitializer& Initializer);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void SetBackend(UMCTutorialWidget* Source);
    void RefreshFromBackend();
    UWidget* GetDefaultFocusTarget() const;
    bool RouteSourceKey(const FKeyEvent& Event);
    void PreparePresentation();
    bool IsPresentationPrepared() const {return bPrepared;}
    /** Only stages with real authored source title/dialogue/instruction are offered. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Tutorial",meta=(ValidEnumValues="Loading,Intro,ToothpickPull,ToothpickBreak,ToothpickHeal,BrushTooth,FoodCut,FreshSort,SpoiledSort,BreakfastRain,BreakfastCleanup,CoffeeWarning,CoffeeWaves,CoffeeCleanup,Calculus,FoamParty,Complete"))
    EMCTutorialStage PreviewLesson=EMCTutorialStage::BrushTooth;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tutorial|Layout",meta=(ClampMin="320")) float MaximumCardWidth=960;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tutorial|Layout",meta=(ClampMin="180")) float MaximumCardHeight=460;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tutorial|Layout",meta=(ClampMin="0")) float BottomInset=200;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void OnLocalizationChanged() override;
private:
    UMCUniversalUIWidget* MakeShape(FName Name,FLinearColor Fill,FMargin InPadding);
    UTextBlock* MakeLabel(FName Name,int32 Size,FLinearColor InLabelColor);
    void UpdateLayout();
    void SyncFrames();
    void BindViewActions();
    void Present(const FMCTutorialPresentationSnapshot& Snapshot);
    UFUNCTION() void ForwardReady();
    UFUNCTION() void ForwardMenu();
    TWeakObjectPtr<UMCTutorialWidget> Backend;
    FMCTutorialPresentationSnapshot LastSnapshot;
    float ViewRefreshElapsed=0;
    FText FixtureCopy(FName Key,const FText& Fallback) const;
    UPROPERTY(Transient) TArray<FSlateFontInfo> PreparedFonts;
    TMap<FName,int32> AuthoredFontSizes;
    TWeakObjectPtr<UDataTable> PreparedCopyTable;
    FString PreparedCulture;
    FVector2D LastViewport=FVector2D::ZeroVector;
    float PreparedFontScale=0;
    bool bPrepared=false,bPreparing=false;
};
