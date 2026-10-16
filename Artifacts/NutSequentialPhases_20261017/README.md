# Knight → Wizard encounter

Only the Knight appears after the rain. Its first defeat splits its head and
reveals the Wizard, with fresh health. Defeating the Wizard completes the event.

- `preview.mp4`: genuine solo PIE capture, normal simulation speed, captioned,
  silent; 15 Hz source repeated at 30 fps.
- `ContactSheet.jpg`: sampled Knight, split, Wizard and victory views.
- `SoloCheck.json`: passing final gameplay check, all 11 assertions and 127
  captured frames. Temporary 40 HP/shield/observer fixtures are described inside.
- `Attempt1`: retained failed test setup, superseded by the passing root report.
- `FocusedTest/index.json`: focused native test Success, zero errors and two
  Engine Cube fixture warnings.
- `EditorBuildWithPhaseUI.log`: final native Editor compile succeeded.
- `AssetBaseline.json`, `Verification.json`: 110 protected asset hashes audited.
- `CompilerSettingsRestored.json`: temporary compiler configuration restored.
- `EditorRestored.json`: fixture settings restored and final shutdown evidence.

Full implementation and validation scope:
`Docs/NutSequentialPhases_20261017.md`. No Windows game package or multiplayer
test is included.
