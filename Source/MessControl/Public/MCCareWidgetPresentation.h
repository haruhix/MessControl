#pragma once
#include "CoreMinimal.h"

class UWidgetComponent;

/** One care owner's temporary screen-layout override; never owns care progress or widget lifetime. */
struct MESSCONTROL_API FMCCareWidgetPresentation
{
    void Refresh(UWidgetComponent* Indicator);
    void Restore();
private:
    TWeakObjectPtr<UWidgetComponent> BoundIndicator;
    TOptional<bool> CurrentDrawAtDesiredSize;
    bool bChangedDrawAtDesiredSize=false;
};
