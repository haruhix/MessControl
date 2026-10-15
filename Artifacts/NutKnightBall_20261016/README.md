# Nut Knight second-source integration evidence

- `SourceInspection.json`: source actions, object inventory and source SHA.
- `Baseline.json`, `Before/`, `UnrelatedAssetHashes.json`: pre-task saved state.
- `BlenderExport.log`, `ArtSource/NutKnightBallUpdate/ExportReport.json`: fourteen
  exports, hierarchy/bind/root checks and source preservation.
- `UnrealImport.log`, `ImportReport.json`: same-skeleton imports, every-frame
  bone samples, durations and protected old assets.
- `ConnectionReport.json`: 15 bindings (Idle serves both shield models), timing
  updates, 0.25-second Knight blend and preserved walking/charge/model bindings.
- `EditorIntegration.log`, `EditorDefaults.log`, `EditorFinal.log`,
  `EditorQueueBlend.log`: successful Editor module integration builds. The last
  includes consecutive queued-gesture transitions and is the loaded build.
- `SoloCheck.json`: 18 passing native combat/roll/retarget observations.
- `Locomotion/SoloCheck.json`: three-second native idle/walk/idle observation.
- `GameplayFrames/`, `Locomotion/GameplayFrames/`: genuine source capture PNGs.
- `preview.mp4`, `full_preview.mp4`, per-case MP4s, `VideoManifest.json`,
  `ContactSheet.jpg`: review footage, encoding provenance and decode checks.
- `CompilerSettingsRestored.json`, `EditorRestored.json`: restoration and final
  preservation checks. Source blend and every pre-existing Tank asset unchanged.

Two character actors are used inside one standalone solo PIE world to exercise
target changes. No network/multiplayer, cook/package or full battle test was run.
