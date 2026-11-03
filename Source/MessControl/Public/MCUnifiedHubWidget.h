#pragma once

#include "MCGameplayUIWidget.h"
#include "MCHubWidget.h"
#include "MCUnifiedHubWidget.generated.h"

class UMCUniversalUIWidget;
class UMCUnifiedHubWidget;

/** Runtime-only forwarding adapter. The original Hub validates every accepted action. */
UCLASS()
class MESSCONTROL_API UMCUnifiedHubBinding : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<UMCUnifiedHubWidget> Owner;
    UPROPERTY(Transient) EMCHubMenuAction Action=EMCHubMenuAction::OpenSelection;
    UFUNCTION() void Clicked();
};

/** PSD Gathering overlay and selection footer. Native Hub retains its scene, input policy and ACKs. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedHubWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedHubWidget();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Presentation") bool bFitViewportColumns=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview") int32 PreviewCoins=800;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview") int32 PreviewFish=200;
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ApplyTheme() override;
    void SetBackend(UMCHubWidget* Source);
    void RefreshFromBackend();
    bool HasBoundBackend() const {return Backend.IsValid();}
    bool RequiresMenuInput() const;
    UWidget* GetDefaultFocusTarget() const;
    UWidget* GetFishCounterIcon() const;
    UWidget* GetFishCounterCard() const;
    void RequestBack();
    bool RouteSourceKey(const FKeyEvent& Event);
    bool RouteSourceMouseWheel(const FPointerEvent& Event);
    void ForwardAction(EMCHubMenuAction Action);
protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
private:
    void BindActions();
    void ShowMode(bool bGathering);
    void RefreshViewportLayout();
    TWeakObjectPtr<UMCHubWidget> Backend;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUnifiedHubBinding>> Bindings;
};
