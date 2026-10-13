# Nut encounter gameplay and VFX previews

- `NutStormArrival.mp4`: the final part of the continuous rain and arrival of both bosses, 8.2 seconds.
- `NutWizardSummon.mp4`: persistent spell nuts, conversion of all 18 available nuts, one real Pickaxe kill before the fuse and remaining detonations, 16.7 seconds.
- `NutShieldDamage.mp4`: uneven dark fractures grow as the shield loses durability, followed by shell debris and the unshielded model, 4.5 seconds.
- `NutFireNut.mp4`: an actual burning nut projectile, flame trail and player impact, 6.6 seconds.

The videos contain genuine solo PIE captures at 6 Hz for gameplay and 12 Hz for VFX. H.264 output repeats those captures at 30 fps; it does not generate intermediate poses or speed up game time. Two final screenshot requests were missing and are recorded in `VideoSourceReport.json`. This sampling is for short visual reviews, not a frame-rate benchmark.

The shield preview temporarily starts at 160 durability to show four ordinary base Pickaxe hits. Game data retains 500 durability. Camera placement, observer dodging, boss walking and ability readiness are temporary PIE fixtures; native damage, projectile collision and the five-second summon fuse remain active. F3 pauses the ordinary event sequence, which explains the background task HUD in these previews.

`Validation.json`, `NativeChecks.json`, `ExactEditorBuild.json` and `CommitSourceHashes.json` record 17 focused native cases, the isolated Editor compilation of the proposed commit source and the short local gameplay/VFX checks. Runtime checks were performed in the main development checkout. Raw screenshot/report paths in the manifests refer to local `Saved/Checks` evidence; generated build output and indexes are not committed.

Full-battle balance, multiplayer, cook and packaging were not run. Existing boss animation assets are reused; the new visual asset is `M_NutShieldFracture`.
