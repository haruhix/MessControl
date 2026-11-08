#pragma once
#include "CoreMinimal.h"

namespace MCNetworkProtocol
{
// Bump both values when the custom movement/interaction wire format becomes
// incompatible. Keep the GUID stable; native handshake rejects older peers.
inline const FGuid FoundationGuid(0x9E14B4D3,0xF8624E7C,0xAF05C093,0x23D8A61F);
inline constexpr int32 FoundationVersion=4;
inline constexpr int32 CompatibleFoundationVersion=4;
}
