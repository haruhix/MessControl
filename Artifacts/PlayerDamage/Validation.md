# Reference enamel fracture review

Unreal Engine 5.8.1, current CharacterCurrent mesh and MI_Character.

- M_TeethGameplay compiled and saved with no shader errors.
- Reviewed the same character and camera in PIE at Damage 0, 0.25, 0.65 and
  0.9, corresponding to 100%, 75%, 35% and 10% health. The actual health
  status was kept unchanged; these frames preview the existing material input.
- Full health restores the original enamel. Low damage shows sparse hairlines;
  stronger damage widens the crown/cheek/root faults and adds crown notches.
- Eye and mouth regions retain their original appearance. Dark facial details,
  the blue costume and tool are excluded by the enamel color mask.
- Checked the orange HitFlash and simultaneous Coffee grime with fractures.
- Artist textures/normals and BodyStretch retained. Crown deformation is a
  cosmetic WPO addition; gameplay collision and rig assets were not changed.
- Reapplied the grime authoring script after adding fractures: compilation
  passed, fracture inputs and normal/WPO connections remained present.
- Restored camera attachment/FOV, material values, component ticks, text
  visibility, HUD and time dilation after the final capture. The same pawn's
  health/status before and after were identical; no dirty content or maps.
- Refreshed the saved master and three material instances: 38 current asset
  snapshots, no export errors, current native build.

Images: FractureHealth100/75/35/10.png, FractureHitFlash.png,
FractureWithGrime.png. Defaults and compilation result: PlayerFractures.json.
Authoring source: Tools/Unreal/build_player_fractures.py and
Tools/Unreal/player_fracture_mask.hlsl. Settings: Docs/PlayerDamage.md.

## Rear chip refinement

- Restricted the large front faults to the front surface. They no longer
  project through the tooth into long rear grooves or closed bands.
- Added two independent rear faults with irregular paths, narrower widths,
  reduced depth and tapered ends. Fine surface cracks remain continuous.
- Reviewed Damage 0.9 from the back, both rear quarters and the front, and
  Damage 0 from the back on the current CharacterCurrent mesh in PIE.
- Original enamel returns at Damage 0; the facial fracture pattern and the
  blue costume retain their appearance. No shader compilation errors.
- Restored the original preview camera, material values, component ticks,
  text visibility, HUD and time dilation after capture.
- Refreshed all four saved material packages: 38 current asset snapshots,
  no export errors, current native build.

Images: RefinedBackDamaged.png, RefinedBackClean.png, RefinedRearQuarter.png,
RefinedRearQuarterOpposite.png and RefinedFront.png.
