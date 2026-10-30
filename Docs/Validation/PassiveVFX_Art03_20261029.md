# Passive VFX Art03 — local review checkpoint

User direction: continue cartoon VFX; leave spray work last. Art03 is a reviewable visual iteration. None of the ten effect families has human artistic approval.

## Implementation

- Added the saved opaque DefaultLit `/Game/Gameplay/VFX/M_CartoonReceiptSolid` material, authored and read back through the live Unreal MCP. Vertex color drives BaseColor and 0.08 emissive; roughness 0.26, specular 0.45. No textures or dynamic material variants.
- `MCCartoonFXGeometry.h` supplies cached closed beveled plus/star, ellipsoid and arc shapes, real normals and stable-topology updates. Passive feedback uses separate pop, squash, travel and exit envelopes. These are native procedural mesh effects, not newly authored Niagara emitters or WPO effects.
- Healing, protection, recovery, acceleration, disappearance/return, neighboring cleanup and combat receipts have distinct shapes. The nonlegacy, unshielded NPC CombatHit path uses a short solid punch and two chips; gameplay damage is unchanged.
- Resources are retained by reflected CDO references and the arena preload manifest. Solid, soft and glow materials also enter the finite Editor first-use shader preparation list for L_Mouth/L_Tutorial.
- Passive soft/glow sections update stable topology instead of recreating the whole mesh component every tick. Empty sections are cleared only when nonempty.

## Actual verification

Build03 succeeded using `-DisablePlugin=Fab` for the Editor target only. This works around the installed engine's FabToolset rules mismatch; project and engine plugin configuration were not rewritten.

Final recordings use genuine owned solo PIE windows, native gameplay events and E: output. Temporary placement/loadout/NPC fixtures are disclosed in each report. No direct cosmetic cue factory calls, forced proc chances or synthetic/interpolated frames were used. WGC's ceiling was 60 fps; actual output is variable frame rate. Clips retain source frame counts and playback timing. Stills were extracted offline.

| Scenario | Evidence | Result |
| --- | --- | --- |
| Heal + Speed, shared cleanup event | `HealSpeed03/PassiveFeedback.json` | 7/7; actual HP restoration and both cues |
| Shield | `Shield02/Report.json` | Passed; actual NPC bite and reduced damage |
| Enamel | `Enamel02/Report.json` | Passed; native random damage prevention |
| Second Wind | `Wind02/Report.json` | Passed; native miss after normal dash and stamina restoration |
| Houdini | `Houdini02/Report.json` | Passed; actual hidden/return phases and cue completion |
| Neighbor cleanup | `Neat02/NeatFreak.json` | 5/5; unchanged 20% chance succeeded on the fifth cleanup; no direct brushing of neighbor |
| NPC, PowerHit, Billiards, shared combat probe | `Combat02/Report.json` | 14/14; damage 25, 31.25 and secondary 7.5 |

All relative evidence paths above are under `E:/CodexWork/MessControl/PassiveVFX_Art03_20261029/`. These are shared-scenario counts, not ten independent test suites.

The first Art03 healing recording showed a dark initial plus. In the final recording, native frame 91 / PTS 3.033333 is green and the inspected consecutive onset frames remain green. The final shader log records the new material incomplete/requested during loading, then complete at 22:18:59.242 UTC, before PreloadReady at .245. Component update, shader preparation and recording changes were combined; the individual cause was not isolated. The failed HealSpeed02 recording-marker API attempt is retained separately and was not published.

## Review and remaining visual issues

`http://127.0.0.1:8765/` now contains a new **Art03** version in all ten VFX cards, with actual videos, peaks and root critique. Root scores are 3–5/10, with rework status. Human healing v2 feedback **2/10, «оч плохо»** and all prior history are preserved byte-for-byte. Sprays are the final three tasks.

- Healing/acceleration are readable, but volume is subtle in the distant camera.
- Shield competes with the existing yellow player-hit ring; Enamel is small and brief.
- Wind lacks contrast against the white character. Houdini's separated faceted spheres still need a coherent cartoon puff treatment.
- NPC impact is partly occluded by the player. PowerHit resembles a large badge and overlaps damage text; Billiards pieces are partly behind the walnuts.
- The legacy task-completion check/stars distract from the neighboring-cleanup flight.

The board HTTP verification checked HEAD and 32-byte Range responses for all 21 new media entries; all ten latest versions have available video. The rendered page was visually inspected. No production feedback was submitted by the tests.

## Preserved state and scope

The ordinary Editor was restored to saved L_Mouth, without PIE or dirty packages. Original user preference bytes and the original absence of the Windows configuration file were restored and verified. Build traces and browser/recording outputs are on E:. The local review server remains running.

No multiplayer, microphone, cook/package, cold packaged PSO check or full-run playthrough was performed. Spray behavior was not further changed in this Art03 iteration. No commit or push was made for this checkpoint.
