#pragma once
#include "CoreMinimal.h"
#include "MCDevCommands.generated.h"

UENUM()
enum class EMCDevAction : uint8
{
    StartStep, DropFood, Infection, DropBrushes, CoffeeDirt, LooseTeeth,
    DamageSelf, Ragdoll, KillSelf, RestoreMouth, StopCoffee, RestartDay, TongueUlcer, TongueJolt
};
