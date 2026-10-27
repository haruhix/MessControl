# Coffee -> six seconds -> ten foods: local PIE checkpoint

Verification media (MP4/PNG/JPG/WAV) is **LOCAL ONLY, NOT IN GIT**. Files remain under `E:/DEVGAME/MessControl/Artifacts/PlaytestCheckpoint_20261027/`; fresh clones contain the reports/metadata, not these media files. Media hashes retain local evidence provenance.

Actual standalone PIE window recorded using Windows Graphics Capture by HWND,
without Computer Use, window activation, synchronous game-thread screenshots,
frame interpolation or playback acceleration. Audio is not included.

The initial segment is entered by exactly one Editor-only native
StartPostNutRelaxPreview fixture using the saved run settings. It is not a
played nut fight or full-run proof. After setup, stage/deadline/food counters,
health and economy are observed rather than assigned.

Report.json passes: one actual coffee cycle completes, then its native six-second
deadline elapses before food; exactly ten whole-food actors are observed, with
no eleventh after the hold and no intervening reward. Unresolved food/cleanup
work is retained. The first food is observed at game time 22.070157 after the native
pause deadline 21.956521; polling accounts for that small observation delay.

The silent video lasts 33.966667 seconds with 857 decoded frames, measured mean 25.226
frames/second and maximum PTS gap 0.166666 seconds. A configured cap of30 is not a
claim of constant30 FPS. This measures recorded PTS, not simulation performance.
ContactSheet.jpg samples the original footage every three seconds in a4x3 grid.

No network, packaged cold launch, persistent profile I/O, cleanup completion,
reward claiming, or complete-run validation is implied. The stale frames_json
field in Report.json belongs to the earlier optional screenshot recorder; this
run used external WGC. Capture.json, RecorderProgress.log and VideoMetadata.json
are the recording evidence. Visual acceptance remains with root's frame review.

Hashes.json preserves matching SHA-256 hashes for every copied source file.
