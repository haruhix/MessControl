#pragma once

#include "CoreMinimal.h"

class AMCToothCharacter;

/** Read-only ownership query; the slip policy's authority gate belongs to its caller. */
namespace MCSlipperyFood
{
/** Ordinary delivery food only, including a real stack or primary/secondary grip.
 * Collection mode/capacity, boss ammunition, tools, foreign objects and basketballs
 * do not count. This query never releases, consumes or changes a carried item.
 */
MESSCONTROL_API bool IsCarrying(const AMCToothCharacter* Tooth);
/** Authority-only slip reaction: release the existing load, then give free food
 * a short upward cartoon arc. Other holders and ordinary drop/throw paths win.
 * Call before CancelGameplayInput, which otherwise releases the load first.
 */
MESSCONTROL_API int32 ReleaseWithCartoonPop(AMCToothCharacter* Tooth);
}
