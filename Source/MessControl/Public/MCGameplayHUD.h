#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCGameplayHUD.generated.h"
class UMCStaminaWidget;
class UBorder;
class USizeBox;
class UTextBlock;
class UProgressBar;
class UButton;
struct FMCGameDirectorState;

UCLASS()
class MESSCONTROL_API UMCGameplayHUD : public UUserWidget
{
    GENERATED_BODY()
public:
    /** Use a Blueprint subclass of MCStaminaWidget to replace the native gauge. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="HUD") TSubclassOf<UMCStaminaWidget> StaminaWidgetClass;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
private:
    UPROPERTY(Transient) TObjectPtr<UMCStaminaWidget> StaminaWidget;
    UPROPERTY(Transient) TObjectPtr<UBorder> DirectorPanel;
    UPROPERTY(Transient) TObjectPtr<USizeBox> DirectorPanelSize;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DirectorText;
    UPROPERTY(Transient) TObjectPtr<UBorder> DirectorCandidatesPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DirectorCandidatesText;
    UPROPERTY(Transient) TObjectPtr<UButton> DirectorLogButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DirectorLogButtonText;
    UPROPERTY(Transient) TObjectPtr<UBorder> ProgressionPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ProgressionText;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> ProgressionBar;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UWidget>> Widgets;
    float RefreshElapsed=1;
    bool bHadSprayReticle=false;
    UWidget* Find(FName Name) const;
    void RefreshState();
    void EnsureDirectorMonitor();
    void EnsureProgressionPanel();
    void RefreshDirectorMonitor(const FMCGameDirectorState& State);
    UFUNCTION() void OpenDirectorLog();
};

/** Replaceable vector icon widget; layout, tint and size are editable in UMG. */
UCLASS()
class MESSCONTROL_API UMCHUDIcon : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") int32 ToolSlot=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") bool bTooth=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") FLinearColor Tint=FLinearColor(.98f,.97f,.93f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Expression") bool bDead=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Expression") bool bSad=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Expression") bool bHappy=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Icon") bool bHost=false;
protected:
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
};
