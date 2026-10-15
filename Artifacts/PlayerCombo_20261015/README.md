# Player three-click combo — 2026-10-15

Delivered for ordinary knife and pickaxe attacks:

1. Right hand loads at the **left shoulder**, with a lateral blade orientation, then cuts across to the right.
2. A curved return stroke crosses the front in the opposite direction.
3. An overhead wind-up finishes with a downward strike.

The crown leans forward and twists into each stroke; the free hand provides
counterbalance. Existing hit flash, directional fragments, sound, small camera
accent and contact hold continue on confirmed hits.

Three rapid primary-input press/release edges buffer a complete 1/2/3 chain.
Holding the button causes one stroke. A pause after recovery resets the next
press to step one. Native authority owns the queue and damage; the reliable
swing event presents the chosen step. Tool damage is unchanged.

Validation after a clean Editor restart:
- `SoloCheck.json`: four passing real-input checks — held input, three knife
  clicks, timeout restart and three pickaxe clicks. A knife combo removes
  75 HP; the pickaxe test defeats its 100 HP nut in three accepted hits.
- `StepSequenceCheck.json`: native accepted steps are exactly
  `[1, 1, 2, 3, 1, 1, 2, 3]` across those four cases.
- `ComboEvents.log`: real queued presses, accepted step starts and swing/hit
  audio events. The first knife wind-up reaches 33 cm to the actor's left in
  the recorded hand samples. Actual captured poses were visually inspected.
- `EditorIntegration.log` and `EditorPoseRefinement.log`: successful native
  Development Editor integration. Two earlier Live Coding failures are
  retained in the diagnostic logs; final validation uses the normal Editor DLL.
- `EditorRestored.json`: original L_Mouth map restored, PIE stopped, no dirty
  content/maps and original runtime performance settings restored. Temporary
  compiler settings were also restored byte for byte.

`preview.mp4` joins the actual knife and pickaxe combo windows, with 147 genuine
frames each from a 60 Hz solo simulation. It is silent; `ComboEvents.log`
verifies the actual audio event paths. `ComboPoses.jpg` shows three different
wind-up/strike poses for each tool. The SceneCapture follows the player's
gameplay camera transform and FOV without HUD or the camera postprocess stack.
Encoder PNG copies are lossless and were verified pixel-identical to the
original captures. No generated or interpolated poses were used.

Multiplayer, game cook/package, production-arena lighting across all cameras
and GPU cost were not tested. The existing mining, brush and continuous
upgrade paths were preserved in source; they were not part of this short
combo check. No shared artist assets or saved production levels were edited.
