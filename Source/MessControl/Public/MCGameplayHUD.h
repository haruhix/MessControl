#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCGameplayHUD.generated.h"
class UMCStaminaWidget;
class UMCGameplayAlertWidget;
class UMCBoosterWidget;
class UMCReserveToothWidget;
class UMCHUDIcon;
class UMaterialInstanceDynamic;
class UDataTable;
class UMCDayEvent;
class UBorder;
class USizeBox;
class UTextBlock;
class UProgressBar;
class UButton;
struct FMCGameDirectorState;

UCLASS()
class MESSCONTROL_API UMCGameplayHUD : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    /** Use a Blueprint subclass of MCStaminaWidget to replace the native gauge. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="HUD") TSubclassOf<UMCStaminaWidget> StaminaWidgetClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="HUD") TSubclassOf<UMCBoosterWidget> BoosterWidgetClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="HUD") TSubclassOf<UMCGameplayAlertWidget> AlertWidgetClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="HUD") TSubclassOf<UMCReserveToothWidget> ReserveToothWidgetClass;
    /** Optional encounter component used only by composition previews, never a second runtime HUD. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Composition") TSubclassOf<UMCGameplayUIWidget> EncounterPreviewWidgetClass;
    /** Opt in to the historical debug sidebar's automatic positioning and text sizing. Otherwise the Designer owns its layout. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="HUD|Layout") bool bAutoArrangeDirectorMonitor=false;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
    UFUNCTION(BlueprintCallable,Category="HUD|Alerts") void QueueGameplayAlert(const FText& Message,float HoldSeconds=3.f,FName EventId=NAME_None);
    void ResetGameplayAlerts();
    void SetBossStackCount(int32 Count,float StackHeight=-1.f);
    UFUNCTION(BlueprintPure,Category="HUD|Food") float GetFoodReticleAlpha() const { return FoodReticleAlpha; }
protected:
    virtual void OnLocalizationChanged() override;
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
private:
    UPROPERTY(Transient) TObjectPtr<UMCStaminaWidget> StaminaWidget;
    UPROPERTY(Transient) TObjectPtr<UMCStaminaWidget> SwimEnergyWidget;
    UPROPERTY(Transient) TObjectPtr<UMCGameplayAlertWidget> AlertWidget;
    UPROPERTY(Transient) TObjectPtr<UMCBoosterWidget> BoosterWidget;
    UPROPERTY(Transient) TObjectPtr<UMCGameplayUIWidget> EncounterPreviewWidget;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCReserveToothWidget>> ReserveTeeth;
    UPROPERTY(Transient) TObjectPtr<UDataTable> LocalizationTable;
    TMap<FString,FName> SourceTextKeys;
    bool bSourceKeysCached=false;
    bool bApplyingPreview=false;
    TMap<TWeakObjectPtr<AActor>,TMap<FName,double>> ObservedAlertStates;
    TWeakObjectPtr<UMCDayEvent> LastAlertDayEvent;
    double LastAlertStepStartedAt=-1;
    TWeakObjectPtr<AActor> LastDrowningHero;
    bool bObservedDrowning=false;
    double DrowningAlertSerial=0;
    UPROPERTY(Transient) TObjectPtr<UBorder> DirectorPanel;
    UPROPERTY(Transient) TObjectPtr<USizeBox> DirectorPanelSize;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DirectorText;
    UPROPERTY(Transient) TObjectPtr<UBorder> DirectorCandidatesPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DirectorCandidatesText;
    UPROPERTY(Transient) TObjectPtr<UButton> DirectorLogButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DirectorLogButtonText;
    UPROPERTY(Transient) TObjectPtr<UBorder> ProgressionPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ProgressionText;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> ProgressionBar;
    UPROPERTY(Transient) TObjectPtr<UBorder> CataclysmPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CataclysmInstruction;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UWidget>> Widgets;
    float RefreshElapsed=1;
    bool bHadSprayReticle=false;
    float FoodReticleAlpha=0;
    float LayoutLogicalWidth=1920.f;
    FString Phase1LayoutSignature;
    int32 SelectedToolIndex=0;
    int32 DisplayedPlayerCount=4;
    UWidget* Find(FName Name) const;
    void CachePresentationWidgets();
    void BindAuthoredPresentation();
    void BindDirectorPresentation();
    void RefreshReserveTeeth(const class AMCGameState& State);
    void RefreshBooster(const class AMCToothCharacter* Hero);
    void EnsureBoosterWidget();
    void ApplyResourceTheme(UMCStaminaWidget* Resource);
    void UpdatePhase1Layout();
    FText Copy(FName Key,const FText& Fallback) const;
    FText LiteralCopy(const FString& Value);
    void SetCopy(FName WidgetName,const FText& Text);
    void RefreshState();
    void EnsureAlertWidget();
    void RefreshGameplayAlerts();
    void EnsureDirectorMonitor();
    void EnsureProgressionPanel();
    void EnsureCataclysmPanel();
    void RefreshDirectorMonitor(const FMCGameDirectorState& State);
    UFUNCTION() void OpenDirectorLog();
};

/** Replaceable vector icon widget; layout, tint and size are editable in UMG. */
UCLASS()
class MESSCONTROL_API UMCHUDIcon : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") int32 ToolSlot=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") bool bTooth=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") bool bReserveTooth=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reserve") float HealthFraction=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reserve") bool bLost=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") FLinearColor InkTint=FLinearColor(.028f,.030f,.043f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") FLinearColor EmptyTint=FLinearColor(.16f,.25f,.29f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") FLinearColor HostTint=FLinearColor(1.f,.64f,.23f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") FLinearColor Tint=FLinearColor(.98f,.97f,.93f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Expression") bool bDead=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Expression") bool bSad=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Expression") bool bHappy=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") bool bHost=false;
protected:
    virtual void NativePreConstruct() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
private:
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlyphMaterial;
    FSlateBrush GlyphBrush;
};

/** One arena reserve tooth; unrelated to the four live player health portraits. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCReserveToothWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Presentation") void ApplyPresentation(float CurrentHealth,float MaximumHealth,bool bIsLost);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Reserve") float Current=100.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Reserve") float Maximum=100.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Reserve") bool bLost=false;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
private:
    UPROPERTY(Transient) TObjectPtr<UMCHUDIcon> Glyph;
};
