#pragma once

#include "MCGameplayUIWidget.h"
#include "MCMainMenuWidget.h"
#include "MCUnifiedSettingsWidget.generated.h"

class UMCUnifiedSettingsWidget;
class USlider;
class UCheckBox;
class UWidgetSwitcher;

/** Runtime delegate adapter. The old menu remains the only settings/draft owner. */
UCLASS()
class MESSCONTROL_API UMCUnifiedSettingsBinding : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<UMCUnifiedSettingsWidget> Owner;
    UPROPERTY(Transient) FName SourceName;
    UPROPERTY(Transient) FName ViewWidgetName;
    UPROPERTY(Transient) EMCMainMenuAction Action=EMCMainMenuAction::Back;
    UFUNCTION() void PickerChanged(FString Value,ESelectInfo::Type SelectionType);
    UFUNCTION() void SliderChanged(float Value);
    UFUNCTION() void CheckChanged(bool Value);
    UFUNCTION() void Clicked();
};

/** PSD settings presentation. Proxies edit the original menu controls, never a second settings copy. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedSettingsWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedSettingsWidget();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview",meta=(ClampMin="0",ClampMax="4"))
    int32 PreviewSettingsTab=0;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void SetBackend(UMCMainMenuWidget* Menu);
    void RefreshFromBackend();
    UWidget* GetDefaultFocusTarget() const;
    bool RouteRebindKey(FKey Key);
    bool IsVoiceRebinding() const;
    void ForwardPicker(FName Name,const FString& Value);
    void ForwardSlider(FName Name,float Value);
    void ForwardCheck(FName Name,bool Value);
    void ForwardAction(FName Name,EMCMainMenuAction Action);
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
private:
    void BindControls();
    void SelectTab(int32 Index);
    void RefreshValueLabels();
    UWidget* SourceControl(FName Name) const;
    TWeakObjectPtr<UMCMainMenuWidget> Backend;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUnifiedSettingsBinding>> Bindings;
    bool bSynchronizing=false;
    bool bVideoModalActive=false;
    float RefreshElapsed=0.f;
};
