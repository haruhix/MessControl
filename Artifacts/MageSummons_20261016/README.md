# Mage summons from Summon.blend

New minion mesh, packed source textures and the 0.8-second `Summon_walk` loop
replace the revived static walnuts. Native summon speed is 360 cm/s (was 260).
The original killable five-second-fuse behavior is preserved.

See `Docs/MageSummons_20261016.md` for behavior, asset paths, fixture scope and
validation limits.

- `SoloCheck.json`: final short native Mage summon/gameplay check.
- `preview.mp4`, `VideoManifest.json`, `ContactSheet.jpg`: genuine gameplay footage.
- `SourceInspection.json` / `ArtSource/NutSummon/ExportReport.json`: source and bake.
- `ImportReport.json`: new mesh, skeleton, animation, textures/material assignments.
- `ConnectionReport.json`: saved speed and preserved boss bindings.
- `EditorIntegration.log`, `EditorFuseAnchor.log`, `EditorCrownOffset.log`: Editor
  module integration and final crown attachment builds.
- `EditorRestored.json`, `CompilerSettingsRestored.json`: source/package preservation
  and restored editor/temporary settings.
- `InitialSamplingCheck.json`: diagnostic from the initial speed sampler; superseded
  by the final check, which includes stationary samples between 20 Hz movement updates.
- `Before`: source/profile files as they were before this task, including the
  previously completed Knight update.
