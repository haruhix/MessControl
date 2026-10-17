# Food fragment placement — 2026-10-18

Cut food now creates a compact group around the parent's actual body/mesh bounds.
The previous floor-wide layout and supported-position search could move pieces
away from the cut or place them in high fallback columns. Placement now uses each
configured mesh variant's bounds and pivot offset, with a small gap between pieces.
The group follows the parent's proportions and preserves the parent's bottom
height, including cuts in the air. Fragment counts, meshes, scales, total mass and
spoil deadline remain authored by the food row. The initial scatter is gentle.

Unreachable fragments recover at their last accessible point, the cut location,
or a supported point within 150 cm of it. They no longer use random recovery
points elsewhere on the tongue. If no local supported point exists, no distant
teleport is performed.

Implementation: `Source/MessControl/Private/MCFoodEnvironment.cpp`. A required
explicit `Kismet/GameplayStatics.h` include was also added to `MCToothCharacter.cpp`;
the previous throw change depended on a unity-build transitive include.

The editor target compiled successfully, and the editor was restarted with the
updated main DLL. This makes the correction persist across editor restarts.
Temporary compiler settings were restored byte for byte.

Validation used one standalone PIE session in the saved L_Mouth map and ordinary
held knife input. Two PIE-only broccoli fixtures requested 20 fragments each,
whole scale 0.55, fragment scale 0.18, Health 1 and a 600-second spoil deadline.
The second parent hovered 75 cm above its floor position. Generated fragments
were never repositioned by the probe. Each group was observed for 3.6 simulation
seconds after cutting.

| Measurement | Ground cut | Air cut |
| --- | ---: | ---: |
| Created fragments | 20 | 20 |
| Maximum horizontal distance at birth | 14.03 cm | 13.44 cm |
| Horizontal group-center error | 0.51 cm | <0.01 cm |
| Lowest fragment bottom above parent bottom | 2.46 cm | 2.99 cm |
| Maximum horizontal distance after settling | 23.29 cm | 41.86 cm |

Both cases passed the count, location, height, mass and settling checks. The probe
uses a fixed 60 Hz simulation step; its wall-clock guard was extended to 240 seconds
for the slow first editor run, without changing gameplay or spatial assertions.
Small broccoli hull contacts emitted Chaos EPA diagnostic warnings; collision
assets were not changed. Render captures include diagnostic food labels.

Evidence: `Artifacts/FragmentOrigin_20261017/Solo/Report.json`, the six scene captures,
`BuildLog.txt`, `BuiltModule.json` and `FinalAudit.json`. The focused probe is
`Tools/Unreal/check_fragment_origin_solo.py`. PIE was stopped, engine frame/throttle
settings restored, and protected saved assets/configuration and the editor's clean
package state were preserved. No multiplayer, broad automation, cook or package
validation was performed.
