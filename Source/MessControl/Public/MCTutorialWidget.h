#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
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
class MESSCONTROL_API UMCTutorialWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCTutorialWidget(const FObjectInitializer& ObjectInitializer);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial") TObjectPtr<UTexture2D> FairyPortrait;
    void RefreshState();
    bool RequiresMenuInput() const;
    bool IsTutorialVisible() const;
    UWidget* GetReadyFocusTarget() const;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Tutorial") FText PreviewTitle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Tutorial", meta=(MultiLine=true)) FText PreviewDialogue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Tutorial", meta=(MultiLine=true)) FText PreviewInstruction;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Tutorial") int32 PreviewCurrent=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Tutorial") int32 PreviewRequired=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Tutorial") bool bPreviewShortFlow=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Tutorial") bool bPreviewReady=false;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    UFUNCTION() void ReadyClicked();
    UFUNCTION() void MenuClicked();
    void UpdatePortrait();
    FText Copy(FName Key,const FText& Fallback) const;
    FText DirectorCopy(const FText& Text) const;
    FString DisplayCulture() const;
    void BindPresentationTree();
    void NormalizePresentationLayout();
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
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> PortraitBorder;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PortraitStar;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PortraitName;
    TWeakObjectPtr<UTexture2D> DisplayedPortrait;
    float RefreshElapsed=0;
    float DesiredCardHeight=230;
    bool bEditorFixture=false;
    bool bFixtureComplete=false;
    bool bFixtureShort=false;
};
