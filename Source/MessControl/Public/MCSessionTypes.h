#pragma once

#include "CoreMinimal.h"
#include "MCSessionTypes.generated.h"

/** Steam lobby visibility. Friends-only and invite-only are distinct modes. */
UENUM(BlueprintType)
enum class EMCRoomPrivacy : uint8
{
    Public,
    FriendsOnly,
    InviteOnly
};
