# Changelog

## 2026-10-12 — Caries boss, phase 2 animation preview

- Added eight separate Blender performances for the asymmetric phase-two boss: awakening/roar, forward hammer, wide backhand, palm shove, infection ground pulse, heavy stagger, exhausted recovery and a supported pivot.
- Added synchronized performances using the existing mouth expression shape keys. The original primary ARP body pose remains frozen; BoneBrush animates the body and huge arm, with the reviewed sole bones preserving foot orientation.
- Preserved the earlier animation actions and the hand-supported walk in the editable derivative. Added a continuous NLA review timeline and pose/event markers for the new clips.
- Added an isolated Blender renderer, compact animation helpers and one complete review video with Russian clip captions. The renderer evaluates real half-frame poses at 60 FPS from the 30 FPS source animation.
- Bundled the reviewed V4 parent rig used by the new authoring script. Historical references to the original user file and phase-three artist source do not need to be present to author or render the new pool.
- Updated AGENTS.md: use short solo PIE checks for gameplay work; run multiplayer tests only on a separate explicit request; include the actual additions/changes and validation in commit and pull request descriptions.

Status: Blender preview for approval. These are newly authored asymmetric performances, not newly downloaded Mixamo clips. Unreal animations, combat hitboxes and Niagara effects are not part of this change.

Validation: key-pose reach and floor/contact checks during authoring, visual review of rendered poses, and verification of the completed video. No Unreal build, cook, package, gameplay or multiplayer run is required for this Blender-only preview.

Review source: `ArtSource/CarriesStage2/CombatPool_v1/Carries_Stage2_CombatPool_v1.blend`.

Video: `Artifacts/CarriesStage2/CombatPool_v1/Carries_Stage2_NewAnimations.mp4`.

To render the saved pool in an isolated Blender process:

```powershell
blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python Tools/Blender/render_carries_stage2_review.py -- --view ThreeQuarter --output-dir Saved/Codex/CarriesStage2NewReview
```

Use `--draft` for event-pose previews. Full rendering requires a fresh output folder. After a completed render, copy `RenderManifest_ThreeQuarter.json` to `RenderManifest.json` in that folder and run:

```powershell
python Tools/BuildCarriesStage2CombatPoolVideo.py --input-dir Saved/Codex/CarriesStage2NewReview
```

The encoder needs Pillow and `ffmpeg`/`ffprobe`; explicit executable paths can be passed with `--ffmpeg` and `--ffprobe`.
