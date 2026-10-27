# Coins — native audio rerun, 27.10.2026

Verification media (MP4/PNG/JPG/WAV) is **LOCAL ONLY, NOT IN GIT**. Files remain under `E:/DEVGAME/MessControl/Artifacts/PlaytestCheckpoint_20261027/`; fresh clones contain the reports/metadata, not these media files. Media hashes retain local evidence provenance.

[Runtime report](Report.json): actual Spray contact completes repair4→0, wallet0→10, no repeated reward, prepared coin presenter animated. [Lifecycle](LifecycleReport.json) passed and restored the unsaved workload fixture.

WAV (`Audio_ActualGameOutput.wav`, local only) is native game master output: stereo48kHz/16-bit, 5.440s. [Signal verification](AudioVerification.json): RMS−35.80dBFS, peak−20.13dBFS, 521151/522240 samples nonzero. No audition or coin-SFX/mix-quality acceptance is claimed. The earlier all-zero recording remains unchanged under `../Coins/Audio_Unaccepted_Silent.wav`.

The temporary Editor fixture enabled `EnableGameSound` and `bAllowBackgroundAudio` before ordinary PIE startup. Both exact originals (`false`) restored after owned PIE closure and Base.finish; no errors, dirty packages, SaveConfig, package saves or master-volume changes were reported.

Native-window MP4 (`ActualWindow.mp4`, local only) has43 measured frames over5.922s at7.53Hz. Contact sheet (`ContactSheet.jpg`, local only) awaits root review of this rerun. The10/15s cells repeat the last available source frame; the video itself is shorter. This is diagnostic capture cadence, not game FPS. Encoding preserves measured wall timestamps, without interpolation/speedup; one terminal duplicate retains the final hold. The WAV is separate: a common absolute wall anchor was not stored, so sample-accurate mux alignment is not claimed.

All copied bytes match their source SHA-256 in [Manifest.json](Manifest.json). Raw PNGs, desktop/private captures and failed attempts are excluded. This transient standalone fixture proves neither hardware input, full-day progression, Hub spending, disk persistence, multiplayer nor cold packaged first use. [ReviewScope.json](ReviewScope.json) records the exact limits.
