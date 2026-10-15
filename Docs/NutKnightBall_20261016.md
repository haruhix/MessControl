# Nut Knight: second supplied animation set

Playback completion, constant size and the floor-level turning axis are corrected
by [the subsequent playback update](NutKnightPlayback_20261016.md). Its source-pose
endpoint checks supersede this earlier scheduler-completion check.

Source: `C:/Users/vasil/Downloads/Telegram Desktop/Nut_Tank2.blend1`, SHA-256
`8e8c35c3d7045c6129330df20aae8a4fd72f5cf5c97065833d94323b192b6dd8`.
Read with automatic embedded script execution disabled, without saving the source.

All fourteen gameplay actions are baked through the existing ARP pipeline and
imported against the existing Tank skeleton. Files are in
`ArtSource/NutKnightBallUpdate`; game clips are in
`/Game/Gameplay/CoreLoop/NutTank/Animations/KnightBallUpdate`.
`/Game/Gameplay/CoreLoop/DA_NutRain` selects the new versions. Existing model,
materials, walking, directional walking, unshielded walking and charge clips
remain intact. The supplied file contains no replacement walking action.

| Source action | Runtime clip/use |
| --- | --- |
| NUT_Idle1 | Idle, with and without shield |
| NUT_Atack1, NUT_Atack2 | Ordinary attacks |
| NUT_Atack3 | Three-contact combo, final AoE |
| NUT_Jump_atack1 | Jump attack |
| NUT_block | Frontal block reaction |
| NUT_damage1, NUT_damage2 | Damage and damage while stunned |
| NUT_stun | Shield-break stun |
| NUT_death1 | Death |
| NUT_ball_start | Roll windup, forward playback |
| NUT_ball_end | Separate roll recovery, forward playback |
| NUT_head_turn_left, NUT_head_turn_right | Random sixth-target-switch gesture |

The updated combo/jump include longer recovery/idle tails. Contact fractions
are recomputed against 154 intervals; attack contacts still correspond to source
frames 16, 30 and 50, jump takeoff/landing to frames 9 and 38. Death now retains
the complete 163/30-second clip. Authority timing does not depend on evaluated
cosmetic bones or loading animation assets on dedicated servers.

The Knight has its own `TankAnimationBlendSeconds=0.25`. Every pose transition
uses it, including reactions and roll mesh handoffs. Locomotion has a separate
0.25-second smoothstep crossfade that advances on the animation thread, so
idle/walk mixtures transition even though the gameplay state remains Idle.
Mage timing retains its existing setting. The Knight's authoritative facing
follows the shortest arc at a bounded rate (180 degrees / blend duration),
including attack windups. Guard and the rendered body use that same facing.
Ball world rotation continues to follow physical travel across steering/bounces.

Only actual changes from one player target to another increment
`TargetSwitchCount`; initial acquisition and reselecting the same target do not.
Both scheduler choices and tool-hit aggro changes count. Each multiple of six
queues one server-random left/right gesture. An active attack/roll/stun finishes
before the gesture starts. The replicated Retargeting state plays the full
55/30-second gesture while stationary and then returns to Idle. Queued gestures
do not overwrite one another or cancel a committed attack.

Validation evidence is in `Artifacts/NutKnightBall_20261016`: source/export/import
reports, protected-asset hashes, Editor module build logs, profile connection,
bounded solo gameplay report and genuine SceneCapture footage. The fixture uses
native scheduling and real primary Pickaxe input. For target switching, a second
character is created inside the same solo world; no network/multiplayer session
is started. Walking/ability readiness, observer placement/health and a shortened
12-second roll are fixture controls. No attack phase, target-switch counter,
head variant or damage outcome is fabricated. Multiplayer, full battle balance,
cook/package and GPU profiling are outside this check.

The final combat check passed all 18 observations: normal contacts 12/12,
three-contact combo 36, jump/walking, native roll windup/execution/recovery,
head gesture on exactly six real switches, return from the gesture, bounded
180-degree turning (largest observed step 36 degrees), frontal guard damage 8
versus rear damage 40, shield break/stun, unshielded mesh and 5.45-second observed
death lifetime. Random choice selected the left gesture in this run; both new
head clips were imported and bound, and the right choice uses the same path.

A separate three-second locomotion observation passed three checks: actual
chase displacement, a distance-driven stop with the gameplay state remaining
Idle throughout, and the configured 0.25-second blend. Both reports retain
native sampled bones and genuine capture frames. `preview.mp4` focuses on this
locomotion round trip, ball transitions and sixth-switch head gesture.
`full_preview.mp4` also retains combat clips; its fixed jump framing crops the
boss in part of the flight. No additional gameplay run was made for framing.
