#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCDeliveryZoneCaptionWidget.generated.h"

class UBorder;
class UTextBlock;

/** Compact screen-space label for a delivery zone; never captures player input. */
UCLASS()
class MESSCONTROL_API UMCDeliveryZoneCaptionWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    /** May be called before initialization. Width is bounded; translated copy grows the desired height. */
    void SetDeliveryCaption(bool bExit,bool bSortingError=false);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeOnInitialized() override;
private:
    void RefreshCaption();
    UPROPERTY(Transient) TObjectPtr<UBorder> Background;
    UPROPERTY(Transient) TObjectPtr<UBorder> IconBackground;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DirectionIcon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Description;
    bool bExitCaption=false;
    bool bErrorCaption=false;
};
