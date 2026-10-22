#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCHubShopWidget.generated.h"

class UBorder;
class UButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCHubShopClosed);

/** Editable empty shop presentation. The controller owns interaction and closing. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCHubShopWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, Category="Shop") FMCHubShopClosed OnClose;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBorder> ShopPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ShopClose;
private:
    void ResolveControls();
    UFUNCTION() void CloseClicked();
    bool bEditorFixture=false;
};
