#pragma once
#include "CoreMinimal.h"
#include "MCPresentationTypes.generated.h"

/** Short cooperative signals shown above the player's avatar. */
UENUM(BlueprintType)
enum class EMCPlayerAlarm : uint8
{
    None,
    Attention,
    Help
};
