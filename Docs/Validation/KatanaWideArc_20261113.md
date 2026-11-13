# Katana wide arcs — 2026-11-13

The katana now performs broad physical lateral cuts across the front of the player. Source/import gates and combined focused solo acceptance pass. The raw reports retain their failures and the exact reasons for retakes; acceptance does not overwrite diagnostic evidence.

## Implemented source and runtime path

- The existing six-clip master retains Carry, right/left diagonals, horizontal, overhead and the three-stroke charged action. The widened horizontal/diagonal source tip arcs span −85° to +85°. Overhead actions retain a vertical corridor and floor clearance.
- `Tools/Blender/widen_katana_swings.py` moves the rigid sword driver and both hand targets together. Recovery detours route the complete grip assembly around the belt/body. Geometry, handle dimensions and grip guides are retained.
- Export samples the five attacks at 240 Hz and Carry at 30 Hz, preserving duration. Import respects each entry's sample rate and calibrates the mount curves against the gameplay hand transform.
- `MCKatanaPlayback.h` shares zero-based active windows between blade contacts, Niagara trails and the end of facing assistance: right/horizontal 6–13, left 7–14, overhead 8–16; charged 13–18 / 20–25 / 27–32, on the original 30 Hz authoring timeline.
- Input retains four ordinary variants without consecutive repeats, immediate press acceptance, a two-press buffer and one automatic charged series after a 0.38 s hold. Releasing does not request another attack. Interruptions clear pending input.
- Ordinary clips last 0.367–0.417 s at base speed; their active phase starts at 0.0667 s. Charged playback includes the complete authored preparation and recovery at 1.4×: 0.881 s, with three active cuts starting at 0.3095 s after acceptance.
- Body playback retains the complete authored guard relative to bind pose. Attacks apply full authored motion; Carry attenuates only its changing motion to 65%. Rendered pose and blade queries use the same basis.
- Before the body overlay, the proxy caches both leg-root component transforms and restores them against the changed parents. Foot IK runs after the katana pose while standing and walking.
- Hits still require the actual finite cutting edge and target collision/food triangles. No additional hit volume, damage fan or enlarged contact radius was added. Existing Niagara/assets/profile references are reused; this pass introduces no new runtime resource root.

## Dense import gate

`Tools/Unreal/validate_katana_grip_subframes.py` evaluated all six imported clips at 960 Hz using COMPRESSED AnimPose, `SK_CharacterGameplay`, curves enabled and retargeting disabled. Both wrist frames are compared with their own time-zero grip relative to the mounted sword.

- Result: all six pass the 1 cm / 5° limits, including samples between the 240 Hz attack keys.
- Worst left-hand displacement: **0.70243 cm**; worst left-hand orientation change: **0.71422°**.
- Report: `E:/CodexTemp/MessControl/KatanaWideArc/Exports/KatanaGripSubframes.json`.
- This gate measures evaluated wrist frames. Visible glove contact and gameplay layering require separate inspection.

## Solo acceptance

Editor Live Coding patch 8 succeeded at 08:01:48 UTC. Run `20261113T080714952408Z` passed **145/146** checks. Its one failed top-view Niagara observation was retaken unchanged in `20261113T081025282545Z`, which passed **10/10**. Combined evidence: `E:/CodexWork/MessControl/Validation/KatanaWideArc/20261113T080714952408Z/Acceptance.json`. No native changes or asset reimports occurred between those two runs.

- The ordinary lateral tip span is approximately 174–175°, centred around forward. At R150/r36, both diagonals and the horizontal cut hit all five −75/−45/0/+45/+75° bodies. At R190/r36, the left diagonal and horizontal hit all five; the right diagonal missed +75°. All four variants were selected naturally, with overhead retaining its vertical centre corridor. Both lateral charged strokes hit all five bodies at both reaches. The precise horizontal R150/r20 matrix also passed. These are measured finite-blade intersections, not a guaranteed sphere at every height/distance.
- Behind bodies at −110/+110/180° remained untouched. The harness asserts actual sphere radii of 20 or 36 cm before input. A prior requested 35 cm radius had been migrated to 58 by `FMCNutEnemySettings::Sanitize`, making those nominal rear bodies protrude into the front. Run `20261113T080301662836Z` retains that failed fixture diagnostic; no native hit filter was added.
- The right diagonal and first charged stroke each cut all five 50 cm ingredient meshes at the front angles at R150; the charged pieces were then consumed. Ground and high overhead centre-food checks passed. The two broad charged strokes and the final vertical stroke use real input, edge contacts and unique actor/stroke damage. R190 food coverage is recorded rather than promised for every variant.
- The three-press queue, nonrepeating choices and automatic held series passed. One positive-velocity attack sample confirms movement occurred; walking evidence is limited. Runtime left wrist drift reached 0.429 cm / 0.293°. Idle foot drift reached 1.28 cm right / 3.88 cm left; walking samples are recorded separately. Sampled blade endpoint clearance above the queried tongue surface was at least 11.14 cm across 444 supported samples. These are sampled diagnostics, not a zero-slide guarantee.
- The heavily instrumented top view had a 0.2218 s render/observer gap across the third cosmetic window. The isolated retake disabled per-frame floor queries and passed the same three-segment check; its maximum native-age gap was 0.0663 s. Arbitrary long-hitch VFX robustness is not established.

The primary inspected actual runtime motion frames from gameplay, closer front/side and top views, plus the source recovery/contact views. Both visible gloves stayed on the handle, the blade cleared the body and ground, lateral cuts covered the front, and recovery returned to the side guard. User artistic approval remains separate. Review media is outside Git: `E:/CodexWork/MessControl/Validation/KatanaWideArc/Review/katana-wide-gameplay.mp4`; its adjacent `Recipe.json` records the unretimed source segments.

## Composer and resources

`Composer/20261113T081254Z/ComposerWide.json` passed discovery, real Target input/contact scenario, live native Niagara, cancellation during the charged action, clean reset and canonical saved-data roundtrip. Opening the actual source system completed; this Python API cannot independently enumerate its asset-editor tab. The tool owner was prepared with zero pending receipts; profile/system bytes and stage settings remained unchanged.

The six existing animation references remain reflected through manifest-rooted `DA_ToothAnimation`; the same tool presentation profile and Niagara systems are reused. Final stopped-sandbox readback reports the system loaded but not retained, with the sandbox readiness phase Idle; it is not a runtime barrier or cold-start proof. No new runtime roots were introduced. `ReadinessAndReset.json` and `EditorFinalState.json` record no PIE, no dirty content/maps, no running compositions and no active Niagara after cleanup.

## Retained repair history and limits

The earlier failed runs `20261113T074423247915Z` and `20261113T075621635903Z` remain in the same validation directory. They exposed the attenuated body motion, guard/bind basis mismatch and recovery issues; they are diagnostic evidence, not accepted previews. The complete charged preparation also replaces the former guard-to-release jump, measured at approximately 95 cm at the blade tip.

Multiplayer, clean packaged startup, cooking and packaging are outside this solo authoring validation.
