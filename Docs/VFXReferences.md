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

## Reference plaque film — 2026-10-17

The user's supplied cream/ochre/brown spotted wash is imported unchanged as
`/Game/Gameplay/Care/Textures/T_PlaqueReference`. The arena coating uses a broad,
irregular envelope above the gum and a 180 cm pigment tile in rest coordinates
multiplied by the crown's scale. This retains grain density when the tooth is
resized and keeps the pigment attached during tooth motion. Thin geometry,
restrained normal slopes and matte roughness replace the dark raised blobs.

| Original source | Adaptation | Status |
|---|---|---|
| [Epic: Texturing / WorldAlignedTexture](https://dev.epicgames.com/documentation/en-us/unreal-engine/texturing?application_version=4.27) | Texture density specified in world units independently of object dimensions. Use scaled rest coordinates here so the moving tooth does not swim through a world-anchored texture. | Applied to the arena plaque pigment; original reference image supplied by the user. |
| [World Aligned Texture & How To Use It, original community tutorial](https://dev.epicgames.com/community/learning/tutorials/34ER/unreal-engine-world-aligned-texture-how-to-use-it) | Separate texture tiling from an object's UV stretch and coverage envelope. | Reviewed for this pass. Existing triplanar projection reused; no extra particle systems. |

The cleaning mask remains local to the brush stroke. Crown-size normalization
keeps mask texels reachable by the ordinary brush; the visible local pawn must
present its bristles before erasing dirt. Solo gameplay measurements and motion
captures are stored in `Artifacts/GrimeReference_20261017`.

The later natural-plaque pass increases the pigment scale from 180 to 420 cm.
Three randomized texture patches meet through continuous triangular blending;
each tooth seed changes their offsets, rotations and scales. A restrained rest-space
warp removes the remaining alignment. Broad, medium and fine erosion soften the
coverage window; lit translucency blends pale washes into the enamel without a
temporal stipple border. The geometry, brush contact and cleaning mask remain
those validated in the earlier pass. New evidence is in `Artifacts/GrimeNatural_20261017`.

Artist settings now live in `/Game/Gameplay/Care/MI_ArenaToothGrime`, with
`M_ArenaToothRelief` as its parent. Pigment size, pattern seed, warp, opacity,
edge softness/erosion, pale-wash density, tint and roughness are instance
parameters. Each tooth's runtime MID inherits the saved instance and continues
to own its cleaning mask and automatic state. The creation/update script is
`Tools/Unreal/create_plaque_material_instance.py`; run it after rebuilding the
parent with earlier material-authoring scripts. Instance-specific overrides
are preserved on reapplication. Evidence is in `Artifacts/GrimeInstance_20261017`.

The normal/opacity follow-up uses [Epic: Bump Mapping Without Tangent Space](https://dev.epicgames.com/documentation/unreal-engine/bump-mapping-without-tangent-space-in-unreal-engine?lang=en-US)
for a bounded world-space surface gradient. Reference-pigment luminance supplies
the main bump, so its size, rotations, blending and per-tooth seed agree with
the color. The existing normal/height textures provide independently adjustable
micro detail. Coverage and the cleaning-volume gradient are no longer differentiated
as height: they must not imprint their interpolation boundaries into lighting.
`GrimeNormalIntensity=0` completely disables both bump and micro normal.

`02 Opacity Mask` exposes threshold, softness, contrast, inversion and strength,
alongside overall opacity. By default, the artist mask comes from the reference
pigment. An optional `GrimeOpacityMask` texture uses the same stochastic projection
when `GrimeMaskTextureStrength` is raised. This artist mask multiplies the native
brush wipe; it does not change gameplay progress. [Epic: Depth Material Expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/depth-material-expressions-in-unreal-engine)
was also reviewed while diagnosing intersections. Depth fading alone cannot
correct a mesh spanning enamel creases, so the visual film instead subdivides
the original enamel triangles and uses their interpolated smooth normals. The
original projected grid remains the gameplay/contact sampler. Existing arena
meshes already allow CPU access; their packages need no edits. A 0.55 cm normal
offset seats the film, with optional bounded instance adjustment. Author with
`Tools/Unreal/refine_plaque_surface.py` after the earlier instance script; user
instance values are preserved. Verification is recorded in
`Artifacts/GrimeSurface_20261017`.

| Original source | Adaptation | Status |
|---|---|---|
| [Thomas Deliot and Eric Heitz: Procedural Stochastic Textures by Tiling and Blending](https://eheitzresearch.wordpress.com/738-2/) | Stochastic neighboring patches with continuous blends. This pass uses a simpler triangular blend of the supplied pigment, without histogram precomputation or copying the authors' demo. | Applied to the rest-space pigment lookup with explicit sample gradients. |
| [tharle VFX: Random Tile Material](https://www.tharlevfx.com/random-tile-material) | Blend separately offset and rotated versions of the same texture to disguise repetition. | Applied per tooth seed and triangular cell, with independent scale variation. |

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
## Progressive coffee deposition and wet film v3 — 2026-10-17

Before authoring, reopened [Sam Perzanowski's original gel breakdown, post 5](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278)
and [Epic's UE 5.8 material inputs](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine).
Adapt the separation between a stable footprint, restrained surface normal and
readable liquid response. No creator texture, mesh or new particle pack is used.

`Tools/Unreal/author_coffee_stain_film_v3.py` authors accepted
`coffee_stain_film_v3:5` on `/Game/Cataclysm/Jelly/M_CataclysmPaintTrail`: calm
espresso with +/-3% fine pigment variation, a .35cm mask-derived shading
meniscus, very low fine-normal strength, roughness .085 and zero emissive.
The underlying bilinear 64x64 cleaning mask and clip .25 remain. The appearance
uses five WipeMask reads; the applied temporal-edge addition below also samples
the Engine noise texture. There is no translucent layer, UV warp or geometric
displacement. Native tongue-face bindings and the uniform 0.6cm offset remain.
The author defaults to a read-only plan, checks ownership/idle clean editor
state, and backs up the saved package before explicitly applied compile/save.

Wave residue now uses the same film path: up to eight supported footprints are
selected before arrival and gain only the texels reached by the native swept
water front. Teeth gain their reached coating during the wave too. Previously
emitted coverage is recorded separately from the live cleaning mask so a passed
wave does not refill brush strokes. This replaces the old end-of-wave deposit.

The accepted 2026-10-18 central-placement follow-up keeps that moving-front
behavior. Future meal/wave puddles use the shared safe central tongue envelope,
full footprint and brush-approach clearance; painted coverage excludes the
outer work-area strips. Actual first-meal run
`E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Cleanup8/Runs/20261018T083313Z_c312dd32/Report.json`
records nine central stains and 6761 wet texels with zero outside in both
placement snapshots. Real input delivered ten native fragments and erased both
floor kinds before automatic `FIRST_MEAL -> MEAL_REST`. The twelve teeth remain
visibly dirty and overall cleanliness is 41%; this is floor-gate evidence.
Accepted placement/brush/completion videos are in
`E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/CentralDirt/Cleanup/`.
See `Docs/CleanupFeedback8_20261018.md` for fixtures and the native regression
boundary. No current performance, network or packaged-parity claim follows.

The later spacing follow-up expands the middle work width from 58% to 72%
while retaining full footprint plus 65 cm brush/static clearance. It replaces
nearest-centre sorted wave-grid origins and ordinary triangular centre bias
with continuous seeded random XY and distance rejection. The actual moving
wet front, coating, sizes and budgets remain unchanged. Two short native
layout captures (`DirtScatter/Runs/20261018T085450Z_74f4d2b0` and
`20261018T085550Z_83268ffb`) each show nine stains, with 7139/7382 wet texels,
zero outside and no ordinary static footprint blocks. Lead review accepted
the wider irregular layout. These are layout-only subsets, not another full
cleanup test, equal-camera A/B or proof of statistical randomness/performance.

The authorized Editor build and solo runtime validation completed. The final
plain-PBR rev5 material compiled and was saved; the lead inspected the actual
gameplay-lighting comparison frames and accepted its direction with default
roughness .085. `Saved/CoffeeFilmPBR/Capture/Runs/20261017T021443Z_paint_13451578`
passed all six native functional gates, including progressively growing tongue
coverage, tooth grime before wave completion and genuine held-brush removal.
The comparison restored its transient MID parameters before real brushing.
`Tools/Unreal/probe_coffee_wave_stains_solo.py` records moving-front, tooth and
held-brush captures. Its disclosed PIE-only fixture clears the native coffee
amount of three existing teeth before F3 so arrival can be seen; it never
assigns masks or calls care/deposit helpers during the wave. Full encounter
cleanup and network tests remain deferred. The bounded same-camera GPU
comparison completed as described below.

Coffee film revision3 research follow-up: [Adam Funari's original wetness/puddle
shader tutorial](https://axton.artstation.com/projects/nYeAJe) varies the material
response from its wetness mask rather than relying on one roughness value.
[Alexander Zawadski's original Unreal water breakdown, Part 4: Glints](https://alexanderzawadski.artstation.com/blog/boaaN/unreal-water-material-breakdown)
describes normal-driven stylized reflections when roughness/specular alone do
not give a readable result, crediting Simon Trümpler's RiME talk. Reopened these
sources before this pass. Revision3 tried a bounded view/normal-dependent reflection lobe
with irregular pigment breakup, two-scale roasted-coffee color, a thin
mask-derived meniscus and rare procedural crema rings. This uses five existing
mask texture reads and arithmetic noise; no new texture, translucent layer,
water-wave displacement, caustics, refraction or Niagara system is introduced.

Revision1 was rejected for beige paste-like reflections and parallel sine bands;
revision2 was rejected after the real mouth-lighting capture showed a mostly
uniform matte brown fill. Revision3 was applied and compiled, but its actual
close-up (`20261017T020451Z_paint_9e53f8d4/frames/00176.png`) was rejected: broad
beige pigment/reflection islands and stronger normals suggested raised pudding.
The tooth review framing passed lead inspection separately.

Revision4 also compiled and passed the functional probe, but lead review of
`20261017T020941Z_paint_e2f2f823/frames/00176.png` rejected its beige parallel
scratch-like glints. The same actual frame already contained a real engine PBR
reflection, so the procedural reflection/streak field is removed entirely in
the accepted `coffee_stain_film_v3:5`. It uses plain masked DefaultLit shading,
a calm espresso base with only +/-3% fine pigment variation, .35cm meniscus
shading, very low fine-normal strength and parameterized .085 roughness. It has
exactly zero emissive, five mask reads and one four-corner arithmetic-noise
evaluation; no reflection power, new texture, transparency layer or global
arena-lighting/reflection changes. Revision3's report incorrectly said two
reflection powers; it actually used three.

`Tools/Unreal/compare_coffee_film_pbr_solo.py` prepares three roughness candidates
(.12, .08 and .05) on the same real wave patch in one PIE, each with three static
seconds and a small camera orbit. Only transient MID values/camera change;
native mask, clocks, AI and normal wave/brush gates remain active. Values are
restored before the ordinary static close-up, real brush input and after-wipe
review. Exact cleaning coverage and conforming geometry remain unchanged.
Rev5 compile/application, candidate comparison, gameplay-camera review and all
six native functional gates completed in the run above. The ordinary-puddle
parity author (`Tools/Unreal/author_ordinary_coffee_film.py`,
`ordinary_coffee_film_v1:2`) was applied and compiled. It replaces only owned
appearance nodes, retains the
original seeded opacity/WPO graph and every MI override array, and calibrates
legacy palette/roughness/specular inputs to the same rev5 response.
`Saved/BrushSelfcare/Capture/Runs/20261017T023710Z_jellies_dac1cf87` passed all
seven native checks: self-care completes, exits automatically and reaches real
environmental cleaning with the brush continuously selected and held input.
Lead visual acceptance recorded here covers the rev5 wave/slime appearance and
the edge A/B below; the ordinary probe establishes its native self-care/brush
flow. No multiplayer measurement is claimed.

### Coffee edge follow-up

The paint mesh follows/clips the actual tongue triangles; its liquid outline
comes from the 64x64 bilinear mask and clip .25. The separate 20x20 grid is for
ordinary puddle geometry. Reviewed [Sigg and Hadwiger's original GPU Gems
chapter on cubic filtering](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-20-fast-third-order-texture-filtering)
and [Epic's utility-material expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/utility-material-expressions-in-unreal-engine).
The first pass tried interpolating cardinal cubic-Hermite weights between the
same texels, but actual A/B frames00156/00206 and after-brush00386 in
`20261017T022734Z_paint_a7c1d8ef` showed more obvious flat zigzags. It failed
visual review and was disabled. No edge improvement is claimed from that pass.
Non-interpolating B-spline blur remains deferred because it changes centre
values and can hide weak remaining dirt.

[Epic's stylized-material breakdown](https://dev.epicgames.com/documentation/unreal-engine/stylized-rendering-materials?application_version=4.27)
uses DitherTemporalAA for a soft masked edge; [Epic's UE5 anti-aliasing
guidance](https://dev.epicgames.com/documentation/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine?lang=en-US)
requires checking temporal methods during movement. Applied and compiled
`coffee_edge_reconstruction_v1:2` removes the two rejected cardinal nodes and
restores original UV. It uses the
existing Engine DitherTemporalAA function on a narrow rendered-opacity band
around coverage .25: target 1.5 screen pixels with a 2–4cm world-width limit and
an alpha half-width cap .24. A final gate keeps exact-zero coverage invisible
and the core opaque; native mask bytes, Finish, brush logic and accepted rev5
appearance/geometry remain unchanged. No translucent layer or emissive effect
is added. Five WipeMask reads remain, but the native function contains an
additional noise-texture sample and temporal arithmetic; total shader/GPU cost
requires measurement. The Engine asset references
`Good64x64TilingNoiseHighFreq`. TAA availability alone is not visual acceptance.

`compare_coffee_edges_solo.py` completed the real wave/brush A/B in
`Saved/CoffeeStainEdges/Capture/Runs/20261017T024049Z_paint_175f61eb`. The lead
inspected hard frame00156, soft frame00206 and after-brush frame00386, and
accepted edge2 for the integrated preview. Narrow soft coverage is visible;
slight native temporal grain remains, and the 64x64 source still limits the
contour shape. The lead decoded and inspected the genuine MP4 previews before
and after brushing, including micro-orbit positions .15/.5/.85, and accepted
the narrow softening for the integrated preview. Perfect contour smoothing
or flicker-free motion is not claimed.

All six native coffee gates passed. Both hard/soft pairs retained identical
native-mask hashes, separately before and after genuine brushing; parameter
changes did not repaint zeroed cells. Actual brush input removed native mask
coverage between those checkpoints. Each candidate includes three static
seconds and a small orbit, parameters restored, and active TAA was recorded.

The clean A/B/A run `20261017T030842Z_paint_31996456` completed with six checks
passed in `Saved/CoffeeFilmPerformance/Report_performance.json`, including
unchanged masks, a stationary actual player camera, no video sampling during
timing, and independent readback confirming settings restored. FFmpeg, builds
and DCC work were absent at startup; the real PIE window was selected before
timing. This supersedes the earlier encoding-overlapped attempt.

Whole Editor RHI-frame GPU medians on the RTX 3080 were A1 original 3.66545ms,
B current 3.62695ms and A2 original 3.63825ms. Relative to mean baseline 3.65185ms,
B differed by -0.02490ms (-0.682%); the A/A repeat drift was 0.02720ms (0.745%).
That delta is smaller than baseline drift: no measurable regression was found
in this one paused-world, same-camera scene. This does not establish a speedup,
isolated material cost or a global game/hardware performance budget.
Texture-read counts and the fixed-rate preview window are not GPU timing
evidence.

### Coffee mask density experiment — 2026-10-17

Reused the accepted rev5 film / Edge2 references above. Tested genuine
independently rasterized 64x64, 128x128 and 256x256 coverage masks, with the
same continuous UV deposit/brush recipe, physical 1200cm tile, native conforming
tongue mesh, bilinear linear-data textures, lighting and camera. This is a
mask-density experiment, not an art-texture resolution or geometry change.
The transient material varies its normal neighbour step as 1/N and keeps the
same five explicit WipeMask reads plus the native DitherTemporalAA noise read.
No production material, map or native resolution was changed or saved.

Data and standalone CPU results: `Saved/CoffeeMaskResolution/Data/cases.json`
and `CpuAlgorithmReport.json`. The original `MCCoffeeWipe.cpp` was compiled
unchanged as a separate MSVC x64 /O2 translation unit with a small standard-C++
UE math/container shim; only the copied header's Size varied. Allocation,
reset, copies and I/O were outside timing. There were 24 warmups and 240
measured calls per operation, on Ryzen 9 5950X. This does not measure UE Core,
allocations, live brush contact/completion, the optional CoverageAllowed
callback, RHI uploads or networking; warm-cache sequential runs are not a
whole-game CPU benchmark.

| Mask | Physical cell | Stroke median / p95 | Deposit median / p95 | Raw BGRA texture / full upload |
| --- | --- | --- | --- | --- |
| 64x64 | 18.750cm | 23.4 / 30.4us | 5.7 / 5.7us | 16KiB |
| 128x128 | 9.375cm | 90.6 / 139.9us | 20.9 / 21.1us | 64KiB |
| 256x256 | 4.6875cm | 345.0 / 378.0us | 76.3 / 81.0us | 256KiB |

Two native byte arrays would contain 8/32/128KiB raw state respectively;
these are not measured network-packet sizes or bandwidth. A full BGRA upload
at the maximum 30Hz would carry 0.469/1.875/7.5MiB/s per actor CPU-to-GPU,
before overhead. Those are calculated payload budgets, not observed transfer
rates or measured total VRAM. The 256 case has four times the cells/storage
and approximately 3.81 times the measured Stroke cost of 128.

Visual run `20261017T045636Z_paint_e7c3a0df` passed all eight native-fixture,
mask-invariance, package and restoration checks. The lead reviewed real PNGs,
same-camera comparison panels, the 1:1-pixel edge crop, and decoded MP4 orbit
frames in `Artifacts/CoffeeMaskResolution_20261017/Review_045636Z`. Native 64
contour steps are visibly larger; 128 reduces them substantially; 256 refines
them further. The same local brush crossing is shown, without scaling the
mesh or upsampling64. Temporal grain remains; this does not prove flicker-free
motion. The first run 045205Z was visually rejected because the full-tile fit
placed the camera through the mouth ceiling; it is not a comparison result.

Separate GPU run `20261017T045931Z_paint_f88f3f10` passed all ten checks,
including stationary actual camera, no video sampling, unchanged readable
native 64 masks and independent settings restoration. FFmpeg, compiler and DCC
work had ended before timing. The world was paused; each candidate had 3s
warmup then 6s CSV capture, with sequence 64/128/64/256/64. Whole Editor RHI-frame
GPU medians were 3.24170/3.20970/3.22040/3.21425/3.28285ms, on RTX 3080,
D3D12/SM5, driver 581.57, UE 5.8.1, actual viewport 1280x722.

128 differed from its bracketed 64 mean by -0.02135ms against 64 repeat drift
0.02130ms; 256 differed by -0.037375ms against drift 0.06245ms. These differences
do not demonstrate a GPU speedup. No measurable regression was found in this
one stationary scene. All six textures were resident together, so this is not
an isolated total-VRAM comparison. An independent agent recomputed all raw CSV
medians/p95 and verified source/package/report hashes and restoration.

The raw reports retain inherited `baseline`/`purpose` strings from the old
helper; their mask-specific scope, sequence and measurements are correct.
Those two metadata fields were corrected in the reusable probe after the run;
recorded executed-source hashes and immutable raw reports remain unchanged.
`TestResults.json` in the reviewed artifact directory records this correction.

Recommended next integration candidate: 128 for large paint tiles. Production
still uses 64. Higher-resolution native cleaning, contact/completion, uploads
and multiplayer/replication remain unintegrated and unvalidated. The protected
LiquidDepositMask ledger could not be read through the live API; readable
WipeMask hashes and reported care state were checked instead. Unsaved user
map edits were preserved. No Editor build, cook, package or network test ran.

### Coffee architecture A/B — 2026-10-17

Practical reference: Epic's [material layering overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/layering-materials-in-unreal-engine),
[Material Layers](https://dev.epicgames.com/documentation/unreal-engine/using-material-layers-in-unreal-engine),
and [From Material Expression shading model](https://dev.epicgames.com/documentation/unreal-engine/from-material-expression-shading-model-in-unreal-engine?lang=en-US).
Adapt the material-attribute blending technique to the existing accepted
espresso film/Edge2 look. A literal Material Layers asset is not required by
this prototype. No new external textures, Niagara assets or project packages
were authored; both candidates clone existing source graphs into Transient.

A renders the coffee as a separate conforming surface. B blends the same
coverage, coffee PBR inputs and normal detail into the original receiving
material. Original graph connections, parameters and cutouts are retained.
The tongue's SubsurfaceProfile shading requires a supported per-pixel
MakeMaterialAttributes/ShadingModel branch: clean tissue keeps its original
model, coffee uses DefaultLit. B has continuous narrow alpha coverage; A uses
the accepted masked temporal-dither edge, so this is explicitly a comparison
of practical renderer candidates, not identical shader instructions.

The accepted tongue preview is
`Saved/CoffeeArchitectureAB/Preview/Fixture/Runs/20261017T054309Z_paint_3836f7f9/Report.json`.
Both variants use the same independently rasterized 128 mask textures on one
actual native 1200cm slime paint tile. Production masks remain 64. The lead
reviewed actual engine PNGs, matched panels and decoded orbit/deformation
videos in `Artifacts/CoffeeArchitectureAB_20261017/Tongue_054309Z`.
B has a smoother narrow edge; the coffee identity remains close. Ten native
StartYawn deformation checkpoints retained identical game time and geometry
hashes between A/B. They are disclosed paused-time paired captures, not an
ordinary continuous gameplay recording. Eight-Hz captures cannot establish
flicker-free motion. Native actors and their geometry updates remain enabled
in both variants; their removal or CPU savings was not tested.

The first tongue GPU run was inconclusive due to baseline drift. The preferred
repeat is `Saved/CoffeeArchitectureAB/Performance/Fixture/Runs/20261017T055553Z_paint_df90e91f/Report.json`:
12s initial preheat, then 3s warmup and 6s CSV per A/B/A window, no video or
FFmpeg during timing. Whole Editor RHI-frame GPU medians were
3.43975 / 3.44930 / 3.45230ms on RTX3080, D3D12/SM5, UE5.8.1. B differs from
the bracketed A mean by +0.003275ms (+0.0950%); A repeat drift is 0.01255ms
(0.3642%). No GPU winner is established. This one paused scene does not measure
an isolated material pass, shipping-game FPS, CPU mesh work or multiplayer.
An independent agent recomputed all raw CSV statistics and hashes. The helper
file changed in tooth-only code during preparation: retain the initial and
build-time hashes. `Saved/CoffeeArchitectureAB/TongueRuntimeCodeAudit_055553.json`
verifies the actual retained bytecode of all14 executed tongue-path functions
against the final helper, plus shared constants; it does not claim whole-file
SHA identity.

Separate CPU kernel A/B evidence is
`Saved/CoffeeWipeBoundedAB/Reports/Report.json`. A compiles unchanged native
MCCoffeeWipe.cpp as its own MSVC /O2 translation unit with a small stdlib UE
shim; B restricts Stroke iteration to the capsule's bounding rectangle. Each
resolution passes708 correctness steps covering identical bytes, fractional
float bits, Changed and aggregates. Each fixture has240 paired AB/BA timings;
reset/copy/I/O are excluded. Short diagonal Stroke median A→B is
23.6→0.8us at64, 95.3→2.7us at128, and417.25→11.2us at256. Long diagonals gain
only about1.13–1.19x; broad brushes are near equal (B up to2.6% slower).
Optional incremental aggregates help localized strokes, but broad strokes
are7–9% slower. These are warm-cache standalone numerical kernels, not a
GameThread, RHI-upload or network measurement. No optimized game code was
integrated by this experiment.

Recommended direction: shared paint/deposit/erase semantics and coffee style,
with surface-specific coordinates: 2D tongue coverage and object-local volume
coverage on curved teeth. Prefer bounded CPU work for local strokes. A direct
surface-material renderer is a promising visual option; measured FPS does not
justify a migration by itself. Tooth rendering, native grime-domain migration,
brush/contact/completion, dirty-region/R8 uploads and replication require
their own evidence before production integration.

The accepted tooth renderer preview is
`Saved/CoffeeToothArchitectureAB/Preview/Runs/20261017T061504Z_8cecefce/Report.json`.
It uses an actual runtime tooth3 mesh, unchanged native grime state, and one
shared synthetic16^3 mask packed into a64^2 atlas. A is an owned copy of the
same native StaticMesh, offset0.6cm by normal WPO; B changes only enamel
material inputs. Both preserve original UVs. Domain selection uses actual
mesh normals and excludes actual reverse-facing vertices (0 painted out of776
in the accepted offline domain). Domains based only on world inward direction
were rejected because rotation could reduce safe depth below a native voxel;
the final gate also requires sufficient painted front area and valid partial
mask data. This is a synthetic future paint-domain experiment, not conversion
of the current native GrimeRelief islands or native brush/contact validation.

The lead rejected run061308Z visually despite telemetry passing: the original
lower-crown domain was hidden by the tongue and the whole-tooth AABB camera
was too distant. The accepted run uses the exposed upper-middle crown and a
disclosed close review camera. Its before/partial/Chipped pairs share mask,
camera and current native mesh. ReceiveArenaHit20 produces the real Chipped
variant; the original Intact coordinates and masks are retained. Both the
changed native tooth and its health exist only in the owned discarded PIE.

The lead inspected engine PNGs, all three comparison panels and decoded A/B
orbit MP4 frames in `Artifacts/CoffeeArchitectureAB_20261017/Tooth_061504Z`.
Both renderer prototypes follow the real curved/chipped enamel. B smooths the
edge and preserves more native crack detail: A's offset coat visibly covers
part of the separate crack detail. This is useful architecture evidence, not
approval of the final tooth-coffee art style. Partial erasure is generated
with the native Stroke formula offline; no actual new tooth brushing mechanic
or continuous erase-input video is claimed. Twelve oblique poses are captured
at matched discrete camera angles, with genuine PNGs and recorded wall gaps.

Separate tooth GPU run
`Saved/CoffeeToothArchitectureAB/Performance/Runs/20261017T061645Z_8086b7f5/Report.json`
passed all eight setup/mask/package/restoration checks. It used12s initial
preheat, then3s warmup and6s CSV per A/B/A window with no screenshots during
timing. GPU medians A1/B/A2 were4.2362 /4.0851 /4.0843ms; their p95 values were
5.2679 /5.0459 /5.0034ms. The B delta is−0.07515ms (−1.806%) against A repeat
drift0.1519ms (3.651%). This does not establish a GPU speedup. Native relief
data/ticks remain intact in both modes, and this is one paused Editor view.
No full gameplay CPU, upload, draw-call budget or network result is implied.
An independent agent recomputed all three raw CSV statistics, verified all
10 source hashes and94 package hashes, and checked actual settings readback.
The accepted tooth preview also passed its independent camera/mask/mesh pairing
audit for three static A/B pairs and all12 orbit poses.

Both experiments restore settings and native material/visibility state and
discard only their owned solo PIE. Saved source packages and unsaved user map
edits were preserved. Zen DDC returned507/InsufficientStorage on C: during
authoring; shader compilation succeeded in memory. No user cache was cleaned.
No production renderer switch, native-resolution change, Editor build, cook,
packaging, network test or push is part of these A/B experiments.

## Three-tier coffee painters — 2026-10-18 (solo accepted)

Reuses the already reviewed [Sam Perzanowski gel breakdown](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278)
and the original coffee-slime hop/pre-burst/burst actions above. The existing
coffee material, clipped tongue coating and finite additive coverage canvas
remain the visual contract. Two large painters split into three medium each;
each medium splits into three small. Every size paints while wandering and
deposits a persistent splash when it pops. The original pre-burst inflation and
bounded ballistic droplets are scaled with the body rather than replaced by a
generic star/flash. A pop pushes nearby players without starting a stun or
ragdoll. No new transparent layer, texture, particle asset or fluid simulation.

`Tools/Unreal/probe_jelly_split_solo.py` prepares six genuine gameplay-camera
clips (trail and rupture/split at each size) using ordinary Pickaxe input. The
focused `MessControl.Cataclysm.JellySplitPaintAndPush` regression covers the
1 -> 3 -> 9 branch and body-state preservation. The Editor build, focused native regression and actual solo traces/pops passed.
Current central-placement evidence:
`E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Jelly7/Runs/20261018T083103Z_paint_5a7a2914/Report.json`.
Lead and independent review accepted the genuine frames and decoded MP4s in
`E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/CentralDirt/Jelly/`.
All three tiers retained connected central trails, three/three/zero children
and an upright push. Each tier recorded 60 new stroke requests and 40/40 wet
path probes. Those probes may overlap earlier paint; they do not independently
isolate every newly covered texel. One lineage was exercised; simultaneous
18-small-jelly behavior, frame cost, multiplayer and packaged parity remain
unvalidated. A 10 Hz paint-contact clock and reuse of verified walking
footprints reduce repeated source-level work; no measured FPS improvement is
claimed.

The first LargeTrail frame at game 9.983 s shows a grey checker/default body
material; by approximately 10.78 s it shows the intended coffee material. The
encoded source sequence retains this frame. Its cause is not established by
the footage. Run `20261018T075939Z_paint_156810aa` remains the accepted earlier
silhouette baseline, not evidence for the new central layout.

## Manual food intake vortex — 2026-10-18 (solo accepted)

References: [Ashif Ali / CGHOW: Tornado in Niagara](https://cghow.com/unreal-engine-tornado-in-niagara-tutorial/)
for varied curl, rotation and eroded wisps; [Epic: Effect Type budgeting](https://dev.epicgames.com/documentation/en-us/unreal-engine/performance-budgeting-using-effect-types-in-niagara-for-unreal-engine)
for bounded effect instances. Adapted here using the existing procedural
`AMCThroatVortex` and its three materials, without a fluid simulation or new
asset dependency. Loose incoming food inside the opening silhouette is reserved
and follows an individual seeded spiral into the visible inlet before delivery
is credited. One shared vortex serves concurrent arrivals, with at most 24 food
wakes; wake history resets when its food identity changes. Completed effects
are replaced if another throw arrives during their destruction grace period.
The player's carried food and objects outside the aperture are not captured.
Lead review accepted the actual gameplay-camera intake in
`E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/SpawnIntake/Runs/20261018T081253Z_caf45b95/Report.json`.
Real Q throws showed individual curved arrivals, concurrent shared-vortex
presentation and native delivery credit. The decoded clip is
`E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/Feedback6_10/Feedback6_10_10_delivery_procedural_vortex.mp4`.
These owned solo fixtures do not establish frame cost, multiplayer behavior or
packaged parity.

## Coffee rupture silhouette refinement — 2026-10-18 (solo accepted)

Reuses the [Sam Perzanowski gel breakdown](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278)
and [Epic material-input guidance](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine)
already reviewed above. The first three-tier solo probe passed its gameplay
checks, but lead and independent visual review rejected the persistent radial
star and the temporary raised brown disc. That baseline remains intact in
`Saved/Feedback5_11_20261018/Jelly7/Runs/20261018T073559Z_paint_d654e4df`.

The source refinement replaces twelve thin radial spokes with a broad pool,
five to seven overlapping round lobes and five to nine separated drops using
the event's seeded random stream. Every deposited footprint retains the real
tongue/accessibility checks, texel support mask and per-texel splash occlusion.
Drop size has a texel-based minimum so visible drops remain brushable. The
temporary expanding sphere is removed; the actual conforming coffee film and
fourteen existing ballistic mesh drops now carry the rupture. No new assets,
transparent layer or render target is introduced. Damage, health, split/push
behavior, animation/warning timing and wandering-trail clocks are unchanged.

The matching Editor build and focused native mask/budget regression passed.
Actual three-tier captures `20261018T075939Z_paint_156810aa` were compared with
the preserved baseline and accepted by lead and independent visual review.
Decoded previews are in `Saved/Feedback5_11_20261018/Previews/Jelly7_075939`.
The later central-layout run `20261018T083103Z_paint_5a7a2914` retained that
irregular pool, separated drops and visible inflation without the raised disc
or symmetric star. Lead and independent review accepted its source and decoded
clips under `E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/CentralDirt/Jelly/`.
The disclosed initial grey LargeTrail body frame above remains in this newer
recording; it is not presented as a solved material-loading issue.
No GPU/frame-time result is claimed; existing coarse mask edges remain a polish
limit, and the older character-reflection streak is visible in both A/B captures.
The same source pass also corrects trail-budget ownership: same-batch wave
residue remains in cleanup accounting but cannot consume the event's separate
bounded trail canvases. Retired trail entries release their slots. The existing
jelly regression passed a four-canvas fixture with same-batch wave residue and
one destroyed/replaced canvas; both producers remain in cleanup accounting.

Portable reviewed previews: [Feedback 5–11 video bundle](../Artifacts/Feedback5_11_20261018/README.md). Raw capture files and full logs remain local under Saved/.
