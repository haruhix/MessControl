# Duplicate coffee pattern removed

The old M_ArenaTooth Coffee pattern painted camouflage-like flat spots over the
crown in addition to the normal projected plaque. The base now remains enamel;
the native localized coating and real brush cleaning remain active.

- `Before.json`: actual shader code and editor state before the repair.
- `M_ArenaTooth_Before.uasset`: original material recovery copy.
- `MaterialFix.json`: both exact node changes, successful compile and protected
  asset hashes. No native or map changes are required.
- `SoloCheck.json`: all nine bounded solo checks passed with dirty teeth and
  actual held Brush input. Temporary fixture details are in the existing probe.
- `CoffeePlaque.png`: genuine gameplay capture of localized plaque on enamel,
  under the saved arena lighting; the full-crown pattern is gone.
- `FinalAudit.json`: protected-file and final editor-state verification.

Implementation and explanation: `Docs/ArenaToothMaterial_20261017.md`.
