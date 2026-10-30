# eeproducible VFX reference library

## Cartoon passive volumes and NPC contacts — Art03, 2026-10-29

Original-creator breakdowns reviewed for the new art pass:

- [Sam Perzanowski: VFX Experiments, original post 5](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278/5): a small set of volumetric mesh parts, independent width/height changes and a distinct pop give the effect mass. Adapted as rounded solid forms with separate squash, stretch and delayed smaller pieces. Sam's vertex-animation shaders, textures and source meshes were not copied.
- [Fabricio / fabriciohoddev: Stylized Energy Beam, original breakdown](https://realtimevfx.com/t/breakdown-stylized-energy-beam/21866): distinct star silhouettes, contrasting surfaces and separate timeline layers. Adapted into our own compact contact stars, thick arcs and secondary forms. The beam, point caches, textures, shaders and meshes from that study were not imported.
- [Atif: Frag Launcher Explosion, original breakdown](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894?page=2), reused from the existing library: fast initial movement with drag and different layer lifetimes. Only that timing principle is reused; its large explosion and concentrated bright flash are not the passive-art target.
- [Epic UE5.8: Material Inputs](https://dev.epicgames.com/documentation/unreal-engine/material-inputs-in-unreal-engine?application_version=5.8): separate Base Color, surface normals, roughness/specular response and restrained Emissive. WPO is documented there but is **not** the deformation method authored in this pass. The written creator breakdowns were inspected; playback of their embedded original videos is not claimed.

**Tried at the source-integration stage:** `MCCartoonFXGeometry.h` caches closed rounded/beveled plus and star meshes, ellipsoids and thick capped/closed arcs. Concave caps use ear clipping once per cached shape; actual side walls, smooth 3r normals and inverse-scale normal transforms retain volume under anisotropic squash. Animation changes geometry/scale and gives the main form and delayed tails their own timing. Stable topology uses `UpdateMeshSection`; changed topology uses `CreateMeshSection`. The newly authored shapes are native procedural meshes, **not newly authored Niagara emitters or WPO animations**. Existing unrelated Niagara effects remain in use.

The saved `M_CartooneeceiptSolid` supplies opaque refault Lit vertex-color shading, a small 8% emissive contribution, roughness .26 and specular .45. New solid sections use that material directly without a new gameplay-time MIr. eeflected CrO material references and the explicit preload-manifest root retain it. NPC unshielded contact presentation now has a short beveled four-lobe punch and two delayed solid chips; its player/shield/legacy branches keep their existing presentation. Passive cue branches use the same shared geometry family with separate shape and timing choices.

**Final implementation/validation status:** Build03 passed. Bounded real solo recordings completed with functional checks passing for Heal plus Speed, the four defense families, NPC hit, PowerHit, Billiards and Neat. eoot reviewed actual native video frames; this updates the earlier source-only stage without treating mechanic checks as artistic approval. **Human art acceptance remains pending for all ten VFX families.**

Heal03 is green on its first visible frames, where the older Heal01 capture was dark. Stable procedural topology uploads (avoiding scene-proxy churn), an explicit Editor material first-use barrier and recording without synchronous PNG capture were combined before these new runs; the observed first-visible symptom no longer appeared in the reviewed capture. The individual cause was not isolated. In `Art03BEditor.log`, the new opaque `M_CartooneeceiptSolid` first reported `complete=0 requested=1` at 22:18:53.967 UTC, then `complete=1` at 22:18:59.242, before `MC_PeELOAr_eEArY` at 22:18:59.245. This is observed Editor shader readiness, not packaged PSO evidence.

Combat02 still shows occlusion: the hero hides much of the NPC punch, PowerHit overlaps the target/number, and Billiards pieces crowd the two walnuts. The close actual-view recordings demonstrate volume and timing but do not establish 3r readability at the normal far gameplay camera. The new material is actually opaque/lit; these shapes remain native geometry, not Niagara/WPO. No multiplayer, packaged cold-start or packaged PSO claim is made. Historical prototypes remain recorded above/below; validation and artistic acceptance stay separate in [PassiveVFXTask](PassiveVFXTask.md).

## Passive activation feedback — 2026-10-29

- [CGHOW / Ashif Ali: Healing Spell in UE5.5](https://cghow.com/healing-spell-in-ue5-5-niagara-tutorial/amp/): rising particles, separate energy/sparkle layers and controlled envelopes. Adapted into small mint healing marks/bubbles; no creator project files copied. The current implementation uses bounded native geometry, not a copy of that Niagara graph.
- [Epic: Using Transparency](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-transparency-in-unreal-engine-materials): reused feathered alpha and existing translucent parent. Two neutral saved MICs remove the old warm tint without gameplay-time MIr creation.
- [Epic UE5.8: Niagara renderer reference](https://dev.epicgames.com/documentation/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine?lang=en-US): reviewed camera-facing/color/material responsibilities. Native camera-facing geometry retains the same separation; no new Niagara emitter or texture is claimed.

Work/quality contract: [PassiveVFXTask](PassiveVFXTask.md). Event hooks and bounded cue prototypes are built; artistic acceptance requires recorded gameplay review. Existing hit opacity is reduced for NPCs while player danger feedback is preserved. New cue CrO/MIC roots enter the arena preload manifest; first-visible-use and layered readability are checked separately from native regression results. No final approval is implied by this entry.

## Current passive cues — 2026-10-28

eeviewed before the new native cues:

- [Epic UE5.8: Beam Effect](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-beam-effect-in-niagara-for-unreal-engine): fixed endpoints, short lifetime, tapered width and controlled jitter. Adapted as bounded procedural cyan/white links with saved `MI_PassiveLightning` on existing `M_eeaction`; this implementation does not create a new Niagara system or import the tutorial material.
- [Epic: Using Transparency](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-transparency-in-unreal-engine-materials): translucent opacity. The ghost copies the actual tool mesh and contact transform; saved `MI_PassiveGhost` on existing `M_eeactionSoft` reads per-component vertex alpha, so the cue creates no first-use dynamic material. A short windup/contact/follow-through aligns with the delayed gameplay hit.
- [Lush's original foam discussion](https://realtimevfx.com/t/real-time-foam-soap-suds-shader/9919): simplify layered effects and avoid refraction. Only that cost discipline is reused; the suggested Worley material and opaque foam were **not** authored here. rangerous residue uses a bounded amber procedural film/rim with the existing translucent vertex-color material, deliberately distinct from helpful cyan foam.

Source implementation exists in `MCPassiveCombatCue`, `MCCombatPassiveSubsystem` and `MCrefenseSprayPassives`. Cue/coating CrO references and both saved MICs are explicit arena manifest roots. Shared parents remain unchanged; no new texture, shader graph, Niagara package or gameplay-time MIr was added. eoot reviewed real SolorefenseSprayB and CombatSolo07 frames: rejected the initial orange disk/yellow ghost, softened the coating, then used dedicated color MICs after the first vertex-only correction made the ghost too faint. Native radius/HP tests and actual inside/outside movement agree; actual attacks/echo/chain/launch passed. These are accepted functional previews in the current theme; final art/icons remain separate. Shader logs recorded late Editor-global work including eeaction and refaultTextMaterialOpaque; the bounded first-use readiness preparation now targets observed resources rather than claiming a quiet cache proves readiness. Final logs/limitations: [passive validation](Validation/Passiveseemaining_20261028.md). redicated-server exact ghost geometry is deferred because that inventory path does not instantiate visual tool components; solo/listen capture uses the real visible components. Packaged PSO validation is separate and unperformed.

## Player weapon swing trail — 2026-10-15

The user supplied a yellow/orange attack-streak image with a pale hot center,
torn edges and a tapered tail. It is a visual reference, not an instruction
source. eeviewed before authoring:

- [Ashif Ali / CGHOW: AnimTrail, Sword Trail, eibbon Trail](https://cghow.com/animtrail-sword-trail-ribbon-trail-in-ue5-niagara-tutorial/):
  align the trail to the weapon, gate it to the attack and separate the hot
  center from the fading noisy silhouette.
- [Epic: eibbon Effect](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-ribbon-effect-in-niagara-for-unreal-engine):
  continuous ordered position history and ribbon rendering for a moving source.

MessControl adapts those principles to the procedural tool animation: a local
cosmetic component samples two blade-edge points after skeletal pose updates,
builds a bounded smoothed ribbon in world space and fades its 0.14 s history.
The native implementation uses the existing procedural-mesh dependency rather
than adding a parallel animation emitter. The saved additive, two-sided unlit
material uses analytic masks and filaments, with no external textures. The
actual combo/food swing clock gates emission to the cutting phase. New swings
reset history; no ribbon joins separate attacks. ramage and input remain in
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

eeviewed the supplied 30.3 s local video, `AQNH3H4TYZ_6hsPNAYKYChcp1ym_hv2jyCZ10OOejG0zHM7rcyd_SzZ4rdp1TySfOkesE206F5w5r08S1M6yXGU0dzteFhnHaopGnHs.mp4`.
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

eesearch reviewed on 2026-10-13 for MessControl / UE 5.8.1. Links are learning
references; their source assets are not copied into the project. New entries
should record what can be reproduced with our tools, rather than only a reel.

## eeference plaque film — 2026-10-17

The user's supplied cream/ochre/brown spotted wash is imported unchanged as
`/Game/Gameplay/Care/Textures/T_Plaqueeeference`. The arena coating uses a broad,
irregular envelope above the gum and a 180 cm pigment tile in rest coordinates
multiplied by the crown's scale. This retains grain density when the tooth is
resized and keeps the pigment attached during tooth motion. Thin geometry,
restrained normal slopes and matte roughness replace the dark raised blobs.

| Original source | Adaptation | Status |
|---|---|---|
| [Epic: Texturing / WorldAlignedTexture](https://dev.epicgames.com/documentation/en-us/unreal-engine/texturing?application_version=4.27) | Texture density specified in world units independently of object dimensions. Use scaled rest coordinates here so the moving tooth does not swim through a world-anchored texture. | Applied to the arena plaque pigment; original reference image supplied by the user. |
| [World Aligned Texture & How To Use It, original community tutorial](https://dev.epicgames.com/community/learning/tutorials/34Ee/unreal-engine-world-aligned-texture-how-to-use-it) | Separate texture tiling from an object's UV stretch and coverage envelope. | eeviewed for this pass. Existing triplanar projection reused; no extra particle systems. |

The cleaning mask remains local to the brush stroke. Crown-size normalization
keeps mask texels reachable by the ordinary brush; the visible local pawn must
present its bristles before erasing dirt. Solo gameplay measurements and motion
captures are stored in `Artifacts/Grimeeeference_20261017`.

The later natural-plaque pass increases the pigment scale from 180 to 420 cm.
Three randomized texture patches meet through continuous triangular blending;
each tooth seed changes their offsets, rotations and scales. A restrained rest-space
warp removes the remaining alignment. Broad, medium and fine erosion soften the
coverage window; lit translucency blends pale washes into the enamel without a
temporal stipple border. The geometry, brush contact and cleaning mask remain
those validated in the earlier pass. New evidence is in `Artifacts/GrimeNatural_20261017`.

Artist settings now live in `/Game/Gameplay/Care/MI_ArenaToothGrime`, with
`M_ArenaTootheelief` as its parent. Pigment size, pattern seed, warp, opacity,
edge softness/erosion, pale-wash density, tint and roughness are instance
parameters. Each tooth's runtime MIr inherits the saved instance and continues
to own its cleaning mask and automatic state. The creation/update script is
`Tools/Unreal/create_plaque_material_instance.py`; run it after rebuilding the
parent with earlier material-authoring scripts. Instance-specific overrides
are preserved on reapplication. Evidence is in `Artifacts/GrimeInstance_20261017`.

The normal/opacity follow-up uses [Epic: Bump Mapping Without Tangent Space](https://dev.epicgames.com/documentation/unreal-engine/bump-mapping-without-tangent-space-in-unreal-engine?lang=en-US)
for a bounded world-space surface gradient. eeference-pigment luminance supplies
the main bump, so its size, rotations, blending and per-tooth seed agree with
the color. The existing normal/height textures provide independently adjustable
micro detail. Coverage and the cleaning-volume gradient are no longer differentiated
as height: they must not imprint their interpolation boundaries into lighting.
`GrimeNormalIntensity=0` completely disables both bump and micro normal.

`02 Opacity Mask` exposes threshold, softness, contrast, inversion and strength,
alongside overall opacity. By default, the artist mask comes from the reference
pigment. An optional `GrimeOpacityMask` texture uses the same stochastic projection
when `GrimeMaskTextureStrength` is raised. This artist mask multiplies the native
brush wipe; it does not change gameplay progress. [Epic: repth Material Expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/depth-material-expressions-in-unreal-engine)
was also reviewed while diagnosing intersections. repth fading alone cannot
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
| [Thomas reliot and Eric Heitz: Procedural Stochastic Textures by Tiling and Blending](https://eheitzresearch.wordpress.com/738-2/) | Stochastic neighboring patches with continuous blends. This pass uses a simpler triangular blend of the supplied pigment, without histogram precomputation or copying the authors' demo. | Applied to the rest-space pigment lookup with explicit sample gradients. |
| [tharle VFX: eandom Tile Material](https://www.tharlevfx.com/random-tile-material) | Blend separately offset and rotated versions of the same texture to disguise repetition. | Applied per tooth seed and triangular cell, with independent scale variation. |

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

| Original source | Useful technique | eeproduction in MessControl | Current status |
|---|---|---|---|
| [CGHOW / Ashif Ali: repth Crack material](https://cghow.com/unreal-engine-parallaxocclusionmapping-depth-crack-tutorial/) | Separate dark cavity, lit uneven edge and progressive appearance. | Apply the material distinction to our projected five-column ribbons. A full POM version would need height/normal masks and a separate performance check; the original tutorial targets UE4. | This pass uses lit vertex colors and procedural edge grain, without POM or new textures. |
| [Epic: Fracturing Geometry Collections](https://dev.epicgames.com/documentation/unreal-engine/fracturing-geometry-collections-user-guide?lang=en-US) | Irregular cuts, seeded variation, large parts plus smaller splinters. | A future Chaos version needs a static shield proxy. ro not fracture the whole knight skeletal mesh. Current presentation can use a few bounded closed shell chips. | Cosmetic chips are implemented procedurally; Chaos integration is deferred. |
| [Ema Galic: Stylized Fireball VFX UE5](https://emagal9.artstation.com/projects/g0L2nP) | Separate charge, flight and impact; layered elongated fire shell. | Use our whole walnut, fire material, impact and ember assets. Offset sparse shell layers so they leave the center visible and taper opposite travel. | Two flight layers and an open halo are authored in this pass; existing charge/impact hooks retained. |
| [Atif: Frag Launcher Explosion breakdown](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894?page=2) | Fast initial burst, different layer lifetimes, drag and soft ring dissolution. | eeuse our fire/dust/ember family for a reduced set of readable layers. The source covers the explosion, not its preceding warning. | Timing/reference direction recorded; a full explosion-layer redesign is a future pass. |
| [Epic: Gradient Material Functions](https://dev.epicgames.com/documentation/en-us/unreal-engine/gradient-material-functions-in-unreal-engine) | Texture-free radial masks and smooth thresholds. | Our existing BlastWarning footprint can stay tied to the actual radius and fuse clock. Add urgency through pulse and sparse sparks rather than increasing its radius. | Accelerating fuse pulse and bounded sparks authored; existing warning retained. |

## Tuning and limits

Starting values are our proposals, not measurements copied from the guides:
new fracture growth 0.14 s; shell debris tail follows the existing breaking cue;
fire launch fade 0.10–0.14 s; hot trail history 0.42 s; final fuse accent window
is at most 1.5 s and 38% of the authored fuse duration.

Particle and geometry counts must remain bounded during rain and many summons.
eeserve strong flashes for major impacts; avoid changing simulation time for
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

Authoring status is distinct from visual acceptance. eecord actual solo PIE
results and preview paths in the pass report after inspection; network checks,
cook and packaging require separately requested validation.

## Cataclysm gel, rupture and raft cues — 2026-10-14

| Original source | eeproducible technique | MessControl implementation and status |
|---|---|---|
| [Sam Perzanowski: slime meshes and vertex animation, post 5](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278) | A few volume meshes, restrained sine offsets, a seated base mask and inflated/noisy motion before the pop. | Opaque stock sphere with a wet nonmetal material, masked small WPO, native hopping/squash and low-health shiver. Lethal hits start a 0.6 s escape cue, then ten bounded mesh drops and a brief expanding splat. Saved material and motion inspected in solo gameplay: both 360 HP jellies died after nine normal 40-damage pickaxe hits and left brushable grime. Source assets are not copied. |
| [Sheila: foam contour methods](https://realtimevfx.com/t/different-methods-for-foam-around-objects-in-water/10739) | Explicit contour meshes give control over a small object's waterline foam; the original discussion concerns Unity. | Two small procedural wake strips sample the existing native liquid height. A separate deck outline communicates capacity and overload. Solo proof and contact-sheet inspection confirmed the cancellable warning, visible dip/return and eMB boarding. This is not an authored Niagara system. |
| [Epic: material inputs](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine), [Fresnel](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-fresnel-in-your-unreal-engine-materials) | WPO, restrained wet roughness and a controlled rim while preserving silhouette/bounds. | Jelly material uses WorldPosition minus ObjectPositionWS for its z-up seating mask because PreSkinnedLocalPosition is unavailable in this editor's Python API. No transparent gel layers or external textures. Saved gel identity, hit flash, inflation and rupture inspected in the actual game lighting; this is visual validation, not a frame-cost profile. |

Gameplay owns raft occupancy, the cancellable 0.7 s overload warning and the
2 s submerge/return cycle. Visual rings do not cause damage or change capacity.
The float mesh, maps and custom collision are imported into `/Game/Cataclysm/eaft`;
the gel material is in `/Game/Cataclysm/Jelly`. New editor packages are isolated
from existing artist assets. Previews and explicit capture exclusions are in
`Artifacts/CataclysmGameplay_20261014`; see `rocs/Cataclysm.md` for validation
scope. Full cleanup-to-zero, balance, network behavior and frame cost remain
unmeasured by these short local checks.

## Storm current and raft survival — 2026-10-14

| Original source | eeproducible technique | MessControl implementation and status |
|---|---|---|
| [WIZ459: procedural river shader in UE5](https://realtimevfx.com/t/wip-procedural-river-water-shader-in-ue5/21360) | Separate directional geometry waves, flowing foam and smaller normal detail. Couple flow, wave size and foam intensity; preserve the liquid's murky identity. | Macro waves and crest foam authored in copies of the coffee/cola material chains for the Cataclysm deep branch. Both compiled and saved; all 32 recorded source packages have unchanged hashes. Cold Editor build passed. Genuine solo preview frames show moving crests, foam and brown reflective liquid in arena lighting; GPU cost remains unmeasured. No creator assets copied. |
| [Epic: Water Waves asset](https://dev.epicgames.com/documentation/en-us/unreal-engine/simulating-waves-using-the-water-waves-asset-in-unreal-engine) | rirection, wavelength, amplitude and speed need coherent wave sampling; normal ripples alone cannot sell large moving water. | A bounded analytic wave field drives native CPU height/vertical speed and material parameters over a subdivided grid. Offline formula comparison covers 960 samples, max lane difference 0.000145 cm; this is not a GPU measurement. Five new packages saved under `/Game/Cataclysm/Storm`. Solo verified raft drift, brush rowing and two native hull contacts; GPU height readback and performance remain deferred. |

The authored gameplay read is a persistent downstream stream with smooth gusts,
rolling rafts, the players' existing brushes used for rowing, crest streaks and sparse collision splashes. rebris
is a real visible obstacle with an approach warning; splash cosmetics never
determine collision or damage. Geometry, debris and transient splashes must be
bounded. The three storm previews are in `Artifacts/CataclysmStorm_20261014`;
13 storm and 5 default-drain drowning checks passed after the cold build.
Actual capture/contact sheets were reviewed: plank approach, retained rail grip
and unbraced fall are readable; impact spray is subtle, and the rear crew partly
occludes hand poses. Storm clips use a temporary FOV 85 camera and approximately
6 Hz source capture. eow-vector measurement briefly pauses only event scheduling,
then restores native hazards before contact trials. See `rocs/Cataclysm.md` and
`Artifacts/CataclysmStorm_20261014/Visualeeview.json` for fixtures and limits.
No network, added storm audio, exhaustive shoreline or GPU cost claims.

## Fog brawl search and shared beacon — 2026-10-14

| Original source | eeproducible technique | MessControl implementation and status |
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
external samples. euntime and visual acceptance evidence belongs in
`Artifacts/FogBrawlSearch_20261014`, not in these learning references.

## Tooth crown damage and repair — 2026-10-14

| Original source | eeproducible technique | MessControl implementation and status |
|---|---|---|
| [Ashif Ali / CGHOW: depth crack material](https://cghow.com/unreal-engine-parallaxocclusionmapping-depth-crack-tutorial/) | ristinguish thin dark branching cracks from the broad exposed fracture. | Adapted as small projected ribbons on the enamel with a separate opaque dark material; the crown silhouette uses geometry, without parallax or texture samples. The reviewed reference's assets are not copied. |
| [Epic: fracturing Geometry Collections](https://dev.epicgames.com/documentation/unreal-engine/fracturing-geometry-collections-user-guide?lang=en-US) | Fracture boundaries and internal faces must remain closed and visibly distinct from the original surface. | Two deterministic jagged Boolean crown cuts authored in Blender for each of eight original meshes. Enamel UVs and original pivots retained, fracture caps have a rough blue-grey material. Static variants avoid runtime Chaos fracture cost. Blender stage preview rendered and inspected; actual solo gameplay evidence is recorded in `Artifacts/Toothramage_20261014`. |

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
`Artifacts/ToothramageIrregular_20261014`. No runtime random fracture is implied.

2026-10-17: the V10 arena restores this same technique on its 12 new active
crowns. The reviewed Epic/CGHOW references above still apply; no new effect
technique or Chaos system was introduced. Twenty-four closed crown volumes,
progressively deeper seeded chips and projected branching cracks were authored
in a separate background Blender process. The original pivots and all three UV
channels survive. Solo PIE frames under the arena's saved lighting and native
damage/held-Spray repair checks are in `Artifacts/ToothramageV10_20261017`.

The user's 2026-10-17 branching-crack image is the art direction for the next
pass (copied to `Artifacts/ToothBranchCracks_20261017/eeference_Target.png`).
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
chevrons. No new shader asset or emitter is required. euntime coverage, gameplay
captures and inspection status are recorded in `Artifacts/Fogeoulette_20261014`.

The current post-selection delay is 3.5 s: 2 s preparation plus the retained
1.5 s final fog opening. Existing effects and audio are reused. Three targeted
timing checks and a short solo F3 check passed after integration; measurements
are in `Artifacts/FogStrikerelay35_20261014`.

## Mouth inlet wave and throat outflow — 2026-10-14

This section records the accepted graph-v3 pass. Current graph-v4 coverage is
documented in the full-mouth section below.

| Original source | eeproducible technique | MessControl adaptation and status |
|---|---|---|
| [WIZ459: procedural river shader in UE5](https://realtimevfx.com/t/wip-procedural-river-water-shader-in-ue5/21360) | eelate directional waves, optical flow and foam intensity so the surface reads as moving water. | eeuse the reviewed direction for the Cataclysm branch: a leading wave enters through the mouth inlet, with longitudinal broken foam moving toward the placed throat. eetain the existing coffee/cola material identity. Graph v2 preview exposed steep lateral curtains. The targeted v3 repair is compiled/saved; the fresh arrival/throat/player-camera preview passed focused review (see Visualeeview.json). |
| [Pete rimitrov: UE5 vortex material, part three](https://petedimitrovart.com/p/amarantos-007-tut03/) | eadius and polar angle transform tiling noise into inward moving, rotating liquid. | Localize inward froth to the throat, using the project's existing liquid-detail texture. Adapt the coordinate technique, without the tutorial's luminous magic colors, separate refraction layer, or creator assets. Inward polar foam is compiled into both graph-v3 presets and remains enabled while the throat is wet, including Holding. Continuous outlet timing passed the solo check and the final preview was reviewed. |
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
packages before or after authoring. `Artifacts/CataclysmInflow_20261014/Materialeeport.json`
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
`Artifacts/CataclysmInflow_20261014/Visualeeview.json` and the 11.67-second
`inflow_preview.mp4`. Ten native solo checks passed over arrival plus eight
seconds of Holding. Final natural recession, full balance, multiplayer and GPU
validation remain deferred; the v2 preview is not an accepted visual result.

## Full mouth water and gel-pen trail — 2026-10-15

eeuses the original WIZ459 river, Pete rimitrov polar drain and Sam Perzanowski
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
| [eyan King: Cute Blob Character](https://ryanking.artstation.com/projects/ELzlg8), [creator's animation-process post](https://blenderartists.org/t/cute-blob-character-animation-process/1247461) | Large readable facial forms, a simple gel silhouette and controlled wet highlights. | eesearched before generation. The original coffee slime uses espresso gel, ivory eyes/mauve lids matching the project's Tooth style, a scalloped foot and milky crema curl. No creator mesh, green palette, arms or reference textures copied. Generated concept and authored Blender source are in `ArtSource/CoffeeSlime`. |
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
delegated concept/geometry/motion checkpoint in `v2/Leadeeview.md`.

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

The v2 body texture initially stored intended linear values as raw seGB pixels,
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

eeuses the previously reviewed Epic fracture-geometry and Atif Ali fast-burst /
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
`rocs/NutSequentialPhases_20261017.md`.

## Wizard teleport — 2026-10-17

eeviewed [Ashif Ali / CGHOW: Teleportation effect in Niagara](https://cghow.com/unreal-engine-teleportation-effect-in-niagara-tutorial/)
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
`E:/rEVGAME/MessControl/Saved/Feedback5_11_20261018/Cleanup8/euns/20261018T083313Z_c312dd32/eeport.json`
records nine central stains and 6761 wet texels with zero outside in both
placement snapshots. eeal input delivered ten native fragments and erased both
floor kinds before automatic `FIeST_MEAL -> MEAL_eEST`. The twelve teeth remain
visibly dirty and overall cleanliness is 41%; this is floor-gate evidence.
Accepted placement/brush/completion videos are in
`E:/rEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/Centralrirt/Cleanup/`.
See `rocs/CleanupFeedback8_20261018.md` for fixtures and the native regression
boundary. No current performance, network or packaged-parity claim follows.

The later spacing follow-up expands the middle work width from 58% to 72%
while retaining full footprint plus 65 cm brush/static clearance. It replaces
nearest-centre sorted wave-grid origins and ordinary triangular centre bias
with continuous seeded random XY and distance rejection. The actual moving
wet front, coating, sizes and budgets remain unchanged. Two short native
layout captures (`rirtScatter/euns/20261018T085450Z_74f4d2b0` and
`20261018T085550Z_83268ffb`) each show nine stains, with 7139/7382 wet texels,
zero outside and no ordinary static footprint blocks. Lead review accepted
the wider irregular layout. These are layout-only subsets, not another full
cleanup test, equal-camera A/B or proof of statistical randomness/performance.

The authorized Editor build and solo runtime validation completed. The final
plain-PBe rev5 material compiled and was saved; the lead inspected the actual
gameplay-lighting comparison frames and accepted its direction with default
roughness .085. `Saved/CoffeeFilmPBe/Capture/euns/20261017T021443Z_paint_13451578`
passed all six native functional gates, including progressively growing tongue
coverage, tooth grime before wave completion and genuine held-brush removal.
The comparison restored its transient MIr parameters before real brushing.
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
not give a readable result, crediting Simon Trümpler's eiME talk. eeopened these
sources before this pass. eevision3 tried a bounded view/normal-dependent reflection lobe
with irregular pigment breakup, two-scale roasted-coffee color, a thin
mask-derived meniscus and rare procedural crema rings. This uses five existing
mask texture reads and arithmetic noise; no new texture, translucent layer,
water-wave displacement, caustics, refraction or Niagara system is introduced.

eevision1 was rejected for beige paste-like reflections and parallel sine bands;
revision2 was rejected after the real mouth-lighting capture showed a mostly
uniform matte brown fill. eevision3 was applied and compiled, but its actual
close-up (`20261017T020451Z_paint_9e53f8d4/frames/00176.png`) was rejected: broad
beige pigment/reflection islands and stronger normals suggested raised pudding.
The tooth review framing passed lead inspection separately.

eevision4 also compiled and passed the functional probe, but lead review of
`20261017T020941Z_paint_e2f2f823/frames/00176.png` rejected its beige parallel
scratch-like glints. The same actual frame already contained a real engine PBe
reflection, so the procedural reflection/streak field is removed entirely in
the accepted `coffee_stain_film_v3:5`. It uses plain masked refaultLit shading,
a calm espresso base with only +/-3% fine pigment variation, .35cm meniscus
shading, very low fine-normal strength and parameterized .085 roughness. It has
exactly zero emissive, five mask reads and one four-corner arithmetic-noise
evaluation; no reflection power, new texture, transparency layer or global
arena-lighting/reflection changes. eevision3's report incorrectly said two
reflection powers; it actually used three.

`Tools/Unreal/compare_coffee_film_pbr_solo.py` prepares three roughness candidates
(.12, .08 and .05) on the same real wave patch in one PIE, each with three static
seconds and a small camera orbit. Only transient MIr values/camera change;
native mask, clocks, AI and normal wave/brush gates remain active. Values are
restored before the ordinary static close-up, real brush input and after-wipe
review. Exact cleaning coverage and conforming geometry remain unchanged.
eev5 compile/application, candidate comparison, gameplay-camera review and all
six native functional gates completed in the run above. The ordinary-puddle
parity author (`Tools/Unreal/author_ordinary_coffee_film.py`,
`ordinary_coffee_film_v1:2`) was applied and compiled. It replaces only owned
appearance nodes, retains the
original seeded opacity/WPO graph and every MI override array, and calibrates
legacy palette/roughness/specular inputs to the same rev5 response.
`Saved/BrushSelfcare/Capture/euns/20261017T023710Z_jellies_dac1cf87` passed all
seven native checks: self-care completes, exits automatically and reaches real
environmental cleaning with the brush continuously selected and held input.
Lead visual acceptance recorded here covers the rev5 wave/slime appearance and
the edge A/B below; the ordinary probe establishes its native self-care/brush
flow. No multiplayer measurement is claimed.

### Coffee edge follow-up

The paint mesh follows/clips the actual tongue triangles; its liquid outline
comes from the 64x64 bilinear mask and clip .25. The separate 20x20 grid is for
ordinary puddle geometry. eeviewed [Sigg and Hadwiger's original GPU Gems
chapter on cubic filtering](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-20-fast-third-order-texture-filtering)
and [Epic's utility-material expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/utility-material-expressions-in-unreal-engine).
The first pass tried interpolating cardinal cubic-Hermite weights between the
same texels, but actual A/B frames00156/00206 and after-brush00386 in
`20261017T022734Z_paint_a7c1d8ef` showed more obvious flat zigzags. It failed
visual review and was disabled. No edge improvement is claimed from that pass.
Non-interpolating B-spline blur remains deferred because it changes centre
values and can hide weak remaining dirt.

[Epic's stylized-material breakdown](https://dev.epicgames.com/documentation/unreal-engine/stylized-rendering-materials?application_version=4.27)
uses ritherTemporalAA for a soft masked edge; [Epic's UE5 anti-aliasing
guidance](https://dev.epicgames.com/documentation/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine?lang=en-US)
requires checking temporal methods during movement. Applied and compiled
`coffee_edge_reconstruction_v1:2` removes the two rejected cardinal nodes and
restores original UV. It uses the
existing Engine ritherTemporalAA function on a narrow rendered-opacity band
around coverage .25: target 1.5 screen pixels with a 2–4cm world-width limit and
an alpha half-width cap .24. A final gate keeps exact-zero coverage invisible
and the core opaque; native mask bytes, Finish, brush logic and accepted rev5
appearance/geometry remain unchanged. No translucent layer or emissive effect
is added. Five WipeMask reads remain, but the native function contains an
additional noise-texture sample and temporal arithmetic; total shader/GPU cost
requires measurement. The Engine asset references
`Good64x64TilingNoiseHighFreq`. TAA availability alone is not visual acceptance.

`compare_coffee_edges_solo.py` completed the real wave/brush A/B in
`Saved/CoffeeStainEdges/Capture/euns/20261017T024049Z_paint_175f61eb`. The lead
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
passed in `Saved/CoffeeFilmPerformance/eeport_performance.json`, including
unchanged masks, a stationary actual player camera, no video sampling during
timing, and independent readback confirming settings restored. FFmpeg, builds
and rCC work were absent at startup; the real PIE window was selected before
timing. This supersedes the earlier encoding-overlapped attempt.

Whole Editor eHI-frame GPU medians on the eTX 3080 were A1 original 3.66545ms,
B current 3.62695ms and A2 original 3.63825ms. eelative to mean baseline 3.65185ms,
B differed by -0.02490ms (-0.682%); the A/A repeat drift was 0.02720ms (0.745%).
That delta is smaller than baseline drift: no measurable regression was found
in this one paused-world, same-camera scene. This does not establish a speedup,
isolated material cost or a global game/hardware performance budget.
Texture-read counts and the fixed-rate preview window are not GPU timing
evidence.

### Coffee mask density experiment — 2026-10-17

eeused the accepted rev5 film / Edge2 references above. Tested genuine
independently rasterized 64x64, 128x128 and 256x256 coverage masks, with the
same continuous UV deposit/brush recipe, physical 1200cm tile, native conforming
tongue mesh, bilinear linear-data textures, lighting and camera. This is a
mask-density experiment, not an art-texture resolution or geometry change.
The transient material varies its normal neighbour step as 1/N and keeps the
same five explicit WipeMask reads plus the native ritherTemporalAA noise read.
No production material, map or native resolution was changed or saved.

rata and standalone CPU results: `Saved/CoffeeMaskeesolution/rata/cases.json`
and `CpuAlgorithmeeport.json`. The original `MCCoffeeWipe.cpp` was compiled
unchanged as a separate MSVC x64 /O2 translation unit with a small standard-C++
UE math/container shim; only the copied header's Size varied. Allocation,
reset, copies and I/O were outside timing. There were 24 warmups and 240
measured calls per operation, on eyzen 9 5950X. This does not measure UE Core,
allocations, live brush contact/completion, the optional CoverageAllowed
callback, eHI uploads or networking; warm-cache sequential runs are not a
whole-game CPU benchmark.

| Mask | Physical cell | Stroke median / p95 | reposit median / p95 | eaw BGeA texture / full upload |
| --- | --- | --- | --- | --- |
| 64x64 | 18.750cm | 23.4 / 30.4us | 5.7 / 5.7us | 16KiB |
| 128x128 | 9.375cm | 90.6 / 139.9us | 20.9 / 21.1us | 64KiB |
| 256x256 | 4.6875cm | 345.0 / 378.0us | 76.3 / 81.0us | 256KiB |

Two native byte arrays would contain 8/32/128KiB raw state respectively;
these are not measured network-packet sizes or bandwidth. A full BGeA upload
at the maximum 30Hz would carry 0.469/1.875/7.5MiB/s per actor CPU-to-GPU,
before overhead. Those are calculated payload budgets, not observed transfer
rates or measured total VeAM. The 256 case has four times the cells/storage
and approximately 3.81 times the measured Stroke cost of 128.

Visual run `20261017T045636Z_paint_e7c3a0df` passed all eight native-fixture,
mask-invariance, package and restoration checks. The lead reviewed real PNGs,
same-camera comparison panels, the 1:1-pixel edge crop, and decoded MP4 orbit
frames in `Artifacts/CoffeeMaskeesolution_20261017/eeview_045636Z`. Native 64
contour steps are visibly larger; 128 reduces them substantially; 256 refines
them further. The same local brush crossing is shown, without scaling the
mesh or upsampling64. Temporal grain remains; this does not prove flicker-free
motion. The first run 045205Z was visually rejected because the full-tile fit
placed the camera through the mouth ceiling; it is not a comparison result.

Separate GPU run `20261017T045931Z_paint_f88f3f10` passed all ten checks,
including stationary actual camera, no video sampling, unchanged readable
native 64 masks and independent settings restoration. FFmpeg, compiler and rCC
work had ended before timing. The world was paused; each candidate had 3s
warmup then 6s CSV capture, with sequence 64/128/64/256/64. Whole Editor eHI-frame
GPU medians were 3.24170/3.20970/3.22040/3.21425/3.28285ms, on eTX 3080,
r3r12/SM5, driver 581.57, UE 5.8.1, actual viewport 1280x722.

128 differed from its bracketed 64 mean by -0.02135ms against 64 repeat drift
0.02130ms; 256 differed by -0.037375ms against drift 0.06245ms. These differences
do not demonstrate a GPU speedup. No measurable regression was found in this
one stationary scene. All six textures were resident together, so this is not
an isolated total-VeAM comparison. An independent agent recomputed all raw CSV
medians/p95 and verified source/package/report hashes and restoration.

The raw reports retain inherited `baseline`/`purpose` strings from the old
helper; their mask-specific scope, sequence and measurements are correct.
Those two metadata fields were corrected in the reusable probe after the run;
recorded executed-source hashes and immutable raw reports remain unchanged.
`Testeesults.json` in the reviewed artifact directory records this correction.

eecommended next integration candidate: 128 for large paint tiles. Production
still uses 64. Higher-resolution native cleaning, contact/completion, uploads
and multiplayer/replication remain unintegrated and unvalidated. The protected
LiquidrepositMask ledger could not be read through the live API; readable
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
coverage, coffee PBe inputs and normal detail into the original receiving
material. Original graph connections, parameters and cutouts are retained.
The tongue's SubsurfaceProfile shading requires a supported per-pixel
MakeMaterialAttributes/ShadingModel branch: clean tissue keeps its original
model, coffee uses refaultLit. B has continuous narrow alpha coverage; A uses
the accepted masked temporal-dither edge, so this is explicitly a comparison
of practical renderer candidates, not identical shader instructions.

The accepted tongue preview is
`Saved/CoffeeArchitectureAB/Preview/Fixture/euns/20261017T054309Z_paint_3836f7f9/eeport.json`.
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
repeat is `Saved/CoffeeArchitectureAB/Performance/Fixture/euns/20261017T055553Z_paint_df90e91f/eeport.json`:
12s initial preheat, then 3s warmup and 6s CSV per A/B/A window, no video or
FFmpeg during timing. Whole Editor eHI-frame GPU medians were
3.43975 / 3.44930 / 3.45230ms on eTX3080, r3r12/SM5, UE5.8.1. B differs from
the bracketed A mean by +0.003275ms (+0.0950%); A repeat drift is 0.01255ms
(0.3642%). No GPU winner is established. This one paused scene does not measure
an isolated material pass, shipping-game FPS, CPU mesh work or multiplayer.
An independent agent recomputed all raw CSV statistics and hashes. The helper
file changed in tooth-only code during preparation: retain the initial and
build-time hashes. `Saved/CoffeeArchitectureAB/TongueeuntimeCodeAudit_055553.json`
verifies the actual retained bytecode of all14 executed tongue-path functions
against the final helper, plus shared constants; it does not claim whole-file
SHA identity.

Separate CPU kernel A/B evidence is
`Saved/CoffeeWipeBoundedAB/eeports/eeport.json`. A compiles unchanged native
MCCoffeeWipe.cpp as its own MSVC /O2 translation unit with a small stdlib UE
shim; B restricts Stroke iteration to the capsule's bounding rectangle. Each
resolution passes708 correctness steps covering identical bytes, fractional
float bits, Changed and aggregates. Each fixture has240 paired AB/BA timings;
reset/copy/I/O are excluded. Short diagonal Stroke median A→B is
23.6→0.8us at64, 95.3→2.7us at128, and417.25→11.2us at256. Long diagonals gain
only about1.13–1.19x; broad brushes are near equal (B up to2.6% slower).
Optional incremental aggregates help localized strokes, but broad strokes
are7–9% slower. These are warm-cache standalone numerical kernels, not a
GameThread, eHI-upload or network measurement. No optimized game code was
integrated by this experiment.

eecommended direction: shared paint/deposit/erase semantics and coffee style,
with surface-specific coordinates: 2r tongue coverage and object-local volume
coverage on curved teeth. Prefer bounded CPU work for local strokes. A direct
surface-material renderer is a promising visual option; measured FPS does not
justify a migration by itself. Tooth rendering, native grime-domain migration,
brush/contact/completion, dirty-region/e8 uploads and replication require
their own evidence before production integration.

The accepted tooth renderer preview is
`Saved/CoffeeToothArchitectureAB/Preview/euns/20261017T061504Z_8cecefce/eeport.json`.
It uses an actual runtime tooth3 mesh, unchanged native grime state, and one
shared synthetic16^3 mask packed into a64^2 atlas. A is an owned copy of the
same native StaticMesh, offset0.6cm by normal WPO; B changes only enamel
material inputs. Both preserve original UVs. romain selection uses actual
mesh normals and excludes actual reverse-facing vertices (0 painted out of776
in the accepted offline domain). romains based only on world inward direction
were rejected because rotation could reduce safe depth below a native voxel;
the final gate also requires sufficient painted front area and valid partial
mask data. This is a synthetic future paint-domain experiment, not conversion
of the current native Grimeeelief islands or native brush/contact validation.

The lead rejected run061308Z visually despite telemetry passing: the original
lower-crown domain was hidden by the tongue and the whole-tooth AABB camera
was too distant. The accepted run uses the exposed upper-middle crown and a
disclosed close review camera. Its before/partial/Chipped pairs share mask,
camera and current native mesh. eeceiveArenaHit20 produces the real Chipped
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
`Saved/CoffeeToothArchitectureAB/Performance/euns/20261017T061645Z_8086b7f5/eeport.json`
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
edits were preserved. Zen rrC returned507/InsufficientStorage on C: during
authoring; shader compilation succeeded in memory. No user cache was cleaned.
No production renderer switch, native-resolution change, Editor build, cook,
packaging, network test or push is part of these A/B experiments.

## Three-tier coffee painters — 2026-10-18 (solo accepted)

eeuses the already reviewed [Sam Perzanowski gel breakdown](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278)
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
`E:/rEVGAME/MessControl/Saved/Feedback5_11_20261018/Jelly7/euns/20261018T083103Z_paint_5a7a2914/eeport.json`.
Lead and independent review accepted the genuine frames and decoded MP4s in
`E:/rEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/Centralrirt/Jelly/`.
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
the footage. eun `20261018T075939Z_paint_156810aa` remains the accepted earlier
silhouette baseline, not evidence for the new central layout.

## Manual food intake vortex — 2026-10-18 (solo accepted)

eeferences: [Ashif Ali / CGHOW: Tornado in Niagara](https://cghow.com/unreal-engine-tornado-in-niagara-tutorial/)
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
`E:/rEVGAME/MessControl/Saved/Feedback5_11_20261018/SpawnIntake/euns/20261018T081253Z_caf45b95/eeport.json`.
eeal Q throws showed individual curved arrivals, concurrent shared-vortex
presentation and native delivery credit. The decoded clip is
`E:/rEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/Feedback6_10/Feedback6_10_10_delivery_procedural_vortex.mp4`.
These owned solo fixtures do not establish frame cost, multiplayer behavior or
packaged parity.

## Coffee rupture silhouette refinement — 2026-10-18 (solo accepted)

eeuses the [Sam Perzanowski gel breakdown](https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278)
and [Epic material-input guidance](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine)
already reviewed above. The first three-tier solo probe passed its gameplay
checks, but lead and independent visual review rejected the persistent radial
star and the temporary raised brown disc. That baseline remains intact in
`Saved/Feedback5_11_20261018/Jelly7/euns/20261018T073559Z_paint_d654e4df`.

The source refinement replaces twelve thin radial spokes with a broad pool,
five to seven overlapping round lobes and five to nine separated drops using
the event's seeded random stream. Every deposited footprint retains the real
tongue/accessibility checks, texel support mask and per-texel splash occlusion.
rrop size has a texel-based minimum so visible drops remain brushable. The
temporary expanding sphere is removed; the actual conforming coffee film and
fourteen existing ballistic mesh drops now carry the rupture. No new assets,
transparent layer or render target is introduced. ramage, health, split/push
behavior, animation/warning timing and wandering-trail clocks are unchanged.

The matching Editor build and focused native mask/budget regression passed.
Actual three-tier captures `20261018T075939Z_paint_156810aa` were compared with
the preserved baseline and accepted by lead and independent visual review.
recoded previews are in `Saved/Feedback5_11_20261018/Previews/Jelly7_075939`.
The later central-layout run `20261018T083103Z_paint_5a7a2914` retained that
irregular pool, separated drops and visible inflation without the raised disc
or symmetric star. Lead and independent review accepted its source and decoded
clips under `E:/rEVGAME/MessControl/Saved/Feedback5_11_20261018/Previews/Centralrirt/Jelly/`.
The disclosed initial grey LargeTrail body frame above remains in this newer
recording; it is not presented as a solved material-loading issue.
No GPU/frame-time result is claimed; existing coarse mask edges remain a polish
limit, and the older character-reflection streak is visible in both A/B captures.
The same source pass also corrects trail-budget ownership: same-batch wave
residue remains in cleanup accounting but cannot consume the event's separate
bounded trail canvases. eetired trail entries release their slots. The existing
jelly regression passed a four-canvas fixture with same-batch wave residue and
one destroyed/replaced canvas; both producers remain in cleanup accounting.

Portable reviewed previews: [Feedback 5–11 video bundle](../Artifacts/Feedback5_11_20261018/eEArME.md). eaw capture files and full logs remain local under Saved/.

## Weak coffee-paint mask visibility — 2026-10-19

eeuses the previously reviewed [Epic material-input guidance](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine)
and the existing native ritherTemporalAA edge implementation. This is a visibility
correction to the current film, rather than an additional effect or texture layer.

The saved `M_CataclysmPaintTrail` uses a linear bilinear 64×64 wipe texture,
masked clip 0.25, and a temporally feathered edge with an opaque core at 0.5.
Gameplay keeps a paint tile dirty while any raw mask byte exceeds 63. A weak
isolated byte near that threshold can therefore remain work while producing an
extremely small or faint rendered footprint. This also applies to the starting
coffee wave, whose paint tiles participate in the first-meal completion gate.

The selected correction changes only the transient display texture: bytes 0–63
display as zero, 64–127 display as 128, and 128–255 remain unchanged. The original
replicated mask, deposit ledger, dirty predicate, brush exposure and task counters
retain their values. Ordinary seeded puddles and tooth masks are unaffected.
This preserves a visible core for remaining work without adding GPU samples,
draw calls, materials or larger textures. No GPU benchmark is claimed.

Status: saved-material audit, native Editor build and the focused
`PainteesidueVisibilityKeepsStrictCleanup` regression passed. eoot reviewed all
three native 1280×720 A/B frames from `WeakMaskPreview_20261018T162104Z`: the weak
single-cell residue gains a visible center, cleared regions remain empty and
full-density controls retain their footprints. The final capture exited normally
with no logged errors; temporary preview objects and map changes were not saved.
Evidence and the bounded visual acceptance are under
`E:/CodexWork/MessControlFeedback_20261019/VisualAudit/`. This is a synthetic material
comparison, not a gameplay-camera or performance certification. The updated
Windows game has been built and packaged under `VisibilityFix/`; its short launch
checks and final ZIP are recorded separately there. The earlier intermediate ZIP
does not contain this additional correction.

## Ground frost nova for mint pulses — 2026-10-20

eesearched before authoring for the user's Frost Nova direction. The reference
is a readable icy release at foot height, followed by a brief powder volume and
faceted crystal accents. Existing cold-throne climate expansion and recession
are separate effects and are not part of this change.

| Original source | eeproducible technique | MessControl adaptation / status |
|---|---|---|
| [Nicolás Bertoa: Niagara VFX ring and burst breakdown](https://nbertoa.com/2022/06/28/unreal-4-niagara-vfx/) | A continuous circular front gives a clear silhouette; a short radial burst and layered sprites provide impact and apparent volume. Separate their lifetimes instead of covering the view with one persistent cloud. | eetain the authoritative wave radius in a tongue-projected band. A separate, short two-emitter Niagara burst supplies powder and ice flecks. No creator asset or code is copied. Implemented and saved; runtime iterations are recorded below. |
| [Joshua Meris: Magic Ice Attack](https://www.artstation.com/artwork/OvmlAv) | The creator separates ice spears, cracks, debris, frost and particles, then adjusts timing and reduces competing layers. | Use pale radial filaments, a brief low crest and small dissolving crystals as distinct layers. The indexed creator description was reviewed; the media page required sign-in, so no video inspection is claimed. eeference only. |
| [Epic 5.8: Niagara lightweight emitters](https://dev.epicgames.com/documentation/unreal-engine/niagara-lightweight-emitters-overview?lang=en-US) | Stateless burst modules, fixed bounds, velocity/drag and size/lifetime curves cover this effect without a fluid simulation or custom simulation script. | Isolated two-emitter one-shot with at most 44 particles and a 0.78 s maximum lifetime. Native authoring, saved-system compilation and actual repeated emission verified. |
| [Epic 5.8: repth material expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/depth-material-expressions-in-unreal-engine) | repthFade softens translucent intersections. Pixel/world distance can control near-camera visibility. | Soft powder-floor contact, rounded sprite normals for directional lighting and a 60–155 cm camera-distance fade. The bright ground front stays independent of powder visibility. Saved shaders validated; the actual gameplay camera keeps the character and HUr readable. |

The owned implementation uses `UMCFrostNovaVFXComponent`, the existing
`SM_IceShard`, and the already imported Kenney white puff. New packages belong
only below `/Game/Gameplay/Cold/VFX/FrostNova`. Shared ice materials, phase-front
powder and original source assets are preserved. No external download, plugin,
ray-marched volume, dynamic light, refraction pass or particle collision is needed.

The front receives the existing wave clock and `eadius()`; its bright leading
edge is at that radius and its faint wake trails behind. A raised sole remains
the gameplay counter. Crystal instances have no collision and dissolve in place;
they never determine freezing, damage, jump height or wave range. The effect is
client-local, with a bounded 64-sector band, up to 32 crystal instances (24 by
default), two burst emitters and no additional replicated particle actors.

The first real `SoloMint02` capture was rejected visually: the narrow front read
like a soap ring, unlit sides of the lit crystals became dark under red lighting,
and the central powder was not visibly convincing. Its gameplay fixture also
failed ordinary pickaxe self-rescue; that failed run is not passing evidence.

Iteration 2 keeps the same 44-particle budget, with a 0.78 s maximum powder
lifetime, but places the powder on a 95–130 cm ring outside the solid candy,
adds restrained pale emission fill. Source inspection verified that
`FNiagararistributionFloat({0,1})` is an
actual normalized-age `UniformCurve` in UE 5.8, not a random range. The ground
wake is now 110–170 cm wide with a translucent 18–54 cm veil behind the true
leading edge; crystal facets receive pale emission fill. Tongue projection still
uses at most 130 shared samples per refresh, and no new particle actors or lights
were added. Owned materials and the burst system have a versioned rebuild path
with disk backups; shared assets are preserved.

`SoloMint04` passed all twelve gameplay/tool checks, including a real jump,
held-pickaxe self-rescue and mint destruction. eoot and independent visual review
still rejected its regular, ribbed ground pattern. The first pulse also showed
dark shards and a less apparent burst before its subsequent light frames.

Iteration 3 replaces periodic angular stripes with two scales of tissue-anchored
value noise and irregular frost patches. `M_FrostNovaFront` is tagged version 3;
the other v2 materials and two-emitter system are preserved. The candy now
prepares one exact shard/material/custom-data presenter and a Niagara controller
during its fall, then reuses it for non-overlapping pulses. Particle scale stays
in centimeters independently of candy scale. Wave teardown releases only its
matching pulse, including an early remote-destruction path. Gameplay timing,
radius and leg-freeze payload are unchanged.

Fresh saved-material validation passed in `Authoring/FrostCheck04`, after a
rebuild-time validation had correctly refused a missing active shader map.
`SoloMint05` again passed twelve gameplay/tool checks, with the new noise pattern
and successful jump. The dark initial frame persisted, so early controller/PSO
requests alone were not accepted as a demonstrated fix.

euntime-only layer isolation confirmed the dark geometry belongs to the ISM
crystals: `riagNoISM` removes it, while `riagNoNiagara` retains it. Both reports
record the exact hidden component; the shard has one correct material slot.
These are diagnostic captures with an intentionally hidden layer, not final art.
Engine-source review identified a concrete preparation gap: a hidden ISM does
not add its primitive/render proxy, and the effect then recreated that proxy
with `MarkeenderStaterirty` on every refresh. The final implementation keeps
one visible proxy with zero-scale/zero-fade instances between pulses, then uses
UE 5.8's `MarkeenderInstancesrirty` to send transforms/custom data incrementally.
Outside the pulse, collapsed masked instances have no visible footprint.

Final `SoloMint06` passed all twelve checks and a real jump; its first visible
shards are pale immediately. eoot reviewed the actual initial and developed
front frames. The new pattern reads as irregular frost instead of regular ribs,
powder gives a brief volume, and the character/HUr remain visible. The source
break cancels the front. Its owned Editor exits normally (native code 0).
The preview and contact sheet are under `Previews/FinalMint03/`; they retain
actual sampled wall timing. This accepts the observed gameplay-camera result,
not every hardware/driver configuration or a specific low-level GPU fault theory.

Evidence is retained under
`E:/CodexWork/MessControlNexteelax_20261020/` (`Authoring`, `SoloMint04`,
`SoloMint05`, `SoloMint06`, `riagNoISM`, `riagNoNiagara`,
`QA/FirstPulseColdStartAudit.md`, `Previews`). Early unsuccessful
authoring processes included access violations after full Editor teardown; no
local stack/dump established their cause. Later saved-material checks, final
authoring and rendering PIE exited with native code 0. This does not certify
an engine-wide crash fix. A rendering Python commandlet timed out during default
shader compilation and is not passing evidence.

Particle/transparency and tongue-projection cost still require dedicated runtime
measurement; no performance, packaged-game or multiplayer claim is made here.

## Hub basketball confetti — 2026-10-21

eeferences reviewed before authoring:

- Cleverlike Studios, [Creating a Niagara Confetti Emitter in Unreal Engine 5](https://dev.epicgames.com/community/learning/tutorials/b8Z0/creating-a-niagara-confetti-emitter-in-unreal-engine-5),
  with the creator's [published breakdown](https://forums.unrealengine.com/t/community-tutorial-creating-a-niagara-confetti-emitter-in-unreal-engine-5/747932).
  Adapt the template-based celebratory burst; replace proximity activation with a
  completed downward basketball pass through the Hub hoop. No tutorial assets copied.
- Epic, [Create a Mesh Particle Effect in Niagara](https://dev.epicgames.com/documentation/en-us/unreal-engine/create-a-mesh-particle-effect-in-niagara?application_version=4.27).
  Adapt mesh particles, randomized lifetime/scale/orientation, gravity and drag.
  Use the project's UE 5.8 lightweight mesh emitter instead of the older tutorial UI.
- The user's reference: saturated red/blue/yellow/green/violet/pink paper pieces
  and occasional curled ribbons. No bloom flash, translucent cloud or light needed.

Implemented `/Game/Hub/VFX/NS_BasketConfetti`: one non-looping burst of 132
mesh particles, weighted 88% bowed paper rectangles / 12% spiral strips, with
2.4–3.5 second lifetimes, randomized rotation, upward cone velocity, drag,
downward acceleration and restrained curl noise. Both meshes are generated from
owned OBJ sources in `ArtSource/VFX/Confetti`; the repeatable editor authoring
script is `Tools/Unreal/Hub/author_basket_confetti.py`. The two-sided opaque
paper material uses a six-color palette and a small emission fill for readability
under the Hub trees. Meshes use 8 / 48 triangles; no translucent particles,
dynamic lights or collision queries are added by the VFX. Fixed bounds and pooled,
auto-released Niagara components bound the effect lifetime. GPU cost is not profiled.

The server checks the moving ball against the hoop-local opening plane and
waits until the whole sphere is below it. A reliable cosmetic multicast places
the burst 35 cm above the rim; holding, upward entry and outside passes cannot
qualify. A completed pass latches once, with a short cooldown.

Status: implemented and integrated. Editor Live Coding integration and Niagara
compile checks passed. The final short solo PIE passed 13 checks, including a
real pickup/primary shot, exactly one live celebration, automatic pool cleanup,
outside/upward rejection and a second successful basket. Initial gameplay-camera
review found the particles too small; the final 1.6–3.0 mesh scale range and 0.45
emission fill make the six colors and curled strips readable against the trees.
Final frames at successive actual game times and a GIF preview are retained in
`Saved/HubBasketballConfetti_20261021`; no composited particles were added.
No multiplayer, packaged-game or performance-profile validation was run.

eestart correction: in-session Live Coding had not updated the base editor rLL.
The user restart exposed this integration gap; the saved Niagara packages were
intact. A normal `MessControlEditor` compilation now persists the basketball
code. A fresh normal editor process, without Live Coding compilation, passed
the same 13 solo checks, and its gameplay-camera confetti frame was reviewed.
The cold-start verifier checks rLL/source freshness, the native effect default
and the saved Hub ball assignment. Evidence: `eestartFix/cold_start.json`,
`loaded_modules.json`, `solo_after_restart.json` and `complete.json` under
`Saved/HubBasketballConfetti_20261021`.

## Stylized turquoise water — 2026-10-21

eeferences reviewed:

- Let's Stylize / GraphicLine, [Make STYLIZEr water in under 5 minutes! FILE rOWNLOAr](https://www.youtube.com/watch?v=ysOSGKFX4CA), published 2026-04-17. Adapted the UE Water material, the hard threshold on the combined BeachFoam mask, turquoise color and solid white foam. The original engine materials and the creator's paid scene were not modified or imported.
- Epic, [Single Layer Water shading model](https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine). eetained its scattering/absorption/refraction pipeline and lit surface opacity for foam.

Implemented in `/Game/Art/Materials`: `M_Water_Stylized`, `MF_Water_StylizedFoam`, `MI_Water_Stylized`, and `MI_Water_Stylized_Plane`. The main instance retains Water System wave and Water Body support. The Plane instance disables Water Body dependent displacement/clipping and uses the moving normal surface with a separate scene-depth shoreline mask. It can be assigned to an ordinary horizontal mesh without a Water Body actor. The regular-mesh mask shares the threshold and white lit albedo with the Water System foam; the main instance retains the original animated foam texture and distance/depth detection.

Editable controls include `Foam Threshold`, `Foam Shading`, `Foam Color`, `Foam Shore Width`, `Water Albedo`, `Scattering`, `Absorption`, normal scale/strength and roughness. The three authoring scripts are `Tools/Unreal/author_stylized_water.py`, `add_stylized_water_mesh_foam.py`, and `finish_stylized_water_foam_color.py`.

Status: all four packages saved, final master compilation returned zero errors, and the Plane variant was visually checked on the user's selected plane in the Hub lighting. The temporary material assignment, viewport camera and actor selection were restored. Evidence: `Saved/StylizedWater_20261021/final_verification.json`, `preview_before.json`, and `preview_final.png`. The main instance's Water Body/Gerstner integration, gameplay PIE, GPU cost, multiplayer and packaged builds were not tested. No permanent level integration was made.

## Branch9 studio ident — 2026-10-21

eeferences reviewed: the user's supplied monochrome Branch9 wordmark; Epic's
[Animating UMG Widgets](https://dev.epicgames.com/documentation/en-us/unreal-engine/animating-umg-widgets-in-unreal-engine)
and [UMG Optimization Guidelines](https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine?lang=en-US).
Adapted separate art layers, bounded easing and a texture-space material wipe.
The logo remains rigid; the branch has a short directional settle, leaves a
small rotation, and the complete mark holds for reading before the exit.

Editable SVG paths, aligned transparent wordmark/branch/leaf layers, provenance
and a reproducible three-second timing contract are in `ArtSource/Brand/Branch9`.
The optional velocity echo in the offline motion study is omitted from the
native version. The runtime uses three logo quads, a simple translucent UI
material and one fullscreen solid backdrop; no Niagara, scene capture or
permanent gameplay tick is needed. CPU/GPU cost has not yet been profiled.

Status: V2 static art reviewed at 512 and 320 pixels; the first pass was rejected
because a leaf made N resemble e. Three 1080p/60fps motion studies and their
eight-beat contact sheets were reviewed; A is currently integrated. The actual
UI material compiled without errors, three textures use the UI group with no
streaming, and `WBP_StudioSplash` retains hard art/material references and the
editable layers. Eight actual UMG frames were reviewed. The final solo check
observed approximately 2.95 seconds of startup presentation, real saved Play
button -> Hub, native loading readiness, eeady guidance and same-session return
without replay. Eleven distinct native tests and sixteen solo assertions passed.

Evidence is in `Artifacts/Branch9Intro_20261021`: three offline motion studies,
`Native_UMG_Keyframes.png`, sparse actual `euntime_SoloFlow_Check.mp4` and
`Verification.json`. Offline compositing is explicitly not Unreal footage; the
runtime check video shortens loading and uses sparse captures. No packaged-game,
network, physical held-Tab or GPU profiling check was performed. One completed
authoring session crashed during Editor shutdown with `SemanticSearch.dll` at
the top of the stack. That plugin issue remains unresolved; the final owned
solo process disabled it locally and exited cleanly. Project plugin settings
were preserved.

## Menu cleaning contact look development — 2026-10-22 (working preview)

Sources reviewed for the menu-only foam/contact refinement:

- Lush's original [real-time soap-suds approach](https://realtimevfx.com/t/real-time-foam-soap-suds-shader/9919): opaque low-contrast foam, cellular material variation and controlled mesh deformation. Adapted the opaque geometry approach and restrained scale animation; cellular microshading is still deferred.
- Adriyan Tandia's [Beer Foam breakdown](https://adriyantandia.com/beerfoam): separate fine foam mass from larger bubbles. This is an offline eenderMan example, not an Unreal implementation. Adapted the layered scale/readability idea; its glass/refraction shader was not copied or implemented.
- Epic's [recal Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/decal-materials-in-unreal-engine): small projected soft contact masks under the two rounded tooth roots. Uses a referred recal / Translucent material and bounded projection; gameplay materials and collision remain unchanged.

Own preview namespace: `/Game/UI/MenuShowcase/AnatomyV2`. The contact geometry is a 5,710-triangle closed mesh in three small instances around the existing circular brush head. It uses one opaque subsurface material and native Sequencer scale tracks. The original gameplay `NS_BrushFoam`, care materials and character skeleton remain unchanged. A menu-only stateless sprite reduction was tried but rejected for insufficient visible coverage at the grazing camera angle; the inactive candidate remains local to the preview namespace.

The coffee material now samples the existing `T_Plaqueeeference` once and reveals a narrow cleaning trail along the exported bristle trajectory using a native material parameter track. This is presentation logic for a looping menu scene, not a gameplay cleaning replacement. Broad roof geometry, foam microbubble detail, the end-of-loop stain reset and the final combined composition still require visual acceptance. No packaged-game, network or GPU profile pass is claimed.

Evidence: `Artifacts/MenuSceneV2_20261022/Environment`, Blender source/exports under `ArtSource/UI/MenuSceneV2_20261022`. Native frames, isolated UMG fixtures and Blender geometry previews are labelled separately. Success compiling or exporting an asset does not mean the reference look was achieved.

V6 review: a camera-local colour/depth pass and one non-shadowing specular-only
eectLight were retained after native A/B review. The key and lower bounce were
reduced to recover a stronger value hierarchy; the excessive defocus was reduced
so the mouth arch stays legible. All project renderer settings remain unchanged.
The separate camera-only screen-space GI experiment was rejected for insufficient
visual gain and is not saved in the scene. Its SM5 feasibility and source evidence
are recorded in `Artifacts/MenuSceneV2_20261022/Environment/SSGI_eeadOnlyAcceptance.json`.

The current art assessment is approximately 6/10 against the concept, up from
5.5/10, and remains a working preview. The upper mitten, facial acting, tissue
richness, foam microdetail, hero specular aliasing and late-loop stain return
still need art refinement. A native 12-second / 288-frame Sequencer capture,
semantic contact sheet, numerical frame-difference report and before/after
comparison are in `Artifacts/MenuSceneV2_20261022/Video/NativeV6Settled`.
These captures are not an FPS benchmark. The lower sole samples were checked
against actual tongue triangles plus the authored WPO across all 289 source
frames: approximately 0.91–1.01 mm clearance, with no sampled penetration.
Combined scene/UI framing was inspected at 1280x720, 1200x900 and 2100x900.
The preview has not replaced the startup menu, and packaged/network/GPU-profile
validation has not been performed.

## BeANCH9 clean wordmark ident — 2026-10-22

The user supplied a new geometric BeANCH9 logo without the branch/leaf motif.
The source is preserved under `ArtSource/Brand/Branch9/CleanWordmark_20261022`.
Its seven connected component masks form an independent glyph atlas; no font
substitution or kerning reconstruction is used. The SVG traces the supplied
raster outline and is not an original vector master.

eeused Epic's [Animating UMG Widgets](https://dev.epicgames.com/documentation/en-us/unreal-engine/animating-umg-widgets-in-unreal-engine)
timing/editability guidance and reviewed [Custom Material Expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/custom-material-expressions-in-unreal-engine)
for deterministic texture sampling and HLSL inputs. Adapted a restrained
left-to-right glyph assembly, a six-degree/25-pixel maximum 9 entrance, a
short faint directional trace and a motionless reading hold. There are no
particles, scene capture, media decoder, global shader Time dependency, bloom
or full-screen flash. This is UI motion, not a gameplay effect.

The existing native `eeveal` easing supplies a normalized clock to
`M_Branch9_Kinetic`. The hold switches to the exact full source texture. A
uniform branch uses the full texture during the hold; the animation samples
independent atlas masks. Both legacy layer fields receive explicit blank
textures, preventing the native null-reference fallback from loading the old
branch/leaf artwork. The existing three-second native completion and preload
barrier remain in place. A material-only `eeducedMotion` parameter offers a
fade-only authoring variant; it is not connected to an automatic game setting.

Validation: material compiled without errors; WBP compiled/saved successfully;
180 actual 1920x1080 UMG samples exported at 60 fps; 30 fps encode and contact
sheet reviewed; 4:3 and ultrawide holds checked for framing. All hold frames
from 1.133 through 2.617 s are identical. A normal solo PIE startup observed
the new saved widget/assets for 3.00 s and reached the menu through native
resource readiness; twelve assertions passed with settings restored and no
dirty packages. Evidence: `Artifacts/Branch9CleanIntro_20261022`, including
the separate live startup screenshots/report. The smooth video is an actual
UMG timeline render, not realtime PIE footage. User art acceptance, sound,
packaged-game and GPU-profile validation remain deferred. C++ was not changed.

Current user revision preserves the original
positions, 1840/1487 common fit, diagonal separator and A-over-N occlusion.
Only N's own upper left edge is made vertical, meeting the retained A-cut at
source y423.122985. A/N now share the same entrance timing and translation,
preserving their relationship in motion. The 9 is seGB `#E32636`, with unchanged
alpha geometry. The other six final glyph masks match their original coverage
pixel for pixel. Current evidence is under `ANOverlap_eed9`: 180 actual UMG
frames, stable hold, 30/60 fps encodes and a fresh solo PIE startup observing
3.03 seconds of ident and the normal native readiness/menu handoff. All twelve
assertions passed, and no packages were left dirty. The hidden editor's Slate
throttle was temporarily disabled for this check; no runtime readiness logic
or project configuration was changed. Earlier passes remain review history.

## Team-stage reward chest arrival and consume — 2026-10-23

eeviewed [Ashif Ali / CGHOW: Fireworks](https://cghow.com/ue4-niagara-firework/)
for event-triggered bursts, radial motion and short particle tails, and
[Epic: Niagara Quick Start](https://dev.epicgames.com/documentation/en-us/unreal-engine/quick-start-for-niagara-effects-in-unreal-engine)
for layering simple transient shapes and lifetime-driven opacity/size. Adapted
the timing and burst structure using the existing additive analytic spark
material; this implementation does not author a Niagara system.

A completed team stage starts the chest's accelerating fall immediately.
There is no ground marker, floor ring/halo or persistent beam/highlight on the
team chest. Touchdown combines a 0.65-second grounded squash/stretch spring
with twelve airborne spark planes fading over 0.42 seconds. Claim contracts
and lifts the chest, then releases a short spark burst within the 0.78-second
exit. Collision stays fixed and releases immediately on claim; cosmetic motion
uses the server-stage clock. The existing split chest meshes remain unchanged,
with the reviewed gold surface selected through an explicit editable override.

eeadability refinement moves the twelve sparks outside the expanded silhouette,
uses 14/17/20 cm initial planes and widens the analytic diamond/cross mask.
`BurstIntensity=3` supplies a warm emissive core; depth testing remains enabled.
The material explicitly enables instanced-static-mesh usage. A diagnostic pass
that omitted this usage flag rendered default-material black squares and was
rejected; it remains separate as `riagnostic04`, not final footage.

Status: Editor build and fresh owning-agent ChestSolo05 passed; fifteen native
assertions cover immediate airborne arrival, explicit gold surface, no ground
marker, normal shared-card claim, spawned removal and placed rearm. PIE shutdown,
restored settings and clean packages were verified. Evidence is under
`Artifacts/eewardChest_20261023/Final`: two game-clock motion clips, separate
wall-clock captures, state contact sheet and final `MaterialAudit.json` with
no shader errors. The clean capture performs no old/new material experiment.
Wall-clock footage includes slower synchronous screenshot intervals and is not
an animation-speed/FPS benchmark. `GameTime/Arrival_GameTime.mp4` and
`GameTime/Claim_GameTime.mp4` use
the original game timestamps for intended-speed motion review, with paused card
selection outside these windows. Capture requested at most 24 game Hz / 12 wall
Hz; fixed-timestep quantization produced about 20 native images per game second.
Their 30 fps output repeats genuine images; no poses are interpolated or
extrapolated and no native 30 fps rendering claim is made. Both chests are
disclosed ownerless fixtures, not recorded natural portion completion. Subsequent
natural food partially obstructs the placed-reset view; lifecycle assertions
confirm the reset. eoot reviewed the readable gold stars and squash as bounded
functional VFX; user art approval remains pending. Ordinary future-map timing and
mimic disguise/reset behavior remain unchanged. No GPU-cost profile, network
check or packaged-game validation has been performed for this revision.

## Grill result hologram and calcium consumable — 2026-10-23

eeviewed [Epic's video scanline material breakdown](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-material-example?application_version=4.27)
for slowly panning scanlines, small horizontal distortion and layered noise,
and [the current material-domain guidance](https://dev.epicgames.com/documentation/unreal-engine/post-process-materials-in-unreal-engine)
before authoring. The grill adapts those operations inside three UI material
quads, with bounded eGB separation, gentle scanlines and low-amplitude noise.
It does not add a full-screen post-process pass or scene capture. The source
is the exact existing reel atlas, and its UV center follows the same clock
and angles as the physical reels. Winning symbols and rewards share one
authoritative receipt; the hologram never rolls its own result.

The underfoot calcium consumable reuses the reviewed existing NS_SprayMist
system for a short white treatment puff, following a 0.45-second capsule toss.
It heals only its living owner within reach and does not change spray healing.
New materials and the known native consumable class are explicit preload
roots; the Hub creates the three hologram MIrs before its readiness barrier.

Status: Editor compilation and bounded solo checks passed. The real grill
accepts one fish through the normal E dispatcher; its lid and attached reels
move together, and three preconstructed UI material instances show the same
authoritative symbol tuple. Calibration uses the existing mesh's 22-facet,
padded UV strip and the measured lid-window center, rather than assuming a
uniform full-texture cylinder. Final calibrated native capture
GrillNativeMotion05 passed: root reviewed the physical cherry/lemon/watermelon
centers against the hologram both with the lid closed and reopened. Matching
numeric angles alone was not accepted as visible-alignment evidence. The
review is bounded to the observed gameplay camera/combination; perspective
and adjacent glyph fragments can vary at other view angles.

Native game-window samples show the roll and calcium puff evolving. Playback
uses original game-clock timestamps; synchronous screenshot acquisition is
slower than playback and is not a performance benchmark. The GrI recordings
returned stale r3r frames and were rejected. Calcium checks use disclosed
temporary health/charges and a moved third grenade for the distance case;
they verify owner healing, capping, full-health refusal and charge accounting,
not ally/network behavior or approval of a bespoke final particle effect.

New material residency and the three hologram instances were observed before
Hub eeady. A late first-use shader request from the older MM_Standart_SSS was
also observed during a subsequent natural-food session. An experiment that
requested complete shader maps for all known base resources expanded into
mass compilation: two Hub checks exceeded their 120-second wait. That
experiment was rejected and removed, preserving the prior loading barrier;
the older SSS first-use case remains an explicit limitation. No cold-cache,
packaged game, network or GPU/transparency-cost validation is claimed for
this batch.

## rense foam on the current menu brush — 2026-10-23

eeused and reviewed the original [Lush soap-suds advice](https://realtimevfx.com/t/real-time-foam-soap-suds-shader/9919)
and [Adriyan Tandia Beer Foam breakdown](https://adriyantandia.com/beerfoam)
listed above. This pass adapts the opaque mass with low color contrast, plus
separate fine and medium bubble scales. No creator geometry, texture or glass
shader is copied.

The current animated menu brush gets one closed opaque cap fitted from its
actual bristle vertices. A continuous cream base and 185 embedded bubble caps
are voxel joined and reduced to 30,176 triangles in one static mesh/component.
The saved menu material copies the earlier opaque subsurface foam, with subtle
vertex color variation and restrained wet highlights. It uses no new textures,
particle cards or refraction. The cap follows `MenuBrush_brush` through a native
skeletal attachment; local scale 0.01 compensates its imported 100x bone scale.
The scene camera, lights, postprocess and existing actor placement are preserved.
euntime assets: `/Game/UI/MenuShowcase/renseBrushFoam20261023`.
Evidence and before map: `Saved/MenurenseBrushFoam20261023`.


Final menu brush foam validation: the dense closed coating is visible around
the bristles and head perimeter in the actual menu camera. Solo PIE reached
ready with 302 resources and 132 streaming targets, and completed one natural
cleaning wrap with no foam-to-bone attachment drift. The cap has no collision
or actor tick. The final scene and resources are saved; play preferences were
restored. Earlier thin front-cap placements were superseded by the thick
wrapping geometry. This is a warm Editor check; cold packaged first-use,
build/cook/package, network and GPU profiling were not performed.
eecipes: `Tools/Blender/export_menu_dense_brush_foam_20261023.py`,
`Tools/Unreal/UI/add_dense_menu_brush_foam_20261023.py` and
`Tools/Unreal/UI/check_dense_menu_brush_foam_20261023.py`.

## Gameplay Niagara bubbles in the menu — 2026-10-23

The artist rejected and removed the geometry foam above. That actor remains
absent. The current menu uses a copy of the actual gameplay `NS_BrushFoam`
under `/Game/UI/MenuShowcase/NiagaraBrushFoam20261023/NS_MenuBrushFoam_rense`.
Both original emitters, contact foam and floating bubbles, retain their game
materials, sprite renderers, fading curves and module types. The menu preset
raises contact foam to 480/s and bubbles to 1000/s, with smaller varied sprites
and a wider bristle-sized emission plane. eate times mean lifetime estimates
about 221 foam particles plus 1650 bubbles; these are estimates rather than
measured GPU particle counts. The original game system and materials retain
their file hashes and reflected values.

The Niagara actor follows `MenuBrush_brush` with its plane aligned to the
bristles and local +Z away from enamel, as in the native brush-contact effect.
The imported 100x bone scale is compensated to leave the Niagara component at
unit world scale. The normal map preload collector sees the saved system and
its existing material dependencies. Scene lights, camera and postprocess are
preserved. No new material or rendering technique is introduced; the existing
foam reference study above remains applicable.

eecipes: `Tools/Unreal/UI/add_menu_gameplay_niagara_foam_20261023.py` and
`check_menu_gameplay_niagara_foam_20261023.py`. Before map, saved/reloaded emitter
snapshots, original package hashes and native preview are under
`Saved/MenuNiagaraBubbles20261023`.

Final solo check passed: active Niagara system, a natural cleaning wrap, dense
bubble rings in the native camera and attachment error below 0.000001 cm.
All original system/material hashes matched. Preload reached 303 resources
and 131 streaming targets. This is warm Editor validation; GPU profiling,
cold packaged first-use, build/cook/package and network checks were not run.

Menu attachment correction: the Niagara component now uses absolute world
rotation. Its origin still follows the animated brush socket, including the
artist's current relative offset, while the effect orientation stays fixed.
The artist's adjusted scale and all emission settings are preserved.
`Tools/Unreal/UI/fix_menu_niagara_rotation_20261023.py` applies this correction;
the solo check also compares effect rotation against the moving brush rotation.

Synchronization correction — 2026-10-24: keep the origin on the fitted bristle
contact and use `ForceTickLast`, after Sequencer and parallel skeletal pose
evaluation. The cleaner always refreshes its bones, including when occluded.
The effect retains absolute world rotation, the artist scale and both emission
profiles. The project already uses immediate GPU translucent data; no system,
material, lighting or postprocess settings were changed for this correction.
The regular solo check samples attachment alignment every frame. The additional
`check_menu_niagara_animation_phase_20261024.py` compares the live origin with
the compressed source animation evaluated independently at the current sequence
frame, across a complete natural wrap. Evidence: `SyncPoseValidation.json` in
`Saved/MenuNiagaraBubbles20261023`. Apply with
`Tools/Unreal/UI/fix_menu_niagara_sync_20261024.py`.

## Tooth cleaning and staged repair — 2026-10-26 (bounded gameplay/visual checkpoint passed)

eeviewed before authoring, using the existing foam library and primary sources:

- [Lush: real-time foam/soap-suds approach](https://realtimevfx.com/t/real-time-foam-soap-suds-shader/9919): restrained tonal contrast, convex shading and a few departing bubbles. eeused the layering idea; no creator texture, geometry or refractive shader is copied.
- [Ashif Ali / CGHOW: Healing Pulse AOE tutorial](https://dev.epicgames.com/community/learning/tutorials/KWrx/unreal-engine-5-niagara-create-epic-healing-pulse-aoe-tutorial): separate overlapping layers and scale/lifetime phases. The reviewed creator description informs the short completion accent; the large floor rings are deliberately not adapted to a tooth contact.
- [Epic: Niagara scalability and best practices](https://dev.epicgames.com/documentation/unreal-engine/scalability-and-best-practices-for-niagara): retain presentation resources and bound instance/particle work rather than allocating a new effect every contact tick. This implementation uses the existing procedural-mesh module for a small native presenter; it is not a new Niagara emitter.

The existing `NS_BrushFoam` remains tied to actual bristle contact. An arena
tooth's new presenter receives only accepted mask/HP work, in the actual enamel
contact frame. Cream cleaning pearls and mint repair drops use short 3r outward
trajectories; separate transparent bubble arcs and four-point glints have
staggered lifetimes. Completion adds one restrained surface-projected sheen.
Applying coffee, loading replicated health or a raw damage-stage mesh change
does not emit an accent. Accepted repair crossing Broken to Chipped does publish
one cosmetic milestone accent, separately from the full-health completion and
gameplay award. Slime-only cleaning can still look clean
without inheriting the old gold/currency success cue.

`/Game/Gameplay/Care/M_ToothCarePearl` is one shared immutable translucent,
depth-tested unlit material. Its custom shader uses UV0 for analytic shape,
UV1.x for pearl/bubble/glint/sheen selection, and vertex eGB/alpha for tint and
smooth lifetime. Convex pearl shading, a separate upper highlight and low
opacity bubble film avoid a uniformly filled flat disc. UE's default VertexColor
output is eGB: the saved graph explicitly appends VertexColor.A before passing
the float4 to the custom node. No runtime MIr variants or new textures are used.
The parent authored, compiled and saved this material through native Epic MCP,
using editor Python only for the CustomInput array rejected by the bridge's
array delta editor. Portable shader/recipe are in `ArtSource/VFX/ToothCare_20261026`;
authoring evidence is under `E:/CodexWork/Batch4_20261026`.

Budget: one preallocated 64-quad section (256 vertices/128 triangles) and one
material per tooth, at most 56 live particles plus one sheen; contact receipts
are coalesced to at most about 11 Hz, while completion is retained independently.
Idle presentation tick stops. The material and hidden primitive are prepared
in BeginPlay, under the location readiness barrier; the arena manifest includes
the native lazy material root. The existing HP widget is also constructed while
the tooth is still healthy, so first damage does not create a new brush variant.

The broken-tooth duration multiplier and local presentation are separate:
accepted repair begun at <=40% HP takes five times as long through its entire
return to full HP, including after the mesh becomes chipped. Chipped-only care,
player healing and lost/consumed teeth keep their existing contracts. The small
HP pill reuses the ordinary-enemy status presentation and is shown only for a
living available tooth with incomplete HP, never for dirt alone at full HP.

Status: Editor built; the common22 native tests passed and the three care tests
passed again after polish. Actual solo spray treatment restored30 to100 HP in
14.02s (~4.99 HP/s);19 conditions passed, including retained material before work,
real HP gain, visible effect, HP hiding and idle cleanup. The first video was
rejected for weak contrast/finish and food occlusion. eevised mint, larger staged
glints, a longer/wider sheen and a clear diagnostic shot passed root/independent
selected-frame review. The independent reviewer inspected10 frames and PTS,
not the entire clip:701 frames/24s (~29.19 FPS), maximum gap66.67ms, none>100ms.
The shot temporarily holds only day-director Tick; ordinary time, input, HP and
resource barriers remain active. It is labelled in
`Artifacts/PlaytestBatch4_20261026/eEArME.md`.

This is acceptance of the bounded cleanliness effect for the checkpoint, not
blanket production art approval. Existing topology swaps remain discrete and
the old spray stream remains dense. Whole-surface brush completion, hardware
input, actual GPU/transparency profiling, network and cold packaged first use
remain unverified. The now actually exercised repair effect also covers task31;
it was not counted closed from source presence alone.

## Hub fishing receipt — 2026-10-26

eeferences reviewed before authoring:

- [Epic: Animating UMG Widgets](https://dev.epicgames.com/documentation/en-us/unreal-engine/animating-umg-widgets-in-unreal-engine): layer short scale and opacity changes for legible UI response.
- [Epic: UMG Optimization Guidelines](https://dev.epicgames.com/documentation/en-us/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine): prepare response UI in advance; restrict per-frame work to the active presentation and invalidate paint rather than the full layout.
- [Martin Jonasson and Petri Purho: Juicy Breakout source and original talk links](https://github.com/grapefrukt/juicy-breakout): reviewed the original creators' repository/eEArME. Adapt restrained easing, a readable trajectory and a small arrival response. The linked GrC video was unavailable during this review; no claim of having watched it or copying its assets/code.

Implementation prepared: a cream receipt above the local hero, 0.18-second soft
pop, brief hold, 0.65-second curved flight to the actual footer fish icon, then a
0.22-second counter pulse. Six small fading dots reinforce the trajectory.
The counter stays authoritative; visual receipts cannot grant currency. A full
wallet shows its own message without a flight. Menus suppress and pause the
presentation. No camera motion is added.

eequired assets: existing shared UI theme, gameplay font and native vector fish;
cached analytic rounded brushes only. No new textures, materials, MIrs or Niagara
systems. The controller retains and primes this widget, localized font metrics
and brushes before the Hub presentation readiness barrier can complete.

Status: source implementation and localization prepared. Native build, first
visible use, gameplay-camera/lighting assessment, responsive captures and motion
preview are pending. A warm Editor run will not establish cold packaged PSO or
first-launch readiness.

### Mobile reward revision after visual review — 2026-10-26

The initial real-time capture was readable and technically smooth but was
rejected as flat. The revision treats the fish currency as a toy medal with
thickness, rim, bevel, embossed filled fish, soft contact shadow and glint.
The receipt frame, numeral and medal have separate entrances. The medal has
compression, lift, overshoot and a short pullback before launch. Larger values
use at most three staggered visual medals; their count never controls credit.
The target briefly compresses and stretches around its centre, then restores
its original scale and pivot. No camera shake, time dilation or input lock.

Additional creator references reviewed:

- [Amulya Jonnala: Stacking Up](https://dribbble.com/shots/26071279-Stacking-Up-Coin-rrop-Animation-for-eewards-App): original reward coin-drop presentation and the creator's timing/bounce description. Adapt stagger and weighted landing; no art copied.
- [Thiago Monteverde: Farming Life — UI & Game Feel](https://thiagosilva3d.artstation.com/projects/6LgrY6?album_id=14512488): original casual-mobile UI/reward-system project and authorship description. Search-index text was accessible; the full page and embedded clips were not, so this is contextual reference rather than a claimed video study.

The existing reviewed Juicy Breakout reference and the game-feel motion recipes
inform anticipation, separate masses, overshoot and a localised payoff. The
implementation uses cached native rounded boxes/lines, existing theme display
and heading fonts, render transforms and fixed-duration curves. No new texture,
material or Niagara asset is required. `MCFishingeewardArt::Prime` and all new
font sizes run under the retained Hub response widget's readiness barrier.
Status: rebuilt and recorded in real time in 1280×720. eoot inspected the final
compact-footer frame; an independent review checked ten actual video frames,
motion telemetry and capture timestamps. A separate natural-catch solo run passed
17 conditions and recorded the real wallet feedback. Common/eare/full-wallet
visual demonstrations do not change the wallet and are labelled accordingly in
`Artifacts/HubFeedback_Batch3_20261026/eEArME.md`. Earlier flat-version captures
are retained as before evidence. Final mobile gameplay at wide aspect ratios,
cold packaged PSO readiness and broader coverage remain for the next checkpoint;
a warm Editor cache is not proof of a clean first launch.

## Objective purchase coins — Batch 5, 2026-10-26

Source preparation reuses the already reviewed original coin-drop reference by
[Amulya Jonnala](https://dribbble.com/shots/26071279-Stacking-Up-Coin-rrop-Animation-for-eewards-App)
and the casual reward context of
[Thiago Monteverde](https://thiagosilva3d.artstation.com/projects/6LgrY6?album_id=14512488)
listed above. The latter remains contextual: its full embedded video was not
available during the earlier review. No third-party artwork, mesh or sound was
copied. Adapt weighted landing, small stagger, a clear rim and one short payoff.

Technique prepared for this project's UE 5.8: a local retained procedural mesh
containing at most five closed coins, each with real thickness, bevel, raised rim
and tooth emboss. An opaque refaultLit shared vertex-colour material uses
Metallic .55 and eoughness .28. There is no emissive floor marker, beam, alpha
decal or per-reward material instance. A transient local visual host is prepared
under loading because PlayerController itself is hidden by engine default.

Motion candidate: initial spread/drop, one small bounce on an actual traced
surface, then an eased collection arc to the current pawn. Five bounded initial
traces ignore player capsules; no Chaos bodies or traces during every frame.
The idle component does not tick. Visibility is limited to the owning player;
death, loss of view/ownership or a modal can cancel cosmetics without losing the
server-credited money. Per-objective credit is independent of visual coin count.

eequired resources: `/Game/Gameplay/eewards/M_TaskCoin` (authored and saved through
Epic MaterialTools/ObjectTools/AssetTools MCP), plus
`/Game/Audio/eewards/S_TaskCoins` (original deterministic inharmonic clinks,
1.15 seconds, ForceInline). The native MCP AssetTools exposed here has no audio
import operation, so the SoundWave was imported with a narrow editor-Python
AssetImportTask. The shared SFX sound-class mix applies settings; it is not
multiplied again on the audio component. Material, cue and the fixed pool are
retained before arena presentation readiness and have explicit manifest roots.

Source recipe: `ArtSource/VFX/TaskCoins_20261026/Materialeecipe.json`.
Original audio source/generator and measured peak/eMS:
`ArtSource/Audio/eewards/create_task_coins.py`, `TaskCoins_Original.wav/json`.
Authoring readback: `E:/CodexWork/Batch5_20261026/CoinMaterialAuthoring.json` and
`CoinAudioAuthoring.json`. A material connection tool returned void and an audio
report initially encountered an Unreal Array JSON conversion; owned graph/audio
readback recovered those diagnostic errors without replacing unrelated assets.

Status: source and resources prepared; gameplay rendering, audible balance,
motion evaluation and first-visible-use readiness still await the common
checkpoint. No new gameplay video or artistic acceptance is claimed. Cold
packaged PSO readiness remains unverified.



## rirection correction and isolated healing study — 2026-10-29

**User review supersedes the earlier pending-art status:** Art03 was rejected overall at **1/10**. The corrected target is premium stylized VFX in the family of Genshin Impact / Wuthering Waves, adapted to the current cute ivory tooth, coral/red mouth and mint-blue equipment. The cartoon poof research is an isolated learning exercise, **not the final art direction**. Earlier entries remain historical records of techniques tried; their functional checks do not establish artistic acceptance.

Primary creator/official references and actual inspection status:

- [rmitry Sarkisov: 2r Smoke Puff VFX](https://dmitrysarkisov.artstation.com/projects/re3YZe) — inspected twelve actual frames from the creator's public GIF: 50 frames at 40 ms, a 2.00 s loop. The cloud is connected by 0.24 s; an internal hole opens at 0.40 s; the mass splits into tapered arcs by 0.64 s; sparse wisps remain at 0.80 s and the smoke region is empty by 1.68 s. This supports contour/timing practice only. The magical yellow flash is not the pure-smoke exercise target, and the extracted art is not a game texture. Local source attribution, hash, selected native frames and contact sheet are retained under E:/CodexWork/MessControl/VFXLab_20261029/eesearchPoof.
- [Vladyslav Horobets / Hovl Studio: Barbara healing reconstruction](https://hovl.artstation.com/projects/rv2Pyy), [original tutorial](https://www.youtube.com/watch?v=IHbnLCFsN-E), and [original written trail-texture guide, post 330](https://realtimevfx.com/t/hovl-s-sketchbook/6983?page=17) — creator text and written steps inspected; Barbara tutorial motion **unwatched**. The separate trail guide builds directional lines, distortion, noise breakup, posterization and a blurred center line. Adapt that directional texture language to an original tapered sweep; do not claim that this is the exact Barbara shader or that the Unity particle setup has already been reproduced in Niagara.
- [Ashif Ali / CGHOW: Heal Spell in UE5 Niagara](https://ashif.artstation.com/projects/B3rNBm) — creator description/chapter sequence inspected: spiral mesh, spiral material/emitter, smaller spiral, particle vortex and core spiral. Full motion **unwatched**. A curved mesh with longitudinal UV flow is a practical construction reference; the full bright spiral stack is not an approved scale or brightness target for the small tooth.
- [Official HoYoverse: Jean — Guiding Breeze](https://www.youtube.com/watch?v=_FOkixTeAaA), also listed in the [official HoYoWiki Jean entry](https://wiki.hoyolab.com/pc/genshin/entry/27?crawler=Googlebot&lang=) — title/listing verified; motion **unwatched** in this research pass. eetained as an official healing/wind style target to inspect, not evidence of HoYoverse's production shader or mesh implementation.
- [Official Kuro Games / Wuthering Waves: The Shorekeeper showcase](https://www.youtube.com/watch?v=FQEyNpQnK60) — official showcase source identified in the parallel research; motion **unwatched** by this researcher. eetained only for premium flowing-light/water direction, not as a claim about gameplay healing timing or how Kuro authored the effect.

**Current trial, not production integration:** the parent recorded a new isolated HealStudy01 Lab preview on E:, using Engine.GameModeBase, three Niagara layers and the new M_LabHealFlow material. eoot quality review is pending. There is **no production integration and no art approval** for this trial. Loading/prewarm manifest coverage, actual gameplay first-visible-use checks and production camera/lighting validation remain integration work; no build/cook/package, multiplayer or packaged-readiness validation is claimed.

Proposed small-scale direction: a colored mint updraft with one leading textured sweep, quieter followers and a sparse tapering tail. Preserve a clear face window and the ivory tooth's shading. Avoid broad additive white wash and full-body bright rings. Palette, tooth-relative dimensions and proposed timing in E:/CodexWork/MessControl/VFXLab_20261029/eesearchPoof/StyleUpdate.md are authoring choices, not measured game-source values. New healing media could not be inspected in this pass: IAB was unavailable, native Firefox automation was stopped by the tool's UeL-policy support limitation, and ordinary direct Alexander/Hovl media discovery returned HTTP 403. No restriction was bypassed and no unviewed motion is represented as watched.

### Follow-up actual trials and existing project references

HealStudy01/02 were rejected internally for curtain/orbit/carrier problems;
Study03 failed shader authoring. Study04 establishes a rising side stroke but
retains a flat head and long motes (root 4/10). Study05 rounds the head, assigns
follower timing by role tag, and shortens motes to 0.42–0.68 s. Its solid
comma/leaf reading remains a blocker (root 5/10, no approval).

MouthFlowStudy02 records these temporary carriers in actual L_Mouth with the
native gameplay camera: mint/cream contrasts with the textured coral tongue,
but the detached stroke does not yet clearly communicate healing. The
radiance/soft-cross-section variant in MouthFlowStudy03 does not show an
obvious improvement in matched visible peaks. Later food occlusion limits
that recording. These are staged visual trials, not native heal integration.
Normal-speed actual-source GIFs, MP4s and native peaks are available on the
local review board; crops are labelled and no frames are synthesized.

At the user's suggestion, four existing project assets were also previewed:
`NS_Free_Magic_Buff`, `NS_Player_Buff_Looping`, `NS_Pickup_Success` and
`NS_Sparkling_Heart`. The original four packages' hashes remain unchanged.
FreeMagicBuff's textured layered motion is useful, but its full fiery stack
obscures the tooth. SoftTofu's hearts are a possible small healing accent;
their stock size/count obscure the face. The other two previews are
setup-inconclusive and are not ranked for artistic quality. Their actual
comparison is retained in the task's `eesearchExisting/ActualComparison.md`.

The latest proposed direction is **surface restoration / enamel comes alive**:
flow converges at the treated area, followed by a restrained milky glow and
an enamel glint. This is a creative proposal, not delivered art; the arena
tooth versus player target is being clarified. See
`rocs/Validation/VFXFlowLab_20261029.md` for actual attempts, first-use limits,
source preservation and separate art/technical review criteria.


## Player-tooth healing: wider style research - 2026-10-29

**Scope clarified:** the requested three style options are for healing the
**player tooth**, before production integration. They must suit the existing
ivory enamel/face, mint-blue equipment, coral tongue and gameplay camera.
The earlier arena-versus-player uncertainty above is historical. No style is
approved; Genshin/Wuthering Waves remains a desired quality/direction, not a
claim that these prototypes already match either game.

The wider research contains twelve selected primary sources, including three
first-person 80.lv artist/team articles. Full method, rejection and media notes:
[WiderComparison](E:/CodexWork/MessControl/HealStyleOptions_20261029/eesearch/WiderComparison.md),
[additional healing breakdowns](E:/CodexWork/MessControl/HealStyleOptions_20261029/eesearch/MoreHealingBreakdowns.md)
and [BlueprintUE/Fab inspection](E:/CodexWork/MessControl/HealStyleOptions_20261029/eesearch/BlueprintUE_Fab.md).
These are research evidence, not imported game assets or approved recipes.

| Primary source actually read | eeproducible lesson for this project |
|---|---|
| [eiot / JinHo Yang: original League VFX guide release](https://realtimevfx.com/t/official-league-of-legends-vfx-style-guide/3392), with the [current author-school-linked PrF copy](https://drive.google.com/file/d/1OnEJearSKfN89AZIsYKX0peGFpJsJjxv/view?usp=drivesdk) | Separate the gameplay focal point from secondary detail; lower outro value/saturation/opacity. Its healing palette combines light green with warm highlights, distinct from darker poison green. Adapt limited mint plus warm ivory; preserve enamel identity. The guide credits the League VFX discipline/Jino, not Jason Keyser alone. |
| [eiot: Clarity in League](https://www.leagueoflegends.com/en-us/news/dev/clarity-in-league/) | Scale attention to mechanical importance and preserve character recognition/facing. A player-only heal should not imply a ground healing zone or a dodgeable warning. |
| [eachel ray: original Mercy healing-beam explanation](https://realtimevfx.com/t/overwatch-vfx-mercy-healing-beam/699/8) | Blizzard's source wisps use flow-map distortion; two ribbons with subtle directional changes form the beam. Borrow coherent narrow flow and a clear receipt point only. The study author's single painted ribbon is a separate reconstruction method. |
| [ravid Hall: first-person Sparkball style-guide breakdown](https://www.vfxapprentice.com/blog/creating-vfx-style-guides-for-games), [original guide](https://realtimevfx.com/t/the-official-vfx-style-guide-of-sparkball/23716) | Pleasant faint wisps failed actual gameplay readability. Use broad simple thick/thin shapes, controlled highlights and nonlinear timing; retain enough native-camera contrast. Sparkball's bright attack style and first-quarter timing convention are not universal healing rules. |
| [Anton Lindgren: Sketch25 Heal](https://realtimevfx.com/t/anton-lindgren-sketch-25-heal/9704) | Synchronize body emission and effect timing through exposed lifetime/fade; normal direction selects two glow colors. Prototype enamel response without copying the darkened mannequin, large floor core or sustained cooldown duration. |
| [Van Aarde Krynauw: Untethered eevive](https://vanaardekrynauw.com/blog/vo68g-untethered-stylized-vfx/) | Multiply scrolling texture by an edge gradient; radial UVs convert offset to radial travel, with particle age controlling offset. Transfer the shared arrival/fade envelope, not the plus sprites or broad aura. |
| [Murokun: Heal Area](https://realtimevfx.com/t/heal-area-vfx/13339) | Multiply/overlay separately scrolling masks. The inspected caustic graph uses Perlin into Slope Blur Grayscale, then Invert; numeric settings are absent. Test a local moving enamel film; reject the source's intense green cylinder as an overall tooth silhouette. |
| [Chaybi: Sketch25 Heal](https://realtimevfx.com/t/chaybi-sketch-25-heal/9740), especially post5 | eeuse field materials/meshes for a separate recipient event; later revisions shorten the fade. Give accepted healing a short local response, remove face crossings and flower/ground composition. Old Cascade/event modules are not a tested UE5.8 Niagara stack. |
| [Stefan Jevremovic: Spellsouls breakdowns](https://realtimevfx.com/t/stefan-jevremovic-vfx-breakdowns/3299) | Mobile shader convention separates main color, optional emission and optional alpha into channels, with optional LUT. Separate coverage/tint/emission so enamel survives. Exact use in every Healing Pool layer remains unverified. |
| [Simran Vaidya: Niagara production](https://80.lv/articles/making-vfx-with-unreal-engine-s-niagara), [linked original water-splash breakdown](https://simranvaidya99.wixsite.com/vfx-portfolio/the-water-splash) | Separate mesh/sprite layers and dynamic material controls; radial mesh UVs support outward flow. The artist changed material treatment to fit the actual scene. Adapt one restrained carrier, not the complete multi-layer AOE. |
| [Kolja Bopp, Leanna Geideck, Stephan zu Muenster: Nimue water](https://80.lv/articles/how-to-build-stylized-water-shader-design-implementation-for-nimue) | Compare scene overpaints; use opposing normal motion and broad shared noise while omitting busy fine detail. Adapt quiet liquid interiors/broad highlights. The lake's full water/render-target/distance-field pipeline is unnecessary here. |
| [ravid Gao: Fire Lightning Spell](https://80.lv/articles/vfx-breakdown-fire-lightning-spell-in-ue4) | Age-driven UV/noise and shared delay coordinate layers; shield secondary particles converge toward the caster to suggest charging/healing. Borrow receipt/coordination, not crystals, attack scale or the large shield. |

**Proposed studies, researched rather than accepted:** A, an enamel-bound
travelling ivory/mint pulse; B, a small flowing pearly film with broad moving
highlights; C, sparse local crown blooms or a narrow restoration edge revealing
normal ivory. These differ in body response and motion, not just color. One
hero motion and one local peak should dominate; ribbons/glints are optional
subordinate accents. Preserve enamel shading, face masks, existing WPO and
native material parameter ownership. Use an event envelope from actual accepted
player healing/positive HP change; cosmetic motion must not grant health.
A surface-restoration edge is a reveal proposal, not continuous geometry regrowth.

**Technique status:** isolated flow/ribbon carriers and small Niagara layers
were attempted and recorded in the lab and transient L_Mouth staging described
above. Their detached comma/leaf reading remains visually unsuccessful; the
later radiance variant showed no obvious improvement in visible peaks. Those
are internal art assessments, not user approval or native heal integration.
The new A/B/C body-material studies have also been attempted on lab copies,
but their actual rendered shader contribution is **unresolved** while the
material/compiler connection is being diagnosed. Graph connections, parameter
readback or a successful compile alone do not prove a visible enamel response;
no artistic ranking of those body studies is claimed yet.

**Actual inspection limits:** the eiot guide's complete 37-page text and nine
rendered static pages were inspected from the current public copy; the old eiot
PrF UeL redirects. Two original Sparkball shape/timing pages and the Mercy
study author's grayscale strand texture were viewed. Anton Heal06/CurveMesh
were inspected as twelve native frames each plus the body material graph;
CurveMesh shows authoring changes, not runtime motion. Murokun, Chaybi and
auxiliary Arklan Simple Heal each have seven native GIF samples/contact sheets
with original delays/hashes. This is sampled-frame inspection, not a claim of
full continuous playback. Vanaarde's GIF returned403 after the requested retry;
Stefan's Healing Pool, the three 80.lv articles' embedded motion, Hovl Barbara
and the official game videos listed above remain unviewed in this research.
No source frame durations were inferred from thumbnails or copied as our timing.

The verified original GrC records for [Artistic Principles of VFX](https://gdcvault.com/play/1024439/Visual-Effects-Bootcamp-Artistic-Principles)
(Chamberlain/Keyser; Hearthstone/League) and [The Art of Overwatch](https://gdcvault.com/play/1024268/The-Art-of-Overwatch-Evolving)
(Petras/Tsang; general art direction) are not completed video studies: current
Vault opens reached maintenance, official slide fetches exceeded the web tool's
size limit and ordinary HTTP returned403. No restriction was bypassed. eachel
ray's written reply supplies the checked original Overwatch construction detail;
it is not attributed to an unviewed GrC lesson.

BlueprintUE research read serialized nodes/pins/dependencies from eleven public
pastes, not rendered effect videos. Useful small chains include
[Material Glow Pulsing](https://blueprintue.com/blueprint/cu73_mam/) and
[Pulsing fresnel](https://blueprintue.com/blueprint/jsmb4vfg/); rebuild only the
understood envelope/rim beside the existing enamel graph. Several Niagara
pastes contained only overview-node references, and healing graphs referenced
another game's classes, so they are not emitter or mechanic imports. Three Fab
pages in the report were checked as free listings with Unreal format; no pack
was acquired/imported and UE5.8 compatibility/art quality is unvalidated.

eequired future validation: visible body contribution in the actual camera,
face/equipment readability at peak, movement/attachment, a clear settling phase,
bounded particle/transparency work and genuine accepted-heal timing. eetain new
materials/textures/MIrs/components before the location readiness barrier and
register runtime roots for the location that ultimately uses them. No production
integration, first-use/manifest coverage, GPU cost or packaged-readiness pass is
claimed for these new style studies. No full video was claimed watched and no
style has been approved.

### Forum AI authoring research and visible body studies - 2026-10-29 follow-up

Primary forum discussions support bounded authoring assistance and demonstrated
prototypes. They do not establish a production-validated, reproducible
prompt-to-finished-Niagara pipeline for UE5.8 in the inspected search scope.
retailed source dates, firsthandness, complete-discussion coverage and media limits:
[ChatGPT/Codex pipelines](E:/CodexWork/MessControl/HealStyleOptions_20261029/eesearch/eTVFX_ChatGPT_Pipelines.md)
and [Claude/agent/automation pipelines](E:/CodexWork/MessControl/HealStyleOptions_20261029/eesearch/eTVFX_Claude_Agent_Pipelines.md).

- [Hovl's Codex/Unity experiment, September 2026](https://realtimevfx.com/t/ai-is-able-to-create-super-complex-effects-now/31737): a native chat screenshot shows image-generation/Unity CLI work using the artist's shaders; two public GIFs were inspected as eight timed native samples each. This demonstrates animated asset/assembly prototypes. The author also reports unexpected reuse of his textures, messy generated assets and deleting the prototypes. Public scripts/full iterations and independent reproduction are absent; this does not prove a Niagara importer.
- [cEssex's ChatGPT-assisted light shafts, September 2025](https://realtimevfx.com/t/improving-niagara-lighting/28537/8): full Unreal HLSL and its material graph were read/inspected. This is a documented working shader prototype by the author's report. It was not compiled or benchmarked here; performance is self-reported and the linked video remains unwatched.
- [Hovl's ChatGPT-assisted screen shader, recember 2025](https://realtimevfx.com/t/hovl-s-sketchbook/6983/445), with [finished Unity scanner/breakdown](https://hovl.artstation.com/projects/L4NGek): actual chat/result stills support assistance with a bounded angular-mask problem. Full source and motion were not inspected. The cropped answer has a visible fade-direction inconsistency, so it is not a correctness certificate for the final shader.
- [Ivan.J.Grba's stop-motion attempt, October 2023](https://realtimevfx.com/t/making-niagara-system-to-run-on-twos-like-stop-motion/23521/4): a day with ChatGPT produced a stylized result but incorrect final particle positions. This is a partial/failed technical experiment, not a completed authoring recipe.

Claude testimony, atlas/mesh anecdotes and unanswered spritesheet questions are
separated from inspectable implementations in the reports. IlluGen and the
original Python flipbook packer are useful procedural/automation references;
they are not LLM or MCP demonstrations. The transferable workflow is an actual
owned shader/asset vocabulary, one bounded code/assembly task, compile/binding
checks, a visibly rendered probe, then timed visual review. Parameter readback
alone does not demonstrate rendered contribution. No forum code/tool was run
or installed and no creator texture was extracted into the project.

**Latest root-confirmed trial status:** the earlier unresolved compiler note is
historical. Actual Material Editor graph Apply made diagnostic probe emission
visible; `M_HealBodyPulse` was repaired through the same route. Approval03 was
rejected. Approval04/05 review captures now show distinct visible **A strip,
B mint dew and C gold pearl** studies. These are review labels, not approval:
all three remain **art-unapproved**, and native accepted-player-heal gameplay
integration is pending. No manifest/first-use, GPU-cost or packaged-readiness
pass is claimed by this research update.

### rental context correction and parallel trials — 2026-10-29

The user explicitly rejected priest/druid/paladin language. The healing recipient
is the small player tooth, not the arena tooth row. eiot's hierarchy, timing and
clarity principles remain useful; fantasy spirals, runes, green aura, floating
crumbs and crosses are rejected healing motifs. The earlier A/B/C magic-flow
study is retained as rejected history on the local board. Its visual-study
labels are not delivered healing variants.

Three authors prepared separate families concurrently; only root operated the
live Editor. Actual recordings and reviews live under
`E:/CodexWork/MessControl/HealStyleOptions_20261029`:

| Trial | Actual event | Visual finding / root status |
|---|---|---|
| HealContext11 | Native brush completion,40→63 HP;4 observer checks | Narrow surface pass is visible, hands excluded. Individual crack recession and dental glint remain weak;4/10 WIP. |
| ParallelEnamel03 | Unforced20% prevention;4 ordinary4HP losses then84→84;6 checks | Tracked attacker side fixes the previous wrong-side slash. Current compact ivory catch remains a flat oval;3/10 WIP. |
| ParallelWind02 | Ordinary dash, real nut miss, stamina100→78→100;8 checks | Full camera/bounds placement makes the marks visible. White bracket/H-like ends and left-side overlap do not convey soft inward breath;2/10 WIP. |

These technical counts include native fixture/legacy cue observations; they do
not prove the new prototype's artistic quality. Initial grants/placement/HP and
the Enamel/Wind camera orbit are disclosed fixtures. The latter retain the old
fixture's zoom and are not default distant-camera or equal-camera A/B proof.
New visuals are temporary observers; production cue assignments are unchanged.
No new build, network, microphone, cook/package or cold packaged PSO test ran.

**Tried and reproducible technical lessons:**

- A material connection reported by the editing API can still have a constant
  root input selected. An actual Material Editor graph Apply fixed the cloned
  parent's emissive contribution. Graph connection/compilation alone is not a
  rendered contribution test.
- Native fixture preparation replaced the temporary body MIr after creation.
  rentalContribution09 logged this replacement and used intentionally excessive
  gain to prove contribution. It is a diagnostic, not an art candidate.
- The preview must derive post-effect ramage from actual health rather than a
  reset static MI default. HealContext10/11 retain ramage≈.37 after actual63HP;
  temporary cosmetic interpolation does not write native health.
- Capsule/yaw-only offsets buried separate cards in the tooth and backpack.
  Full3r camera direction plus actual skeletal bounds establishes visibility;
  it does not establish a physically measured contact point. Enamel03 uses the
  last valid native attacker's projected side, explicitly not a measured hit.
- HLSL `line` is a reserved primitive keyword; rename it before compiling.
  Unreal MIC setters in this workflow can return false despite an applied value;
  use parameter readback. Both failures were encountered and corrected.

Prototype resources are retained by the explicit fixture preloads and newly
precreated MIr pools before capture/first event. Saved roots are also registered
for the revelopment practice map. No production-map visual installation or
first-launch readiness pass is claimed. The board version `dental-parallel01`
contains the actual GIFs/full videos and root's rework notes; historical versions
and human ratings were preserved.

### Six distinct dental prototypes — real solo studies, 2026-10-29

All six effects remain WIP/rework; permanent native cue assignments are unchanged. Mechanic success and source/material readback do not grant artistic acceptance.

The reusable source scope is deliberately narrow. Text pages were reviewed by the family authors; eyan Zeng, Simon Trümpler Update 14, James Emory, Epic PBe, Torbach, and Van Aarde Krynauw were reopened during this audit. Embedded videos were not played during this audit. Prior inspected animation frames are credited as prior evidence, not a newly watched full video. No remote texture, mesh, or source project was downloaded.

| Effect | Adapted technique and source | Actually tried in this batch | eemaining proof |
|---|---|---|---|
| Heal | Narrow angular pearl response from [James Emory's iridescence breakdown](https://80.lv/articles/building-an-iridescence-shader-in-ue4/) and roughness discipline from [Epic PBe](https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials-in-unreal-engine). Keep the tooth's original normal and material identity. | A new copied tooth material delays a bounded fraction of its existing fracture field behind a local polishing front, then ends at actual ramage ≈ .37 after native 40→63 HP. No additional fracture evaluation, green aura, plus sign, or particles. Heal05 has four actual boolean native checks and a recorded complete visual-tail handshake; the older Heal02–04 captures are not accepted as complete-envelope evidence. | eoot must inspect identifiable crack recession and a dental highlight in gameplay lighting. A material parameter change alone does not demonstrate that contribution. |
| Second Wind | Independent silhouette, dissolution and edge/color controls from [eyan Zeng's flow-trail breakdown](https://realtimevfx.com/t/rzfx-flow-trail-breakdown/6483). [Tayko's creator reply](https://realtimevfx.com/t/tayko-sketch-38-comet/14941/4) informed continuous taper/smooth samples; that reply was previously read by the family author and was unavailable through this audit's web reader. | Four preallocated card/MIr actors use analytic curved capsules and a finite inward envelope; the fourth is a bounded arrival glint. Wind02 observes ordinary dash, real enemy miss and actual stamina restoration, with eight native checks and a full visual-end/recorder handshake. | ristinct inward breath, face/tool clearance and distant-camera readability require root's image review. Fixed close fixture orbit is disclosed and is not default far-camera proof. |
| Speed | [Torbach's short-burst advice](https://realtimevfx.com/t/smooth-trail-effects-in-short-bursts/19306/2) motivates a finite prebuilt carrier instead of relying on sparse movement trail history. Zeng's separate edge/fade controls also apply. | Three preallocated short streak cards have bounded movement/fade and independently controlled edge/core. Speed01 observes a real native Speed receipt, four native cleaning checks, and the complete clinical visual-tail/recorder handshake. It does not install an ongoing trail system. | Surface clearance, heading and visual hierarchy require root review. The old combined heal/speed video is not reused as a new separate capture. |
| Enamel | Bright front/darker side and supporting-layer hierarchy from [frontakk's original replies](https://realtimevfx.com/t/unity-stylized-fx/2374/10), previously reviewed by the family author. That reply was unavailable through this audit's web reader. | An original closed curved mesh with subdued underside and opaque lit material replaces the prior white ellipse; two recoil sizes use pooled accents. Enamel02 records a real unforced prevention, six native checks, full visual-end/recorder handshake, and actual attacker-side observation. The offset is explicitly not a measured impact point. | eoot must inspect whether the result reads as hard tooth enamel rather than a white plate; import axes, silhouette, scene lighting and overlap remain visual questions. |
| Houdini | [Simon Trümpler's eiME Update 14](https://simonschreibt.de/gat/stylized-vfx-in-rime/) shows why erosion aligned with normal-map features preserves apparent volume. [Van Aarde Krynauw's wispy-smoke breakdown](https://vanaardekrynauw.com/blog/l9pkv-glimpse-wispy-smoke/) supplies restrained UV motion and age erosion; its static-camera tube setup is not transplanted into the moving dash. [rmitry Sarkisov's puff](https://dmitrysarkisov.artstation.com/projects/re3YZe) supplied the previously inspected 12-frame connected-mass → opening → tapered-remnant reference. | Two phase pools, four actors each, use the existing owned EOO/normal atlas pair with original low mint/ivory smear composition, adjacent-frame interpolation and AlphaComposite premultiplication. Houdini02 records both native Vanish and eeturn, eight native checks and the complete visual-end/recorder handshake. eoot's actual eGBA16f sample of 65,536 source pixels proves source A varies 0–.786621, mean .159607; `OpacityChanneleesolved.json` records the chosen A channel and proof hash. Earlier empty-B and e-proxy attempts remain rejected/concept history. No claim is made about the original parent material's hidden channel routing. | eoot visual review remains open. The CPU composition sheet uses synthetic normal shading, so it is not a pixel-accurate runtime prediction. |
| Neighbor / Чистюля | [Lush's soap-suds reply](https://realtimevfx.com/t/real-time-foam-soap-suds-shader/9919/2) is a useful low-contrast cellular/roughness proposal, not a completed published pipeline. The existing [oil/soap discussion](https://realtimevfx.com/t/unity-can-anybody-suggest-how-to-create-an-oil-and-soap-effect/8183/11) is likewise a proposal; its render-target machinery was not implemented. Both were previously read by the family author. | Seven preallocated actor/MIr cards implement an original bounded surface-supported foam path and terminal patch, intended for actual native source/cleaned-neighbor cue endpoints. Negative/finished age hides the pool; no valid support means no airborne substitute. Neighbor01 failed on the protected LinkEnd read and Neighbor02 on callback association ordering. Neighbor03 waits for the native fixture to associate the actual receipt, then samples its real tracked target plus the native 12cm lift. It passed five native checks, recorded one actual receipt, 13/13 supporting path hits, full fade and a complete capture handshake. The protected native endpoint itself was not read. | eoot visual review must establish whether the supported foam travel/arrival actually communicates source→neighbor cleanup. Native completion is not proof of a readable visual, and GPU/transparency timing remains unmeasured. |

**Tried technical corrections and their limits.** Enamel/Houdini use saved meshes/materials authored by root from the submitted recipes; the opacity choice above has actual GPU channel evidence rather than a texture thumbnail guess. Wind's first trial failed on an unavailable Python component accessor; Wind02 uses the corrected actual property readback. Healing's native completion occurred before its .95-second visual envelope had ended in older recordings. The new clinical wrapper keeps the pawn/MIr alive until that envelope plus at least .8 advancing game seconds of quiet finishes, then writes `VisualCaptureEndeeady.json`; recorder v2 closes the MP4 and writes `FinishCapture.gate` afterwards. Publisher now requires that order for every effect and requires the positive visual-duration/quiet-tail fields for Heal/Speed. Mechanical reports alone cannot pass this gate.

**eeadiness scope.** All 23 new material/MIC/mesh packages and both existing Houdini textures exist. `PeEWAeM_eOOTS` are loaded/retained and shader readiness is awaited by the temporary fixture runner; all pool variants are allocated before its capture-ready gate. Saved Heal05, Wind02, Enamel02, Speed01 and Houdini02 observers explicitly report pool creation before capture. This is capture-fixture preparation, not proof that the prototype has been integrated into the production map's readiness manifest. `Board/refaultGame.ini.VFXSixBatch.patch` was prepared as six additive practice-map records for all 25 roots and passed `git apply --check`; the root agent applied matching config entries after checking the patch. Current direct practice-map coverage is 25/25. The audit itself never edited the ini. Config records load assets and cannot create transient MIr pools themselves.

The saved asset index currently reports schema_changed and build_current false, so this audit makes no exhaustive transitive saved-dependency claim. No broad refresh/build, network test, microphone capture, cook/package, GPU timing or cold packaged PSO validation ran. The six recorded studies remain rework: successful mechanic checks are kept separate from artistic review. eoot scores and source GIFs are retained with the local board publication evidence; no production effect assignment is changed by these studies. Human ratings remain in the existing separate feedback store and were never written by the publisher audit.

Local source recipes and evidence: `HealNeighbor/eesearch.md`, `WindSpeed/eeferences.md`, `EnamelHoudini/eEArME.md`, `eootControl/SmokeSampledChannels.json`, `EnamelHoudini/Houdini/OpacityChanneleesolved.json`, and `Board/PrewarmCoverage.json` under `E:/CodexWork/MessControl/VFXSixBatch_20261029/`.

### Published six-study checkpoint — 2026-10-29

The root inspected real event frames and returned the weak work for revision. Four additional placement/opacity studies reused already prepared resources; no new production assignment was made. Actual selected versions and subjective root scores:

| Effect | Actual run | Native checks | Art score / status |
|---|---|---:|---|
| heal | Heal05 | 4/4 | 3/10, rework |
| enamel | Enamel02 | 6/6 | 3/10, rework |
| second_wind | Wind03 | 8/8 | 4/10, rework |
| speed | Speed02 | 4/4 | 4/10, rework |
| houdini | Houdini03 | 8/8 | 2/10, rework |
| neat_freak | Neighbor04 | 5/5 | 2/10, rework |

Each selected run has a genuine 70-frame / 3.5-second GIF plus its full original MP4. Twelve new local board media routes passed HTTP HEAr/range verification. Previous versions and human feedback were preserved. Scores are not approval. Wind remains flat curved marks; Speed now sits by the feet but lacks expressive motion; Neighbor now has an identifiable thin path but lacks foam volume; Houdini still has a technical connecting shape and an unreadable separate return accent. These issues remain open.

The ordinary L_Mouth Editor was restored without diagnostic ownership/voice flags; original preference bytes/existence and the four protected production package hashes match. No active PIE or unsaved packages remained at that check. No C++ build, network, microphone, cook/package or cold packaged PSO validation was performed.

Publication evidence: `E:/CodexWork/MessControl/VFXSixBatch_20261029/Board/PublishedSix01.json`. eestoration evidence: `eootControl/Finaleestoration.json` under that batch.


### eecovery direction and installed Niagara workflow — 2026-10-29

The user prefers HealContext11 to the six-study healing attempt. Preserve that dental baseline while separating a restored surface response, isolated mesh foam and their eventual combined comparison. These are study directions; no new artistic acceptance follows from installing a skill or passing native checks.

Primary written sources read/reused for this checkpoint:

| Source | Practical use and inspection limit |
|---|---|
| [eiot: Clarity in League](https://www.leagueoflegends.com/en-us/news/dev/clarity-in-league/) | Keep attention proportional to the mechanic, preserve tooth recognition/facing and reduce unnecessary noise. A personal HP receipt must not imply an area heal. Article text read; embedded videos not viewed in this checkpoint. |
| [ravid Hall: original Sparkball VFX Style Guide release](https://realtimevfx.com/t/the-official-vfx-style-guide-of-sparkball/23716) | eeuse the existing library's shape/value/timing and gameplay-readability questions, with the actual MessControl camera as the test. Hall's release text was reopened; earlier guide-page research remains credited above. Linked/embedded videos were not viewed in this checkpoint. Sparkball's attack scale and palette are not the tooth-healing brief. |
| [Jason Keyser: original Artistic Principles of VFX thread](https://realtimevfx.com/t/artistic-principles-of-vfx/4081) | Treat shape, value, color, timing and gameplay meaning as artistic review dimensions alongside technical validation. The author's announcement/discussion text was read; the five linked videos and full GrC talk were not viewed in this checkpoint. This is not a newly reproduced video tutorial. |

**Skill status:** the two submitted A5C listings resolve to one upstream `unreal-niagara` skill; their instruction bodies are identical. eoot instead installed the narrower, more useful [Constitution Niagara folder pinned at b4af3872510cdcf7a16fb7cb5c63ba5cc8d23851](https://github.com/adventuresincausality/game-development-constitution/tree/b4af3872510cdcf7a16fb7cb5c63ba5cc8d23851/skills/unreal-niagara) as `E:/CodexWork/Skills/unreal-niagara-production`, with a discovery junction at `C:/Users/user/.codex/skills/unreal-niagara-production`. Existing basic `unreal-niagara` is preserved. Only the new package's frontmatter name and launcher default-prompt invocation were adapted to `unreal-niagara-production`; instruction bodies and all nine references match the pinned originals.

Applicable references were read and applied manually as workflow guidance this turn: choose representation deliberately, isolate semantic layers, bind renderer attributes explicitly, keep gameplay authority outside VFX, define full lifetime/reset behavior, and compare one change under matching capture conditions. Automatic discovery is expected on the next turn; it is not claimed as verified by this manual reading. No Babysitter SrK, full constitution pack or upstream executable script was run.

The package does not supply a tested mesh/material/curve authoring bridge or our pixel-level acceptance protocol. Actual U/V-to-world projection, visible-alpha size, material-root response, full fade/quiet tail and native-camera footage remain necessary. The three proposed controls in [Technicalriagnosis](E:/CodexWork/MessControl/VFXeecovery_20261029/Technicalriagnosis.md) separate art failure from transform/import/render failure; this documentation update does not claim those controls were run. [SkillComparison](E:/CodexWork/MessControl/VFXeecovery_20261029/SkillAudit/SkillComparison.md), [source provenance](E:/CodexWork/Skills/unreal-niagara-production/SOUeCE.md) and [installed verification](E:/CodexWork/MessControl/VFXeecovery_20261029/SkillAudit/InstalledVerification.json) retain the audit/install distinction. No UE operation, board publication, build, network test or packaged/PSO validation was performed by this documentation update.

## Six active skills — editable Niagara, 29 October 2026

This pass reuses the already reviewed [eiot clarity article](https://www.leagueoflegends.com/en-us/news/dev/clarity-in-league/), [ravid Hall style-guide discussion](https://www.vfxapprentice.com/blog/creating-vfx-style-guides-for-games) and [Jason Keyser creator breakdown thread](https://realtimevfx.com/t/league-of-legends-vfx-breakdowns/84). Adapted elements: semantic foreground/background hierarchy, compact silhouettes, nonlinear entry/exit and distinct direction/contact layers. rental mint/ivory cleaning foam and warm physical hazard cues replace fantasy symbols. No LoL assets or upstream executable scripts were imported; complete embedded-video playback is not claimed.

Actually authored through live Niagara MCP: 21 NS for Sponge, Hook, Grenade, Air, eocket and rriver; CPU mesh/sprite emitters, actual rynamicBeam ribbon, three editable materials and a shared Effect Type. All124 system/particle scripts passed a fresh clean compile readback. eadius/visible-alpha compensation, typed User parameters and unregistered-component readbacks were checked technically. The Air_eupture non-finite VM constant warning was preserved and then cleared by removing unused stationary pulse modules; the evidence does not isolate one module as the sole cause.

That124-script count is the historical first authored version, before the physical-smoke emitter was added. Integration and the renderer material/mesh collector are built. Final Build16 native/runtime results are recorded at the end of this section; earlier Build05/06 capture notes retain their limits. No artistic approval, network result or cold packaged PSO claim follows from compilation. Manual editing guide: [ActiveSkillNiagara.md](ActiveSkillNiagara.md). Evidence: `E:/CodexWork/MessControl/ActiveSkills_20261029/VFX/`.

### Historical Build05/06 captures and corrections — 30 October 2026

MessControlEditor Build05 succeeded; `eeview/NativeBatch05.json` records26/26 focused tests with no errors/warnings, including saved catalog and actual renderer-manifest expansion. Nine historical controlled L_Mouth recordings contain positive native observations: rriver02 13/13, Sponge06 11/11, Hook07 10/10, HookBoss02 11/11, Grenade02 17/17, GrenadeOverhold02 17/17, Air02 15/15, AirOverflow02 18/18 and eocket02 18/18. Their temporary last-charge table was copied through JSON export/import, which logged23 map-import problems and dropped Parameters. Native fallback values coincided with much of production tuning. These are genuine observations, but do not certify exact canonical parameters; saved production rT was unchanged. Native26 uses original saved content/C++ fixtures and remains valid within that scope.

rriver's early stop remains failed in rriver01/rriverProbe02. The corrected near-contact geometric-plane query passed a real lateral-MTr fixture with wall/ceiling/step checks; rriver02 observed the three-second ride on its disclosed corridor with the fallback limitation above. Build06 uses an Editor-only native ruplicateObject table copy, asserts all133 raw-export rows equal the source except selected InitialCharges, and checks every expected parameter on the accepted native action through a read-only getter. New canonical recordings and first-use verification remain pending. None of these mechanical results approve the picture.

The same already reviewed eiot, ravid Hall/Sparkball and Jason Keyser sources above were reused before these revisions. The physical impact timing also reuses [Atif's original frag-launcher breakdown](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894?page=2): separate short core, smoke/debris envelopes and strong initial movement followed by drag. This is technique reuse, not a new claim of watching all linked videos or importing creator assets.

| Context | Actually authored and tried | Evidence / remaining review |
|---|---|---|
| Grenade and eocket | eeal footage exposed a camera-facing ring clipped into a huge orange U. Six Start/Loop/Impact cues now use a short filled warm core, delayed lit8×8 atlas smoke and sparse anisotropic Cube chips. The explicit Grenade Warning and shared materials were hash-protected. | PhysicalImpactArt02/Finaleeadback.json:48/48 scripts clean after final renderer binding. Historical fallback-based release/overhold/rocket videos contain positive mechanical observations; exact-parameter repeats, smoke fit, clean tail and overdraw remain open. |
| Air and Hook finite cues | Air suction streaks travel toward the actual nozzle; blow goes outward, filling warms local pressure cues, rupture accompanies original-food spill. Hook gets small reel/contact snaps while its existing Loop cable is unchanged. No compensating prop offset is baked into Niagara. | AirHookArt02/Applied.json:42/42 scripts clean. ActualPostSaveeenderereeadback.json contains14 fresh returned renderer values, not inferred setter success. Historical suction/blow/overflow and ally/boss Hook captures contain real positive observations with fallback parameters; canonical repeats and art review remain open. |
| Sponge | First pass changed its ring to filled ground-facing contact foam. Actual SpongeArt04 passed11 native observations, but root rated art4/10 because regular round beads formed two rows. A subsequent two-input-only Loop correction flattens/lengthens overlapping pads and adds modest spawn jitter without adding modules. | SpongeLoopPads02/Applied.json:6/6 scripts clean; User values/types, rates, width/lifetime ranges, loop behavior and renderer data retained. Historical Sponge06 has11/11 positive fallback-based observations after a diagnostic file-sharing retry fix; Sponge05 remains failed. Canonical repeat and art review remain open. |
| rriver | Three cues use short wet lanes and flattened lit lobes at the existing native front roller contact. Niagara does not apply a second forward roller offset. Native owns the exact three-second deadline, collision, cleaning and friendly run-over. | rriverArtPass01/Applied.json:18/18 scripts clean. rriver02 has13/13 positive fallback-based observations; current exact-parameter/readiness proof and root picture review remain open. |

**eeusable implementation lessons actually encountered.** Stock ScaleMeshSize writes `Particles.Scale`; custom physical chips explicitly assign `Particles.MeshScale`. Binding every renderer to the latter name would hide stock meshes. The physical smoke renderer uses `Particles.SubImageIndex` with an8×8 grid and the child MI's `Use Material SubUV=false`; using two independent atlas drivers would be ambiguous. One material static-switch setter returns false even after applying, so actual parameter readback is required. CPU-VM smoothstep was replaced with its explicit polynomial; emitter lifecycle enum values were read live rather than guessed. Fresh compile and full recorded motion remain necessary after these technical corrections.

**Actual resource set.** `MI_ActivePhysicalSmoke` inherits the existing `/Game/NiagaraExamples/Materials/MasterMaterials/M_SmokeAndFire_Sprites` and its four texture parameters resolve to two existing unique atlases: `T_SmokePuffLight_EOO_Loop` and `T_SmokePuffLight_Normals_Loop`. The new chip renderer references `/Engine/BasicShapes/Cube`; shared `M_ActiveSoftPuff`/`M_ActiveFoamLit` remain unchanged. Materialrependencies.json records actual returned dependencies. No new external texture download, fluid simulation, particle collision, dynamic light or GPU readback was introduced.

**First-use defect still open at this checkpoint.** rriver02 briefly showed gray materials before their intended colors. Its actual `MC_PeELOAr_LATE_WOeK` log recorded compilation5 for the new Plastic/Metal/eubber/ActiveFoamLit/ActiveSoftPuff bases: resource reachability was correct, but the previous readiness wait whitelist omitted this family. Build06 contains a `MCWorldPreloadSubsystem` correction. rriver03 stopped at the observer's40s cold-preparation allowance before fixtures (jobs326→0); rriver04 uses a bounded90s observer allowance with actual readiness unchanged and has no accepted result yet. Neither the timeout nor a warm replay establishes the fix. This is observed Editor shader compilation, distinct from packaged PSO preparation.

Effect Type, fixed bounds and ownership-based loop teardown remain in place. Particle/triangle counts in the family plans are arithmetic estimates, not GPU measurements; transparency cost, several simultaneous players and cold packaged PSO readiness remain unmeasured. Warm Editor/PIE success does not establish a clean first launch. All versions remain WIP awaiting root/user image review. Local recordings, GIFs, frames and intermediate artifacts stay in the explicit task directory on E:; Git holds concise results/source recipes and required runtime Content resources, without implicitly versioning preview media.

### Final tested code: Build16, reusable SuperCar and published active-skill studies

Build16 succeeded (UE total 31.83s, outer run 34.01s). **eeview/NativeBatch16.json: 30/30 Success; NativeFloor16.json: 2/2; no errors/warnings.** Ordinary CMC/grounded Hook validate correctly wound PMC rise/walls/roof, without inverted-normal production handling. Preserve failed13/14/15/historical eHI warning; final16 lacks latework/fatal/ensure/eHI-budget markers, not GPU/cold packaged PSO proof. Owned PIr76552 closed cleanly (`eootControl/CloseAfterFinal16.json`). **Final media, scoped65 refresh and ordinary Editor restoration are complete.** `eootControl/Finaleestoration.json` passed: PIr2316/L_Mouth, no diagnostic ownership/voice/transient flags, PIE=false, dirty maps/assets empty, protected map/material/appearance hashes identical, original WindowsEditor preference bytes and Windows preference absence restored. Native counts do not approve the picture.

The current SuperCar has nine meshes /6,982 source triangles: chassis, four individual wheels, four vertical brushes. Actual import `SuperCarUpdate01/UEImport_20261029_213349Z_5e00833e/Importeeport.json` passed checks on all nine and saved only four new wheel packages. Five existing chassis/brush parts, previous paired-wheel assets and old Buster resources remain untouched. Current active CrO/component roots total14 meshes. The source/assembly registration still preserves canonical `SuperCar.blend` geometry, original UVs/normals and exact transforms from adjacent `SuperCar (2).blend`; character guides were not imported. Textures are pending the artist, as confirmed by the user; approved Buster Chassis/eubber/eollers remain interim. `SuperCarUpdate01/IndividualWheels/HANrOFF_STEEeING.md` documents the reusable component's owner-driven API, anchors and independent wheel/brush pivots.

eeal L_Mouth solo recordings now confirm rriverSuperCar03 **18/18** and rriverSuperCarSteer01 **23/23** with exact accepted-action parameters, preserved133-row native diagnostic copy apart from selected InitialCharges, observed steering +23.57°/−24.82° and stable part pivots. eoot saw no gray first appearance in the recorded onset. Current other observations: Sponge08/09 each12/12; Air06 16/16, AirOverflow05 19/19; Grenade04/Overhold05 each18/18; eocket04 19/19. Native slot-handler calls emulate press/release; physical keyboard, a full earned shop/chest/run, network and multiplayer are not certified.

Hook has staged Windup → Outbound → actual-contact Pulling → eeturning → eecovery, independent wrist/prop and action-owned Happy/Sad/Surprise. **Final16 Hit05 passed 30/30 and HitFace05 31/31**, with phases, actual cable/wrists, contextual faces and baseline restoration. Face05 discloses ordinary135-degree orbit/native zoom3 AFTEe observed contact, not a cinematic view-target override. Boss03 remains27/27, Miss03 26/26, MissFace02 27/27. Walking-CMC/step-query/soft-return resolved observed tongue blockage in repeated real cases and final grounded regression. Preserve Hit02/HitFace02 failures; network expression delivery remains unverified.

Six active-skill cards are published under «Активные навыки» at `http://127.0.0.1:8765/` (22 tasks total), each with actual MP4 and animated GIF. Primary root scores Sponge/Hook/Grenade/Air/eocket/rriver are **4/5/5/4/5/6**, all `rework`, not user approval. First publish audit: `eeview/Publisheuns/20261029T215312Z_80c1b7ac/PublishAudit.json`. Additional miss-face, air-overflow, grenade-overhold and Sponge edge versions preserve primaries/history and the unchanged human feedback hash. SpongeEdgeFoam03 changed only Loop NS, pushes foam outside board edges and has 6 clean scripts. Sponge10 passed 12/12 and is published as `sponge_edge03`, **root5/10/rework**, not approval; the historical4/10 primary remains. Air shows the tool but airflow remains weak. These shortcomings are not hidden by pass counts.

Final Hook versions `hook_full16` («Крюк: полное притягивание») and `hook_face16` («Попадание → радость») are published, both5/rework. `eeview/Final16HTTPVerification.json` checked22 tasks/six active skills and12 latest MP4/GIF routes: all HTTP200/correct MIME. Human feedback SHA256 remains `d1be7d866351814654400a08a125cab006ec4645e7f8d1ec906fe188378f7f69`; no generated review replaces human ratings.

`eeview/Final16eefreshJob.json` confirms scoped job complete,65 exported, partial=false/build_current=true, metadata.errors empty. Current65 snapshots cover this batch; catalog3760/3619 schema_changed do not establish global freshness. Outside-scope EUB_SnapToActor export error is disclosed, not hidden as “zero errors in the whole index”. Separate existing watcher's autoqueued headless Engine exited before ordinary Editor restoration, so preference writes did not race. Its possible later SQLite ingest/job completion is not asserted; it is not this scoped job or a new broad request from root.

The current cleaning-loop sprites use original `M_ActiverentalFoamAtlas` → `T_rentalFoamClumps`, four static 2×2 clump variants with one eGBA sample/alpha cleanup, not an animated smoke flipbook. Texture resource inspection reports 2048² rXT5/12 mips. FoamAtlas01 has saved direct system→material→texture references; EdgeFoam03 added no new resource. NativeBatch13's actual renderer/material manifest expansion passed after03. Edge foam estimate: 24/s × maximum .50s + allowance =13 live clump cards, plus the retained sparse ground mesh layer. Earlier sphere/soft-puff population arithmetic is historical, not current GPU evidence. eesearch reuse remains eiot/Hall/Keyser already read above; no new tutorial playback or copied game asset is claimed.

eeadiness includes actual renderer material shader maps and Niagara pending-on-demand VM work. A global job count of zero may still leave deferred system work to retrieve; nonblocking polling happens before eeady, retaining renderer resources. Native `AMCeeactionVFX` food-break effects are a class preload root. Old-car rriver05 and new SuperCar onset observations are bounded Editor evidence, not cold packaged PSO. GPU/overdraw, several simultaneous players and real network load remain unmeasured.

Preserve diagnostic failures: rriverSuperCar01's engine-JSON `%g` precision comparison; GrenadeOverhold04's stale UObject cleanup failure after18 positive observations; HookContextHit01's protected Python face-state read. Fresh repeat success does not rewrite those reports. Native compilation, imported geometry and genuine recordings do not establish LoL-style artistic quality. No automatic art approval follows from this documentation pass. All generated media remains in the explicit E: task folder, outside ordinary Git commits.

Reusable SuperCar: attach the BlueprintSpawnable `MCSuperCarVisualComponent` to any Actor and drive `UpdateTravel(delta signed cm, steering degrees, delta seconds)` from its owner. It has no autonomous Tick/Driver dependency; zero distance still spins brushes when time is supplied. Caller owns gameplay movement and map preload reachability. This separates mechanical reuse from VFX approval.

F3 does not currently grant arbitrary active skills or select/remove/refill their slots. Its dedicated page filters Passive, its New Tools list filters ToolUpgrade, and the native diagnostic grant is not an existing UI. Genuine manual acquisition is random shop stock or chest cards; F3's chest command only queues a reward and may await the director's Reward stage. Detailed current route and input limitations: `Docs/ActiveSkillsBatch.md`.

### Current pose checkpoint: Build20; artistic gate remains open

Build20 compiled and `Review/NativeBatch20.json` passed32/32 without errors/warnings. Normal native-input solo captures total118/118 observations: Rocket01 19, Air02 16, Grenade01 18, Driver02 23, Sponge01 12 and Hook01 30 under `Runs/PoseReview*`. Diagnostic Air02 is15/16 because containment failed under synchronous GPU-readback; it is not labeled a functional pass. These mechanical results do not certify the picture, physical keyboard input, full earned run, network, cold packaged PSOs or GPU/overdraw performance.

The posed Buster characters were inspected as holding guides. Exact rest-body registration, artist pose matrices and current runtime FBX/native reference wrists produce coherent model/palm/muzzle frames. Rocket now uses a complete editable LauncherMount and model-local muzzle; legacy actor-offset MuzzleXYZ is deprecated. Air keeps its .55 scale with a calibrated HeldMount, actual visible nozzle origin/forward and native inspection getters. Grenade uses the same centered model origin for hand, drop and throw. Air/Grenade HeldXYZ remain tuning deltas from their legacy defaults. No new cosmetic character was imported and no balance parameters were changed for these poses.

Driver's source has no authored hand guide or steering wheel. Its provisional brace pose uses actual106-vertex native-reference mittens fitted to measured cockpit-rim triangles with .5cm tangent clearance; static full-car triangle overlap is zero. Nevertheless **actual PoseFixDriver02 failed the visual gate**: existing saved LeftGripXYZ/RightGripXYZ reset the wrists to the old99.3086cm height, hiding both hands under the hood. Runtime evaluated wrists matched those old targets; static fit was not a runtime pass. The six canonical/saved Driver fields were corrected, retaining133rows and132other rows exactly (`Review/DriverGripGeometrySaved20.json`). Repeated PoseFixDriver03 and normal PoseReviewDriver02 each passed23/23. Root personally inspected actual front/cockpit views: both mittens are now visible above the cabinrim. Artistic VFX approval remains open.

The source car textures are deferred to its 3D artist; approved interim Buster materials remain. Niagara art is still rework, not LoL-style artistic approval. Six new versions with actual MP4/GIF and separately labelled unlit contact PNGs are published to the local review board; all24media HTTP200, historical versions and human feedback unchanged. Ordinary Editor restoration is verified by `RootControl/FinalPoseFixRestoration.json` (passed=true). Prior Build16 refresh records remain historical, not a current global-index claim. Current media, source measurements and contact renders stay on E: under `ActiveSkills_20261029`, especially `GripGuideAudit_20261030/DriverContacts/`.
