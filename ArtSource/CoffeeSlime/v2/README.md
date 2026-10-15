# Coffee slime v2

Original low, broad coffee gel design. The official Hydro/Geo studies informed simple coherent masses and flat eye marks; no reference image, exact face, emblem, crown, texture or geometry is reused. See reference_notes.md and concept_prompt.md.

Root directly approved the concept, repaired graybox, corrected source material, final multiview form and critical motion poses. The first graybox lower cut and incorrectly encoded dark gradient were rejected and repaired. V1 remains untouched and rejected for this art direction; technical metrics do not constitute visual approval.

## Delivered files

- CoffeeSlimeV2.blend: durable source with one joined skinned mesh, five bones, five baked actions, UVs, packed body texture and neutral review stage.
- exports/SK_CoffeeSlime.fbx and A_CoffeeSlime_{Idle,Hop,Hurt,PreBurst,Burst}.fbx: canonical cm-coordinate mesh and five separate rig-only clips.
- exports/CoffeeSlime.glb: meter-space mesh, materials, texture, skin and all five actions.
- manifest.json: importer contract, retaining the existing runtime asset identities.
- review/hero.png, multiview_contact_sheet.png, motion_contact_sheet.png and fresh_glb_comparison.png: actual Blender renders, labelled without generated or painted corrections.
- review/CoffeeSlimeV2_Motion.mp4: H264,960×720,24fps,158frames,6.583s; Idle×2,Hop×3,Hurt×2,PreBurst,Burst.
- audit/*.json: source/fresh-import checks. TextureColorParity.json: actual PNG samples and sRGB-decoded linear values.

## Mesh and materials

Source/GLB meters, +X forward, +Z up, floor pivot. Body dimensions X1.58/Y1.68/Z.90m; flush crema raises total exported rest height to .90119m. One skin has12,458triangles, a closed manifold body, open surface-conforming eye/crema patches, finite UVs, no degenerate triangles and at most four normalized influences. Three ordered opaque slots:

| Slot | Base color, linear | Roughness | Specular | Coat |
| --- | --- | --- | --- | --- |
| M_CoffeeSlimeV2_Body | Gradient .22/.072/.017 to .38/.15/.046 | .30 | .28 | .05 |
| M_CoffeeSlimeV2_Crema | .82/.59/.28 | .42 | .20 | .02 |
| M_CoffeeSlimeV2_Eyes | .94/.81/.52 | .48 | .15 | 0 |

The512² body PNG is linear-to-sRGB encoded and remains an sRGB texture. Its middle texel decodes to(.3005,.1119,.0319), matching the intended linear constant. No color-gain/emissive compensation is baked in.

Bones: root→gel_body→gel_top; face under gel_body; foam_tip under gel_top. Root is fixed. Face/foam_tip are passive deform bones, with no independent face motion. Graphics and support body share the position-based weighting function, constant across mark support regions. Audits measure deformed mark-to-body gaps at critical poses; the positive rest offset is1.2mm and scales during inflation.

| Action | Seconds | Frames at100Hz | Behavior |
| --- | --- | --- | --- |
| A_CoffeeSlime_Idle | 1.20 | 1–121 | Gentle breathing, loop |
| A_CoffeeSlime_Hop | .90 | 1–91 | Anticipation, takeoff,34cm apex, landing squash, settle, loop |
| A_CoffeeSlime_Hurt | .28 | 1–29 | Short backward recoil and recovery |
| A_CoffeeSlime_PreBurst | .60 | 1–61 | Balloon inflation, bounded wobble |
| A_CoffeeSlime_Burst | .18 | 1–19 | Exact PreBurst terminal pose to flat collapse/native splash |

## Unit-safe FBX

Saved source and GLB remain meters. FBX uses a disposable in-memory copy: geometry, armature rest coordinates, pose-location keys and handles ×100; object scale1; scene unit .01. Wrapper name Armature, FBX axes -Y forward/Z up, yielding the established UE +Xforward/+Zup contract. Reference and animated root must remain scale1 in Unreal. Never compensate with component scale100.

All bone tracks baked100Hz, no key simplification or leaf bones. Fresh Blender imports verify physical dimensions and≈34cm Hop. Actual Unreal reference/animation transform gates, lighting and gameplay remain root integration checks.

## Reproduce and validate

The approved graybox is graybox/CoffeeSlimeV2_Graybox.blend; its SHA256 is recorded in the manifest. Run ../author_coffee_slime_v2.py in isolated Blender5.1 with --background --factory-startup --disable-autoexec --threads2 --python-exit-code1; script arguments follow --:

1. --stage author rebuilds final source/texture/skin/actions from the approved graybox.
2. --stage export writes canonical cm FBXs and meter GLB.
3. --stage stills and --stage motion reproduce retained views, poses and video frames. --stage top is only the accepted framing repair, ortho_scale2.85.
4. Run validate_v2.py -- --asset CoffeeSlimeV2.blend, then each exports/file. Add --render to the GLB check for untouched fresh-import shader/pose renders.
5. Run make_review_sheets.py with Python/Pillow. Encode review/motion_frames/%04d.png at24fps with FFmpeg/libx264,CRF19,yuv420p,faststart.

Checks passed for source, fresh GLB, mesh FBX and all five clip FBXs: hierarchy/material ordering, triangles/closed body/UV/weights, size/floor, durations/root stability/Hop lift/mark attachment and PreBurst/Burst continuity where both clips are present. Fresh GLB rendering appends only the approved review stage; it does not repair imported shaders or geometry. Video passed full FFmpeg decode. Root owns Unreal integration, actual game/animation/material acceptance and Git; these are not claimed by this DCC report.

## Root integration result

The root subsequently imported and saved the v2 presentation at the existing
native paths. Engine gates verified the updated mesh/skeleton rest pose, unit
root, three ordered materials and all five clips, including33.998cmHop. The root
inspected final genuine gameplay frames for the body, coffee trail, coffee-profile
rupture drops and normal brush cleanup. All15focused solo checks passed.
Evidence and short videos are in `Artifacts/CoffeeSlimeV2_20261015` at the project
root. No native build, network test, cook, packaging or GPU profile was run for
this asset-only revision. See `LeadReview.md` for review decisions and limitations.
