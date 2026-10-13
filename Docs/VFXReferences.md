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
