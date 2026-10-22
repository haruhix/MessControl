# Reward chest presentation — current state, 2026-10-23

The existing `/Game/Gameplay/Roguelike/BP_RewardChest` is the team-stage
reward class. It keeps the original `/Game/FromBlender/SM_Chest_Bot` and
`SM_Chest_Top` geometry and UVs. A geometry/UV corner-hash comparison confirmed
that these match the source-comparison meshes; duplicate source meshes are not
integrated. The previous gold booster screenshot identified by the owner is
`Artifacts/SocialChestReview_20261005/Opening.png`.

`M_RewardChest_Gold` uses the source-compatible atlas with an editable gold
palette. `SurfaceMaterialOverride` explicitly selects that surface on the actor
and applies it in BeginPlay, avoiding inconsistent inherited component material
defaults. Original meshes, shared material masters and the historical glow
assets remain unchanged.

## Gameplay and presentation

- The existing director spawns the saved Blueprint class with its authored
  bounds. A team reward enters `Falling` immediately after stage completion;
  native landing safety checks remain active. Ordinary future-map chest timing
  and mimic disguise/capture/rescue/reset behavior are preserved.
- Fall accelerates with an alpha-squared trajectory and mild aerial stretch.
  Landing uses a bottom-centred 0.65-second spring, up to 34% Z compression and
  compensating XY scale. Gameplay collision does not deform.
- There is no landing disc, ground ring/halo or persistent team-chest highlight.
  The saved compatibility `LandingTelegraph` and `ClaimRing` subobjects stay
  hidden. Team rewards suppress the old Blueprint ambient VFX/light components.
- Touchdown has a 0.42-second airborne burst using twelve camera-facing
  instanced planes. The revised trajectory starts outside the expanded chest
  silhouette, with 14/17/20 cm initial plane sizes and a short radial/upward arc.
  The additive mask has a wider diamond/cross and authored `BurstIntensity=3`;
  depth testing remains enabled. Runtime changes only `BurstOpacity`.
- Claim anticipates, stretches, contracts/lifts and emits a short spark burst
  over 0.78 seconds. Collision releases immediately. Spawned actors are removed
  after 0.85 seconds; placed actors become hidden and remain rearmable through
  `ResetPlacedReward`, returning visible, solid and undeformed.
- No tick-spawned effect actors, floor stamp, added light or rigid-body effect
  simulation is used. Geometry count is bounded; GPU cost has not been profiled.

## Authoring and evidence

`restore_reward_chest_authored_materials.py` saves the explicit surface override
on the existing Blueprint with verified backups. The spark-only
`author_reward_chest_spark_readability.py` edits existing Custom/code and color
nodes in place, reuses UV/Opacity links, and adds/reuses only the intensity
parameter and Emissive multiply. It never deletes nodes or modifies the unused
ring material. The owner confirmed a successful authoring run and an unchanged
repeat. An earlier node-deletion attempt crashed before saving the package; its
backup/log are retained separately.

ChestSolo03 passed fifteen lifecycle assertions and shutdown/settings cleanup,
with a clean gold-material presentation and no A/B switching. The owner rejected
the weak/occluded sparks, leading to the bounded trajectory/material revision.

ChestSolo04 passed the same lifecycle assertions after Build06, but **failed
visual review**: the spark material lacked `Used with Instanced Static Meshes`,
causing default-material black squares. Its encoded media is diagnostic only
under `Artifacts/RewardChest_20261023/Diagnostic04`. It is not a final VFX result.
The material usage flag was corrected without another C++ change. ChestSolo05
then passed all fifteen assertions and verified shutdown/settings cleanup.
`MaterialAudit.json` confirms instanced-static-mesh usage, Additive blending,
depth testing, intensity 3 and no shader compilation errors. The owner reviewed
visible gold stars, no black squares and clear squash, accepting bounded
functional VFX for user review. User art approval remains pending; no AAA
quality claim is made. Final evidence comes exclusively from ChestSolo05 under
`Artifacts/RewardChest_20261023/Final`. The final saved material audit hash is
included there alongside the source-frame/clock manifests.

All solo checks disclose two ownerless review fixtures. They use native
initialization, landing, approach and `ChooseRewardPerk`; camera/player staging
is explicit. They do not demonstrate natural portion completion and do not
force director progress. Game-clock clips repeat genuine PNGs without pose
interpolation; wall-clock captures include synchronous screenshot stalls.
Final capture requested at most 24 game Hz / 12 wall Hz; fixed-timestep
quantization yielded approximately 20 native samples per game second. Encoded
30 fps repeats these actual images and is not a native 30 fps render. The
natural subsequent food portion partly obstructs the placed-reset camera;
reset visibility/collision/unit scale are proven by the native assertions.
No network, packaged-game or GPU-profile validation is claimed.

Reviewed references and technique status live in `Docs/VFXReferences.md`:
[CGHOW fireworks](https://cghow.com/ue4-niagara-firework/) for triggered short
bursts/arcs, and [Epic Niagara Quick Start](https://dev.epicgames.com/documentation/en-us/unreal-engine/quick-start-for-niagara-effects-in-unreal-engine)
for lifetime-driven shape/opacity. The implementation is analytic instanced
planes rather than a new Niagara system.
