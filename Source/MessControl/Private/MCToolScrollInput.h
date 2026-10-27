#pragma once

#include "CoreMinimal.h"

class AMCToothCharacter;

namespace MCToolScrollInput
{
    /** Inventory owns prediction/acknowledgement; no independent stale wheel cursor. */
    struct FCursor
    {
        void Reset() {}
    };

    bool Handle(AMCToothCharacter& Hero,float Delta,FCursor& Cursor);
}
