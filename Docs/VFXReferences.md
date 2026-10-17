# Reproducible VFX reference library

## Player weapon swing trail — 2026-10-15

The user supplied a yellow/orange attack-streak image with a pale hot center,
torn edges and a tapered tail. It is a visual reference, not an instruction
source. Reviewed before authoring:

- [Ashif Ali / CGHOW: AnimTrail, Sword Trail, Ribbon Trail](https://cghow.com/animtrail-sword-trail-ribbon-trail-in-ue5-niagara-tutorial/):
  align the trail to the weapon, gate it to the attack and separate the hot
  center from the fading noisy silhouette.
- [Epic: Ribbon Effect](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-ribbon-effect-in-niagara-for-unreal-engine):
  continuous ordered position history and ribbon rendering for a moving source.

MessControl adapts those principles to the procedural tool animation: a local
cosmetic component samples two blade-edge points after skeletal pose updates,
builds a bounded smoothed ribbon in world space and fades its 0.14 s history.
The native implementation uses the existing procedural-mesh dependency rather
than adding a parallel animation emitter. The saved additive, two-sided unlit
material uses analytic masks and filaments, with no external textures. The
actual combo/food swing clock gates emission to the cutting phase. New swings
reset history; no ribbon joins separate attacks. Damage and input remain in
the existing gameplay paths. Implementation and genuine solo evidence are
recorded separately under `Artifacts/PlayerWeaponTrail_20261015`.

Tried and integrated: all three ordinary knife/pickaxe combo strokes and held
overhead food chopping. The bright center, orange filaments, tapered history
and short fade are visible in genuine solo PIE captures. A small camera-facing
minimum thickness keeps the descending diagonal readable when the blade plane
is edge-on. It retains the sampled blade span and does not change the swing.
The same effect was checked with the actual L_Mouth camera, post-process and
lighting. One collision-free ribbon section is bounded to 24 history samples,
630 vertices / 1,104 triangles; the 60 Hz checks peaked at 225 vertices and
cleared to zero with ticking disabled after recovery. No textures or extra
emitters are required. No GPU profiling, multiplayer or packaged-game check
was performed for this pass.

## Player tool contact feel — 2026-10-15

Reviewed the supplied 30.3 s local video, `AQNH3H4TYZ_6hsPNAYKYChcp1ym_hv2jyCZ10OORjG0zHM7Dcyd_SzZ4rdp1TySfOkRsE206F5w5D08S1M6yXGU0dztRFhnHaopGnHs.mp4`.
Its sequence demonstrates a white target flash, a sound on the accepted impact,
directional sparks, opposing recoil, a small camera shake and a brief contact
hold, followed by a death pop. The video is a visual reference; its on-screen
text is not a separate project instruction.

The implementation sharpens the existing shared hit overlay to 0.105 s, uses
the existing KnifeHit/PickaxeHit audio events on confirmed contact, redirects
ten existing spark shapes into a fan and adds a 0.045–0.055 s rendered pose
hold to ordinary knife/pickaxe swings. The attacker recovers with a small
backward body recoil; ordinary nuts retain their procedural displacement and
gain a brief inflation/shrink with the existing FoodBreak burst on a fatal hit.
The hold affects presentation only: input, actor positions, world time, damage,
attack rate and cooldown clocks continue normally. Continuous tool upgrades
retain their existing working poses. Existing additive materials and the owned
nut dust system are reused; no external source assets are copied.

The previously reviewed Atif impact layering and Epic gradient references below
remain applicable to the short core and bounded fragment tail. Solo PIE
evidence and validation limits are recorded in `Artifacts/ToothAttackFeel_20261015`.

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

2026-10-17: the V10 arena restores this same technique on its 12 new active
crowns. The reviewed Epic/CGHOW references above still apply; no new effect
technique or Chaos system was introduced. Twenty-four closed crown volumes,
progressively deeper seeded chips and projected branching cracks were authored
in a separate background Blender process. The original pivots and all three UV
channels survive. Solo PIE frames under the arena's saved lighting and native
damage/held-Spray repair checks are in `Artifacts/ToothDamageV10_20261017`.

The user's 2026-10-17 branching-crack image is the art direction for the next
pass (copied to `Artifacts/ToothBranchCracks_20261017/Reference_Target.png`).
The reviewed CGHOW separation of dark cavities from exposed fracture remains
applicable. Seeded irregular paths now form several main arms, secondary forks
and tapering hairline tips; the severe stage retains the primary cluster and
adds another. Projected ribbons are subdivided to stay above the curved enamel.
The legacy periodic sine-wave overlay in `M_ArenaTooth` is removed. No source
texture from the reference is sampled or tiled. Current crown cuts and caps are
unchanged. Close and wider solo gameplay captures, damage/repair checks and
protected-asset hashes are recorded in `Artifacts/ToothBranchCracks_20261017`.

## Fog roulette — 2026-10-14

The roulette reuses the fog/beacon material techniques and reviewed Epic/CGHOW
sources above. A moving amber copy of each eligible tooth and a soft cap scan
the complete reserve; selected teeth then use red/amber to separate the current
and waiting sides. Each side has its own 1.5 s clear-fog cue and crown-height
chevrons. No new shader asset or emitter is required. Runtime coverage, gameplay
captures and inspection status are recorded in `Artifacts/FogRoulette_20261014`.

The current post-selection delay is 3.5 s: 2 s preparation plus the retained
1.5 s final fog opening. Existing effects and audio are reused. Three targeted
timing checks and a short solo F3 check passed after integration; measurements
are in `Artifacts/FogStrikeDelay35_20261014`.

## Mouth inlet wave and throat outflow — 2026-10-14

This section records the accepted graph-v3 pass. Current graph-v4 coverage is
documented in the full-mouth section below.

| Original source | Reproducible technique | MessControl adaptation and status |
|---|---|---|
| [WIZ459: procedural river shader in UE5](https://realtimevfx.com/t/wip-procedural-river-water-shader-in-ue5/21360) | Relate directional waves, optical flow and foam intensity so the surface reads as moving water. | Reuse the reviewed direction for the Cataclysm branch: a leading wave enters through the mouth inlet, with longitudinal broken foam moving toward the placed throat. Retain the existing coffee/cola material identity. Graph v2 preview exposed steep lateral curtains. The targeted v3 repair is compiled/saved; the fresh arrival/throat/player-camera preview passed focused review (see VisualReview.json). |
| [Pete Dimitrov: UE5 vortex material, part three](https://petedimitrovart.com/p/amarantos-007-tut03/) | Radius and polar angle transform tiling noise into inward moving, rotating liquid. | Localize inward froth to the throat, using the project's existing liquid-detail texture. Adapt the coordinate technique, without the tutorial's luminous magic colors, separate refraction layer, or creator assets. Inward polar foam is compiled into both graph-v3 presets and remains enabled while the throat is wet, including Holding. Continuous outlet timing passed the solo check and the final preview was reviewed. |
| [Epic 5.8: coordinate expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinates-material-expressions-in-unreal-engine), [math expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/math-material-expressions-in-unreal-engine) | World-space projection and time-driven UV offsets; dot products select the downstream coordinate and angle/radius select the drain neighborhood. | Use the same inlet, throat, event time and front parameters as native simulation. A material mask cannot determine gameplay containment or collision. Material shader compilation, source-package preservation and a short solo preview are required; network and GPU-cost measurement remain outside this authoring pass. |

The presentation target is an entering, persistent river flowing toward a hole.
The deep Cataclysm must not reuse the ordinary top-down jet, radial impact mound,
or a globally rising bathtub plane as its arrival cue. A front crossing the
arena, faster longitudinal streaks and bounded throat turbulence must agree
with native current direction. This is an analytic game-water treatment rather
than a fluid simulation; no external texture or fluid cache is required.

The material migration saved exactly four owned liquid packages under
`/Game/Cataclysm/Storm`; the spray material was unchanged. All 32 recorded
source liquid packages retained their SHA256, and the editor had no dirty
packages before or after authoring. `Artifacts/CataclysmInflow_20261014/MaterialReport.json`
records graph version 3 and the native math hash. These authoring checks do not
prove runtime coverage, GPU height agreement, multiplayer behavior or frame cost.
Source review for that pass confirmed that native `StormHeightAt` and the generated
WPO use the same wet mask, micro/macro waves, entering crest and continuous
drain dip. Surface normals take a 2 cm central difference of the whole height
field, then add the retained texture normal detail. A separate 40 cm material
border fade softens the arena edge; it does not change native containment.
The historical v2 material report records the header hash at authoring time;
a subsequent C++ cast correction changed that hash without changing its formula.
The v3 report was authored after the native arrival-field correction.
The first gameplay-camera review found a concrete v2
failure: applying the whole wet/domain mask to base-depth lowering created
steep side curtains that refracted the green tongue zones. V3 separates arrival
from domain clipping, so only the advancing/drying front changes base depth.
In v3, lateral/outlet taper controlled opacity and wave/drain displacement.
Native public height returns dry height at wetness <=0.001 to keep rafts off
invisible water; fully masked GPU vertices retain the smooth base plane.
Height parity applies to visible wet water, not those invisible vertices.
The corrected v3 preview passed the focused gameplay-camera review: the incoming
front, continuous throat outflow and native player-camera wave motion are readable,
and the reviewed lateral edges no longer show v2 curtains. Evidence is in
`Artifacts/CataclysmInflow_20261014/VisualReview.json` and the 11.67-second
`inflow_preview.mp4`. Ten native solo checks passed over arrival plus eight
seconds of Holding. Final natural recession, full balance, multiplayer and GPU
validation remain deferred; the v2 preview is not an accepted visual result.

## Full mouth water and gel-pen trail — 2026-10-15

Reuses the original WIZ459 river, Pete Dimitrov polar drain and Sam Perzanowski
gel references reviewed above. Water graph v4 removes the lateral/outlet taper:
the whole mouth rectangle receives the passing front, while the drain radius
only controls local depression and turbulence. The descending lip spans the
mouth width using the actual source-mesh bounds. Both isolated storm presets
compiled and saved; source liquid packages remained unchanged. Twelve focused
solo checks passed, including wet side lanes and a 3811.07 cm outlet lip matching
the mouth width. Arrival, held outlet and native hero-camera captures were
reviewed in `Artifacts/CataclysmFullWidth_20261015`; GPU readback/cost and network
validation remain deferred.

The gel-pen trail uses a separate masked, lit wet-film material driven solely
by a replicated 64×64 additive coverage mask. It has a small mask-derived
meniscus, restrained reflected glint and no extra transparent layer or external
textures. The saved material is `/Game/Cataclysm/Jelly/M_CataclysmPaintTrail`.
Wandering gel deposits connected strokes; its balloon rupture adds a wide splat
and bounded radial drops. Cold Editor integration passed, as did two focused
native tests and nine solo checks. The reviewed clips show connected green
paths, inflation/pop, a persistent splat and local removal by the ordinary brush.
The mask edges remain angular and the coating fairly flat; existing white hit
flashes partly hide the gel silhouette. This center-arena run coated no teeth,
so nearby tooth presentation and full encounter cleanup remain separate checks.
Evidence and genuine clips are in `Artifacts/JellyPaint_20261015`.

## Original coffee slime form and motion — 2026-10-15

| Original source | Applicable technique | MessControl authoring status |
|---|---|---|
| [Ryan King: Cute Blob Character](https://ryanking.artstation.com/projects/ELzlg8), [creator's animation-process post](https://blenderartists.org/t/cute-blob-character-animation-process/1247461) | Large readable facial forms, a simple gel silhouette and controlled wet highlights. | Researched before generation. The original coffee slime uses espresso gel, ivory eyes/mauve lids matching the project's Tooth style, a scalloped foot and milky crema curl. No creator mesh, green palette, arms or reference textures copied. Generated concept and authored Blender source are in `ArtSource/CoffeeSlime`. |
| [Blender Studio: Simple Bouncing Ball, Part 1](https://studio.blender.org/training/animation-fundamentals/5d70d714521d407842bf90fd/), [Into Animation: squash and stretch rigging](https://www.intoanimation.com/articles/squash-and-stretch---blender-rigging-tutorial) | Anticipation, spacing, aerial stretch, impact squash and delayed settling communicate elastic motion. | Applied in five bone-keyed actions: Idle, Hop, Hurt, PreBurst and Burst. The hop has separate compression, flight and landing poses; foam settles with delay. PreBurst grows in all three axes and Burst begins from the matching swollen pose. No soft-body/fluid cache or Python-only animation handler is required. |
| [Blender: armature skinning](https://docs.blender.org/manual/en/latest/animation/armatures/skinning/introduction.html), [armature modifier](https://docs.blender.org/manual/en/latest/modeling/modifiers/deform/armature.html) | Bound local deformations with stable hierarchy and normalized vertex weights. | A single 18,236-triangle mesh uses five bones and at most two normalized influences. UV-mapped original 512² color/roughness images preserve opaque coffee and matte crema materials. Fresh Blender FBX/GLB imports pass technical checks; static/motion renders were inspected. Engine import, gameplay-camera acceptance and performance evidence are recorded separately by the integration workflow. |

The research informs reusable form/material/motion techniques rather than a copied
character. The asset's source, contract, fixed views and pose sheets live in
`ArtSource/CoffeeSlime/v1`; its existing native paint and rupture effects remain
separate gameplay presentation. Source `.blend`/GLB use meters; canonical UE FBX
bakes geometry, rest bones and translation keys into centimeters with unit root
scale. The first meter-space FBX contract failed the real engine hop gate; the
corrected centimeter set passed with five bones, root scale1 and 33.998cm hop.
Exact tested bytes are promoted under `v1/exports`. Blender appearance evidence
does not establish Unreal material parity, network behavior or runtime frame cost;
gameplay-camera and runtime acceptance remain separate integration checks.

## Coffee slime v2 and coffee paint — 2026-10-15

The user rejected v1's busy human-like face and requested the simplicity of an
elemental blob. The lead inspected the [Hydro reference on HoYoWiki](https://wiki.hoyolab.com/pc/genshin/entry/2132?lang=ja-jp)
and [Geo reference](https://wiki.hoyolab.com/pc/genshin/entry/2137?lang=ja-jp):
one broad rounded volume, flat eye marks, and a wide grounded squash. These are
style and motion references; no source pixels, models, emblems or texture assets
are reused. The original warm coffee design and small flush crema crescent are
recorded in `ArtSource/CoffeeSlime/v2/concept_v2.png`. The lead reviews each
delegated concept/geometry/motion checkpoint in `v2/LeadReview.md`.

For the coffee trails and rupture drops, reuse [Sam Perzanowski's original gel
breakdown, post 5](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278)
and [Epic's material inputs documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine),
reopened before authoring. Adapt the restrained meniscus and seated bounded
vertex motion; the project uses opaque/masked surfaces rather than additional
transparent layers. `Tools/Unreal/author_coffee_slime_surfaces_v2.py` authors warm
coffee pigment, a lighter thin edge, moderate wet reflections and a small color
floor under the mouth's colored lighting. `CoffeeTheme=1` makes the rupture gel
use its authored coffee palette despite the legacy runtime green tint overrides.
The exact bilinear 64x64 cleaning mask, clip threshold .25, coverage and brush
logic remain intact. The first surface revision read too matte in gameplay;
revision2 strengthens the mask-derived shading meniscus and a bounded warm
view-dependent sheen. Two owned materials compiled and saved after disk backups.
The lead opened final actual hero-camera, trail, rupture and brush frames; all
15 focused solo checks passed. The trail retains the native angular mask edges.
No added external textures, network tests or GPU-cost measurement are claimed.

The v2 body texture initially stored intended linear values as raw sRGB pixels,
darkening the sampled material. The source now encodes the PNG correctly and
records texture parity; the lead reviewed the repaired hero and fresh GLB poses.
The final mesh has12,458triangles, five bones, three material slots and five
baked actions. Eight source/FBX/GLB technical audits and the raw FBX unit contract
passed. The engine migration also checks updated rest transforms and all five
clips, including a33.998cmHop. `Artifacts/CoffeeSlimeV2_20261015` contains final
solo reports, sampled review frames and short genuine gameplay previews;
`v2/review/CoffeeSlimeV2_Motion.mp4` shows the authored motion in studio lighting.

Follow-up surface-fit correction: user screenshots exposed straight triangle
cutouts missed in the first gameplay review. The independent coarse paint grid
intersected the deforming tongue; increasing mask resolution would not address
that geometric cause. The repair clips native tongue faces to each tile, carries
barycentric bindings through clipping and follows each tongue revision with a
uniform 0.6 cm upward offset. It reuses the existing masked material and cleaning
UVs, with no new texture, transparency layer or collision. The focused probe is
`Tools/Unreal/probe_coffee_surface_fit_solo.py`; baseline, fixed validation and
gameplay previews are recorded separately from the original v2 acceptance.
The authorized Editor build and 18 focused solo checks passed: 4663 wet samples
showed no intersection, with a consistent 0.6 cm surface offset. The lead opened
the gameplay, trail, post-rupture and brush stills. The pressure dent under the
player remains; Walking mode and actual blocking floor hits confirm support in
both fixed snapshots. Videos repeat genuine 12 Hz captures at 30 fps, without
generated motion; four startup editor-window frames are explicitly excluded.

## Knight head split into Wizard — 2026-10-17

Reuses the previously reviewed Epic fracture-geometry and Atif Ali fast-burst /
controlled-tail references from the walnut effects work above. The visible
technique uses two existing `SM_Walnut_HalfShell` instances opening outward,
then drifting away around a bounded purple arcane burst and ring. Existing
walnut and arcane assets are reused; no Chaos fracture or skeletal mesh asset
change is introduced. The halves have no collision or damage, and cue age is
reconstructed from the replicated server clock with existing owner cleanup.

The 2.4-second phase transition was exercised in solo PIE under `L_Mouth`
lighting. The lead inspected the actual shell separation and Wizard emergence
frames in `Artifacts/NutSequentialPhases_20261017/ContactSheet.jpg`; the genuine
motion preview is `preview.mp4` beside it. The probe observed exactly two shell
instances. No GPU cost or multiplayer measurements are claimed. Implementation,
fixture details and validation are recorded in
`Docs/NutSequentialPhases_20261017.md`.

## Wizard teleport — 2026-10-17

Reviewed [Ashif Ali / CGHOW: Teleportation effect in Niagara](https://cghow.com/unreal-engine-teleportation-effect-in-niagara-tutorial/)
before authoring. Adapt the charge/disappear/arrival timing, surrounding rings
and rising sparks to the existing bounded `MCNutCombatEffect` meshes and arcane
burst. Three counter-rotating portal bands and sixteen rising motes at each
endpoint frame the wizard; the character shrinks away and returns around the
native relocation time. Existing materials and Niagara burst assets are reused;
no reference asset or texture is copied. Gameplay-camera validation is recorded
separately in `Artifacts/NutWizardUpdate_20261017`.
