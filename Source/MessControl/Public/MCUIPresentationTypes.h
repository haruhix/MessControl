#pragma once

#include "CoreMinimal.h"
#include "MCUIPresentationTypes.generated.h"

/** Client-local presentation choice. Never replicated or stored in the gameplay profile. */
UENUM(BlueprintType)
enum class EMCUIVersion : uint8
{
    Current,
    New
};

DECLARE_MULTICAST_DELEGATE(FOnMCUIPresentationChanged);
