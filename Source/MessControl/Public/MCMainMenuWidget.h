#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "MCMainMenuWidget.generated.h"

class UCheckBox;
class UBorder;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;
class USlider;
class UMCMainMenuWidget;

/** Small editable front-end status, visible only while creating a room or after an actual failure. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCHubPlayStatusWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
private:
    float RefreshElapsed=0.f;
};

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

UENUM(BlueprintType)
enum class EMCMainMenuAction : uint8
{
    Tutorial, CreateLobby, JoinLobby, Settings, Credits, Exit, Back,
    Host, FindRooms, JoinRoom, JoinAddress, ApplySettings, KeepSettings, RevertSettings,
    StartLobby, Invite, LeaveLobby, ConfirmLeave, ConfirmExit, Resume, Emotes,
    SettingsGraphics, SettingsAudio, SettingsLanguage, SettingsControls, SettingsVoice
};

/** Saved pages in WBP_MainMenu. All pages remain available in the UMG Designer. */
UENUM(BlueprintType)
enum class EMCMainMenuPage : uint8
{
    Main, Host, Join, Settings, Credits, Exit, Lobby, Leave, VideoConfirm, Pause, Connection
};

/** Small delegate adapter so runtime-generated buttons can share the same action handler. */
UCLASS()
class MESSCONTROL_API UMCMainMenuActionButton : public UButton
{
    GENERATED_BODY()
public:
    void Configure(UMCMainMenuWidget* InMenu, EMCMainMenuAction InAction);
    /** Select the existing game action in Designer; presentation and button children remain editable. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu")
    EMCMainMenuAction Action=EMCMainMenuAction::Back;
private:
    UFUNCTION() void Clicked();
    UPROPERTY(Transient) TObjectPtr<UMCMainMenuWidget> Menu;
};

/** Editable stock combo box whose initial font uses the same packaged composite as the menu. */
UCLASS()
class MESSCONTROL_API UMCMainMenuPicker : public UComboBoxString
{
    GENERATED_BODY()
public:
    void InitializePresentationFont(const FSlateFontInfo& InFont) { InitFont(InFont); }
    /** Store defaults as reflected data as well as the live list, so saved Designer shells keep their options. */
    void SetPresentationOptions(const TArray<FString>& InOptions,int32 SelectedIndex=0);
    virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
    virtual void PostLoad() override;
private:
    void RestorePresentationOptions();
};

/** Basic menu and lobby; does not modify arena actors or run rules. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCMainMenuWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Front End") void ShowLobby();
    UFUNCTION(BlueprintCallable, Category="Front End") void ShowPause();
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    /** Select a page in Class Defaults to preview all of its editable controls. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview")
    EMCMainMenuPage PreviewPage=EMCMainMenuPage::Main;
    /** Graphics, Audio, Language, Controls, Voice. Never changes player settings in Designer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview",meta=(ClampMin="0",ClampMax="4"))
    int32 PreviewSettingsTab=0;
    UFUNCTION(BlueprintPure, Category="Front End") UWidget* GetActiveMenuPageRoot() const;

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
    /** The authored 3D front end explicitly supplies this overlay palette. Disable to use only Designer styling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Front End|Embedded")
    bool bUseEmbeddedPresentationStyle=true;
    /** Normalize a local IPv4/DNS endpoint into an unambiguous Unreal network URL. */
    static bool NormalizeJoinAddress(const FString& Input, FString& OutAddress);
    UFUNCTION(BlueprintCallable, Category="Front End") void HandleAction(EMCMainMenuAction Action);
    /** Shared entry point for the native and authored 3D Play buttons. No day/menu/lobby page is opened. */
    UFUNCTION(BlueprintCallable, Category="Front End", meta=(WorldContext="WorldContextObject"))
    static bool PlayToHub(const UObject* WorldContextObject);
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void OnLocalizationChanged() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry, const FAnalogInputEvent& Event) override;
private:
    using EPage=EMCMainMenuPage;
    enum class EEmbeddedRoot : uint8 { None, Connection, Settings };
    FString LocalizedCopy(FName Key,const FString& Fallback) const;
    FString FormatCopy(FName Key,const FString& Fallback,const FFormatNamedArguments& Arguments) const;
    void ShowPage(EPage NewPage);
    void BuildPageContents();
    void CacheRootControls();
    void CollapseRetiredMainMenuCopy();
    void CachePageControls();
    void RefreshPageData(bool bResetSettings=true);
    void RefreshSettingsControls();
    void RefreshSettingsOptionLabels(int32 WindowModeIndex, int32 QualityIndex, bool bIncludeCustomQuality);
    void CacheSettingsControls();
    void EnsureVoiceSettingsPresentation();
    void SwitchSettingsTab(int32 Index);
    void RefreshSettingsValueLabels();
    void RefreshSettingsQualityOptions(bool bPreserveSelection);
    FName PageWidgetName(const TCHAR* Suffix) const;
    void ApplyPresentation();
    void ApplyEmbeddedDesignerStyle(bool bEmbedded);
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
    UFUNCTION() void SettingsSliderChanged(float Value);
    UFUNCTION() void SettingsPresetChanged(FString Value,ESelectInfo::Type SelectionType);
    UFUNCTION() void SettingsGroupChanged(FString Value,ESelectInfo::Type SelectionType);
    USlider* AddSettingsSlider(FName Key,const TCHAR* Copy,const TCHAR* Suffix,float Minimum,float Maximum,float Step,float Value);
    UCheckBox* AddSettingsCheck(FName Key,const TCHAR* Copy,const TCHAR* Suffix);
    UTextBlock* AddText(const FString& Text, int32 Size=17, const TCHAR* Suffix=nullptr);
    UMCMainMenuActionButton* AddButton(const FString& Text, EMCMainMenuAction Action);
    UComboBoxString* AddPicker(const FString& Label, const TCHAR* Suffix);

    // Keep brush resources referenced while an explicitly embedded palette temporarily overrides them.
    UPROPERTY(Transient) FSlateBrush DesignerBackdropBrush;
    UPROPERTY(Transient) FSlateBrush DesignerCardBrush;
    FLinearColor DesignerBackdropColor=FLinearColor::White;
    FLinearColor DesignerCardColor=FLinearColor::White;
    FMargin DesignerCardPadding;
    UPROPERTY(Transient) TMap<TObjectPtr<UButton>,FButtonStyle> DesignerButtonStyles;
    UPROPERTY(Transient) TMap<TObjectPtr<UButton>,FLinearColor> DesignerButtonColors;
    UPROPERTY(Transient) TMap<TObjectPtr<UTextBlock>,FSlateColor> DesignerTextColors;
    bool bEmbeddedStyleApplied=false;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
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
    UPROPERTY(Transient) TObjectPtr<UCheckBox> CameraSmoothingCheck;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> SettingsSections;
    UPROPERTY(Transient) TObjectPtr<class UMCVoiceSettingsWidget> VoiceSettings;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UComboBoxString>> SettingsPickers;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<USlider>> SettingsSliders;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> InvertYCheck;
    bool bUpdatingSettingsControls=false;
    int32 ActiveSettingsTab=0;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> HostTutorialCheck;
    UPROPERTY(Transient) TArray<TObjectPtr<UButton>> Buttons;
    UPROPERTY(Transient) TArray<TObjectPtr<UButton>> OnlineButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GamepadTargets;
    // Native fallback generation is used only when no authored tree exists.
    bool bNativeLayout=false;
    bool bBuildingTemplate=false;
    int32 TemplateTextIndex=0;
    mutable TMap<FString,FName> LocalizationKeysByCopy;
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
