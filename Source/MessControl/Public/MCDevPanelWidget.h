#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MCDevCommands.h"
#include "MCDevPanelWidget.generated.h"
class AMCPlayerController;
class UTextBlock;
class UVerticalBox;

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
    UPROPERTY() TObjectPtr<UVerticalBox> Steps;
    UPROPERTY() TObjectPtr<UVerticalBox> Actions;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
};
