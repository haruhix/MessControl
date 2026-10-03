#pragma once

#include "CoreMinimal.h"

class UDataTable;
class UMCFoodCollisionData;

namespace MCFoodCollisionBaker
{
    /** Bakes collision-only profiles. The caller owns saving the menu. */
    bool BakeFoodMenuCollisions(UDataTable* Menu, bool bSaveProfiles,
        TArray<FString>& OutErrors, TArray<UMCFoodCollisionData*>& OutChanged);
    bool IsFoodMenuCollisionCurrent(UDataTable* Menu, TArray<FString>& OutErrors);
}
