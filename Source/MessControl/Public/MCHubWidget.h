#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/Button.h"
#include "MCHubWidget.generated.h"

class AMCHubGameState;
class UBorder;
class UCanvasPanel;
class UHorizontalBox;
class UOverlay;
class UTextBlock;
class UVerticalBox;
class UMCHubWidget;

UENUM()
enum class EMCHubMenuAction : uint8 { OpenSelection, SelectDay, Launch, Return, InviteFriends };

/** Native controls are the only interactive part of the shared mouth selector. */
UCLASS()
class MESSCONTROL_API UMCHubActionButton : public UButton
{
    GENERATED_BODY()
public:
    UMCHubActionButton();
    void Configure(UMCHubWidget* InOwner, EMCHubMenuAction InAction, int32 InDay=0, bool bFocusable=true);
    int32 GetDestinationDay() const { return Day; }
private:
    UFUNCTION() void Clicked();
    TWeakObjectPtr<UMCHubWidget> HubOwner;
    EMCHubMenuAction Action=EMCHubMenuAction::OpenSelection;
    int32 Day=0;
};

/** Hub-only readiness and authoritative controls; no level task/director HUD. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCHubWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCHubWidget();
    void ShowInteractionFeedback(const FText& Message,float Seconds=2.5f);
    void Init(AMCHubGameState* State);
    UFUNCTION(BlueprintCallable, Category="Hub|Presentation") void RefreshFromState();
    UFUNCTION(BlueprintPure, Category="Hub|Presentation") bool RequiresMenuInput() const;
    UFUNCTION(BlueprintPure, Category="Hub|Presentation") UWidget* GetFocusTarget() const;
    /** Initial Hub and Selecting ACKs require the same preconstructed, ready stage. */
    UFUNCTION(BlueprintPure, Category="Hub|Presentation") bool IsSceneReady() const;
    /** Call once from the local PlayerController tick, before skeletal stage ticks. */
    void TickPresentation(float DeltaSeconds);
    void HandleAction(EMCHubMenuAction Action, int32 Day=0);
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ApplyTheme() override;

    UPROPERTY(EditDefaultsOnly, Category="Hub|Presentation")
    TSoftClassPtr<UUserWidget> SelectionPresentationClass;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void OnLocalizationChanged() override;

private:
    bool IsLocalHost() const;
    FText InteractionFeedback;
    double InteractionFeedbackUntil=0;
    AMCHubGameState* State() const;
    bool EnsureSelectionPresentation();
    void ResetSelectionPresentation();
    void AdvanceSelectionPresentation();
    void UpdateMouthHitAreas();
    void SelectRelative(int32 Direction);
    FText Format(FName Key,const FText& Fallback,const FFormatNamedArguments& Arguments) const;
    UTextBlock* AddLabel(UVerticalBox* Parent,FName Name,int32 Size,bool bBold=false);
    void BindPresentationTree();
    void EnsureGatheringGuidance();
    void UpdateReadyWaypoint();
    UMCHubActionButton* MakeAction(FName Name,EMCHubMenuAction Action,int32 Day=0);
    UMCHubActionButton* AddAction(UVerticalBox* Parent,FName Name,EMCHubMenuAction Action,int32 Day=0);

    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
    UPROPERTY(Transient) TObjectPtr<UOverlay> PresentationHost;
    UPROPERTY(Transient) TObjectPtr<UBorder> ReadinessPanel;
    UPROPERTY(Transient) TObjectPtr<UBorder> SelectionPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReadinessTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReadinessCount;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlayerNames;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReadinessHint;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> InviteHint;
    UPROPERTY(Transient) TObjectPtr<UMCHubActionButton> InviteButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> InviteButtonLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> ReadyWaypoint;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReadyWaypointLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReadyWaypointArrow;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BasketballHint;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BasketballReticle;
    UPROPERTY(Transient) TObjectPtr<UBorder> FishingPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FishingCount;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectionTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectionStatus;
    UPROPERTY(Transient) TObjectPtr<UMCHubActionButton> OpenButton;
    UPROPERTY(Transient) TObjectPtr<UMCHubActionButton> LaunchButton;
    UPROPERTY(Transient) TObjectPtr<UMCHubActionButton> ReturnButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> OpenButtonLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LaunchButtonLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReturnButtonLabel;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCHubActionButton>> MouthHitAreas;
    UPROPERTY(Transient) TObjectPtr<UUserWidget> SelectionPresentation;
    TWeakObjectPtr<AMCHubGameState> BoundState;
    TWeakObjectPtr<UWidget> BoundPresentationRoot;
    bool bAuthoredPresentation=false;
    int32 PresentedDay=INDEX_NONE;
    bool bPresentationReady=false;
    bool bPresentationFailed=false;
    bool bRefreshingState=false;
    float RefreshElapsed=0.f;
    double NextNavigationAt=0.;
    FString LastStateSignature;
    TArray<FVector> BasketballTrajectory;
    double NextBasketballPreviewAt=0.;
    double NextReadyWaypointAt=0.;
};
