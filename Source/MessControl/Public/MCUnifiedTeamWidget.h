#pragma once

#include "MCGameplayUIWidget.h"
#include "MCScoreboardWidget.h"
#include "MCUnifiedTeamWidget.generated.h"

class UMCUniversalUIWidget;
class UTextBlock;
class UVerticalBox;
class SMCUnifiedMicrophone;

/** Editable vector microphone; no texture, material, or first-use resource load. */
UCLASS()
class MESSCONTROL_API UMCUnifiedMicrophone : public UWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") FLinearColor Tint=FLinearColor(.024f,.024f,.227f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") FLinearColor MutedTint=FLinearColor(.456f,.046f,.09f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") bool bMuted=false;
    void SetMuted(bool Muted);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};

USTRUCT()
struct FMCUnifiedTeamRow
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> Surface;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Name;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Points;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Ping;
    UPROPERTY(Transient) TObjectPtr<UMCUnifiedMicrophone> Microphone;
    UPROPERTY(Transient) TObjectPtr<UMCScoreboardMuteButton> MuteButton;
};

/** A view adapter: forwards Hub actions to the original scoreboard's controls. */
UCLASS()
class MESSCONTROL_API UMCUnifiedPartyButton : public UButton
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") FName SourceControl;
    void SetBackend(UMCScoreboardWidget* InBackend);
private:
    UFUNCTION() void Forward();
    TWeakObjectPtr<UMCScoreboardWidget> Backend;
};

/** PSD TEAM card. Player/voice/room state and accepted actions remain in the source scoreboard. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedTeamWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedTeamWidget();
    /** PSD heading face only; ordinary player/voice text keeps the shared body font. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance|Typography") FSlateFontInfo TeamTitleFont;
    /** Maximum width of the centered team window in logical viewport units. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hub Party|Layout",meta=(ClampMin="240",ClampMax="900")) float HubPanelWidth=760.f;
    /** Horizontal and vertical clearance from the viewport edges. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hub Party|Layout",meta=(ClampMin="0")) FVector2D HubPanelInset=FVector2D(32,48);
    /** Scales roster fonts, row sizes and spacing within the window. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hub Party|Layout",meta=(ClampMin="0.75",ClampMax="1.5")) float HubContentScale=1.f;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void SetBackend(UMCScoreboardWidget* InBackend);
    void RefreshFromBackend();
    UWidget* GetDefaultFocusTarget() const;
    void FocusVoiceControls();
    void RequestClose();
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void OnLocalizationChanged() override;
private:
    void EnsureRowsViewport();
    void UpdateLayout();
    FVector2D LastLayoutViewport=FVector2D::ZeroVector;
    void CacheControls();
    void PresentEntries(const TArray<FMCScoreboardEntry>& Entries,bool bHub);
    void RefreshHubControls();
    void MoveVoiceFocus(int32 Direction);
    UPROPERTY(Transient) TArray<FMCUnifiedTeamRow> Rows;
    TWeakObjectPtr<UMCScoreboardWidget> Backend;
};
