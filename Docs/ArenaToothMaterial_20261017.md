# Requested tooth material — 2026-10-17

All current arena teeth now use the exact asset selected by the user:
`/Game/Art/Materials/M_ArenaTooth`. The initial assignment retained its graph;
the follow-up below removes an obsolete coffee-pattern layer.

The assignment covers all 12 active socket previews, 12 upper decorative
crowns and both decorative front rows. Their derived mesh default slots are
updated as well. `DA_ArenaTooth.GameplayMaterial` points to the same asset so
spawned gameplay teeth keep this material in Play. This supersedes the earlier
`M_ArenaToothCareV10` tooth shader assignment; its derived meshes and auxiliary
UV data remain available. The separate gum/wall edge masks and tongue material
are preserved.

`Tools/Unreal/assign_arena_tooth_material.py` applies the change in an Unreal
transaction and saves only the affected meshes, profile and map. The user's
existing unsaved gum/texture edits remain open. Unreal MCP tools were unavailable;
the existing local Unreal Python bridge was used.

Evidence in `Artifacts/ArenaToothMaterial_20261017` records a short standalone
solo PIE check of all 12 actual gameplay material parents and 14 decorative
material assignments, followed by a saved editor audit. No geometry, material
graph, gameplay rule or lighting change is part of this assignment.

## Follow-up: duplicate flat coffee spots

The user's gameplay screenshot exposed the legacy `Coffee` sine-wave pattern
inside `M_ArenaTooth`. Native arena code sets `Coffee` from tooth dirt status,
so this material painted flat brown patches across the whole crown while the
current `GrimeRelief` component separately rendered actual projected plaque.
The white material preview has `Coffee=0` and did not reveal this conflict.

`Tools/Unreal/fix_arena_tooth_legacy_coffee.py` changes the base initialization in
both matching legacy custom nodes from the procedural Coffee blend to `Enamel`.
Cracks, hit flashes, frost, material identity and the native projected coating
remain. No gameplay damage/cleaning rules or other material assets change.

`Artifacts/ArenaToothSpots_20261017` records the original node code and asset
backup, successful material compilation and a final nine-check solo PIE run.
All twelve teeth were soiled in the temporary fixture; the actual captures show
clean enamel around localized plaque. Held Brush input removes native grime
(mask sum 1,044,480 → 1,044,456). The probe restores frame settings and its PIE
is stopped. Existing gum/wall masks, tongue, imports and unsaved gum edits are
preserved. The earlier material-assignment check did not test the soiled state;
this follow-up covers that state explicitly.
