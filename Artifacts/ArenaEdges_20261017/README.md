# Correct external tissue masks

The four new gum/wall meshes now fade along their own open borders instead of
sampling the old throat mask. Material color/texture tuning is preserved.

- `Before.png`, `After.png`: actual solo PIE views at the same stored camera.
- `Before_Unlit.png`, `After_Unlit.png`: the same comparison with capture-only
  lighting disabled, making the contour visible under the dark arena lighting.
- `SoloCheck.json`: final successful comparison and reference restoration.
- `FinalAudit.json`: shader, geometry, UV, collision and preservation checks.
- `Integration.json`: exact affected actors and copied material parameters.
- `Before.json`, `EdgeBake.json`, `EdgeBakeSummary.json`, `ProtectedHashes.json`:
  source geometry, surface-distance bake and protected-file evidence.
- `DiskBefore`: recovery copies of the map and original gum material.

Implementation details: `Docs/ArenaEdges_20261017.md`. No authored lighting,
native build, game package or multiplayer validation is included.
