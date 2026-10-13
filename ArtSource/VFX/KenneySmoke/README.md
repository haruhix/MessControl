# Kenney snow powder source

Selected original `PNG/White puff/whitePuff00.png`, `whitePuff12.png` and
`whitePuff24.png` from the user-provided `kenney_smoke-particles.zip`.
Original pixels are preserved.

- Author: Kenney Vleugels, https://kenney.nl/assets/smoke-particles
- License: CC0; original license is kept in `license.txt`.
- Archive SHA256: `97A1D09C66E4FD6C247C8EA87F84C0CC59CAAECEAE19414C995AFB1616A1E1C9`
- PNG SHA256: `1A62910A4C91177DE2720432C5F86EC19211DB6DB6CECE29AD91FB78D5869`

These are standalone puff variations, not a sequence of animation frames.
Niagara motion and a lifetime-driven material provide growth and dissolution.

Imported textures: `/Game/Gameplay/Cold/VFX/T_KenneyWhitePuff00`,
`T_KenneyWhitePuff12`, `T_KenneyWhitePuff24`.
Owned material: `/Game/Gameplay/Cold/VFX/M_IcePhasePowder`.
Layer instances: `MI_IcePowderLow`, `MI_IcePowderBody`, `MI_IcePowderHigh`.
Phase-front system: `/Game/Gameplay/Cold/VFX/NS_IcePhasePowder`.

Authoring:

- `Tools/Unreal/author_kenney_snow_powder_mcp.py` creates the original base.
- `Tools/Unreal/enhance_kenney_snow_volume_materials.py` adds directional
  lighting, rounded normals, texture variants and three layer instances.
- `Tools/Unreal/author_ice_powder_volume_system.py` creates the separate
  lightweight powder system with 3D spawn spread and independent rotation.

The material is used by the ice boss outward phase front and its central
release burst. The returning frost front emits no powder clouds.
Three staggered particle layers give the outward front depth;
rounded lighting normals give each sprite relief. Settled wind and the boss's
attacks keep their materials.
