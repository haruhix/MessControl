# Knight playback correction evidence

- `Baseline.json`, `Before/`, `BuildConfiguration.xml`: pre-task state and backups.
- `RawPoseInspection.json`, `Reference.json`, `ScaleInspection.json`,
  `LengthInspection.json`, `MeshWeights.json`: read-only source/skeleton evidence.
- `EditorIntegration.log`: successful Development Editor module build.
- `ConnectionReport.json`: explicit full jump duration; existing bindings retained.
- `SoloCheck.json`: final passing 11-check solo gameplay observation and all samples.
- `InitialSetupFailure.json`: retained zero-sample fixture API error, corrected
  before the final run. It did not reach an attack or modify saved assets.
- `GameplayFrames/`: genuine capture PNGs, including the PIE-only scale-key stress
  reaction, which is explicitly identified in the fixture and report scope.
- `preview.mp4`, per-case MP4s, `ContactSheet.jpg`, `VideoManifest.json`: normal
  speed review media, provenance and strict full-decode checks.
- `EditorRestored.json`, `CompilerSettingsRestored.json`: preservation/restoration.

The source blend is read-only; all existing animation, mesh and material packages
are unchanged. No multiplayer or cook/package validation was run.
