# Nut Knight animation update — 2026-10-16

The supplied `Nut_Tank.blend` now drives the Knight in the existing Nutaggedon
event. The source blend is read-only; its SHA-256 before/after is
`82ee69ef6b39f856714eb06d3216e56e7b65541695ab86c4cbd678c5e6ab28ef`.
All ten clips use the existing Tank skeleton, meshes and artist materials.

| Supplied action | Game use |
| --- | --- |
| `NUT_Idle1` | Idle, including the unshielded mesh |
| `NUT_Jump_atack1` | Replacement jump attack |
| `NUT_block` | Response to an accepted frontal guarded hit |
| `NUT_damage1` | Ordinary damage reaction |
| `NUT_stun` | Shield-break stun loop |
| `NUT_damage_2` | Damage received during stun |
| `NUT_Atack1` | First ordinary single-contact attack |
| `NUT_Atack2` | Second ordinary single-contact attack |
| `NUT_Atack3` | One continuous three-contact combo; last contact is circular AoE |
| `NUT_death1` | Full 5.167-second death performance |

The event profile is `/Game/Gameplay/CoreLoop/DA_NutRain`. New clips are in
`/Game/Gameplay/CoreLoop/NutTank/Animations/KnightUpdate`; FBX files and the
checked export manifest are in `ArtSource/NutKnightUpdate`. Existing walking,
left/right walking, unshielded walking, charge clips and ball transformation
bindings are retained. Every pre-existing Tank asset remained byte-identical.

## Behavior and timing

Native AI cycles the three melee performances. Attack1/Attack2 each resolve
one frontal contact. Combo contacts correspond to source frames 16, 30 and 50;
only its last contact uses the existing ground-slam area damage and warning.
The authored clip runs continuously across telegraph, execution and recovery.
Server timing uses saved normalized contact fractions and authored durations,
so dedicated servers do not need evaluated cosmetic bones or loaded animations.
Jump takeoff/landing fractions now match the new clip rather than the old one.

The shield retains its existing frontal guard cone, durability and 20% boss
health damage while blocking, including the breaking hit. Rear hits and lowered
guard retain full damage. Breaking the shield cancels pending attack effects,
removes the shield mesh and enters a real three-second `Stunned` state. The
Knight cannot attack or move during it. Further hits play StunHit and do not
extend the window. State and melee variant replicate through the existing actor.

The native animation proxy retains its 0.18-second pose transitions; reactions
use at most 0.10 seconds. Authored reactions suppress duplicate procedural
recoil, and the new death clip suppresses the procedural Tank death fall.
Mage behavior and shared Mage death duration retain their existing settings.

## Validation

`Artifacts/NutKnightAnimations_20261016/SoloCheck.json` is the final passing
solo PIE check. It uses the native boss scheduler and actual primary-input
tool hits. Observed: 12 damage for each normal attack, 36 over the three combo
contacts, successful new jump and legacy walking, 8 health damage from a
40-damage frontal Pickaxe hit, 40 from the rear, real stun on shield break,
40 during stun without extending it, the unshielded model, and a death actor
lifetime of 5.183 seconds from the observed defeat frame. The shield-mesh swap
moved the sampled head by 0.274 cm and hand by 1.139 cm across that update.

The fixture isolates player/boss positions and cooldown readiness, restores
player health before reaction checks, and temporarily uses 40 shield durability
to show a break with one hit. Opening reward choices are resolved in disposable
PIE; ordinary event directors are paused for isolation. No attack phase or
damage result is fabricated. Earlier fixture attempts encountered reward-menu
pauses, player knockdown or opening coffee/food interference; the final run
waits for standing before delivering a tool hit and completed all 14 checks.

`preview.mp4` is genuine SceneCapture2D footage under L_Mouth lighting and copied
player-camera post-process settings, with a fixed review camera and wider jump
framing. It is silent, captured at 15 Hz and encoded at 30 fps with repeated
frames at normal simulation speed. Captions are added; poses are not generated
or interpolated. `GameplayContactSheet.jpg` provides representative frames.

The Development Editor module build succeeded. No multiplayer, cook/package,
full-battle balance or GPU profiling was run. The editor is left on L_Mouth,
outside PIE, with no dirty content or maps. Original compiler, frame-rate and
background-throttle settings are restored. Reports include source/asset hashes.

## Pipeline references

The export uses the project's existing ARP bake/import policy, checked against
[Auto-Rig Pro's game export documentation](https://www.lucky3d.fr/auto-rig-pro/doc/ge_export_doc.html),
[Blender's Action Slot API](https://docs.blender.org/api/dev/bpy.types.ActionSlot.html)
and [Epic's FBX content pipeline](https://dev.epicgames.com/documentation/unreal-engine/fbx-content-pipeline).
The exporter verifies the original hierarchy, reference bind matrices and
constant root before importing animations against the current skeleton.
