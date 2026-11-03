#pragma once

#include "CoreMinimal.h"
#include "MCPerkVFXCompositionPlayer.h"

class AMCToothCharacter;
enum class EMCVFXPresentationResult : uint8;

/** Causal contracts shared by gameplay, preparation and the editor's canonical
 * DT profile writer. They describe visuals, never gameplay/RNG/stat policy. */
namespace MCPassivePresentation
{
    /** Empty for unsupported/legacy/active IDs. Stable event names for the 27 current passives. */
    MESSCONTROL_API const TArray<FName>& GetAllowedEvents(FName PerkID);
    /** Source-aware cosmetic only. Missing ownership/preparation retains native fallback. */
    MESSCONTROL_API EMCVFXPresentationResult Play(AMCToothCharacter* Hero,FName PerkID,FName Event,
        const FTransform& Frame,const FMCPerkVFXGameplayValues& Values=FMCPerkVFXGameplayValues());
    /** Bad/unprepared cosmetics never remove the existing native feedback. */
    MESSCONTROL_API bool UsesNativeFallback(EMCVFXPresentationResult Result);
    /** Ends only an accepted cosmetic episode; never changes native timers/ownership. */
    MESSCONTROL_API void StopEpisode(AMCToothCharacter* Hero,FName PerkID,FGuid EpisodeID,bool bImmediate=true);
    /** Updates one existing accepted cosmetic episode without restarting it. */
    MESSCONTROL_API bool UpdateEpisode(AMCToothCharacter* Hero,FName PerkID,FGuid EpisodeID,
        const FTransform& Frame,const FMCPerkVFXGameplayValues& Values);
    /** Authority presentation-only edges for real carry/buff state; no emissions from stat queries. */
    MESSCONTROL_API void TickStates(AMCToothCharacter* Hero);
    /** Discards only this source's cosmetic edge cache on accepted removal/regrant. */
    MESSCONTROL_API void ForgetSourceState(AMCToothCharacter* Hero,FName PerkID);
}
