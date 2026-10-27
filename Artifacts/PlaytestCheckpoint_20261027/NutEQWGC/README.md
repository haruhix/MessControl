# Nut E/Q focused solo capture

Verification media (MP4/PNG/JPG/WAV) is **LOCAL ONLY, NOT IN GIT**. Files remain under `E:/DEVGAME/MessControl/Artifacts/PlaytestCheckpoint_20261027/`; fresh clones contain the reports/metadata, not these media files. Media hashes retain local evidence provenance.

Native checks PASS for run `20261027T000051Z_cf9639c7`. Normal Pickaxe cutting created native nut fragments; E held one fragment without a tower; Q emptied the hand, reenabled native physics, and reduced the knight shield from 500 to 482. Boss HP remained 1400. Observed native flight speed was approximately 2389.6 cm/s.

Video (`Nut_Complete_090050.mp4`, local only) · [Native report](Report.json) · [Measured video metadata](Nut_Complete_090050.VideoMetadata.json) · [File manifest](Manifest.json)

Root viewed ActionContactSheet and accepted the limited static-piece/frozen-boss local-input sequence: real cut, one held shell/Q hint, flight and visible BLOCK impact. This is limited visual readability acceptance, not a full fight or chest validation. The earlier approved capture gate does not constitute final video acceptance. The filename `Nut_Complete_090050.mp4` identifies a completed focused capture, not a complete boss fight.

The 1286×760 video contains 131 decoded frames over 4.833333 seconds. Timestamp measurement gives 27.08 FPS, a 133.3 ms maximum gap, two gaps above 100 ms and none above 250 ms. These are measured encoded timestamps, not native simulation FPS. Capture used independent Windows Graphics Capture of the explicit PIE window with VFR output; no interpolation or timewarp. The video has no audio.

Disclosed fixture: native F3 starts the encounter; supported player/whole-nut/fragment placement, one gate-time placement refresh, temporarily paused knight actor tick, and a temporary CameraActor isolate one shot. The selected native cut fragment receives `SetSimulatePhysics(false)` only for initial E pickup setup. Collision remains QueryAndPhysics; no HP, phase or held-state values are written. Actual native Q reenabled physics. No natural loose-fragment pickup claim is made.

This is one focused local action. It does not validate a full fight, reward-chest timing, physical keyboard delivery, multiplayer, packaged cold start or sound. Exact shield/HP changes come from the native report. Four source screenshots, the automation result, recorder launch metadata and raw ffprobe measurements are retained. The recorder metadata's `recording` status is its launch snapshot; measured metadata contains final progress and decoded frame count.

All 10 source copies were verified against their originals using SHA-256. Source files remain unchanged. Parent-generated contact sheets are listed in Manifest.json only when present and hashed; they are visual navigation aids rather than additional runtime proof.

The author agent reviewed ActionContactSheet.png and observed one held shell with the Q prompt, projectile flight, then visible BLOCK impact in the final row. Root subsequently accepted that limited visual sequence; the fixture and full-fight/chest limitations above remain. ContactSheet.png, ActionContactSheet.png and ContactSheetTiming.json are present and hashed in the manifest.
