#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCLoadingWidget.generated.h"

class UButton;
class UDataTable;
class UHorizontalBox;
class UMCWorldPreloadSubsystem;
class UProgressBar;
class UTextBlock;

/** Asset-free loading fallback, shared by gameplay and the front end. */
UCLASS()
class MESSCONTROL_API UMCLoadingWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetPreparation(UMCWorldPreloadSubsystem* Preparation);
    void SetWaitingForPlayers(bool bWaiting);
    void SetWaitingDetail(const FText& Detail);
    void RefreshPresentation();
    UWidget* GetFocusTarget() const;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeOnInitialized() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;

private:
    void BuildTree();
    FText Text(FName Key,const FText& Russian,const FText& English) const;
    UFUNCTION() void RetryPreparation();
    UFUNCTION() void ReturnToMenu();
    UPROPERTY(Transient) TObjectPtr<UMCWorldPreloadSubsystem> Preparation;
    UPROPERTY(Transient) TObjectPtr<UDataTable> LocalizationTable;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Detail;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ProgressLabel;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> AssetProgress;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ReturnButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RetryLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReturnLabel;
    FText WaitingDetail;
    bool bWaitingForPlayers=false;
};
