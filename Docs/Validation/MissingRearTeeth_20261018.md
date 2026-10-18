# Restore the rear arena teeth — 2026-10-18

The saved `L_Mouth` map contains twelve tooth sockets, IDs 1–12, but the active `/Game/Data/DA_RunRules` asset limited spawning to eight. `AMCGameMode::RestartShift` sorts sockets by ID and creates only the configured count. IDs 9–12, the last two teeth on each side, therefore never spawned while their placement previews remained hidden in game.

Changed only `DA_RunRules.Settings.InitialArenaTeeth`, from 8 to 12, and saved the asset. The original asset matched the committed SHA-256 `38e55be16bbb796ab8f81eb23ce63a3d46dc4d3cb4b20331f325dae258baca16`; it had not been modified by the preceding wizard ability changes.

A fresh solo PIE session confirmed twelve spawned teeth, IDs 1–12, all visible, with none lost or consumed. PIE was stopped afterward. No native compilation was needed.

Backup and measurements: `Artifacts/MissingRearTeeth_20261018/DA_RunRules_Before.uasset`, `Change.json`, and `SoloVerification.json`.
