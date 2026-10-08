# Raised free camera

Camera pitch now spans -85 to +85 degrees. Horizontal orbit remains independent
of the body and covers a full turn. CameraOrbitHeight is editable in Camera/Orbit
and defaults to 100 cm instead of the old 30 cm pivot. The initial overview eye
and focus receive the same 70 cm lift.

Above the horizon, the view can pitch upward while the eye stays at the raised
height behind the character. This avoids swinging the camera into the floor and
retracting it against the character. Wall sweeps still start at the original low
collision anchor. Shoulder aiming and camera-centre spray/watergun targets remain
active.

Validation:

- Final editor build succeeded (`free_camera_build_final.log` in the task scratch
  workspace). No game packaging was requested or performed.
- `TestsFinal/index.json`: MessControl.Camera.OrbitAndWallCollision succeeded,
  including upward/downward pitch, raised/configurable eye height, independent
  yaw, zoom, wall retraction and recovery.
- `Runtime.json` and PNGs show the raised initial view, upward/downward/level
  views, four quarter turns and upward spray/watergun aiming. This is a local
  rendered PIE check, not a multiplayer input test.

The saved-package index refresh could not complete: the existing tracked
`Content/Gameplay/CoreLoop/DA_SingleDayDirector.uasset` contains committed Git
merge conflict markers between two LFS object IDs, and Unreal rejects its package
header. That unrelated asset was left unchanged. Runtime results and the current
source/build are the validation evidence for this camera change.
