#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MCPerkTypes.h"
#include "MCPerkChoiceWidget.generated.h"

class AMCPlayerController;
class UHorizontalBox;
class UTextBlock;

UCLASS()
class UMCPerkCardButton : public UButton
{
    GENERATED_BODY()
public:
    UMCPerkCardButton();
    void Configure(AMCPlayerController* Player, int32 Index);
private:
    UFUNCTION() void Choose();
    TWeakObjectPtr<AMCPlayerController> Controller;
    int32 ChoiceIndex = INDEX_NONE;
};

/** Three mutually exclusive personal level or chest reward cards. */
UCLASS()
class MESSCONTROL_API UMCPerkChoiceWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void ShowChoices(const TArray<FName>& IDs, EMCPerkPolarity Polarity);
    void ShowLevelChoices(const TArray<FName>& IDs,int32 TeamLevel,int32 PendingChoices);
    void ShowRogueChoices(const FMCRogueChestOffer& Offer);
    void UpdateLevelChoiceHeader(int32 TeamLevel,int32 PendingChoices);
    void SetSelectionPending(bool bPending);
    bool IsShowingChoices() const { return bShowingChoices; }
protected:
    virtual void NativeOnInitialized() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry, const FAnalogInputEvent& Event) override;
private:
    void BuildChoices(const TArray<FName>& IDs,EMCPerkPolarity Polarity,bool bPersonal);
    void AddAction(const FText& Text,int32 Index);
    void MoveGamepadSelection(int32 Direction);
    void RefreshGamepadSelection();
    UPROPERTY() TObjectPtr<UTextBlock> Title;
    UPROPERTY() TObjectPtr<UTextBlock> Subtitle;
    UPROPERTY() TObjectPtr<UHorizontalBox> Cards;
    UPROPERTY() TObjectPtr<UHorizontalBox> Actions;
    UPROPERTY() TObjectPtr<UTextBlock> Hint;
    UPROPERTY() TArray<TObjectPtr<UMCPerkCardButton>> Buttons;
    TArray<FButtonStyle> ButtonStyles;
    int32 GamepadSelection=0;
    double NextStickNavigationAt=0;
    bool bShowingChoices = false;
    bool bSelectionPending = false;
    bool bLevelPanel=false,bOptionalReward=false,bConfirming=false;
};
