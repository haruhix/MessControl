#pragma once

#include "MCGameplayUIWidget.h"
#include "MCGameplayMenuWidget.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "MCUnifiedGameplayMenuWidget.generated.h"

class UMCUnifiedGameplayMenuWidget;
class UMCUniversalUIWidget;
class UVerticalBox;
class UWidgetSwitcher;
class USizeBox;

/** Runtime-only adapters retain exact source controls and their active-page identity. */
UCLASS()
class MESSCONTROL_API UMCUnifiedGameplayMenuBinding : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<UMCUnifiedGameplayMenuWidget> Owner;
    UPROPERTY(Transient) TWeakObjectPtr<UWidget> Source;
    UPROPERTY(Transient) TObjectPtr<UWidget> View;
    UPROPERTY(Transient) FName PageName;
    UFUNCTION() void Clicked();
    UFUNCTION() void PickerChanged(FString Value,ESelectInfo::Type Selection);
    UFUNCTION() void SliderChanged(float Value);
    UFUNCTION() void CheckChanged(bool Value);
    UFUNCTION() void TextChanged(const FText& Value);
    UFUNCTION() void TextCommitted(const FText& Value,ETextCommit::Type Commit);
    UFUNCTION() UWidget* GeneratePickerItem(FString Item);
};

USTRUCT()
struct FMCUnifiedGameplayMenuControl
{
    GENERATED_BODY()
    UPROPERTY(Transient) FName Name;
    UPROPERTY(Transient) int32 PageIndex=0;
    UPROPERTY(Transient) TObjectPtr<USizeBox> Host;
    UPROPERTY(Transient) TObjectPtr<UWidget> Control;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> Surface;
    UPROPERTY(Transient) TObjectPtr<UMCUnifiedGameplayMenuBinding> Binding;
    EMCGameplayMenuPresentationKind Kind=EMCGameplayMenuPresentationKind::Text;
};

USTRUCT()
struct FMCUnifiedGameplayPickerLabel
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
    UPROPERTY(Transient) TWeakObjectPtr<UWidget> Picker;
};

/** PSD-family gameplay menus; no duplicate preferences, session actions or video transaction. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedGameplayMenuWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedGameplayMenuWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void SetBackend(UMCGameplayMenuWidget* Source);
    bool HasBoundBackend() const { return Backend.IsValid(); }
    void RefreshFromBackend();
    UWidget* GetDefaultFocusTarget() const;
    void RequestBack();
    bool RouteSourceKey(const FKeyEvent& Event);
    bool RouteSourceMouseButton(const FPointerEvent& Event);
    bool RouteSourceAnalog(const FAnalogInputEvent& Event);
    bool IsCapturingKey() const;
    UWidget* MakePickerItem(UMCUnifiedGameplayMenuBinding* Binding,const FString& Item);
    void ForwardButton(UMCUnifiedGameplayMenuBinding* Binding);
    void ForwardPicker(UMCUnifiedGameplayMenuBinding* Binding,const FString& Value);
    void ForwardSlider(UMCUnifiedGameplayMenuBinding* Binding,float Value);
    void ForwardCheck(UMCUnifiedGameplayMenuBinding* Binding,bool Value);
    void ForwardText(UMCUnifiedGameplayMenuBinding* Binding,const FText& Value);
    void ForwardTextCommit(UMCUnifiedGameplayMenuBinding* Binding,const FText& Value,ETextCommit::Type Commit);
    /** Pause, Settings, Credits (design only), Leave, Exit, VideoConfirm, Connection, Audio. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview",meta=(ClampMin="0",ClampMax="7")) int32 PreviewMenuPage=0;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void OnLocalizationChanged() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event) override;
private:
    FMCUnifiedGameplayMenuControl& EnsureControl(int32 PageIndex,const FMCGameplayMenuPresentationControl& Definition);
    void BindControl(FMCUnifiedGameplayMenuControl& View,const FMCGameplayMenuPresentationControl& Source,FName PageName);
    void Present(const FMCGameplayMenuPresentationSnapshot& Snapshot);
    void PresentPreview();
    bool CanForward(const UMCUnifiedGameplayMenuBinding* Binding) const;
    bool MovePresentationFocus(int32 Direction);
    UWidget* ViewFocusForSource(const UWidget* Source) const;
    UMCUniversalUIWidget* MakeShape(FName Name,FLinearColor Fill,bool bButton=false);
    UTextBlock* MakeText(FName Name,int32 Size);
    TWeakObjectPtr<UMCGameplayMenuWidget> Backend;
    UPROPERTY(Transient) TArray<FMCUnifiedGameplayMenuControl> Controls;
    UPROPERTY(Transient) TArray<FMCUnifiedGameplayPickerLabel> GeneratedPickerLabels;
    FName ActiveSourcePage;
    int32 ActivePageIndex=0;
    bool bSynchronizing=false;
    float RefreshElapsed=0;
    double NextNavigationAt=0;
};
