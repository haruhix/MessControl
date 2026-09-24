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
class UScrollBox;

UCLASS(Blueprintable)
class MESSCONTROL_API UMCPrototypeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void ToggleTuning();
    void ToggleConnection();
    bool IsPanelOpen() const;
    bool IsTuningOpen() const;
    void ScrollToPhysics();
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
    UFUNCTION() void PhysicsChanged(float Value);
    UFUNCTION() void FallClicked();
    UFUNCTION() void GetUpClicked();
    UFUNCTION() void DummyClicked();
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
    UPROPERTY() TArray<TObjectPtr<USlider>> PhysicsSliders;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> PhysicsLabels;
    UPROPERTY() TObjectPtr<UVerticalBox> PhysicsBox;
    UPROPERTY() TObjectPtr<UScrollBox> TuningScroll;
    int32 LastDay = -1;
    int32 LastPhase = -1;
    bool bRefreshing = false;
};
