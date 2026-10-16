# Nut Knight body volume — 2026-10-17

The knight now uses dual-quaternion GPU skinning on both `SK_NutTank` and
`SK_NutTank_NoShield`. This preserves the walnut body's volume when head/root
rotations blend, matching the source Blender mesh's Preserve Volume mode.

## Cause

`SM_NutTank` in `Nut_Tank2 (3).blend` has Preserve Volume enabled on its armature
modifier. The walnut body has 1007 vertices with strong influences from both
`root.x` and `head.x`. Ordinary linear skinning averages their rotated positions
and compresses the body, even with unit bone scales. Reproducing both methods in
the supplied Blender scene reduced the body's smallest principal spread to
64.0% of its volume-preserving value during the sampled left head turn. Idle
also lost about 9–11% in the sampled poses. The original mesh's rest geometry
matches the source; this was a skinning-method mismatch rather than a changed
bind pose or broken scale keys.

The previous animation update's finite-transform and scale checks did not
measure this vertex deformation and were insufficient to rule it out.

## Change

- Enabled the engine's `DeformerGraph` plugin in `MessControl.uproject`.
- Assigned `/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth` as the default
  mesh deformer on both Tank meshes. This covers existing locomotion and every
  animation, including the shieldless variant, without replacing weights.
- Enabled MeshDeformer usage on `M_CombatHitOverlay` and recompiled/saved it so
  real hits retain the existing overlay rather than falling back to a default
  material.

Animation tracks, rig, skeleton, geometry, skin weights, artist material graphs,
combat profile, native source and project configuration remain unchanged. Only
the two mesh assets, overlay usage flag and project plugin list changed. Backups
are in `Artifacts/NutKnightBodyFix_20261017/Backup`. The supplied Blender file
remains byte-identical.

The [Epic Deformer Graph guide](https://dev.epicgames.com/documentation/unreal-engine/deformer-graph-in-unreal-engine)
documents enabling the plugin, restarting the editor and assigning mesh
deformers. The specific dual-quaternion graph and shader implementation were
inspected in the installed UE 5.8 engine content/source. The engine graph is
referenced directly; no engine files were edited.

## Verification

- The deformer shader compiled successfully for the current `PCD3D_SM5` renderer.
- A bounded solo PIE pass completed 21 checks covering native attacks, jump,
  walking, ball entry/exit, actual target switching/head turn, real tool hits,
  shield break, stun, mesh swap and full death. Both meshes had active GPU
  deformer instances. This produced 635 gameplay captures.
- A separate real frontal hit after compiling the overlay completed three
  checks. There were no new missing MeshDeformer material-usage warnings during
  this pass.
- Six pairs of rendered frozen poses compare linear and dual-quaternion
  skinning: Idle, HeadLeft and HeadRight on both meshes. Each pair uses identical
  bone transforms, materials, camera and lighting. The captured body keeps its
  volume with the deformer. These are isolated pose comparisons, separate from
  the native gameplay pass.
- 517 protected files retain their baseline hashes. The source Blender file is
  unchanged. PIE has ended, editor frame-rate/throttle settings are restored,
  packages are clean, and the editor remains open on L_Mouth as originally found.

The first comparison fixture hid the unused actor globally, which prevented
consistent GPU updates in subsequent frozen poses. The final fixture keeps both
actors active and filters the unused actor through SceneCapture's hidden list.
The final image files and `Comparison.json` come from this corrected fixture.

Evidence: `SkinningProbe.json`, `SourceMeshComparison.json`,
`IntegrationReport.json`, `SoloCheck.json`, `OverlayCheck/ReactionCheck.json`,
`Comparison/Comparison.json`, `EditorRestored.json` and `Verification.json` under
`Artifacts/NutKnightBodyFix_20261017`.

`BeforeAfter.png` shows the same left-turn pose with and without the shield.
`preview.mp4` is genuine gameplay capture at normal simulation speed. That full
pass preceded the overlay usage fix; the follow-up `OverlayCheck` captures
validate the final overlay material. No generated poses are used.

Deformer Graph requires Compute Framework support. This editor/render path was
verified; a packaged build, Steam Deck and multiplayer were not tested or built
for this request.

## Tools

With a clean editor and backups, use `fix_nut_knight_body_volume.py`, then
`fix_nut_knight_deformer_materials.py` under `Tools/Unreal`. In standalone solo
PIE, `check_nut_knight_body_volume_solo.py` runs the native pass. Once its fixture
finishes, `capture_nut_knight_body_comparison.py` captures the frozen comparisons
in the same PIE world. `check_nut_knight_deformer_overlay_solo.py` is a short
independent real-hit check; `encode_nut_knight_body_volume.py` encodes the full
gameplay capture.
