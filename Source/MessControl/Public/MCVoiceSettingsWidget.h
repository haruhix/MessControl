#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "InputCoreTypes.h"
#include "Components/ComboBoxString.h"
#include "MCVoiceSettingsWidget.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class UProgressBar;
class USlider;
class UTextBlock;
class UVerticalBox;
class APlayerState;
class UMCVoiceChatSubsystem;

/** Editable voice controls. Preferences remain pending until the parent Apply action. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCVoiceSettingsWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    UFUNCTION(BlueprintCallable,Category="Voice") void LoadPendingSettings();
    UFUNCTION(BlueprintCallable,Category="Voice") void ApplyPendingSettings();
    UFUNCTION(BlueprintCallable,Category="Voice") void StopInteraction();
    /** Gameplay ESC applies and saves each deliberate edit; main menu retains its Apply button. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Voice") bool bApplyChangesImmediately=false;
    bool RouteRebindKey(FKey Key);
    bool IsRebinding() const { return bAwaitingKey; }
    void GetFocusableControls(TArray<UWidget*>& Out) const;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void OnLocalizationChanged() override;
private:
    void BindControls();
    void RefreshLive();
    UMCVoiceChatSubsystem* Voice() const;
    UTextBlock* AddLabel(UVerticalBox* Parent,FName Name,FName Key,const TCHAR* Copy);
    USlider* AddSlider(UVerticalBox* Parent,FName Name,FName Key,const TCHAR* Copy,float Maximum,float Value);
    UButton* AddButton(UVerticalBox* Parent,FName Name,FName Key,const TCHAR* Copy);
    UCheckBox* AddCheck(UVerticalBox* Parent,FName Name,FName Key,const TCHAR* Copy);
    UFUNCTION() void RebindClicked();
    UFUNCTION() void MicTestClicked();
    UFUNCTION() void DeviceClicked();
    UFUNCTION() void PlayerChanged(FString Item,ESelectInfo::Type SelectionType);
    UFUNCTION() void PlayerMuteChanged(bool Muted);
    UFUNCTION() void PreferenceToggleChanged(bool Value);
    UFUNCTION() void PreferenceModeChanged(FString Item,ESelectInfo::Type SelectionType);
    UFUNCTION() void PreferenceValueChanged(float Value);
    void CommitImmediatePreferences();
    UPROPERTY(Transient) TObjectPtr<UCheckBox> EnabledCheck;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> SelfMuteCheck;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> PlayerMuteCheck;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> ModePicker;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> PlayerPicker;
    UPROPERTY(Transient) TObjectPtr<USlider> GainSlider;
    UPROPERTY(Transient) TObjectPtr<USlider> ThresholdSlider;
    UPROPERTY(Transient) TObjectPtr<USlider> VolumeSlider;
    UPROPERTY(Transient) TObjectPtr<UButton> KeyButton;
    UPROPERTY(Transient) TObjectPtr<UButton> TestButton;
    UPROPERTY(Transient) TObjectPtr<UButton> DeviceButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KeyLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DeviceLabel;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> InputMeter;
    TArray<TWeakObjectPtr<APlayerState>> Players;
    FKey PendingKey;
    bool bAwaitingKey=false;
    bool bOwnsMicTest=false;
    bool bUpdating=false;
    bool bDraftLoaded=false;
    float RefreshElapsed=0;
    FString PlayerSignature;
};
