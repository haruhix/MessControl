#pragma once
#include "CoreTypes.h"

namespace MCPaintVisibility
{
// The authoritative wipe remains unchanged. Give its last working paint cells
// enough display density to survive bilinear sampling and the material's .25
// clip; cells already cleared by the brush must not become visible again.
inline constexpr uint8 UploadByte(uint8 Value,bool bPaintTile)
{
    return !bPaintTile?Value:Value<=63?0:Value<128?128:Value;
}
}
