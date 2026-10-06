#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCDeliveryZoneCaptionWidget.generated.h"

class UBorder;
class UTextBlock;

/** Compact screen-space label for a delivery zone; never captures player input. */
UCLASS()
class MESSCONTROL_API UMCDeliveryZoneCaptionWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    /** May be called before the widget tree is initialized. Desired screen size is 280 x 56. */
    void SetDeliveryCaption(bool bExit,bool bSortingError=false);
protected:
    virtual void NativeOnInitialized() override;
private:
    void RefreshCaption();
    UPROPERTY() TObjectPtr<UBorder> Background;
    UPROPERTY() TObjectPtr<UBorder> IconBackground;
    UPROPERTY() TObjectPtr<UTextBlock> DirectionIcon;
    UPROPERTY() TObjectPtr<UTextBlock> Title;
    UPROPERTY() TObjectPtr<UTextBlock> Description;
    bool bExitCaption=false;
    bool bErrorCaption=false;
};
