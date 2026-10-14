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

## Fog brawl search and shared beacon — 2026-10-14

| Original source | Reproducible technique | MessControl implementation and status |
|---|---|---|
| [Ashif Ali / CGHOW: light streak material and ribbon](https://cghow.com/ue5-niagara-light-streak-fx-custom-material-ribbon-tutorial/amp/) | Soft edge masks and a bright narrow core keep a beam legible. | Applied to a bounded camera-facing procedural beacon with a soft radial cap. No tutorial assets or Niagara emitters copied. Three new unlit additive materials authored, compiled and saved in the live editor; gameplay inspection recorded separately. |
| [Epic: Fog Sheet and Light Beams](https://dev.epicgames.com/documentation/unreal-engine/fog-sheet-and-light-beams?application_version=4.27) | Translucent cards, controlled width/brightness and fading distinguish haze from a directed beam. | A broad offset amber card indicates an approximate search area. Marking replaces uncertainty with a tall cyan signal. The existing smoke billows remain; these cues do not add volumetric lighting or change shared artist materials. |
| [Epic: Exponential Height Fog](https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine) | Fog density and material fogging need explicit coordination for readable cues. | The final 1.5 s reduces the event's own density and billow opacity; prior level fog is restored on exit. Cue materials disable Apply Fogging. The shared beacon and final footprint/arrows disable depth testing so the tongue crest cannot hide vital cues; the approximate search glow retains geometry occlusion. |

Gameplay owns the 7 s search, 1.5 s final deadline, validated team marker and
strike direction. The red perimeter identifies the threatened tooth footprint
at crown height so its projection stays near the tooth across the tongue crest;
it does not introduce player area damage. Presentation is bounded to three
signal quads and one small direction/perimeter mesh. Sources were reviewed
before authoring. New sounds are original deterministic PCM synthesis, without
external samples. Runtime and visual acceptance evidence belongs in
`Artifacts/FogBrawlSearch_20261014`, not in these learning references.

## Tooth crown damage and repair — 2026-10-14

| Original source | Reproducible technique | MessControl implementation and status |
|---|---|---|
| [Ashif Ali / CGHOW: depth crack material](https://cghow.com/unreal-engine-parallaxocclusionmapping-depth-crack-tutorial/) | Distinguish thin dark branching cracks from the broad exposed fracture. | Adapted as small projected ribbons on the enamel with a separate opaque dark material; the crown silhouette uses geometry, without parallax or texture samples. The reviewed reference's assets are not copied. |
| [Epic: fracturing Geometry Collections](https://dev.epicgames.com/documentation/unreal-engine/fracturing-geometry-collections-user-guide?lang=en-US) | Fracture boundaries and internal faces must remain closed and visibly distinct from the original surface. | Two deterministic jagged Boolean crown cuts authored in Blender for each of eight original meshes. Enamel UVs and original pivots retained, fracture caps have a rough blue-grey material. Static variants avoid runtime Chaos fracture cost. Blender stage preview rendered and inspected; actual solo gameplay evidence is recorded in `Artifacts/ToothDamage_20261014`. |

Health selects the intact/chipped/broken meshes in both damage and repair directions.
Existing spray/watergun fluid effects communicate treatment; no new particle system
or continuous damage debris emitter is added. The original physical support body
stays stable while brush-surface geometry follows the current crown. Shader and
multiplayer performance are not inferred from mesh counts or the rendered preview.

The later irregular-fracture pass replaces the extruded saw profile with multiple
seeded convex-hull Boolean chips. Unequal hull directions, facet sizes, depths
and tilts give each source tooth a distinct silhouette. Cutters open and flare
above the crown, avoiding drilled holes and narrow vertical slots. Severe damage
reuses the initial chip volumes and adds deeper breaks. The same Epic fracture
reference above remains applicable to closed interior faces and varied cuts.
All 16 closed enamel volumes were checked in Blender; eight-tooth render sheets
and a short solo gameplay-camera check are recorded separately under
`Artifacts/ToothDamageIrregular_20261014`. No runtime random fracture is implied.

## Fog roulette — 2026-10-14

The roulette reuses the fog/beacon material techniques and reviewed Epic/CGHOW
sources above. A moving amber copy of each eligible tooth and a soft cap scan
the complete reserve; selected teeth then use red/amber to separate the current
and waiting sides. Each side has its own 1.5 s clear-fog cue and crown-height
chevrons. No new shader asset or emitter is required. Runtime coverage, gameplay
captures and inspection status are recorded in `Artifacts/FogRoulette_20261014`.

The later timing adjustment reduces post-selection preparation to 0.5 s, followed
by the retained 1.5 s final fog opening: 2 s total until impact. Audio urgency
uses that shorter preparation interval. Existing cue assets are reused. Three
targeted rule tests and one short solo F3 timing check passed; measurements are
in `Artifacts/FogStrikeDelay_20261014`.

The current post-selection delay is 3.5 s: 2 s preparation plus the retained
1.5 s final fog opening. Existing effects and audio are reused. Three targeted
timing checks and a short solo F3 check passed after integration; measurements
are in `Artifacts/FogStrikeDelay35_20261014`.
