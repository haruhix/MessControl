# v09 mouth morph buffer analysis

Date: 2026-10-16. Engine: local UE 5.8.1 source at `E:/UE/UE_5.8`.
Status: source-backed diagnosis; saved local repair passes sampled native geometry-continuity gates15-17.
Scope: menu jaw driver only. This document does not approve final art, a 1:1 match, or performance.

## Observations and controls

The first selected Human/Predator frame could show the new layout/interior with the old closed jaw pose.
CPU `GetMorphTarget` maps for all three rows matched their canonical Open-to-hat-weight mapping
(run 11 maximum error 1.31e-8; run 12 maximum error approximately 2.98e-8).
Those getters expose `MorphTargetCurves`, not the rendered morph vertex buffer:
[SkeletalMeshComponent.cpp:4011](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/SkeletalMeshComponent.cpp:4011), setters/getters 4011-4052.
PostUpdateWork, FXAA, SingleNode evaluation and direct `OverrideAnimationData` did not establish a fix.
The direct-evaluation node was removed; see `v09_mouth_morph_snapshot_rollback.json`.

| Run | Controlled difference | Observed result |
|---|---|---|
| 12 DirectViewport | Engine Shot without SHOWUI | First boundary still failed; forced Slate redraw alone does not explain it. |
| 13 ForceMorphUpdate | `r.MorphTarget.ForceUpdate=1` throughout capture | PASS for sampled first/next world-geometry boundaries only. |
| 14 StartupWarmupOnly | ForceUpdate returned to 0 at about 0.843 s, before catalog | FAIL; main-page-only warmup did not retain boundary coherence. |
| 15 LocalBuffers | Jaw keepalive/mask plus tiny Pose_000 modulation; ForceUpdate=0 | PASS for sampled first/next world-geometry boundaries and main-page masking. |
| 16 LocalFullRoute | Saved15, normal route with 7→3 offscreen return and immediate catalog/Select1 | PASS for reviewed sampled jaw/interior continuity. Physical wheel not exercised. |
| 17 LocalSlowCorners | Saved15, explicit .15 dilation for partial corner inspection | PASS for inspected Vampire→Predator and Back-closing left walls.52frames/~12s. |

Receipts are under `ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute/`,
in the respective `LowerRow_v09_InteriorSync12_DirectViewport` / `13_ForceMorphUpdate` / `14_StartupWarmupOnly` / `15_LocalBuffers` directories.
For 13-15 use `QA_NativeLook/independent_diagnostic_receipt.json`; preserve their narrow status wording.

## Source mechanism: first use of the other morph-buffer slot

1. Morph changes set `bMorphNeedsUpdate` from the previous/current dynamic snapshots:
   [SkeletalRenderGPUSkin.cpp:405](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/SkeletalRenderGPUSkin.cpp:405), comparison 849-863 and 3121-3146.
2. With double buffering, a new revision flips `CurrentBuffer`:
   [SkeletalRenderGPUSkin.cpp:305](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/SkeletalRenderGPUSkin.cpp:305), lines 305-315.
3. `GetReadingIndex` falls back to slot 0 if the selected slot has not been updated:
   [SkeletalRenderGPUSkin.cpp:319](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/SkeletalRenderGPUSkin.cpp:319), especially 333-337.
4. Foreground processing calls `UpdateMorphState` before the later upload stage:
   [SkeletalMeshUpdater.cpp:477](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/SkeletalMeshUpdater.cpp:477), foreground 477-505 versus upload 509-515;
   [SkeletalRenderGPUSkin.cpp:1147](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/SkeletalRenderGPUSkin.cpp:1147), morph-state call 1149.
5. That call selects the current readable buffer and binds its stream source:
   [GPUSkinVertexFactory.cpp:798](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/GPUSkinVertexFactory.cpp:798), lines 803-806.
6. Upload subsequently writes morph deltas and sets `bHasBeenUpdated=true`:
   [SkeletalRenderGPUSkin.cpp:1487](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/SkeletalRenderGPUSkin.cpp:1487), flag at 1590.

Inference: the first use of an unfilled second slot can bind old slot 0 for one draw before slot 1 is populated.
This matches the cold Human/Predator boundary pattern and the ForceUpdate control; actual GPU slot contents were not instrumented.
The relevant double-buffer policy is the inline GPU-skinning fallback, not a proven universal skinning-path defect:
[SkeletalRenderGPUSkin.cpp:1242](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/SkeletalRenderGPUSkin.cpp:1242), lines 1244-1251.
Skin-cache/mesh-deformer paths do not require that double-buffer policy.

## Why hiding can invalidate startup warmup

