#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MCEmoteWidget.generated.h"
class UVerticalBox;
class UTextBlock;
class AMCPlayerController;
UCLASS()
class UMCEmoteButton : public UButton
{
    GENERATED_BODY()
public:
    void Configure(AMCPlayerController* Player,FName Emote);
    FName Id;
private:
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
private:
    UPROPERTY() TObjectPtr<UVerticalBox> Items;
    UPROPERTY() TObjectPtr<UTextBlock> Hint;
    UPROPERTY() TArray<TObjectPtr<UMCEmoteButton>> Buttons;
};
