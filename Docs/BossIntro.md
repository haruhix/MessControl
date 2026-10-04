# Zombie boss presentation

Bosses and the intro remain opt-in F3 tools. No map actor or normal encounter is
enabled by this change.

F3 → **Босс — интро / рёв [5 секунд]** places the test Zombie on a free Boss
NavMesh point near the centre of the tongue. It runs the five-second Roar clip
and `/Game/Gameplay/Boss/Sequences/LS_ZombieIntro`. The real Sequencer camera
track moves from a three-quarter view toward the face. `AMCBossIntro` binds a
local CineCamera to the saved sequence's `MC_IntroCamera` possessable and offsets
the authored camera coordinates by the boss's floor position and facing.

`UMCBossHealthWidget` displays black cinematic bars during playback. The player
controller hides its normal HUD and menus, cancels held actions and blocks
movement/look. Natural finish, stop, boss removal, death or pawn replacement
restores the prior camera and HUD/input. The server returns the test boss to
idle after the clip; AI/combat still requires the separate F3 button.

During active combat the same native UMG widget displays **Гнилой страж** and a
long red HP bar at bottom centre. Gold remaining behind a hit briefly holds and
then catches up. It reads the replicated `FMCBossRuntimeState` every 100 ms, does
not deal damage and does not show a health bar during animation previews. Death
holds the empty bar for two seconds. Invalid/removed actors hide it.

Combat uses a separate torso capsule (85 cm radius, 100 cm half-height), while
the 60/110 cm movement capsule remains the navigation shape. Knife and pickaxe
measure reach to this capsule's surface and confirm a hit only after accepted
damage. Slot 3 deals 25 HP; slot 2 deals 40 HP. Animation previews reject damage.

`UMCBossFaceComponent` drives cached blink, squint, brow, pain and roar morphs
from the replicated snapshot. Pain remains visible during attack clips. Its
cosmetic tick is disabled on dedicated servers. Eight owned clips keep the arms
forward; `AN_Zombie_Roar` has no root motion. Intro lighting is a temporary local
fill and camera exposure adjustment. A dedicated roar sound is not assigned.

`Tools/Unreal/author_zombie_intro.py` rebuilds only the owned sequence. Its
temporary editor camera is transient and removed; the script never saves a map
or creates a boss. The original artist meshes, player skeleton and Blueprint HUD
are not changed.
