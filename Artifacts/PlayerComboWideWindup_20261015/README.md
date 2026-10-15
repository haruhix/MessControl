# Wider second-stroke wind-up

The hand draws farther back behind the right shoulder before the second hit,
settles briefly (35 ms at normal attack rate), then sweeps a wider descending
curve from upper right to lower left. The crown twists farther into the load.
The curve retains its existing midpoint/contact position and attack clocks;
the cutting edge continues to lead along its tangent through the real grip.

## Validation and preview

- `EditorIntegration.log`: successful cold Development Editor module compile
  followed by a clean editor restart; no game cook/package.
- `SoloCheck.json`: two short solo gameplay combos with real primary press
  edges, three accepted contacts per tool and the expected target damage.
- `DiagonalCheck.json`: actual descending hand path and cutting-edge alignment.
- `WindupComparison.json` and `WindupComparison.jpg`: measured retraction and
  lateral sweep against the preceding diagonal-stroke implementation, plus
  genuine gameplay-camera frames for both tools.
- `preview.mp4`: full gameplay-camera view. `preview_close.mp4`: a crop of the
  same footage to show the wind-up more clearly. Both combos run at real speed.
- `VideoManifest.json`: source capture and strict video decode verification.
- `EditorRestored.json`: L_Mouth restored, no active PIE or dirty packages,
  original frame-rate and editor throttle settings restored.

`Before/` preserves the source and mechanic documentation before this change.
The preceding attack-feedback and combo work is retained in neighboring
artifact folders. Multiplayer and game packaging were not part of this edit.
