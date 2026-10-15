# Second melee stroke: descending diagonal

The second stroke now winds to the character's upper right, cuts on a curved
diagonal across the front, and follows through to the lower left. The knife's
cutting edge and the pickaxe's lower sharp point lead along the curve. Equipment
orientation is converted through the actual grip after the body's lean and
twist, so the flat side does not replace the cutting edge during the stroke.

The first and third stroke definitions and all combo input/timing are retained.

## Evidence

- `GeometryBefore.json`, `ToolGeometry.jpg`: actual authored mesh vertices and
  grip transforms; the blade lies in local XZ, its sharp side along local -Z.
- `EditorIntegration.log`: successful cold Development Editor module compile
  and restart. No game cook/package.
- `SoloCheck.json`: short solo PIE with real primary press/release edges.
  Three rapid clicks produce three knife hits (75 damage total) and three
  pickaxe hits (100 damage capped at target health). Holding causes one hit;
  a click after the chain timeout starts a fresh first hit.
- `DiagonalCheck.json`: captured hand path and actual tool axes during the
  descending second stroke. Both tools cross from positive to negative local
  right, drop over 100 cm, and have the sharp side aligned with hand travel
  (dot product above 0.92 in the sampled pre-contact strike frames).
- `SecondStrike.jpg`: cropped genuine gameplay frames at wind-up, cut and
  follow-through. `preview.mp4` shows both complete combos at normal speed;
  `preview_close.mp4` crops the same footage for easier viewing.
- `VideoManifest.json`: 294 genuine solo gameplay-camera captures at fixed
  60 Hz; lossless PNG normalization before video encoding and strict full
  decode verification. Silent preview; gameplay audio remains enabled.
- `EditorRestored.json`: original L_Mouth map restored, PIE stopped, no dirty
  content/maps, original timing and background throttle restored. Temporary
  compiler settings were restored after validation.

Source baselines for this correction are in `Before/`. Earlier combo and hit
feedback work is preserved in the neighboring artifact folders. Multiplayer
and game packaging were not part of this correction.
