# Coffee slime v2 — lead review

Request: redesign the coffee slimes with the simplicity and elastic elemental-blob appeal of Genshin slimes, without copying a character. The lead reviews the delegated concept, model and motion before integration.

## Baseline diagnosis

The v1 files remain intact. The user rejected their appearance. The main art-direction failures are protruding ivory eyes, heavy eyelids/brows and a human-like mouth, a busy cream cap, and a dark plastic response in the mouth lighting. Successful export and gameplay checks did not establish an acceptable design.

This is an art-direction and review failure, rather than evidence of a missing Blender technique. Existing modeling, animation, export and visual-validation workflows are sufficient; no generic skill changes are required.

## Reference observations

- [HoYoWiki Hydro Slime](https://wiki.hoyolab.com/pc/genshin/entry/2132?lang=ja-jp), [illustration](https://upload-static.hoyoverse.com/hoyolab-wiki/2023/08/03/94675339/f4a8ac4ac5a51c9f240fe482e3c34272_2830087593203918075.png), inspected in the browser on 2026-10-15: one uninterrupted soft volume, two simple flat eye shapes, broad highlight/shadow masses, secondary elemental markings. The illustration is a style reference, not calibrated orthographic geometry. No source pixels or textures are reused.
- A second search result labeled as official slime artwork was opened and rejected: it actually depicted three saurians. Search descriptions are not accepted as visual evidence. Our coffee identity, proportions, face and accent remain original.
- [HoYoWiki Geo Slime](https://wiki.hoyolab.com/pc/genshin/entry/2137?lang=ja-jp), [illustration](https://upload-static.hoyoverse.com/hoyolab-wiki/2023/08/04/94675339/94bc1197fd03575ee801f185bd71e42e_7569532490463302891.png), actually opened in the browser: airborne rounded mass versus a wide low grounded squash. This informs deformation and the footprint, not the stone crown or glowing face design.

## Binding design criteria

1. A low rounded dome with a soft footprint and restrained lower lobes; no limbs, eye sockets or human facial anatomy.
2. Two flat pale eye marks. No pupils, eyebrows, lids, teeth or modeled human mouth.
3. Warm readable coffee/caramel body. A small shallow crema accent belongs to the body rather than becoming a whipped-cream hat.
4. The face and silhouette remain legible at normal third-person gameplay distance.
5. Elastic anticipation, takeoff, delayed upper-body follow-through and settling. No additional native deformation on top of the authored animation.

## Review gates

- Concept: accepted by the lead after opening `concept_v2.png`. The large view establishes the design; smaller side/front panels are approximate perspectives, not calibrated blueprints. The simple low dome, restrained integrated lower lobes, flat cream eye marks and small flush crema crescent meet the requested direction. No human anatomy remains.
- Graybox first pass: rejected after opening all four actual renders. The simple face and broad dome fit, but the long straight lower edge and nearly vertical profile cut read like a rigid helmet/half sphere. Requested a soft rounded lower roll in the bottom 10–15% of the height, slight narrowing below maximum girth and restrained integrated contact lobes. The crema mark needed about twice its first thickness to read as a crescent rather than a scratch.
- Graybox repair: accepted after reopening the actual front/profile/three-quarter renders in `graybox/`. The rounded lower roll removes the rigid cut, the contact lobes remain integrated and restrained, and the broader crescent reads as a flat mark. Final rig/animation/export may now proceed. The rejected baseline is retained under `graybox_before_lower_roll/`.
- Final source model/materials and sampled motion: accepted after the color repair. The lead opened the motion contact sheet and full multiview sheet, caught the cropped top view, requested its repair and opened the corrected top. The sampled anticipation, lift, apex, landing, inflation and collapse are coherent; flat marks remain seated. Fresh GLB Hop/PreBurst renders were opened and retain appearance/poses. Eight focused source/FBX/GLB audits report technical passes. The short video is provided separately; still/contact-sheet review is not a claim of continuous real-time playback inspection.
- Unreal: accepted. Eleven owned mesh/skeleton/clip/material/texture packages migrated and saved at the existing native paths. Unit root, five clips, three ordered material slots and a 33.998 cm Hop passed engine checks. The lead opened genuine final gameplay frames for the actual hero view, trail, rupture and brush; all 15 focused solo checks passed. Network, GPU profiling, cook and packaging remain outside this request.

## Integration constraints

Retain the existing five bone names/hierarchy, unit root scale, +X forward, +Z up, floor pivot and 100 Hz baked clip durations (Idle 1.20 s, Hop 0.90 s, Hurt 0.28 s, PreBurst 0.60 s, Burst 0.18 s). Bone rest positions and material layout are validated against the v2 manifest, not assumed from v1. No native gameplay change is required for this visual replacement.

Concept review decision: target rest bounds are 1.58 m in +X depth, 1.68 m in +Y frontal width and 0.90 m in +Z height. The old 1.30 m height is superseded to preserve the broad low concept. Native collision remains unchanged. A crown-height `foam_tip` keeps the existing label anchor meaningful despite the absence of a raised foam cap.

## Added user scope: game shaders and coffee trails

The user explicitly requested control of the in-game shaders, including the coffee trails, and a push after completion. The root owns these Unreal material edits. The old trail and rupture-drop materials still used green palettes independently of the v1 coffee mesh.

`Tools/Unreal/author_coffee_slime_surfaces_v2.py` replaces only the two tagged Cataclysm materials after saved-package backups. The trail uses coffee pigment, a restrained lighter thin edge and moderate wet response; its exact existing mask/cleaning contract remains unchanged. The drop/fallback gel exposes `CoffeeTheme=1` so legacy green runtime overrides cannot repaint it green. The first revision compiled but read too matte in gameplay. Revision2 strengthens the shading meniscus and adds restrained warm view-dependent sheen; the lead opened the final trail/rupture/brush frames and accepted it. Five mask samples remain in the trail, with no extra translucent surface or external texture. Existing mask edges remain angular at the native 64x64 resolution.

## Final source material checkpoint

The lead independently rendered the current source's Idle, Hop apex and PreBurst end to `Saved/CoffeeSlimeV2/LeadPreview/` without saving or changing the source. Shape and the flat surface marks remained coherent in those sampled poses. The textured body regressed to a dark glossy appearance compared with the approved constant-color graybox; the large upper highlight nearly hid the crema mark. Requested a linear-color versus saved-sRGB texture parity check before any runtime color compensation, and reduced coat. These three stills do not establish full motion/export acceptance.

Repair verified: the original PNG contained raw intended linear values in an sRGB image (midpoint RGB76/28/8), causing a second decoding. The source now encodes the gradient into sRGB before saving, with a parity audit against the intended linear palette. Coat is .05. The lead opened the corrected hero and fresh GLB renders; the warm medium coffee response returns and the crema mark remains readable. Rejected renders are retained in `Artifacts/CoffeeSlimeV2_20261015/BeforeGamma`.

## Engine migration correction

The first import gate correctly rejected the stale v1 skeleton reference pose. UE5.8's reimport path replaces the task's skeletal ImportData with the existing mesh's AssetImportData. The importer now updates the effective existing data before reimport, then checks mesh/skeleton rest parity and all five new clips. A second guarded resume exposed a non-reflected `last_import_content_type` property; that setter was removed. The corrected migration passed without weakening the pose checks, preserving the original disk backup and all unrelated dirty work. No native build was needed.

Final evidence, sampled PNGs and short encoded clips: `Artifacts/CoffeeSlimeV2_20261015`. Capture sampling is12Hz, encoded by repeating frames at30fps with no generated motion. Review is based on opened stills/contact sheets and recorded native telemetry, not a performance or network claim.
