#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/Button.h"
#include "MCScoreboardWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UMCHUDIcon;
class UBorder;
class APlayerState;
class UMCScoreboardWidget;
class UHorizontalBox;

/** Keeps its target by identity, even when the table is reordered by score. */
UCLASS()
class MESSCONTROL_API UMCScoreboardMuteButton : public UButton
{
    GENERATED_BODY()
public:
    void Configure(UMCScoreboardWidget* Scoreboard,APlayerState* Player);
    /** A pointer/keyboard press retains its participant if scores reorder before release. */
    APlayerState* GetActionTarget() const;
private:
    UPROPERTY(Transient) TWeakObjectPtr<UMCScoreboardWidget> OwnerScoreboard;
    UPROPERTY(Transient) TWeakObjectPtr<APlayerState> TargetPlayer;
    UPROPERTY(Transient) TWeakObjectPtr<APlayerState> PressedPlayer;
    bool bPressedTarget=false;
    UFUNCTION() void Pressed();
    UFUNCTION() void Clicked();
};

USTRUCT(BlueprintType)
struct FMCScoreboardEntry
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    UPROPERTY(BlueprintReadOnly) int32 Points=0;
    UPROPERTY(BlueprintReadOnly) float PingMilliseconds=0;
    UPROPERTY(BlueprintReadOnly) FLinearColor PlayerColor=FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) bool bHost=false;
    UPROPERTY(BlueprintReadOnly) bool bSpectating=false;
    UPROPERTY(BlueprintReadOnly) bool bLocalPlayer=false;
    UPROPERTY(BlueprintReadOnly) bool bVoiceMuted=false;
    UPROPERTY(BlueprintReadOnly) bool bVoiceControlEnabled=false;
    UPROPERTY(Transient) TObjectPtr<APlayerState> PlayerState;
};

USTRUCT()
struct FMCScoreboardRow
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Name;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Points;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Ping;
    UPROPERTY(Transient) TObjectPtr<UMCHUDIcon> Icon;
    UPROPERTY(Transient) TObjectPtr<UBorder> Background;
    UPROPERTY(Transient) TObjectPtr<UMCScoreboardMuteButton> MuteButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MuteLabel;
};

/** Toggled Hub party panel; held Tab scoreboard in arenas. Mute is local only. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCScoreboardWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Scoreboard") void Refresh();
    void BeginHubPartyPresentation();
    /** Disable for a completely artist-authored party card and animation. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hub Party|Style") bool bUseSoftPartyPresentation=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hub Party|Style",meta=(ClampMin="0",ClampMax="64")) float PartyCornerRadius=24.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hub Party|Style",meta=(ClampMin="0",ClampMax="1")) float PartyPanelOpacity=.92f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hub Party|Motion",meta=(ClampMin="0",ClampMax="1")) float PartyFadeSeconds=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hub Party|Motion",meta=(ClampMin="0",ClampMax="1")) float PartyHoverSeconds=.12f;
    void FocusVoiceControls();
    void TogglePlayerVoice(APlayerState* Player);
    static bool CanControlPlayerVoice(const APlayerState* Player,bool bLocalPlayer);
    UPROPERTY(BlueprintReadOnly,Category="Scoreboard") TArray<FMCScoreboardEntry> PlayerEntries;
    UFUNCTION(BlueprintImplementableEvent,Category="Scoreboard") void OnScoreboardUpdated();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float Dt) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Players;
    UPROPERTY(Transient) TArray<FMCScoreboardRow> Rows;
    float RefreshElapsed=.25f;
    void RebuildRows(int32 Count);
    void EnsureVoiceControls();
    void EnsureHubPartyControls();
    void RefreshHubPartyControls();
    void RefreshSoftPartySurfaces(float Dt);
    float PartyOpenElapsed=1.f;
    TMap<TWeakObjectPtr<UButton>,float> PartyHoverAmounts;
    bool IsHubPartyPresentation() const;
    UFUNCTION() void InviteHubFriends();
    UFUNCTION() void SetPublicRoom();
    UFUNCTION() void SetFriendsRoom();
    UFUNCTION() void SetInviteRoom();
    UFUNCTION() void ClosePartyPanel();
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> HubPartyControls;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> HubPrivacyControls;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HubPartySummary;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HubPartyStatus;
    UPROPERTY(Transient) TObjectPtr<UButton> HubInviteButton;
    UPROPERTY(Transient) TObjectPtr<UButton> HubPublicButton;
    UPROPERTY(Transient) TObjectPtr<UButton> HubFriendsButton;
    UPROPERTY(Transient) TObjectPtr<UButton> HubInviteOnlyButton;
    UPROPERTY(Transient) TObjectPtr<UButton> HubCloseButton;
    void MoveVoiceFocus(int32 Direction);
    void PresentEntries();
    void UpdatePresentationLayout(FVector2D Area=FVector2D::ZeroVector);
    FVector2D LastLayoutArea=FVector2D::ZeroVector;
    bool bLayoutDirty=true;
};
