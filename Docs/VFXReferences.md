# Reproducible VFX reference library

Research reviewed on 2026-10-13 for MessControl / UE 5.8.1. Links are learning
references; their source assets are not copied into the project. New entries
should record what can be reproduced with our tools, rather than only a reel.

## Current visual direction

Keep the walnut and shell recognizable under the actual mouth lighting and
third-person camera. Shell damage has dark irregular gaps and rough warm cut
edges. Physical chips have thickness, varied silhouettes and a quick initial
impulse followed by a slower fall. Fire uses an orange outer layer and a small
yellow accent, with an open center around the walnut. Summon urgency increases
toward the existing fuse deadline. These changes are cosmetic; damage, blast
radii, fuse clocks and targeting remain owned by gameplay.

The representative target is the existing walnut shield, approved for this
polish pass. The pass adapts its material response, contour and motion while
preserving the boss mesh and shared artist materials. Validate at normal game
distance as well as in close-up; inspect motion with short solo PIE clips.

## Guides and reproduction notes

| Original source | Useful technique | Reproduction in MessControl | Current status |
|---|---|---|---|
| [CGHOW / Ashif Ali: Depth Crack material](https://cghow.com/unreal-engine-parallaxocclusionmapping-depth-crack-tutorial/) | Separate dark cavity, lit uneven edge and progressive appearance. | Apply the material distinction to our projected five-column ribbons. A full POM version would need height/normal masks and a separate performance check; the original tutorial targets UE4. | This pass uses lit vertex colors and procedural edge grain, without POM or new textures. |
| [Epic: Fracturing Geometry Collections](https://dev.epicgames.com/documentation/unreal-engine/fracturing-geometry-collections-user-guide?lang=en-US) | Irregular cuts, seeded variation, large parts plus smaller splinters. | A future Chaos version needs a static shield proxy. Do not fracture the whole knight skeletal mesh. Current presentation can use a few bounded closed shell chips. | Cosmetic chips are implemented procedurally; Chaos integration is deferred. |
| [Ema Galic: Stylized Fireball VFX UE5](https://emagal9.artstation.com/projects/g0L2nP) | Separate charge, flight and impact; layered elongated fire shell. | Use our whole walnut, fire material, impact and ember assets. Offset sparse shell layers so they leave the center visible and taper opposite travel. | Two flight layers and an open halo are authored in this pass; existing charge/impact hooks retained. |
| [Atif: Frag Launcher Explosion breakdown](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894?page=2) | Fast initial burst, different layer lifetimes, drag and soft ring dissolution. | Reuse our fire/dust/ember family for a reduced set of readable layers. The source covers the explosion, not its preceding warning. | Timing/reference direction recorded; a full explosion-layer redesign is a future pass. |
| [Epic: Gradient Material Functions](https://dev.epicgames.com/documentation/en-us/unreal-engine/gradient-material-functions-in-unreal-engine) | Texture-free radial masks and smooth thresholds. | Our existing BlastWarning footprint can stay tied to the actual radius and fuse clock. Add urgency through pulse and sparse sparks rather than increasing its radius. | Accelerating fuse pulse and bounded sparks authored; existing warning retained. |

## Tuning and limits

Starting values are our proposals, not measurements copied from the guides:
new fracture growth 0.14 s; shell debris tail follows the existing breaking cue;
fire launch fade 0.10–0.14 s; hot trail history 0.42 s; final fuse accent window
is at most 1.5 s and 38% of the authored fuse duration.

Particle and geometry counts must remain bounded during rain and many summons.
Reserve strong flashes for major impacts; avoid changing simulation time for
multiplayer visual feedback. Check that cancelled/killed actors lose their
effects, late presentation reconstructs the current clock, and the playable
area remains readable.

## Tool provenance

Installed `create-game-assets`, `game-feel`, `unreal-niagara`, and
`shader-programming` from
[gamedev-skills/awesome-gamedev-agent-skills](https://github.com/gamedev-skills/awesome-gamedev-agent-skills)
at revision `0a70cfc64672d512c5b5b6006b5dcb5cf90dbdbd`. The discipline recipes
are adapted to Unreal; their Godot/Unity/GLSL examples are not copied as UE code.
Our existing UE C++/rendering skills remain the integration references.

Authoring status is distinct from visual acceptance. Record actual solo PIE
results and preview paths in the pass report after inspection; network checks,
cook and packaging require separately requested validation.

## Cataclysm gel, rupture and raft cues — 2026-10-14

| Original source | Reproducible technique | MessControl implementation and status |
|---|---|---|
| [Sam Perzanowski: slime meshes and vertex animation, post 5](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278) | A few volume meshes, restrained sine offsets, a seated base mask and inflated/noisy motion before the pop. | Opaque stock sphere with a wet nonmetal material, masked small WPO, native hopping/squash and low-health shiver. Lethal hits start a 0.6 s escape cue, then ten bounded mesh drops and a brief expanding splat. Saved material and motion inspected in solo gameplay: both 360 HP jellies died after nine normal 40-damage pickaxe hits and left brushable grime. Source assets are not copied. |
| [Sheila: foam contour methods](https://realtimevfx.com/t/different-methods-for-foam-around-objects-in-water/10739) | Explicit contour meshes give control over a small object's waterline foam; the original discussion concerns Unity. | Two small procedural wake strips sample the existing native liquid height. A separate deck outline communicates capacity and overload. Solo proof and contact-sheet inspection confirmed the cancellable warning, visible dip/return and RMB boarding. This is not an authored Niagara system. |
| [Epic: material inputs](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine), [Fresnel](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-fresnel-in-your-unreal-engine-materials) | WPO, restrained wet roughness and a controlled rim while preserving silhouette/bounds. | Jelly material uses WorldPosition minus ObjectPositionWS for its z-up seating mask because PreSkinnedLocalPosition is unavailable in this editor's Python API. No transparent gel layers or external textures. Saved gel identity, hit flash, inflation and rupture inspected in the actual game lighting; this is visual validation, not a frame-cost profile. |

Gameplay owns raft occupancy, the cancellable 0.7 s overload warning and the
2 s submerge/return cycle. Visual rings do not cause damage or change capacity.
The float mesh, maps and custom collision are imported into `/Game/Cataclysm/Raft`;
the gel material is in `/Game/Cataclysm/Jelly`. New editor packages are isolated
from existing artist assets. Previews and explicit capture exclusions are in
`Artifacts/CataclysmGameplay_20261014`; see `Docs/Cataclysm.md` for validation
scope. Full cleanup-to-zero, balance, network behavior and frame cost remain
unmeasured by these short local checks.

## Storm current and raft survival — 2026-10-14

| Original source | Reproducible technique | MessControl implementation and status |
|---|---|---|
| [WIZ459: procedural river shader in UE5](https://realtimevfx.com/t/wip-procedural-river-water-shader-in-ue5/21360) | Separate directional geometry waves, flowing foam and smaller normal detail. Couple flow, wave size and foam intensity; preserve the liquid's murky identity. | Macro waves and crest foam authored in copies of the coffee/cola material chains for the Cataclysm deep branch. Both compiled and saved; all 32 recorded source packages have unchanged hashes. Cold Editor build passed. Genuine solo preview frames show moving crests, foam and brown reflective liquid in arena lighting; GPU cost remains unmeasured. No creator assets copied. |
| [Epic: Water Waves asset](https://dev.epicgames.com/documentation/en-us/unreal-engine/simulating-waves-using-the-water-waves-asset-in-unreal-engine) | Direction, wavelength, amplitude and speed need coherent wave sampling; normal ripples alone cannot sell large moving water. | A bounded analytic wave field drives native CPU height/vertical speed and material parameters over a subdivided grid. Offline formula comparison covers 960 samples, max lane difference 0.000145 cm; this is not a GPU measurement. Five new packages saved under `/Game/Cataclysm/Storm`. Solo verified raft drift, brush rowing and two native hull contacts; GPU height readback and performance remain deferred. |

The authored gameplay read is a persistent downstream stream with smooth gusts,
rolling rafts, the players' existing brushes used for rowing, crest streaks and sparse collision splashes. Debris
is a real visible obstacle with an approach warning; splash cosmetics never
determine collision or damage. Geometry, debris and transient splashes must be
bounded. The three storm previews are in `Artifacts/CataclysmStorm_20261014`;
13 storm and 5 default-drain drowning checks passed after the cold build.
Actual capture/contact sheets were reviewed: plank approach, retained rail grip
and unbraced fall are readable; impact spray is subtle, and the rear crew partly
occludes hand poses. Storm clips use a temporary FOV 85 camera and approximately
6 Hz source capture. Row-vector measurement briefly pauses only event scheduling,
then restores native hazards before contact trials. See `Docs/Cataclysm.md` and
`Artifacts/CataclysmStorm_20261014/VisualReview.json` for fixtures and limits.
No network, added storm audio, exhaustive shoreline or GPU cost claims.
