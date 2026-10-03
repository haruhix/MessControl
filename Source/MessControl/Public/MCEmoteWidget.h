#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MCPresentationTypes.h"
#include "MCEmoteWidget.generated.h"
class UVerticalBox;
class UTextBlock;
class AMCPlayerController;
UCLASS()
class UMCEmoteButton : public UButton
{
    GENERATED_BODY()
public:
    UMCEmoteButton();
    void Configure(AMCPlayerController* Player,FName Emote);
    void ConfigureAlarm(AMCPlayerController* Player,EMCPlayerAlarm Value);
    void ConfigureColor(AMCPlayerController* Player,FLinearColor Value);
    FName Id;
    EMCPlayerAlarm Alarm=EMCPlayerAlarm::None;
    bool bColorChoice=false;
private:
    FLinearColor PlayerColor=FLinearColor::White;
    UFUNCTION() void Clicked();
    UPROPERTY() TObjectPtr<AMCPlayerController> Controller;
};
UCLASS()
class MESSCONTROL_API UMCEmoteWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Refresh();
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float Dt) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
private:
    UPROPERTY() TObjectPtr<UVerticalBox> Items;
    UPROPERTY() TObjectPtr<UTextBlock> Hint;
    UPROPERTY() TArray<TObjectPtr<UMCEmoteButton>> Buttons;
    UPROPERTY() TObjectPtr<UTextBlock> CategoryTitle;
    int32 Category=0;
    int32 KeyboardChoice=0;
    float RefreshElapsed=.1f;
    void SelectCategory(int32 Value);
    void RefreshAvailability();
};
