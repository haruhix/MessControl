# Player weapon swing trail — 2026-10-15

Added a gold/orange blade trail with a pale hot center, thin bright filaments,
irregular rim and tapered fading tail, following the supplied image's visual
direction. It follows each of the three ordinary knife/pickaxe combo strokes
and each repeated overhead food chop. It emits during the cutting phase and
fades over a 0.14 s world-space history. Wind-up and idle do not emit.

## Implementation

- `UMCWeaponTrailComponent` samples the actual tool's cutting span after the
  skeletal pose tick, smooths paired positions and renders one procedural
  ribbon. A small screen-visible thickness makes the diagonal stroke readable
  when the physical blade plane is edge-on to the gameplay camera.
- `/Game/Gameplay/VFX/WeaponTrail/M_WeaponSwingTrail` is a saved additive,
  two-sided, unlit material using analytic masks. No external textures or
  tutorial assets were copied. Research and adaptation notes are in
  `Docs/VFXReferences.md`.
- The existing swing presentation multicast starts the cosmetic component.
  The existing combo/food clock gates emission. No new network RPCs or damage
  rules were added. Continuous tool upgrades retain their existing effects.
- History is bounded to 24 pairs; one section has at most 630 vertices and
  1,104 triangles. It has no collision, shadows, navigation or decals. A new
  swing clears the previous history; recovery clears geometry and disables
  the component tick. `mc.WeaponTrail.Enable 0` disables new trails.

## Validation

Both cold Development Editor module compilations succeeded; see
`EditorIntegration.log` and `EditorReadability.log`. The second includes the
camera-visible thickness adjustment. The current editor loaded that module.
Temporary compiler settings were verified against their exact temporary
contents and restored from pre-task byte backups; see
`CompilerSettingsRestored.json`.

Short solo PIE checks using actual primary-input press/release edges passed:

| Scenario | Result |
| --- | --- |
| Knife: three rapid clicks | Exactly three strikes, 75 total damage |
| Pickaxe: three rapid clicks | Exactly three strikes, 120 total damage |
| Knife: hold over food | Repeated overhead chops, 75 damage during the check |
| Pickaxe: hold over food | Repeated overhead chops, 120 damage during the check |
| Actual L_Mouth player camera and lighting | All three trails appeared, then cleared |

`SoloCheck.json`, `FoodSoloCheck.json` and `MouthCheck.json` contain the measured
samples. The 60 Hz combo checks peaked at 225 vertices / 9 samples. Geometry
cleared to zero and ticking stopped after each sequence. Visual inspection
covered the diagonal stroke, held food chops and actual mouth lighting.
This was not a GPU profile, multiplayer, cook or package check.

## Preview

`preview.mp4` shows, at normal simulation speed: knife combo, pickaxe combo,
held knife food chops, then the actual L_Mouth camera. The first three clips
crop the comparison-stage camera for readability. `mouth_combo.mp4` preserves
the full gameplay camera and its copied post-process settings; HUD is excluded.
All frames are genuine SceneCapture2D output, with no synthesized motion.
The preview is silent. `VideoManifest.json` records source reports, framing and
successful strict full-video decode checks. `TrailContactSheet.jpg` provides
before/after-contact frames for each combo stroke.

The editor is left on `/Game/Maps/L_Mouth` outside PIE, with no dirty maps or
content packages. Frame-rate/background-throttle settings were restored.
`EditorRestored.json` records source/material hashes and final editor state.
