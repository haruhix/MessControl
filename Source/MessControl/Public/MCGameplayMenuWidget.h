#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/Button.h"
#include "MCGameplayMenuWidget.generated.h"

class UBorder;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;
class UMCGameplayMenuWidget;

UENUM(BlueprintType)
enum class EMCGameplayMenuAction : uint8
{
    Resume, Emotes, Settings, Credits, Leave, Exit, Back,
    ApplySettings, KeepSettings, RevertSettings, ConfirmLeave, ConfirmExit,
    Host, FindRooms, JoinRoom, JoinAddress, Invite
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCGameplayMenuRequest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMCGameplayMenuJoinRequest, const FString&, Address);

/** Authored buttons retain their action when copied into a Widget Blueprint. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCGameplayMenuActionButton : public UButton
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") EMCGameplayMenuAction MenuAction=EMCGameplayMenuAction::Back;
    void Configure(UMCGameplayMenuWidget* InMenu);
private:
    UFUNCTION() void Clicked();
    UPROPERTY(Transient) TObjectPtr<UMCGameplayMenuWidget> Menu;
};

/** Gameplay-only pause/settings/connection presentation. The controller owns input modes and travel. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCGameplayMenuWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Menu") void ShowPause();
    UFUNCTION(BlueprintCallable, Category="Menu") void ShowConnection();
    UFUNCTION(BlueprintCallable, Category="Menu") void FocusMenu();
    UFUNCTION(BlueprintCallable, Category="Menu") void RequestBack();
    UFUNCTION(BlueprintCallable, Category="Menu") void HandleAction(EMCGameplayMenuAction Action);

    UPROPERTY(BlueprintAssignable, Category="Menu|Requests") FMCGameplayMenuRequest OnResumeRequested;
    UPROPERTY(BlueprintAssignable, Category="Menu|Requests") FMCGameplayMenuRequest OnEmotesRequested;
    UPROPERTY(BlueprintAssignable, Category="Menu|Requests") FMCGameplayMenuRequest OnConnectionCloseRequested;
    UPROPERTY(BlueprintAssignable, Category="Menu|Requests") FMCGameplayMenuRequest OnLeaveRequested;
    UPROPERTY(BlueprintAssignable, Category="Menu|Requests") FMCGameplayMenuRequest OnExitRequested;
    UPROPERTY(BlueprintAssignable, Category="Menu|Requests") FMCGameplayMenuRequest OnHostRequested;
    /** Normalized unreal://host:port URL; controller should ClientTravel with TRAVEL_Absolute. */
    UPROPERTY(BlueprintAssignable, Category="Menu|Requests") FMCGameplayMenuJoinRequest OnJoinAddressRequested;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview") bool bPreviewSteamAvailable=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview") bool bPreviewHost=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview",meta=(MultiLine=true)) FText PreviewStatus;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview",meta=(ClampMin="0",ClampMax="15")) int32 PreviewVideoSeconds=15;

    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
    static bool NormalizeJoinAddress(const FString& Input, FString& OutAddress);
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event) override;
private:
    enum class EPage:uint8 { Pause,Settings,Credits,Leave,Exit,VideoConfirm,Connection };
    void BindPresentationTree();
    void ShowPage(EPage NewPage);
    void PopulateSettings();
    void ApplySettings();
    void RevertVideoSettings();
    void RefreshConnection();
    bool MoveFocus(int32 Direction);
    bool IsPreviewing() const {return bEditorFixture || IsDesignTime();}
    bool SteamAvailable() const;
    FText Copy(FName Key,const FText& Fallback) const;
    void SetStatus(const FText& Text);
    void UpdatePageCopy();
    UTextBlock* AddText(UVerticalBox* Parent,FName Name,const FText& Text,int32 Size=18);
    UMCGameplayMenuActionButton* AddButton(UVerticalBox* Parent,FName Name,const FText& Text,EMCGameplayMenuAction Action);
    UComboBoxString* AddPicker(UVerticalBox* Parent,FName Name,FName LabelName,const FText& Label);
    UFUNCTION() UWidget* GeneratePickerItem(FString Item);

    UPROPERTY(Transient) TObjectPtr<UBorder> Backdrop;
    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Heading;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Hint;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> ResolutionPicker;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> ModePicker;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> QualityPicker;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> VSyncCheck;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> RoomPicker;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> AddressBox;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCGameplayMenuActionButton>> Buttons;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> FocusTargets;
    // ComboBox-generated content lives in Slate rather than the panel hierarchy.
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GeneratedPickerLabels;
    TArray<FIntPoint> Resolutions;
    EPage Page=EPage::Pause;
    bool bConnectionRoot=false;
    bool bEditorFixture=false;
    bool bCloseRequested=false;
    bool bVideoConfirmationPending=false;
    FIntPoint PreviousResolution=FIntPoint::ZeroValue;
    int32 PreviousWindowMode=0;
    double VideoConfirmationEndsAt=0;
    double NextStickNavigationAt=0;
    float RefreshAccumulator=0;
    int32 LastSearchRevision=INDEX_NONE;
    FString LastSessionStatus;
    TWeakObjectPtr<UWidget> LastFocusTarget;
};
