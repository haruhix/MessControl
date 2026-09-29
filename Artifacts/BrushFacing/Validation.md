# Camera and cleaning correction — 2026-09-30

## Changes

- CharacterMovement turns the body toward the acquired cleaning contact.
- Acquisition checks the actual stain direction on both tooth enamel and tongue.
  Retained contacts cannot bypass facing; walking away cancels cleaning.
- Pose, mask erasure and Niagara emission require a forward contact, including
  headless server simulation.
- Camera position remains at the front of the mouth. Viewport fitting checks
  an expanded capsule and only adjusts rotation. Vertical FOV is preserved.
- The front pawn boundary ignores the camera and no longer inherits the mesh's
  collision defaults. The spring-arm sweep starts above the tongue.

## Verification

- Editor Development and Game Development builds succeeded.
  Logs: `Saved/CameraFacingRouteBuild.log`, `Saved/CameraFacingGameBuild.log`.
- Native Unreal MCP automation: 57 passed, 0 failed, 0 skipped.
  Report: `Artifacts/Inventory/NativeAllTestsAfterFacing.json`.
- Rendered cleaning: left row, right row and tongue passed. Each case starts
  with a 45-degree offset, then turns 180 degrees and walks away while holding
  the brush. Recorded contacts face the surface; masks stop changing during
  the turn-away and retreat. Screenshots: `Facing00.png`–`Facing02.png`.
- Camera: 13 placements plus a continuous 9-point walking route passed in
  16:9 and 4:3. The actor remains alive throughout the route. The full test
  capsule stays in the viewport; the camera keeps its front anchor and does
  not cross the palate shell. 1079 screen samples at 16:9, 461 at 4:3/15 FPS.
  Reports: `Artifacts/Camera/Validation_16x9.txt`, `Validation_4x3.txt`.
- Two-peer network brush test passed with 75 ms latency and 2% packet loss;
  both peers ended with the same mask hash, 3900449120.
  Report: `Artifacts/Brush_Validation.txt`.

The first four-peer run missed the initial untouched-state observation on
one late-starting client (seen=30 rather than 31). All four peers ended with
the same mask hash. That run is retained in `Network4First.txt`; it is not
counted as a passing four-peer test.

The authored meshes remain the current blocking. The new camera does not
create the future front-incisor mesh from the reference image.
