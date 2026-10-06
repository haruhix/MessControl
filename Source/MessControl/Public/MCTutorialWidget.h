#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCTutorialWidget.generated.h"

class AMCTutorialDirector;
class UBorder;
class UButton;
class UCanvasPanelSlot;
class UImage;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;

/** Local presentation of the server-owned Day 0 state. Input mode remains the controller's responsibility. */
UCLASS()
class MESSCONTROL_API UMCTutorialWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UMCTutorialWidget(const FObjectInitializer& ObjectInitializer);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial") TObjectPtr<UTexture2D> FairyPortrait;
    void RefreshState();
    bool RequiresMenuInput() const;
    bool IsTutorialVisible() const;
    UWidget* GetReadyFocusTarget() const;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    UFUNCTION() void ReadyClicked();
    UFUNCTION() void MenuClicked();
    void UpdatePortrait();
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> CardSlot;
    UPROPERTY() TObjectPtr<USizeBox> CardSize;
    UPROPERTY() TObjectPtr<UBorder> Card;
    UPROPERTY() TObjectPtr<UImage> PortraitImage;
    UPROPERTY() TObjectPtr<UVerticalBox> PortraitFallback;
    UPROPERTY() TObjectPtr<UTextBlock> StageTitle;
    UPROPERTY() TObjectPtr<UTextBlock> Dialogue;
    UPROPERTY() TObjectPtr<UTextBlock> Action;
    UPROPERTY() TObjectPtr<UTextBlock> OwnProgress;
    UPROPERTY() TObjectPtr<UTextBlock> TeamProgress;
    UPROPERTY() TObjectPtr<UTextBlock> Timer;
    UPROPERTY() TObjectPtr<UProgressBar> Progress;
    UPROPERTY() TObjectPtr<UButton> ReadyButton;
    UPROPERTY() TObjectPtr<UTextBlock> ReadyLabel;
    UPROPERTY() TObjectPtr<UButton> MenuButton;
    TWeakObjectPtr<UTexture2D> DisplayedPortrait;
    float RefreshElapsed=0;
    float DesiredCardHeight=230;
};
