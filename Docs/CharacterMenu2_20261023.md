# CHARACTER_MENU2 main-menu replacement — 2026-10-23

`MENU | Character Menu 2` replaces the former `MENU | Love` actor in
`/Game/Maps/L_MainMenu`, preserving its actor identity, position, rotation and
scale. The current cleaner, two dancers, camera, lighting and postprocess
remain unchanged. The old procedural observer loop is no longer assigned.

The authored `rig.001` animation from `CHARACTER_MENU2.blend` is baked to
`SK_CharacterMenu2` and `A_CharacterMenu2_Loop` under
`/Game/UI/MenuShowcase/CharacterMenu2_20261023`. The 96-bone body mesh retains
4006 vertices and its face morphs. The `Eyes_Blink` curve is imported with the
bone motion. The original 220 frames at 24 fps form a 9.125 s repeat; source
bone endpoints already match. No animation is appended. Export vertex errors
at four source poses were below 0.000048 cm. The source blend is unchanged.
The new character uses the existing gameplay body and purple bag materials.

The saved menu component plays the clip continuously with collision and
overlap events disabled. Mesh, clip, skeleton and materials are reflected map
dependencies, covered by the normal preload collector. Before packages,
import/integration reports, final solo validation and preview are under
`Saved/CharacterMenu2_20261023`. Export inputs and evidence are under
`ArtSource/UI/CharacterMenu2_20261023`.

Recipes: `export_character_menu2_20261023.py` under `Tools/Blender`; and
`import_character_menu2_20261023.py`, `integrate_character_menu2_20261023.py`,
`check_character_menu2_20261023.py` under `Tools/Unreal/UI`.
No build/cook/package or network test was requested.

Final solo PIE passed a natural loop wrap, authored head movement and blink
(peak 0.9455), with the actual menu/camera and no player pawn. Preload reached
ready with 300 resources and 131 streaming targets in 1.33 s. No late shader
compilation was logged in this warm Editor check. Play preferences restored.

