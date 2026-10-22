# Branch9 studio mark

**Current runtime ident, 2026-10-22:** the user's new clean geometric BRANCH9
wordmark supersedes the earlier branch/leaf designs. Source, traced SVG,
independent glyph atlas and a reproducible builder are in
`CleanWordmark_20261022/`. The saved `WBP_StudioSplash` now references
`/Game/Gameplay/UI/Brand/Branch9Clean` and its kinetic UI material. Seven
source-derived glyphs assemble in sequence, the 9 finishes with a small angled
settle, and the exact full source holds motionless before a stationary fade.
Duration remains three seconds. Old branch/leaf layers have explicit blank
texture overrides so native fallback loading cannot reintroduce them.

Current user revision: the original A-over-N motif, diagonal separator and
letter positions are restored. Only N's visible upper outer edge is made
vertical; the diagonal cut beneath A remains part of the original motif.
A/N enter together during the motion. The 9 is red (`#E32636`) in static art
and motion. Current evidence is in
`Artifacts/Branch9CleanIntro_20261022/ANOverlap_Red9`. The separate full N in
`VerticalN_Red9` was rejected because it changed the interlocking wordmark.

Actual 1080p/60fps UMG renders, a 30fps encode and a contact sheet are in
`Artifacts/Branch9CleanIntro_20261022`. A short ordinary solo PIE startup
observed the new saved assets for 3.00 seconds and completed the native
loading/menu handoff. C++ rebuild, cook/package, sound design and GPU profiling
were not performed. Motion is awaiting the user's visual feedback; source
silhouettes and kerning were checked against the supplied image. The older
notes below describe superseded artwork, not the current runtime ident.

**Current fidelity status, 2026-10-21:** the user rejected the geometric V2 interpretation below because it differs from the supplied mark. The source-matched replacement is in `ReferenceMatch_v3/`: it preserves the reference silhouette, including the large descending 9, irregular letter edges and curved branch. Root reviewed `validation/Original_Old_New.png` and `validation/front_overlay_reference.png`. `Branch9_2048x768.svg` contains traced paths; `Branch9_ExactAlpha_Source.png` retains the screenshot half-tones. The source is only 485×227, so this is not a recovered high-resolution master.

The top-level V2 PNGs and SVG remain the legacy inputs currently used by the existing intro studies/runtime packages. **V3 has not been imported into Unreal and the intro animation has not been updated.** Do not describe the legacy V2 mark as source-faithful or user-approved. The historical “approved V2” statements below refer only to the earlier internal review. Do not run `BuildBranch9.py` to overwrite V3: it reconstructs the superseded geometric design. V3 is reproduced by `ReferenceMatch_v3/BuildReferenceMatch.py`.

Original vector construction prepared from the user's monochrome Branch9 reference on 2026-10-21. The source contains editable paths for all six letters, the tall 9, an ascending branch and seven leaves. It contains no fonts, traced bitmap, background, reference number “05”, external artwork or downloaded dependencies.

`Branch9.svg` is the editable master. `BuildBranch9.py` recreates it and the six transparent PNGs deterministically with Pillow and fontTools. The PNGs are straight-alpha RGBA, 2048×768, with shared canvas and center pivot. White foreground can be tinted by UMG; a nearly-black `#060708` full-screen background preserves the reference direction. `AssetManifest.json` records per-layer bounds and hashes. The user-provided reference itself stays in the task workspace, outside the shipped art source.

Runtime layers:

- `T_Branch9_Wordmark`: lettering with an organic diagonal negative-space cut.
- `T_Branch9_Branch`: positive white branch and twigs.
- `T_Branch9_Leaves`: seven leaves, aligned to branch endpoints.
- `T_Branch9_Logo`: complete static composition for fallback or reduced motion.

`SolidWordmark` and `RevealMask` are optional authoring layers. The cut mask is not an additional visible graphic. All layers share exactly the same canvas; do not independently crop or fit them.

Suggested three-second motion:

| Time | Action |
| --- | --- |
| 0.00–0.10 s | Black anticipation. |
| 0.10–0.52 s | Fast wordmark wipe, cubic deceleration, rigid letterforms. A very faint directional echo disappears during the reveal. |
| 0.29–0.65 s | Branch sweeps upward with the same directional wipe; translate only 8 pixels at 960-pixel preview width. |
| 0.43–0.85 s | Leaves enter and settle by at most 1.6 degrees. |
| 0.85–2.61 s | Clean, stable reading hold. |
| 2.61–2.98 s | Short soft fade and 18-pixel exit translation. |
| 3.00 s | Hand off to the game front end. |

Overall scale settles from 1.012 to 1.0. Avoid character stretching, repeated shake, strobing or added confetti. At reduced motion use the complete logo with fade only. The offline `MotionStudy.py` reproduces these timings using the supplied PNGs and additionally needs NumPy and ffmpeg. Its output is explicitly a compositing study, not proof of Unreal playback. Native WBP layout, shared theme integration and runtime capture belong to the main implementation. `Timeline.json` contains exact easing equations and normalized wipe widths.

Import PNGs as UI textures, straight alpha, sRGB, no mipmaps, UI compression; keep filtering bilinear/default. The SVG is a source deliverable and does not require an SVG runtime plugin. The complete vector has generous transparent margins and was inspected at 2048 px and at a 640 px canvas width.

Provenance: bespoke path construction by Codex for this project, with visual direction supplied by the user. No third-party font license or attribution dependency was introduced. Studio name and brand-reference ownership remain with the user; this work does not independently establish trademark clearance.

Three comparable 1920×1080, 60fps, silent three-second studies are reproducible with `BuildIntroVariants.py --variant A|B|C`. A uses the approved word-then-branch contract. B establishes the branch before revealing the word. C opens the word rapidly along the branch diagonal and holds the complete mark longest. `IntroVariants.json` records each exact mask/easing/timing. Each uses the same approved V2 letter, branch and leaf paths; the alternatives do not modify the native Unreal defaults. Only the final MP4s/contact sheets are published under `Artifacts/Branch9Intro_20261021`; raw validation and encoding/decode logs stay in the task workspace on E:.
