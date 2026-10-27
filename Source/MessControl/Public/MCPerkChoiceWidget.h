#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/Button.h"
#include "MCPerkTypes.h"
#include "MCPerkChoiceWidget.generated.h"

class AMCPlayerController;
class UHorizontalBox;
class UTextBlock;
class UBorder;
class UDataTable;
class UMCPerkCardWidget;
struct FMCPerkCardPresentation;

UCLASS()
class UMCPerkCardButton : public UButton
{
    GENERATED_BODY()
public:
    UMCPerkCardButton();
    void Configure(AMCPlayerController* Player, int32 Index);
    int32 GetChoiceIndex() const { return ChoiceIndex; }
    void SetInteractionAllowed(bool bAllowed);
    bool IsInteractionAllowed() const { return bInteractionAllowed; }
private:
    UFUNCTION() void Choose();
    TWeakObjectPtr<AMCPlayerController> Controller;
    int32 ChoiceIndex = INDEX_NONE;
    bool bInteractionAllowed=true;
};

/** Three mutually exclusive personal level or chest reward cards. */
UCLASS()
class MESSCONTROL_API UMCPerkChoiceWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    void ShowChoices(const TArray<FName>& IDs, EMCPerkPolarity Polarity);
    void SetTeamBoosterHeader();
    /** Presentation of an authority timestamp; only the server resolves an expired choice. */
    void SetStageChoiceDeadline(double ServerEndsAt);
    void ShowLevelChoices(const TArray<FName>& IDs,int32 TeamLevel,int32 PendingChoices);
    void ShowRogueChoices(const FMCRogueChestOffer& Offer);
    void UpdateLevelChoiceHeader(int32 TeamLevel,int32 PendingChoices);
    void SetSelectionPending(bool bPending);
    void ShowTeamWaiting(int32 Ready,int32 Total);
    bool IsWaitingForTeam() const { return bWaitingForTeam; }
    bool IsShowingChoices() const { return bShowingChoices; }
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI|Data") TObjectPtr<UDataTable> CardTable;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI|Data") TObjectPtr<UDataTable> LocalizationTable;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI|Components") TSubclassOf<UMCPerkCardWidget> CardWidgetClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Preview|Rewards") TArray<FName> PreviewPerkIDs;
    virtual UDataTable* GetResolvedTranslationTable() const override;
protected:
    virtual void OnLocalizationChanged() override;
    virtual void NativeDestruct() override;
    virtual void BeginDestroy() override;
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry, const FAnalogInputEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event) override;
private:
    void BuildChoices(const TArray<FName>& IDs,EMCPerkPolarity Polarity,bool bPersonal,bool bReadOnly=false);
    void AddAction(const FText& Text,int32 Index);
    void MoveGamepadSelection(int32 Direction);
    void RefreshGamepadSelection();
    FText Copy(FName Key,const FText& Fallback) const;
    FText Format(const FText& Pattern,const FFormatNamedArguments& Arguments) const;
    FString DisplayCulture() const;
    FMCPerkCardPresentation MakePresentation(FName ID,const FMCPerkDefinition& Definition,bool bPersonal,int32 PreviewStacks=INDEX_NONE) const;
    void SetOrdinaryHeader();
    void RefreshRogueCopy(const FMCRogueChestOffer& Offer);
    void CacheDataTables();
    void UnobserveCardTable();
    void NormalizePresentationLayout();
    void EnsureStageChoiceCountdown();
    void UpdateStageChoiceCountdown();
    UPROPERTY(Transient) TObjectPtr<UBorder> PanelBorder;
    UPROPERTY(Transient) TObjectPtr<UBorder> HeaderBorder;
    UPROPERTY(Transient) TObjectPtr<UBorder> StageCountdownBorder;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StageCountdownText;
    UPROPERTY() TObjectPtr<UTextBlock> Title;
    UPROPERTY() TObjectPtr<UTextBlock> Subtitle;
    UPROPERTY() TObjectPtr<UHorizontalBox> Cards;
    UPROPERTY() TObjectPtr<UHorizontalBox> Actions;
    UPROPERTY() TObjectPtr<UTextBlock> Hint;
    UPROPERTY() TArray<TObjectPtr<UMCPerkCardButton>> Buttons;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCPerkCardWidget>> CardWidgets;
    UPROPERTY(Transient) TObjectPtr<UDataTable> PreviewDefinitions;
    UPROPERTY(Transient) TObjectPtr<UDataTable> CachedCardTable;
    UPROPERTY(Transient) TArray<FName> CurrentChoiceIDs;
    UPROPERTY(Transient) TArray<FMCPerkDefinition> CurrentDefinitions;
    UPROPERTY(Transient) FMCRogueChestOffer CurrentRogueOffer;
    UPROPERTY(Transient) TArray<FButtonStyle> ButtonStyles;
    int32 GamepadSelection=0;
    double NextStickNavigationAt=0;
    double StageChoiceDeadline=0;
    int32 LastStageChoiceSecond=INDEX_NONE;
    bool bShowingChoices = false;
    bool bNavigationHighlight=false;
    bool bSelectionPending = false;
    bool bWaitingForTeam=false;
    bool bTeamBooster=false;
    bool bLevelPanel=false,bOptionalReward=false,bConfirming=false;
    bool bApplyingPreview=false,bPreviewRerollSpent=false;
    bool bHasPreviewFixture=false;
    FName LastPreviewState;
    FString LastPreviewCulture;
    TArray<FName> LastPreviewIDs;
    EMCGameplayUIPreviewScenario LastPreviewScenario=EMCGameplayUIPreviewScenario::Default;
    TWeakObjectPtr<UDataTable> LastPreviewCardTable;
    TWeakObjectPtr<UDataTable> LastPreviewLocalizationTable;
    TWeakObjectPtr<UClass> LastPreviewCardWidgetClass;
    TWeakObjectPtr<UDataTable> ObservedCardTable;
    FDelegateHandle CardTableHandle;
    int32 CurrentTeamLevel=0,CurrentPendingChoices=0,CurrentReady=0,CurrentTotal=0;
};
