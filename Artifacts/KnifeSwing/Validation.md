# Knife overhead chop

Validated against the current UE 5.8.1 project.

- MessControlEditor Development Win64: build succeeded.
- Rendered inventory scenario at 60 FPS: passed, including matching food damage and cooldown protection.
- Measured right wrist: 84.7 cm above its initial height during the windup, 56.1 cm below during follow-through.
- Swing duration: 0.70 seconds; authoritative contact: 0.28 seconds.
- Inventory automation: 3 passed, 1 failed. The failure is `MessControl.Inventory.HoldSprayAndPreserveProgress`, assertion `Released food in front of the ulcer still blocks treatment`. Spray and food collision behavior were not changed by this animation update.
- Saved asset index refreshed: 34 current snapshots, no export errors, current build.

Runtime capture: `Artifacts/Inventory/Stage02.png`.
Full automation report: `Saved/TestReports/KnifeSwing/index.json`.
