#pragma once
#include "CoreMinimal.h"

namespace MCNetworkProtocol
{
// Bump both values when the movement/interaction contract becomes incompatible
// (wire format or collision/prediction semantics). Keep the GUID stable.
inline const FGuid FoundationGuid(0x9E14B4D3,0xF8624E7C,0xAF05C093,0x23D8A61F);
inline constexpr int32 FoundationVersion=6;
inline constexpr int32 CompatibleFoundationVersion=6;
}
