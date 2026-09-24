#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCPrototypeWidget.generated.h"
class UTextBlock;
class UProgressBar;
class UVerticalBox;
class USlider;
class UBorder;
class UEditableTextBox;

UCLASS(Blueprintable)
class MESSCONTROL_API UMCPrototypeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void ToggleTuning();
    void ToggleConnection();
    bool IsPanelOpen() const;
    bool IsTuningOpen() const;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    UTextBlock* AddText(UVerticalBox* Box,const FString& Text,int32 Size,FLinearColor Color);
    void RefreshSliders();
    UFUNCTION() void TuningChanged(float Value);
    UFUNCTION() void SaveClicked();
    UFUNCTION() void ResetClicked();
    UFUNCTION() void HostClicked();
    UFUNCTION() void JoinClicked();
    UPROPERTY() TObjectPtr<UTextBlock> DayLabel;
    UPROPERTY() TObjectPtr<UTextBlock> EventLabel;
    UPROPERTY() TObjectPtr<UTextBlock> InstructionLabel;
    UPROPERTY() TObjectPtr<UTextBlock> TimeLabel;
    UPROPERTY() TObjectPtr<UTextBlock> HealthLabel;
    UPROPERTY() TObjectPtr<UTextBlock> TaskLabel;
    UPROPERTY() TObjectPtr<UTextBlock> SaveLabel;
    UPROPERTY() TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY() TObjectPtr<UBorder> TuningPanel;
    UPROPERTY() TObjectPtr<UBorder> ConnectionPanel;
    UPROPERTY() TObjectPtr<UEditableTextBox> AddressBox;
    UPROPERTY() TArray<TObjectPtr<USlider>> Sliders;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SliderLabels;
    int32 LastDay = -1;
    int32 LastPhase = -1;
    bool bRefreshing = false;
};
