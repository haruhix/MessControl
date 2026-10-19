#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MCMainMenuWidget.generated.h"

class UCheckBox;
class UBorder;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UMCMainMenuWidget;

/** Presentation used only when an authored front end embeds this widget. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCEmbeddedMenuStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    bool bTransparentBackdrop=true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    FLinearColor CardColor=FLinearColor(.025f,.004f,.009f,.94f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    FLinearColor TextColor=FLinearColor(.90f,.855f,.73f,1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    FLinearColor AccentColor=FLinearColor(.258f,.956f,.708f,1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance")
    FLinearColor ButtonColor=FLinearColor(.07f,.013f,.024f,1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance", meta=(ClampMin="0.0", ClampMax="32.0"))
    float CornerRadius=12.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCMainMenuBackRequested);

enum class EMCMainMenuAction : uint8
{
    Tutorial, CreateLobby, JoinLobby, Settings, Credits, Exit, Back,
    Host, FindRooms, JoinRoom, JoinAddress, ApplySettings, KeepSettings, RevertSettings,
    StartLobby, Invite, LeaveLobby, ConfirmLeave, ConfirmExit, Resume, Emotes
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

    UFUNCTION(BlueprintCallable, Category="Front End")
    void FocusMenu();

    /** Bind OnBackRequested, enter this mode, then add to the authored host. Only day 1 has a launch target. */
    UFUNCTION(BlueprintCallable, Category="Front End|Embedded")
    void ShowEmbeddedConnection(int32 SelectedDay);

    /** Uses the existing settings and display-confirmation policy; root Back returns control to the host. */
    UFUNCTION(BlueprintCallable, Category="Front End|Embedded")
    void ShowEmbeddedSettings();

    /** Same policy as Escape/B: nested page Back, video rollback, or one root close request. */
    UFUNCTION(BlueprintCallable, Category="Front End|Embedded")
    void RequestBack();

    /** The host removes this widget and restores its own focus; this widget never changes the host's page. */
    UPROPERTY(BlueprintAssignable, Category="Front End|Embedded")
    FMCMainMenuBackRequested OnBackRequested;

    /** Set before ShowEmbeddedConnection/ShowEmbeddedSettings; does not change the legacy main/lobby/pause style. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Front End|Embedded", meta=(ExposeOnSpawn="true"))
    FMCEmbeddedMenuStyle EmbeddedStyle;
    /** Normalize a local IPv4/DNS endpoint into an unambiguous Unreal network URL. */
    static bool NormalizeJoinAddress(const FString& Input, FString& OutAddress);
    void HandleAction(EMCMainMenuAction Action);
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry, const FAnalogInputEvent& Event) override;
private:
    enum class EPage : uint8 { Main, Host, Join, Settings, Credits, Exit, Lobby, Leave, VideoConfirm, Pause, Connection };
    enum class EEmbeddedRoot : uint8 { None, Connection, Settings };
    FString LocalizedCopy(FName Key,const FString& Fallback) const;
    FString FormatCopy(FName Key,const FString& Fallback,const FFormatNamedArguments& Arguments) const;
    void ShowPage(EPage NewPage);
    void ApplyPresentation();
    void LeaveEmbeddedMode();
    bool CanLaunchEmbeddedDay() const;
    const FLinearColor& MenuTextColor() const;
    const FLinearColor& MenuAccentColor() const;
    void BuildSettings();
    void RefreshLobby();
    void FocusFirst();
    bool MoveGamepadFocus(int32 Direction);
    void SetStatus(const FString& Text);
    void ApplySettings();
    void RevertVideoSettings();
    UFUNCTION() void HostTutorialChanged(bool bChecked);
    UFUNCTION() UWidget* GeneratePickerItem(FString Item);
    UTextBlock* AddText(const FString& Text, int32 Size=17);
    UMCMainMenuActionButton* AddButton(const FString& Text, EMCMainMenuAction Action);
    UComboBoxString* AddPicker(const FString& Label);

    UPROPERTY(Transient) TObjectPtr<UBorder> Backdrop;
    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
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
    UPROPERTY(Transient) TObjectPtr<UCheckBox> HostTutorialCheck;
    UPROPERTY(Transient) TArray<TObjectPtr<UButton>> Buttons;
    UPROPERTY(Transient) TArray<TObjectPtr<UButton>> OnlineButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GamepadTargets;
    double NextStickNavigationAt=0;
    TArray<FIntPoint> Resolutions;
    EPage Page=EPage::Main;
    EEmbeddedRoot EmbeddedRoot=EEmbeddedRoot::None;
    int32 EmbeddedSelectedDay=0;
    bool bEmbeddedCloseRequested=false;
    bool bLobby=false;
    bool bPauseMenu=false;
    bool bHostStartWithTutorial=false;
    bool bVideoConfirmationPending=false;
    FIntPoint PreviousResolution=FIntPoint::ZeroValue;
    int32 PreviousWindowMode=0;
    double VideoConfirmationEndsAt=0;
    float RefreshAccumulator=0;
    int32 LastSearchRevision=INDEX_NONE;
    FString LastSessionStatus;
};
