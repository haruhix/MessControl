# Main menu character replacement — 2026-10-23

The saved `/Game/Maps/L_MainMenu` uses the character, facial performance and
deforming MegaBrush from the artist's
`D:/PROJECT/GAME/TEETH CONTROL/teeth/MENU/CHARACTER_MENU.blend`.
The original Blender file and open Blender sessions were not changed.

The private runtime assets live under `/Game/UI/MenuShowcase/CharacterMenu20261023`:

- `SK_CharacterMenu`, with the character and brush in one skeletal mesh;
- `SK_CharacterMenu_Skeleton`, independent of the gameplay skeleton;
- `A_CharacterMenu_Cleaning_Loop`, 24 fps, 9.125 seconds, root motion disabled.

The 220 source frames retain their timing. An approximately one-degree endpoint
discrepancy is eased out over the existing final eight frames. The final bone
pose and facial values match the first frame exactly. There is no appended hold.
The Blender bake matched sampled artist vertices to within 0.000087 cm.
Existing menu body/bag materials and the gameplay MegaBrush material are reused.

The existing cleaner actor keeps its Sequencer binding. Its saved single-node
animation is also configured to play and loop. `LS_MainMenuCleaningV2` now spans
219 frame intervals and its LevelSequenceActor autoplays indefinitely. The other
sequence tracks were retimed to the same period. The previous separate brush
and its attached foam are hidden; their animation no longer drives the scene.
The private AnatomyV2 preview map and its sequence are unchanged.

The cleaner was placed at `(-650, 250, 3.525917)`, yaw 120 degrees, unit scale.
The foot contact decals were projected onto the tongue geometry. Current artist
lighting and camera edits in the open target map were preserved when saving.
The mesh, animation and materials have reflected references on the saved menu
component, and the sequence references the clip, so the existing map preload
collector can discover them before presentation.

Recipes are in `Tools/Blender/export_character_menu_20261023.py` and
`Tools/Unreal/UI/{import,integrate,check}_character_menu_20261023.py`.
Derived FBX and export evidence are in `ArtSource/UI/CharacterMenu20261023`.
Import, integration, solo validation, screenshots and backups are in
`Artifacts/CharacterMenu20261023`. Backups include the on-disk map before saving
the open artist edits, a saved copy of those edits before replacement, and the
previous live sequence. They are local recovery evidence.

The short solo check covers saved binding resolution, menu/resource readiness,
body/brush motion, blinking and two natural sequence wraps. See `Validation.json`
for the actual result. Packaging/cook, a cold packaged first launch and network
tests are outside this task.
