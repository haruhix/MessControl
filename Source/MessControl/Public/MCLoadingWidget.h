#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCLoadingWidget.generated.h"

class UButton;
class UBorder;
class UHorizontalBox;
class UImage;
class UMCWorldPreloadSubsystem;
class UProgressBar;
class UTextBlock;
class USizeBox;

/** Designer-authored loading screen with a native fallback, shared by gameplay and the front end. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCLoadingWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    void SetPreparation(UMCWorldPreloadSubsystem* Preparation);
    void SetWaitingForPlayers(bool bWaiting);
    void SetWaitingDetail(const FText& Detail);
    void RefreshPresentation();
    UWidget* GetFocusTarget() const;
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;

    /** PreviewState: Assets, Graphics, Streaming, Waiting, Failed or Connecting. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview", meta=(ClampMin="0",ClampMax=".99"))
    float PreviewLoadingProgress=.62f;

    /** Retry and exit controls can also be wired from an authored Blueprint. */
    UFUNCTION(BlueprintCallable, Category="Loading") void RetryPreparation();
    UFUNCTION(BlueprintCallable, Category="Loading") void ReturnToMenu();

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void OnLocalizationChanged() override;

    /** Optional custom animation/state feedback; called only when loading presentation changes. */
    UFUNCTION(BlueprintImplementableEvent, Category="Loading", meta=(DisplayName="Loading Presentation Changed"))
    void OnLoadingPresentationChanged(float Progress, bool bFailed, bool bWaiting);

private:
    void BindPresentationTree();
    void EnsureTipPresentation();
    void RefreshTips(double ClockSeconds,bool bForceCopy=false);
    FText Text(FName Key,const FText& Russian,const FText& English) const;
    UPROPERTY(Transient) TObjectPtr<UMCWorldPreloadSubsystem> Preparation;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Detail;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ProgressLabel;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> AssetProgress;
    UPROPERTY(Transient) TObjectPtr<UImage> Artwork;
    UPROPERTY(Transient) TObjectPtr<UBorder> Footer;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ReturnButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RetryLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReturnLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> TipPanel;
    UPROPERTY(Transient) TObjectPtr<USizeBox> TipWidth;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TipHeading;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TipCurrent;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TipNext;
    TArray<FText> TipTexts;
    TWeakObjectPtr<UDataTable> TipTable;
    FString TipCulture;
    int32 LastTipCurrent=INDEX_NONE,LastTipNext=INDEX_NONE;
    FText WaitingDetail;
    bool bWaitingForPlayers=false;
    float TargetProgress=0.f;
    float DisplayedProgress=0.f;
    float FooterOpacity=1.f;
    int32 LastProgressStage=-1;
    bool bHasMeasuredProgress=false;
    FString LastPresentationSignature;
    TWeakObjectPtr<UWidget> BoundPresentationRoot;
};
