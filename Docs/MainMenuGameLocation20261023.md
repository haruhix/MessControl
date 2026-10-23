# Main menu uses the gameplay mouth location — 2026-10-23

`/Game/Maps/L_MainMenu` now contains the saved scenery from `/Game/Maps/L_Mouth`
at its original transforms, scale and material assignments. The original game
map is unchanged. Its 19 visible static actors, 12 tooth placements, tongue,
fixed throat pose, uvula and six lighting/postprocess actors are represented in
the menu. Tooth placements become ordinary static mesh actors. The tongue uses
its saved rest mesh and the throat uses the game's baked fixed aperture.

The game's throat lining, normally constructed in `AMCThroat::BeginPlay`, is
baked separately into `SM_ThroatDepth` under
`/Game/UI/MenuShowcase/GameLocation20261023`, with the same aperture, seven
rings, vertex colors and material settings. This hides the exterior sky through
the aperture without spawning the throat gameplay actor. A local menu fill
light keeps the foreground character readable.

The menu retains its native game mode, UI, camera and new character's 9.125 s
infinite cleaning loop. The cleaner was moved beside the original Tooth 04 and
projected onto the tongue. Obsolete coffee/foam bindings and the previous scene
decorations were removed. Only the cleaner and camera bindings remain in the
menu sequence. The game map's triggers, tooth sockets, directors, rewards,
player starts, navigation and procedural gameplay actors are not copied. All
menu scenery has collision, overlap events, replication and actor ticking off.

All visual resources are reflected references on saved menu components and are
covered by the existing map preload collector. The short solo run reached the
resource barrier, displayed the menu, contained no player pawn or gameplay
actors, and completed a natural animation wrap. No late compilation was logged
during this local check. This is a warm Editor run; cold packaged first-use,
build/cook/package and network tests were not performed.

Recipes:

- `Tools/Unreal/UI/use_game_location_in_main_menu.py`
- `Tools/Unreal/UI/bake_passive_menu_throat_depth.py`
- `Tools/Unreal/UI/check_passive_menu_game_location.py`

Reports, screenshots, original on-disk packages and the saved artist menu before
the swap are under `Saved/MenuGameLocation20261023`. Earlier character placement
coordinates in `Docs/CharacterMenu20261023.md` describe the previous stage.
