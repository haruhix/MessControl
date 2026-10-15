# Coffee slime work state

Requested: original coffee slime concept, game-ready model and springy hop/hurt/preburst animation in MessControl's tactile cartoon style. No Unreal import or native edits by this asset pipeline.

Current stage: DCC deliverable complete and frozen for the root integration workflow. Original concept, polished geometry, UV/image materials, rig, five actions, exports and review evidence are ready.

Integration correction accepted: the first UE import correctly rejected the original meter-space FBX animation contract. Mesh reference inherited scale100, but imported animation root was scale1 and Hop lift was .34cm instead of34cm. The isolated centimeter-coordinate derivation scales rest geometry/bones and location keys by100, scene unit length .01, object/reference root scale1. Seven fresh Blender checks passed and the binary FBX unit factor is1. Root's actual UE gate then passed: 17 owned packages saved, reference root scale1, +X face and Hop33.998cm. Exact tested bytes are promoted to `v1/exports`; CanonicalPromotion.json records equal candidate/final SHA256 values. Candidate audit/image evidence remains, duplicate candidate binaries/extracted textures ignored. Source `.blend`, GLB and textures remain meters/unchanged.

Blender: 5.1.0, `D:/BLENDER_TEST/blender.exe`. Live official MCP inspected the unsaved unrelated Legogo PC_Common project. Preserve it; execute this asset in isolated background processes using the same executable.

Source: `author_coffee_slime.py`. Output revision: `v1/`. Concept: `v1/concept_v1.png`. Editable source: `v1/CoffeeSlime.blend`.

Contract: floor pivot, +X face/forward, +Z up, Blender meters, approximately 1.68 × 1.58 × 1.30 m. Single skinned mesh plus five deform bones, no root/XY motion. Native primitives/material/eyes and body squash must be replaced during later Unreal integration to avoid duplicate presentation.

Completed: graybox/fixed-view visual inspection, polished material views, portable skeletal actions, mesh/clip/static FBX + GLB, nine isolated source/fresh-import technical checks, final FBX/GLB appearance renders, motion contact sheet and 6.58-second labelled MP4. 18,236 triangles, six materials, five bones, two maximum normalized influences, no degenerate/nonmanifold geometry after temporary glTF seam welding, fixed root and correct durations. All generated image textures are packed, 512², and source has no linked libraries.

PreBurst was refined from a lateral-only first pose to full XYZ balloon inflation; intermediate evidence is labelled separately. Blender-only rendering intermediates/backups are ignored by this asset's .gitignore.

Next: root-owned saved import-path normalization, gameplay-camera and runtime validation. Actual UE import/units/hierarchy are confirmed by root; this asset pipeline itself performed no Unreal package/native edit or Editor/build/Git action. Do not infer multiplayer behavior or GPU cost from DCC evidence.
