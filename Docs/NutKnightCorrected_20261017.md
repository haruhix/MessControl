# Corrected Nut Knight animations — 2026-10-17

Follow-up: [body volume preservation](NutKnightBodyVolume_20261017.md) fixes the
linear-skinning collapse that the bone-transform checks below did not detect.

All fourteen supplied game actions from
`C:/Users/vasil/Downloads/Telegram Desktop/Nut_Tank2 (3).blend` now replace the
active AnimSequences in place under
`/Game/Gameplay/CoreLoop/NutTank/Animations/KnightBallUpdate`.
Existing references therefore use the corrected motion immediately. The source
Blender file is unchanged. Export/validation began before midnight; artifacts
remain in `Artifacts/NutKnightCorrected_20261016`.

| Supplied action | Existing asset suffix |
| --- | --- |
| NUT_Idle1 | Idle |
| NUT_Jump_atack1 | Jump |
| NUT_block | Block |
| NUT_damage1 | Hit |
| NUT_stun | Stun |
| NUT_damage2 | StunHit |
| NUT_Atack1 | Attack1 |
| NUT_Atack2 | Attack2 |
| NUT_Atack3 | Combo |
| NUT_death1 | Death |
| NUT_ball_start | BallStart |
| NUT_ball_end | BallEnd |
| NUT_head_turn_left | HeadLeft |
| NUT_head_turn_right | HeadRight |

The Auto-Rig Pro export uses the existing Universal/Unreal deformation hierarchy
and the same FBX axis, units and reference-pose policy. All fourteen FBX files
have the original hierarchy, identical bind matrices (maximum difference 0),
and a constant root. Blender runs with auto-execution disabled and never saves
the supplied source. New FBX files and their checked manifest are in
`ArtSource/NutKnightCorrected`.

The Unreal import explicitly scopes the legacy FbxFactory importer and restores
the previous Interchange flag afterward. Animation lengths, float curves,
notifies, sync markers and root-lock settings are preserved. Each animation has
updated source-action/source-SHA metadata and a working reimport path to its new
FBX. The existing `MCAuthoredCartoon` curve remains enabled.

The skeleton reference pose, both Tank meshes, artist materials, older
locomotion/charge clips, maps and `DA_NutRain` remain unchanged. Combat timings,
including the complete jump/combo recovery and 5.433-second death clip, retain
their existing profile values. No native source or project configuration was
changed by this update. Original animation packages are backed up under
`Artifacts/NutKnightCorrected_20261016/Backup/Content`.

## Validation

All 984 source-rate animation frames evaluate to finite transforms for every
bone. The imported skeleton reference is unchanged and root transforms remain
constant. Package hashes verify all fourteen target files changed, while 81
protected packages and the pre-existing native/configuration files retain their
baseline hashes.

One final 42.27-second solo PIE capture passed 20 gameplay checks: ordinary
attacks deal 12 damage each; the combo deals 36; jump finishes its full recovery;
legacy walking remains active; roll entry, travel and exit complete; six actual
target switches trigger a head gesture and smooth turning; frontal/rear tool
hits deal 8/40; breaking the shield produces a three-second stun; further hits
deal full damage without extending stun; the shield mesh is removed; and the
death actor stays for the full clip (5.45 seconds observed). Non-ball bone scales
match the skeleton throughout this pass (maximum error 1.57e-16); mesh component
scale stays fixed, including death.

The fixture uses the native scheduler and actual primary-tool input. It isolates
boss/player positions and cooldown readiness, pauses ordinary event directors,
and uses one second of PIE-only ball travel plus 40 shield durability for a
single-hit break. A second unpossessed observer character enables target
switching within the same standalone PIE world. No attack phases, poses or damage
results are fabricated. The head gesture variant selected in this gameplay pass
was 0; both supplied head actions are covered by the frame-by-frame asset checks.

The initial fixture attempt stopped at its old six-second jump timeout. The
existing configured windup, flight and full recovery total about 6.22 seconds.
The final fixture derives that timeout from the actual profile, with a small
margin. Animation assets and gameplay timing were not shortened. The failed
attempt is retained in `Attempt1Check.json`.

`ImportReport.json`, `SoloCheck.json`, `EditorRestored.json` and
`Verification.json` contain the detailed evidence. The disposable PIE world
is ended, frame-rate/throttle/importer settings are restored, and no dirty
content/map packages remain. The editor is closed to restore its initial state.
No multiplayer test, native rebuild, cook or package was run for this update.

`preview.mp4` contains 635 genuine SceneCapture2D frames under L_Mouth lighting
with the player's camera post-process settings. Capture is 15 Hz; 30 fps encoding
repeats frames at normal simulation speed. Captions are added; motion is not
generated or interpolated. The video is silent. The fixed jump review framing
can crop the character near the end of recovery. `ContactSheet.jpg` provides
representative poses. Full video decoding succeeded.

## Reproduction

Export with Blender using `Tools/Blender/export_nut_knight_corrected.py` in an
isolated process (`--background --factory-startup --disable-autoexec
--python-exit-code 1`). The exporter refuses to overwrite previous FBX files.
Use `Tools/Unreal/reimport_nut_knight_corrected.py` with an idle, clean editor and
a fresh backup/baseline. Start standalone solo PIE, then run
`Tools/Unreal/check_nut_knight_corrected_solo.py`. Its fixture derives from the
existing native Knight check. Encode a passing capture with
`Tools/Unreal/encode_nut_knight_corrected.py`.
