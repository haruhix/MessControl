#pragma once

#include "MCUniversalUIWidget.h"
#include "MCMainMenuWidget.h"
#include "MCTutorialTypes.h"
#include "MCUnifiedUIWidget.generated.h"

class UMCGameplayHUD;
class UMCHUDIcon;
class UMCScoreboardWidget;
class UMCHubWardrobeWidget;
class UMCHubShopWidget;
class UMCHubWidget;
class UMCPerkChoiceWidget;
class UMCGameplayMenuWidget;
class UMCFishingRewardWidget;
class UMCBossHealthWidget;
class UMCGameplayAlertWidget;
class UWidgetSwitcher;
class UMCTutorialWidget;
class UMCEmoteWidget;
class AMCGameState;
class UTextBlock;

UENUM(BlueprintType)
enum class EMCUnifiedUIScreen : uint8
{
    Menu, Gameplay,
    Hub, Wardrobe, Boosters, Tools UMETA(Hidden), Perks UMETA(Hidden),
    Settings, Team, Reward, Frontend, GameplayMenu, Catch, HUDExtras, Loading, Boss, Alert, Tutorial, Emotes
};

/** One authored HUD rectangle in the 1778x1080 PSD coordinate system. */
USTRUCT(BlueprintType)
struct FMCUnifiedHUDLayoutMetric
{
    GENERATED_BODY()
    /** Local Canvas position; viewport groups use an offset from Anchor. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") FVector2D Position=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") FVector2D Size=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") FVector2D Anchor=FVector2D::ZeroVector;
    /** Zero retains the authored font. Group scale applies to all text uniformly. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout",meta=(ClampMin="0")) int32 FontSize=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") bool bOverrideShapeMetrics=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape",meta=(EditCondition="bOverrideShapeMetrics")) float CornerRadius=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape",meta=(EditCondition="bOverrideShapeMetrics")) float OutlineWidth=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape",meta=(EditCondition="bOverrideShapeMetrics")) FVector2D ShadowOffset=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape",meta=(EditCondition="bOverrideShapeMetrics")) TArray<FVector2D> ContourPoints;
};

/** One editable root for the PSD interface, including screen selection in Designer. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedUIWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedUIWidget();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") EMCUnifiedUIScreen PreviewScreen=EMCUnifiedUIScreen::Menu;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen",meta=(ClampMin="0",ClampMax="4")) int32 PreviewSettingsTab=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewHubMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewRewardMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewGameplayMenuMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewCatchMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewHUDExtrasMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewLoadingMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewBossMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewAlertMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewTutorialMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen",meta=(ValidEnumValues="Loading,Intro,ToothpickPull,ToothpickBreak,ToothpickHeal,BrushTooth,FoodCut,FreshSort,SpoiledSort,BreakfastRain,BreakfastCleanup,CoffeeWarning,CoffeeWaves,CoffeeCleanup,Calculus,FoamParty,Complete")) EMCTutorialStage PreviewTutorialLesson=EMCTutorialStage::BrushTooth;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") FName PreviewEmoteMode;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen",meta=(ClampMin="0",ClampMax="2")) int32 PreviewEmoteCategory=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen",meta=(ClampMin="0",ClampMax="4")) int32 PreviewFrontendPage=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Art") TSoftObjectPtr<UTexture2D> Logo;
    /** PSD tool artwork; perk icons remain owned by DT_Perks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Art|Tools") TMap<FName,TSoftObjectPtr<UTexture2D>> ToolIconArt;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="HUD|PSD Layout") FVector2D HUDReferenceSize=FVector2D(1778,1080);
    /** Named existing widgets remain editable; use this contract to apply the PSD reference rectangles. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="HUD|PSD Layout") TMap<FName,FMCUnifiedHUDLayoutMetric> HUDLayoutMetrics;
    /** Explicit authoring action; runtime only reapplies metrics when their contract changes. */
    UFUNCTION(BlueprintCallable,CallInEditor,Category="HUD|PSD Layout") void ApplyPSDGameplayLayout(bool bApplyStyleDefaults=true);
    UFUNCTION(BlueprintCallable, Category="Interface") void ShowScreen(EMCUnifiedUIScreen Screen);
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ApplyTheme() override;
    void SetMenuBackend(UMCMainMenuWidget* Backend);
    void SetHUDBackend(UMCGameplayHUD* Backend);
    void SetTeamBackend(UMCScoreboardWidget* Backend);
    void SetWardrobeBackend(UMCHubWardrobeWidget* Backend);
    void SetShopBackend(UMCHubShopWidget* Backend);
    void SetHubBackend(UMCHubWidget* Backend);
    void SetRewardBackend(UMCPerkChoiceWidget* Backend);
    void SetGameplayMenuBackend(UMCGameplayMenuWidget* Backend);
    bool PrepareCatchPresentation(UMCFishingRewardWidget* Backend);
    bool PrepareHUDExtrasPresentation();
    bool PrepareEncounterPresentation();
    bool PrepareOverlayPresentation(UMCTutorialWidget* TutorialSource,UMCEmoteWidget* EmoteSource);
    void SetTutorialBackend(UMCTutorialWidget* Source);
    void SetEmoteBackend(UMCEmoteWidget* Source);
    bool HasTutorialPresentation() const;
    bool RequiresTutorialMenuInput() const;
    bool HasEmotePresentation() const;
    /** Focus ownership only; never begins/confirms/closes an emote or changes its held state. */
    void SetEmoteFocusLease(bool bActive);
    void SetBossBackend(UMCBossHealthWidget* Backend);
    void SetAlertBackend(UMCGameplayAlertWidget* Backend);
    void SetCatchBackend(UMCFishingRewardWidget* Backend);
    bool HasHubPresentation() const { return HubBackend.IsValid(); }
    bool HasRewardPresentation() const { return RewardBackend.IsValid(); }
    bool HasGameplayMenuPresentation() const { return GameplayMenuBackend.IsValid(); }
    bool RequiresHubMenuInput() const;
    bool GetHubFishCounterTargets(UWidget*& Icon,UWidget*& Card) const;
    bool HasCommercePresentation() const { return WardrobeBackend.IsValid() || ShopBackend.IsValid(); }
    bool HasTeamPresentation() const { return TeamBackend.IsValid(); }
    EMCUnifiedUIScreen GetActiveScreen() const { return ActiveScreen; }
    bool HasMenuPresentation() const { return MenuBackend.IsValid(); }
    UWidget* GetDefaultFocusTarget() const;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
private:
    UTexture2D* ResolveToolArt(FName Key) const;
    void EnsureGameplayControls();
    void EnsureAdditionalScreens();
    void RefreshAdaptiveLayout();
    void RefreshHUDReferenceScale(FVector2D AvailableSize);
    void RefreshRuntimePresentation();
    void RefreshReserveCounter();
    void FinishReserveLoss();
    UMCHUDIcon* FindReserveIcon() const;
    TMap<FName,TWeakObjectPtr<UWidget>> HUDControls;
    TWeakObjectPtr<UMCMainMenuWidget> MenuBackend;
    TWeakObjectPtr<UMCGameplayHUD> HUDBackend;
    TWeakObjectPtr<const AMCGameState> ReserveCounterState;
    TWeakObjectPtr<UTextBlock> ReserveNumber;
    TWeakObjectPtr<UMCHUDIcon> ReserveIcon;
    FWidgetTransform ReserveNumberTransform;
    FWidgetTransform ReserveIconTransform;
    FSlateColor ReserveNumberColor;
    float ReserveNumberOpacity=1.f;
    double ReserveLossStartedAt=-1.0;
    int32 LastReserveCount=INDEX_NONE;
    int32 DisplayedReserveCount=INDEX_NONE;
    TWeakObjectPtr<UMCScoreboardWidget> TeamBackend;
    TWeakObjectPtr<UMCHubWardrobeWidget> WardrobeBackend;
    TWeakObjectPtr<UMCHubShopWidget> ShopBackend;
    TWeakObjectPtr<UMCHubWidget> HubBackend;
    TWeakObjectPtr<UMCPerkChoiceWidget> RewardBackend;
    TWeakObjectPtr<UMCGameplayMenuWidget> GameplayMenuBackend;
    TWeakObjectPtr<UMCFishingRewardWidget> CatchBackend;
    TWeakObjectPtr<UMCBossHealthWidget> BossBackend;
    TWeakObjectPtr<UMCGameplayAlertWidget> AlertBackend;
    TWeakObjectPtr<UMCTutorialWidget> TutorialBackend;
    TWeakObjectPtr<UMCEmoteWidget> EmoteBackend;
    FString PreparedEmoteCorpus;
    FVector2D LastHUDReferenceArea=FVector2D::ZeroVector;
    uint32 LastHUDLayoutHash=0;
    bool bHUDReferenceLayoutApplied=false;
    bool bApplyingHUDReferenceLayout=false;
    float RefreshDelay=0.f;
    EMCUnifiedUIScreen ActiveScreen=EMCUnifiedUIScreen::Menu;
};

/** One existing action owner; this view never creates rooms or saves settings itself. */
UCLASS()
class MESSCONTROL_API UMCUnifiedMenuButton : public UMCUniversalUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedMenuButton();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") EMCMainMenuAction Action=EMCMainMenuAction::CreateLobby;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    void SetBackend(UMCMainMenuWidget* Menu);
private:
    UFUNCTION() void ForwardAction();
    TWeakObjectPtr<UMCMainMenuWidget> Backend;
};
