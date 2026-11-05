#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCCommerceUIValidationLibrary.generated.h"

class UWorld;

/** Bounded actual standalone PIE commerce UI inspection; no gameplay setters. */
UCLASS()
class MESSCONTROLEDITOR_API UMCCommerceUIValidationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Reads only the current single local controller's exact source widgets and
     * its unique visible top-level unified root. Never enumerates stale card objects. */
    UFUNCTION(BlueprintCallable,Category="Mess Control|UI|Validation")
    static FString InspectPIECommerce(UWorld* World);
    /** One +/-1 notch through the actual cached SScrollBox OnMouseWheel handler.
     * Requires -MCUIFoundationOwned and -MCHubTransientProfile, ready standalone
     * PIE and an already visible Wardrobe/Shop grid. This is a native handler
     * probe, not hardware or global Slate routing proof. No gameplay/profile mutation. */
    UFUNCTION(BlueprintCallable,Category="Mess Control|UI|Validation")
    static FString SendPIEGridWheel(UWorld* World,FName Target,float Delta);
};
