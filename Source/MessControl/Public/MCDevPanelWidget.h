#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MCDevCommands.h"
#include "MCDevPanelWidget.generated.h"
class AMCPlayerController;
class UTextBlock;
class UVerticalBox;
class UCheckBox;
class UComboBoxString;
class UWidgetSwitcher;
class UBorder;
class UMCGameplayUITheme;

// Each button carries a typed action; no console strings or arbitrary commands.
UCLASS()
class UMCDevActionButton : public UButton
{
    GENERATED_BODY()
public:
    void Configure(AMCPlayerController* Player,EMCDevAction Command,int32 Index);
private:
    UFUNCTION() void Clicked();
    UPROPERTY() TObjectPtr<AMCPlayerController> Controller;
    EMCDevAction Action=EMCDevAction::StartStep;
    int32 StepIndex=INDEX_NONE;
};

UCLASS()
class MESSCONTROL_API UMCDevPanelWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void RefreshActions();
    void SetFeedback(const FText& Text);
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float Dt) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    UTextBlock* AddText(UVerticalBox* Box,const FString& Text,int32 Size);
    void AddAction(UVerticalBox* Box,const FString& Text,const FString& Hint,EMCDevAction Action,int32 Step=INDEX_NONE);
    UFUNCTION() void CloseClicked();
    UFUNCTION() void PlayerOverlayChanged(bool Checked);
    UFUNCTION() void DirectorOverlayChanged(bool Checked);
    UFUNCTION() void StartBotsClicked();
    UFUNCTION() void StopBotsClicked();
    UFUNCTION() void ReportBotsClicked();
    UFUNCTION() void NativeAnimationClicked();
    UFUNCTION() void StylizedAnimationClicked();
    UFUNCTION() void KawaiiAnimationClicked();
    void RefreshAnimationComparison();
    void BuildPassivesPage();
    void RefreshPassives();
    bool IsPassivesOpen() const;
    UFUNCTION() void OpenPassivesClicked();
    UFUNCTION() void PassivesBackClicked();
    UPROPERTY() TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY() TObjectPtr<UButton> OpenPassivesButton;
    UPROPERTY() TObjectPtr<UButton> PassivesBackButton;
    UPROPERTY() TObjectPtr<UVerticalBox> PassiveList;
    UPROPERTY() TObjectPtr<UTextBlock> PassiveSummary;
    UPROPERTY() TObjectPtr<UTextBlock> PassiveFeedback;
    UPROPERTY() TObjectPtr<UMCGameplayUITheme> PassiveTheme;
    UPROPERTY() TMap<FName,TObjectPtr<UBorder>> PassiveRows;
    UPROPERTY() TMap<FName,TObjectPtr<UTextBlock>> PassiveNames;
    UPROPERTY() TMap<FName,TObjectPtr<UTextBlock>> PassiveDescriptions;
    UPROPERTY() TMap<FName,TObjectPtr<UTextBlock>> PassiveOwnedLabels;
    UPROPERTY() TMap<FName,TObjectPtr<UButton>> PassiveGrantButtons;
    UPROPERTY() TMap<FName,TObjectPtr<UButton>> PassiveRemoveButtons;
    TArray<FName> DisplayedPassiveIDs;
    float PassiveRefreshDelay=0.f;
    UPROPERTY() TObjectPtr<UButton> NativeAnimationButton;
    UPROPERTY() TObjectPtr<UButton> StylizedAnimationButton;
    UPROPERTY() TObjectPtr<UButton> KawaiiAnimationButton;
    UPROPERTY() TObjectPtr<UTextBlock> AnimationModeStatus;
    uint8 DisplayedAnimationMode=254;
    UPROPERTY() TObjectPtr<UComboBoxString> BotCount;
    UPROPERTY() TObjectPtr<UComboBoxString> BotSkill;
    UPROPERTY() TObjectPtr<UComboBoxString> BotMode;
    UPROPERTY() TObjectPtr<UButton> StartBotsButton;
    UPROPERTY() TObjectPtr<UButton> StopBotsButton;
    UPROPERTY() TObjectPtr<UButton> ReportBotsButton;
    UPROPERTY() TObjectPtr<UTextBlock> BotsStatus;
    UPROPERTY() TObjectPtr<UCheckBox> PlayerOverlayCheck;
    UPROPERTY() TObjectPtr<UCheckBox> DirectorOverlayCheck;
    UPROPERTY() TObjectPtr<UTextBlock> DirectorLogText;
    UPROPERTY() TObjectPtr<UVerticalBox> Steps;
    UPROPERTY() TObjectPtr<UVerticalBox> Actions;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
};
