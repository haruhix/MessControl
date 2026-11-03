#pragma once

#include "MCGameplayUIWidget.h"
#include "MCMainMenuWidget.h"
#include "MCUnifiedFrontendWidget.generated.h"

class UMCUnifiedFrontendWidget;
class UTextBlock;

/** Only existing runtime secondary pages; retired Host/Credits/Tutorial are not reactivated. */
UENUM(BlueprintType)
enum class EMCUnifiedFrontendPage : uint8 { Join, Connection, Lobby, Leave, Exit };

/** A retained delegate bridge pins each interaction to the exact source page/control it displayed. */
UCLASS()
class MESSCONTROL_API UMCUnifiedFrontendBinding : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<UMCUnifiedFrontendWidget> Owner;
    UPROPERTY(Transient) TWeakObjectPtr<UWidget> SourcePage;
    UPROPERTY(Transient) TWeakObjectPtr<UWidget> SourceControl;
    UPROPERTY(Transient) FName ViewName;
    UPROPERTY(Transient) FName SourceSuffix;
    UPROPERTY(Transient) EMCUnifiedFrontendPage Page=EMCUnifiedFrontendPage::Join;
    UPROPERTY(Transient) EMCMainMenuAction Action=EMCMainMenuAction::Back;
    UPROPERTY(Transient) uint8 Kind=0;
    UFUNCTION() void Clicked();
    UFUNCTION() void PickerChanged(FString Value,ESelectInfo::Type SelectionType);
    UFUNCTION() void AddressChanged(const FText& Value);
};

/** Editable PSD-family shapes over the retained main-menu session/action owner. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedFrontendWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedFrontendWidget();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview")
    EMCUnifiedFrontendPage PreviewPage=EMCUnifiedFrontendPage::Join;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    void SetBackend(UMCMainMenuWidget* Menu);
    void RefreshFromBackend();
    bool HasBoundBackend() const;
    UWidget* GetDefaultFocusTarget() const;
    void RequestBack();
    void ForwardAction(UMCUnifiedFrontendBinding* Binding);
    void ForwardPicker(UMCUnifiedFrontendBinding* Binding,const FString& Value);
    void ForwardAddress(UMCUnifiedFrontendBinding* Binding,const FText& Value);
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void OnLocalizationChanged() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    void BindControls();
    void UnbindControls();
    void SelectPage(EMCUnifiedFrontendPage Page);
    void ApplyPreviewFixture();
    void RefreshTextBounds();
    bool CanForward(const UMCUnifiedFrontendBinding* Binding) const;
    bool IsSourceVisible(const UWidget* Source,const UWidget* Page) const;
    UWidget* FindSource(EMCUnifiedFrontendPage Page,FName Suffix) const;
    UFUNCTION() UWidget* GeneratePickerItem(FString Value);
    TWeakObjectPtr<UMCMainMenuWidget> Backend;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUnifiedFrontendBinding>> Bindings;
    // Slate dropdown rows retain brush pointers; retain their owning UWidgets across GC/reopens.
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GeneratedPickerItems;
    bool bSynchronizing=false;
    bool bEditorFixture=false;
    float RefreshElapsed=0.f;
};
