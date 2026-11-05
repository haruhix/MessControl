#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "Components/SlateWrapperTypes.h"
#include "MCUIPresentationSubsystem.generated.h"

class APlayerController;
class IInputProcessor;
class UMCUIPresentationPanelWidget;
class UMCUnifiedUIWidget;
class UMCGameplayHUD;
class UMCHubWidget;
class UMCFishingRewardWidget;
class UWidget;
class UMCWorldLabelWidget;
class SWidget;

/** One F3 entry point, including menus using UIOnly input. No gameplay/profile mutations. */
UCLASS()
class MESSCONTROL_API UMCUIPresentationSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool HandleF3();
    bool IsCapturingSettingsKey() const;
    bool IsPanelOpenFor(const APlayerController* Controller) const;
    bool ApplyPanelInputMode(APlayerController* Controller);
    void ClosePanel(bool bRestoreInput=true);
    void CheckOwner();
    /** Construct and retain the view/material variants inside location preparation. */
    bool PrepareInterface(APlayerController* Controller);
    bool PrepareFishingCatch(APlayerController* Controller,UMCFishingRewardWidget* Source);
    bool GetUnifiedFishCounterTargets(const APlayerController* Controller,UWidget*& Icon,UWidget*& Card) const;
    /** Actual visible New Wardrobe panels reserve this normalized viewport rectangle for its original 3D display. */
    bool GetWardrobePreviewViewportRect(const APlayerController* Controller,FBox2D& OutRect) const;
private:
    bool PrepareWorldLabels(APlayerController* Controller);
    void RefreshInterface();
    void RestoreLegacy();
    UPROPERTY(Transient) TObjectPtr<UMCUIPresentationPanelWidget> Panel;
    UPROPERTY(Transient) TObjectPtr<UMCUnifiedUIWidget> Interface;
    UPROPERTY(Transient) TObjectPtr<UMCWorldLabelWidget> PreparedWorldLabelView;
    TSharedPtr<SWidget> PreparedWorldLabelSlate;
    TWeakObjectPtr<APlayerController> PanelOwner;
    TWeakObjectPtr<APlayerController> InterfaceOwner;
    TWeakObjectPtr<UMCGameplayHUD> ActiveHUD;
    TWeakObjectPtr<UMCHubWidget> ActiveHub;
    TSet<TWeakObjectPtr<UMCGameplayHUD>> SuppressedHUDs;
    TSet<TWeakObjectPtr<UMCHubWidget>> SuppressedHubs;
    bool bInterfaceTutorialMenuInput=false;
    bool bInterfaceHubMenuInput=false;
    int32 InterfaceZOrder=5;
    TMap<TWeakObjectPtr<UWidget>,float> LegacyOpacity;
    TMap<TWeakObjectPtr<UWidget>,ESlateVisibility> LegacyVisibility;
    FDelegateHandle PresentationHandle;
    TSharedPtr<IInputProcessor> InputProcessor;
};
