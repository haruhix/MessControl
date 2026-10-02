# Food knife contact

Verified on UE 5.8.1, 2026-10-02.

Food strikes previously used the closest point of the visual bounding box and
required a nonzero forward direction. A player inside the grape bounding box
could therefore fail the direction test. The subsequent care-contact check also
limited the intended 180 cm weapon range to the care profile's 115 cm reach.
Damage was checked once, at 0.28 seconds into the knife swing.

Food targeting now sweeps a sphere over three heights against `GripSurface`'s
mesh triangles. It selects the closest visible contact within 180 cm, checking
occlusion separately for each height. Physics hull simplification does not define
the knife target. The server retries a missed knife contact at 30 Hz for another
0.12 seconds; the first successful hit ends that swing's contact checks.

Validation:

- Editor and game Development Win64 builds succeeded.
- 13 inventory and food collision automation tests passed without unexpected warnings.
- Actual green and purple grape assets passed 32 contact cases: eight directions,
  at 40 and 130 cm from the surface, including positions inside the visual bounds.
- Moving food entering after the first contact sample passed at 30, 60 and 120 Hz.
- Single damage per swing, cooldown protection, rear and distant targets, full and
  partial occlusion, hard-food rejection and fragment creation passed.
- Rendered inventory scenario at 60 FPS passed, including knife and pickaxe damage.
- Native Unreal MCP confirmed one real swing changes each grape's health from 100
  to 75 in PIE, with physics simulation enabled.
- Saved asset index: all 40 cached snapshots current; no export errors.

Automation report: `Saved/TestReports/KnifeFoodContact/index.json`.
Implementation: `MCFoodActor::FindToolContact`, `AMCToothCharacter::ResolveSwing`.
Regression tests: `Source/MessControl/Private/Tests/MCFoodStrikeTests.cpp`.