`ShouldRender` rejects actor/component hidden flags in a game world:
[SceneComponent.cpp:3515](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/SceneComponent.cpp:3515), lines 3515-3518.
`ShouldComponentAddToScene` requires renderability or explicit hidden-shadow/indirect-light exceptions:
[PrimitiveComponent.cpp:2106](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/PrimitiveComponent.cpp:2106), lines 2118-2121.
Skinned components create `MeshObject` only if that test succeeds:
[SkinnedMeshComponent.cpp:1230](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/SkinnedMeshComponent.cpp:1230), lines 1230-1231.
Consequently, AlwaysTickPoseAndRefreshBones can maintain CPU data while a genuinely hidden mouth has no GPU pool.
An unsuccessful recreation also disposes of `PreviousMeshObject` at 1292-1299 in that file.

Candidate 15 keeps jaw actors/components unhidden and maps logical Hidden to `CardHalfHeight=-1`.
The authored masked master computes `CardHalfHeight - abs(localPosition.z)` without a nonnegative clamp:
[prepare_reference_finish_materials.py:95](/E:/DEVGAME/MessControl/Tools/Unreal/Menu3D/prepare_reference_finish_materials.py:95), lines 95-102.
Negative half-height clips the jaw visually while preserving render-state eligibility; this relies on both jaw material slots using that masked master.
Interior/tongue and character visibility drivers remain outside this local policy.
The candidate record and actual before/after DSL are `v09_morph_buffer_keepalive_candidate.json` and `v09_morph_buffer_keepalive_*.dsl`.

## Local weight modulation and geometric bound

Candidate formula: `Pose_000 = canonicalHatWeight * lerp(0.99999, 1, float(GetFrameCount() % 2))`.
The other three channels, OpenAmount, layout and camera retain their normal inputs.
`GetFrameCount` returns GFrameCounter: [KismetSystemLibrary.cpp:367](/E:/UE/UE_5.8/Engine/Source/Runtime/Engine/Private/KismetSystemLibrary.cpp:367).
Source weight-comparison threshold defaults to 1e-8; root also verified live approximately 9.9999999e-9.
The float weight difference is about 1.00136e-5, above that threshold; partial Pose_000 weights receive proportionally less bias.
For each frozen source array, bound = `max(norm(Pose_000 - Basis)) * floatWeightBias * nativeUniformScale`.

| Source geometry | Maximum Pose_000 displacement before actor scale | Conservative native world bias bound |
|---|---:|---:|
| DAY01 UV_v02_GumReturns | 170.0001 cm | <0.00087 cm |
| DAY02 UV_v01 | 170.0001 cm | <0.00088 cm |
| DAY03 Final96_GumMinStretch_LocalFangRepair | 194.0032 cm | <0.00105 cm |

These are NumPy measurements of frozen `source_world_poses_cm.npz`, with native scales approximately .510365, .514498 and .537654.
They bound geometry perturbation, not image equality, flicker visibility or rendering cost.

## Lifecycle, capture and validation limits

`bHasBeenUpdated` becomes false in InitRHI (SkeletalRenderGPUSkin.cpp:249-250), true after upload (1590).
Same-render-data/feature-level recreation can reuse the pool (594-604, 631-639, 1701-1707); ordinary morph changes do not clear its flag.
New mesh/render data, RHI reinitialization, LOD-pool changes or component destruction require renewed validation.
Continuous local modulation is more resilient than a fixed startup tick count, but GFrameCounter parity is not a GPU-ready fence.
Minimized/skipped viewport draws may omit uploads or repeatedly sample one parity; minimized/reveal behavior is not yet proven.
SHOWUI uses Slate TakeScreenshot (GameViewportClient.cpp:2392-2413); direct Shot uses ReadPixels plus FlushRenderingCommands (UnrealClient.cpp:61-76, 2407-2410).
Direct Shot also failed before the local candidate, so screenshot-path differences are not the sole explanation.
Runs16/17 independently pass sampled normal-route, seven-day reappearance and slowed corner continuity.
They do not establish a continuous high-frame-rate flicker, minimized/reveal, performance or final1:1 pass.

Collision is authored NO_COLLISION for DAY01/02 (`author_functional_blocking.py:111`), DAY03 (`author_native_level.py:56`) and DAY04-07 (`extend_seven_day_blocking_scene.py:30`).
Family import changes mesh/materials and does not enable collision; ASkeletalMeshActor's class default is nevertheless PhysicsActor (Animation/SkeletalMeshActor.cpp:23-27).
Actual runtime collision is confirmed NO_COLLISION on all seven jaws in `v09_mouth_keepalive_runtime_policy.json`;
future rows use CardHalfHeight=-1 and ForceUpdate remains0. ActorHidden itself never disabled collision (Actor.cpp:5313-5319).
