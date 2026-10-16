# Corrected Nut Knight animation update

All 14 active Knight animations were reimported from `Nut_Tank2 (3).blend`.
See `Docs/NutKnightCorrected_20261017.md` for the action mapping and pipeline.

- `preview.mp4`: genuine solo PIE review, 42.27 seconds, silent.
- `ContactSheet.jpg`: representative gameplay poses.
- `Verification.json`: final package/source preservation audit.
- `ImportReport.json`: all 984 imported animation frames and scale-channel data.
- `SoloCheck.json`: final passing 20-check native gameplay pass, 635 captures.
- `EditorRestored.json`: ended PIE, clean packages and restored editor settings.
- `Backup/Content`: original versions of the 14 replaced animation packages.
- `Baseline.json` / `AssetBaseline.json`: original hashes, settings and reference pose.
- `SourceInspection.json`: inspected source rig/actions; source file is unchanged.
- `Attempt1Check.json`: initial fixture's outdated jump timeout; final check uses the configured full duration.

Exported FBX and their checked manifest are in `ArtSource/NutKnightCorrected`.
The existing skeleton, meshes, materials, profile, timings and walking are preserved.
No cook/package or multiplayer test was requested or run.
