# External gum and wall borders — 2026-10-17

Follow-up to the new arena integration: the screenshot showed a hard tissue
border and a pointed cutout above the upper teeth. The four new gum/wall meshes
were still sampling `T_ExitOuterEdgeDistance`, which belongs to the old throat
UV layout. The previous tooth-contact mask did not repair these tissue borders.

`SM_Gum_Bot`, `SM_Gum_Up.001`, `SM_Wall_L` and `SM_Wall_R` now use derived meshes
and an edge material under `/Game/Gameplay/Arena/V10/Edges`. UV0, positions,
triangle counts and collision modes are preserved. UV1 holds normalized surface
distance from each mesh's own welded open border. A multi-source shortest-path
bake follows surface edges, so it does not feather across unrelated nearby folds.
The mask is zero at open borders and one in the interior.

The existing tissue shader's perimeter-fade input now reads that UV1 distance.
The existing 100 local-cm fade width and all current gum color, texture,
roughness/normal and climate parameters are copied unchanged. This retains the
material's appearance while correcting which geometry controls its opacity.
The original gum instance and its existing unsaved texture edits are preserved.
No old import or previous tooth/tongue asset is overwritten.

Tools:

- `Tools/Blender/bake_arena_v10_tissue_edges.py`: plain Python boundary-distance
  calculation; it does not open or save any blend file.
- `Tools/Unreal/integrate_arena_v10_tissue_edges.py`: creates the derived assets
  and updates four scene components inside a scoped editor transaction.
- `Tools/Unreal/check_arena_v10_tissue_edges_solo.py`: bounded solo PIE comparison
  at the user's recorded camera, restoring temporary component references.
- `Tools/Unreal/audit_arena_v10_tissue_edges.py`: compilation, geometry, UV,
  collision and source-preservation audit.

Evidence is in `Artifacts/ArenaEdges_20261017`. `Before.png` / `After.png` are
genuine PIE captures under saved lighting. `Before_Unlit.png` / `After_Unlit.png`
disable lighting only on the transient capture so the masked contour is readable.
The author inspected both views; the pointed cutout is removed and the actual
external contour receives the fade. Initial capture/probe setup was corrected
before the final comparison; only the final `SoloCheck.json` is validation.

The short PIE check retains twelve active teeth and one moving tongue. The
existing first-perk pause is preserved around comparison captures. Only runtime
mesh/material references are swapped for the Before view; authored references
and component mobility are restored afterward. The owned PIE is stopped and
the user's editor and Blender remain open. Final saved checks and protected-file
hashes are recorded in `FinalAudit.json`.

Available Unreal/Blender MCP tools were checked but not exposed in this session;
authoring used Unreal's existing loopback Python bridge. No native source build,
packaging, multiplayer test or authored lighting change was needed.
