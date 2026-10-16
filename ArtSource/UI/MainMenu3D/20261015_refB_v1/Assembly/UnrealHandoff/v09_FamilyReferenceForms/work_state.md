# Family reference forms — frozen handoff checkpoint

DAY01 Human and DAY02 Predator geometry, seam UV and exports are frozen for native Unreal/Painter handoff. Root opened the actual shape views, front/oblique checker views and both atlases for each asset and accepted these stages. Final Unreal art, physical material calibration, character/cavity fit and runtime transition review remain root-owned and pending. Independent family QA is running read-only; producer checks below do not substitute for it.

## Current authoritative handoffs

- DAY01/UV_v02_GumReturns/frozen_handoff_receipt.json — 27 exact file hashes. Accepted shape DAY01/v03, 240324 vertices / 480512 triangles, 32 paired crowns and two gums.
- DAY02/UV_v01/frozen_handoff_receipt.json — 27 exact file hashes. Accepted shape DAY02/v02, 168644 vertices / 337152 triangles, 32 paired crowns and two gums.
- current_proofs.json contains absolute paths and core hashes. Every file in both frozen receipts was rehashed when this checkpoint was published. Frozen folders must remain immutable. Independent QA should publish sibling evidence, not alter these receipts.

The frozen receipts are current handoff authority. Original export-time producer receipts preserve old candidate metadata hashes. DAY01 inherited UV_status='WITHHELD until root shape review' and final_export=false fields in its already-frozen metadata are obsolete snapshots; its explicit frozen status, approval_scope and frozen receipt record the accepted handoff. They were preserved to avoid invalidating existing hashes. DAY02 metadata explicitly corrects these legacy fields before its freeze.

## Geometry changes

Human v03 retains broad rounded rectangular crown shoulders, cervical taper, paired center seams and readable lower teeth. The rejected circular gingival tube became an anatomical closed collar/root eminence, receding attached gingiva and lingual return. Thirty-two crowns provide a real U arch and posterior depth; no image planes or baked radiance.

Predator v02 uses thick convex pointed crown lofts, broad soft shoulders and continuous rounded tip taper. Tip bend was halved from the rejected first proof. Shorter posterior crowns turn into actual arch depth rather than doubling the visible edge needles. Paired center seams and lower posterior readability must remain guarded during native transition review.

All five absolute source/morph arrays remain bit-exact through UV/export authoring. Saved legacy rigid row transforms are preserved. Source units are unchanged, native +Y front/+Z up, one root, all weights 1, Basis fully open plus Pose_000/Pose_025/Pose_050/Pose_075, enamel slot 0 and gum slot 1. No whole-mouth nonuniform scale was introduced. Per-asset uniform framing belongs to root's native projected-width comparison.

## Seam UV and actual measurement

Both assets have 96 enamel charts (three logical charts per crown) and 10 gum charts (continuous hero labial strip plus bounded posterior returns/endcaps). Four hidden posterior cuts total; endcaps minimum 70.4 px at 4096. MINIMUM_STRETCH solver, no SmartProject/body fan charts or stacked islands. UV density is allocated to surface quality rather than arbitrary occupancy.

Human: enamel/gum coverage 62.7%/42.9%; stretch P95 1.243/1.224; angle P95 7.26/7.91 degrees; density P95/P05 1.173/1.147. Median density 204.9/213.2 px per source unit. Human gum A/B retains enamel UVs bit-exact from UV_v01.

Predator: enamel/gum coverage 46.2%/44.7%; stretch P95 1.076/1.217; angle P95 2.74/8.04 degrees; density P95/P05 1.057/1.149. Median density 233.3/214.3 px per source unit. Its UVs were freshly solved for its own geometry, not copied from Human.

Both: zero raster overlaps, collapsed UV triangles or mixed-winding charts; finite UV0 inside 0–1, approximately 16 px borders/32 px chart spacing. Producer opened actual front/oblique/back checker renders and both atlas images. Measurement reports are explicitly labelled producer, not independent.

## Fresh export validation

Read-only fresh FBX validation across nine source/runtime-interpolated poses passes for both assets: one root, four morphs, all-root weights, matching geometry counts/material slots/UV0, smooth faces, closed manifold components, positive triangle areas and no zero edges.

Human maximum source/fresh agreement error 0.0000833 cm; fresh reflection error 0.0001683 cm. Predator agreement 0.0000822 cm; reflection 0.0001648 cm. Source paired crown/gum errors remain at floating point noise. All validation input hashes remained unchanged. These are producer technical passes; actual native PBR/motion acceptance is pending.

## Handoff contracts and next owner

FBX export: forward -Y/up Z, apply_unit_scale=false, FBX_SCALE_NONE, no modifier evaluation, leaf bones, animation baking or tangent export. Native import: convert_scene=false, convert_scene_unit=false, force_front_x_axis=false, import morphs=true, IMPORT_NORMALS; no materials/textures/animations/physics asset creation. Source-unit-to-native conversion is 100 via existing metadata; do not multiply geometry by another 100.

Painter OBJ contains explicit corner normals. texture_mesh_packet.npz contains vertices, triangle loops/materials, loop UVs and root/tip/gum-margin masks. Expected normal map convention is DirectX. Painter owns separate projects: Interior first, then Human, then Predator. Family agent authored no physical Painter maps.

Root owns actual Unreal width/cavity/character fit, physical material/lighting calibration and menu flow. Independent QA owns the separate frozen-source/fresh-export gate. No further family geometry job is scheduled; DCC/GPU slot is released. No Unreal, Painter, Git, shared vampire helper, existing character or unrelated GUI project edits were made by this subtask.

## Reproducibility

Own durable scripts are under Tools/Blender/Menu3D_20261015/family_finish_*. They cover reference analysis, accepted shape generation, seam UV/export, fresh FBX nine-pose validation, approved freeze metadata/hash publication and this index. Each source/candidate preserves its executed authoring script. Earlier rejected clay and UV stages are retained for audit.

Live Blender MCP failed at localhost:9876; isolated Blender 5.1.0 CLI at D:/BLENDER_TEST/blender.exe was used with coordinated DCC slots. All authoring, solver, render and read-only Blender jobs have completed.
