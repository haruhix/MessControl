# Spray at face height

The right hand extends in front of the posed face during spray activation, with or without a healing or fire target. It returns smoothly after release. Swimming, climbing and food grips retain their own hand poses.

- UE 5.8.1 Development Win64 editor build: passed.
- Inventory automation: 4 passed, 0 failed. Checks targeted and untargeted hand poses, release and traversal/grip precedence.
- Rendered inventory scenario: passed. Measured wrist height relative to the eyes: 0.0 cm; forward reach: 45.0 cm. The knife overhead chop and food damage checks also passed.
- Four-process spray network scenario: passed with 75 ms packet lag and 2% loss. The rendered owning client checks mist activation and the actual hand pose of all four characters during two hold/release cycles.
- Saved asset index: 34 current snapshots, no export errors, current build.

The previous spray obstacle assertion used the prototype food's bounding box as evidence of ray obstruction. Its new mesh-shaped collision does not necessarily cover that ray. The test fixture now configures a solid cube through the production food setup; food collision and treatment rules are unchanged.

See `InventoryTests.json`, `NetworkValidation.txt`, `Hold.png` and `Release.png` in this folder.
