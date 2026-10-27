#pragma once

/** Baseline stain work only: leave reach, tool/perk multipliers and treatment/healing alone. */
namespace MCBrushCleaningBalance
{
// A stronger dose alone still leaves the player traversing the same narrow
// stripe. Widen the actual mask footprint; interaction reach stays separate.
inline constexpr float OrdinaryBrushRadiusCm=60.f;
// At the same accepted bristle path, a mask cell needs about 60% of its former
// contact exposure. Apply after the elapsed-frame clamp and the tool-rate cap;
// scaling elapsed time before that clamp would lose the improvement at low FPS.
inline constexpr float TargetContactDurationScale=.6f;
inline constexpr float MaskDoseMultiplier=1.f/TargetContactDurationScale;
inline constexpr float PuddleDissolvePerSecond=12.f*MaskDoseMultiplier;
inline constexpr float ToothMaskUnitsPerSecond=950.f*MaskDoseMultiplier;
}
