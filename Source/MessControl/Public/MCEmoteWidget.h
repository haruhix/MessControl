#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/Button.h"
#include "MCPresentationTypes.h"
#include "MCEmoteWidget.generated.h"
class UVerticalBox;
class UTextBlock;
class AMCPlayerController;
class UBorder;
class UMCEmoteLibrary;

/** Paint-only child placed below the wheel's normal UMG labels. */
UCLASS()
class UMCEmoteWheelWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) FLinearColor SelectedTint=FLinearColor::White;
    UPROPERTY(Transient) FLinearColor NormalTint=FLinearColor::White;
    UPROPERTY(Transient) FLinearColor BorderTint=FLinearColor::White;
    UPROPERTY(Transient) int32 SelectedSector=0;
protected:
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
};

UCLASS()
class UMCEmoteButton : public UButton
{
    GENERATED_BODY()
public:
    UMCEmoteButton();
    void Configure(AMCPlayerController* Player,FName Emote);
    void ConfigureAlarm(AMCPlayerController* Player,EMCPlayerAlarm Value);
    void ConfigureColor(AMCPlayerController* Player,FLinearColor Value);
    void ActivateChoice();
    int32 ChoiceIndex=INDEX_NONE;
    FName Id;
    EMCPlayerAlarm Alarm=EMCPlayerAlarm::None;
    bool bColorChoice=false;
private:
    FLinearColor PlayerColor=FLinearColor::White;
    UFUNCTION() void Clicked();
    UFUNCTION() void Hovered();
    UPROPERTY() TObjectPtr<AMCPlayerController> Controller;
};
UCLASS()
class MESSCONTROL_API UMCEmoteWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    void Refresh();
    void BeginSelection(bool bHeld);
    void SelectChoice(int32 Index);
    bool ConfirmSelection();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Emotes") TSoftObjectPtr<UMCEmoteLibrary> PreviewLibrary = TSoftObjectPtr<UMCEmoteLibrary>(FSoftObjectPath(TEXT("/Game/Data/DA_Emotes.DA_Emotes")));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Emotes", meta=(ClampMin="0",ClampMax="2")) int32 PreviewCategory=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Emotes") bool bPreviewActionsAvailable=true;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float Dt) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeOnFocusLost(const FFocusEvent& Event) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
private:
    UPROPERTY() TObjectPtr<UVerticalBox> Items;
    UPROPERTY() TObjectPtr<UTextBlock> Hint;
    UPROPERTY() TArray<TObjectPtr<UMCEmoteButton>> Buttons;
    UPROPERTY() TObjectPtr<UTextBlock> CategoryTitle;
    UPROPERTY(Transient) TObjectPtr<UBorder> ListPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Footer;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ColorHeading;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SectorLabels;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCEmoteButton>> ColorButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ColorLabels;
    UPROPERTY(Transient) TObjectPtr<UMCEmoteLibrary> LoadedPreviewLibrary;
    UPROPERTY(Transient) TObjectPtr<UMCEmoteWheelWidget> Wheel;
    int32 Category=0;
    int32 KeyboardChoice=0;
    float RefreshElapsed=.1f;
    void SelectCategory(int32 Value);
    void RefreshAvailability();
    void EnsureWheelPresentation();
    FText Copy(FName Key,const FText& Fallback) const;
    FText CategoryCopy(int32 Index) const;
    bool IsPreviewing() const { return bEditorFixture || IsDesignTime(); }
    bool bEditorFixture=false;
};
