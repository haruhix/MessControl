# Ulcer rework verification — 2026-09-30

- Editor build: successful.
- Game Win64 Development build: successful; Binaries/Win64/MessControl.exe.
- Automation: 63 passed, 0 failed. 58 succeeded and 5 succeeded with warnings.
- The warnings concern the unit fixtures' engine Plane without CPU Access and the editor's HTTP connectivity probe. No production tongue asset was changed to hide them.
- EventSandbox rerun after making the preview use ordinary Egg explicitly: passed.
- L_Mouth rendered smoke: passed. Neglected food visibly absorbs, the ulcer keeps its meal Batch and incomplete cleanup task, held spray advances progress, a two-second pause preserves 28.57%, and seven effective treatment seconds resolve the task.
- Seven unedited game screenshots are in this folder. The circular UMG progress indicator was visually inspected at 29% and 80%.
- Inventory network test: listen server and one client passed all four tool stages, absorption position and replacement ulcer checks with 75 ms packet lag and 2% packet loss. Knockdown waves are deferred in the inventory fixture; the wave mechanic has separate tests.
- Settings.json records the saved per-row FragmentScale and absorption settings. FragmentScale is applied independently to the visual mesh and collision, with mass handled separately.

Reports: Saved/UlcerTestReports/index.json, Saved/UlcerDevReports/index.json, Saved/Logs/UlcerReworkSmoke.log, Saved/Logs/InventoryNet0.log and InventoryNet1.log.
