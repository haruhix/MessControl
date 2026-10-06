#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MCMainMenuWidget.generated.h"

class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UMCMainMenuWidget;

enum class EMCMainMenuAction : uint8
{
    Tutorial, CreateLobby, JoinLobby, Settings, Credits, Exit, Back,
    Host, FindRooms, JoinRoom, JoinAddress, ApplySettings, KeepSettings, RevertSettings,
    StartLobby, Invite, LeaveLobby, ConfirmLeave, ConfirmExit, Resume
};

/** Small delegate adapter so runtime-generated buttons can share the same action handler. */
UCLASS()
class MESSCONTROL_API UMCMainMenuActionButton : public UButton
{
    GENERATED_BODY()
public:
    void Configure(UMCMainMenuWidget* InMenu, EMCMainMenuAction InAction);
private:
    UFUNCTION() void Clicked();
    UPROPERTY(Transient) TObjectPtr<UMCMainMenuWidget> Menu;
    EMCMainMenuAction Action=EMCMainMenuAction::Back;
};

/** Basic menu and lobby; does not modify arena actors or run rules. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCMainMenuWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void ShowLobby();
    void ShowPause();
    void FocusMenu();
    /** Normalize a local IPv4/DNS endpoint into an unambiguous Unreal network URL. */
    static bool NormalizeJoinAddress(const FString& Input, FString& OutAddress);
    void HandleAction(EMCMainMenuAction Action);
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    enum class EPage : uint8 { Main, Host, Join, Settings, Credits, Exit, Lobby, Leave, VideoConfirm, Pause };
    void ShowPage(EPage NewPage);
    void BuildSettings();
    void RefreshLobby();
    void FocusFirst();
    void SetStatus(const FString& Text);
    void ApplySettings();
    void RevertVideoSettings();
    UTextBlock* AddText(const FString& Text, int32 Size=17);
    UMCMainMenuActionButton* AddButton(const FString& Text, EMCMainMenuAction Action);
    UComboBoxString* AddPicker(const FString& Label);

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Content;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LobbyPlayers;
    UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> RoomPicker;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> AddressBox;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> ResolutionPicker;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> WindowModePicker;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> QualityPicker;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> VSyncCheck;
    UPROPERTY(Transient) TArray<TObjectPtr<UButton>> Buttons;
    UPROPERTY(Transient) TArray<TObjectPtr<UButton>> OnlineButtons;
    TArray<FIntPoint> Resolutions;
    EPage Page=EPage::Main;
    bool bLobby=false;
    bool bPauseMenu=false;
    bool bVideoConfirmationPending=false;
    FIntPoint PreviousResolution=FIntPoint::ZeroValue;
    int32 PreviousWindowMode=0;
    double VideoConfirmationEndsAt=0;
    float RefreshAccumulator=0;
    int32 LastSearchRevision=INDEX_NONE;
    FString LastSessionStatus;
};
