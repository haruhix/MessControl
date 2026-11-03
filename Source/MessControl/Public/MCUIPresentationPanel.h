#pragma once

#include "MCGameplayUIWidget.h"
#include "MCUIPresentationPanel.generated.h"

UCLASS()
class MESSCONTROL_API UMCUIPresentationSelectorWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUIPresentationSelectorWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
private:
    UFUNCTION() void ToggleVersion();
};

/** The same F3 UI selector in UI-only screens, where gameplay dev actions do not apply. */
UCLASS()
class MESSCONTROL_API UMCUIPresentationPanelWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUIPresentationPanelWidget();
    virtual void BuildPresentationTree() override;
protected:
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
};
