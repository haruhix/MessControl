# Mage summon replacement — 2026-10-16

The Mage's revived ground nuts now use the character and `Summon_walk` action
from the supplied `Summon.blend`. Generated assets live under
`/Game/Gameplay/CoreLoop/NutSummon`: `SK_NutSummon`, its own skeleton,
`Animations/A_NutSummon_Run`, two source textures and two materials. The source
mesh is named `SM_NutWizard` inside the file; it is imported as the summoned
minion, leaving the existing Mage and Knight meshes/animations untouched.

The source blend remains read-only, SHA-256
`a480c7269a07a7fe0e4acd527856f916f9a2e37c7b088bb040d91920948793dc`.
The ARP export checks that mesh/run share a deform hierarchy and bind pose,
with a stationary FBX root. The supplied cycle is 24 frames at 30 Hz (0.8 s).
Packed textures are extracted from the source; its Walnut pixels match the
earlier artist Walnut texture. The new art has independent materials/skeleton.

## Behavior

The existing `AMCNutKamikaze` owns a collision-free skeletal presentation.
Its run animation plays continuously, with play rate smoothed from observed
ground travel relative to a 130 cm/s reference cadence. A stationary minion
stops advancing its run; contact pose holds pause it briefly. The saved movement
speed is **360 cm/s**, up from the previous hardcoded **260 cm/s** (+38.46%).
Tune it in `DA_NutRain → Settings → Boss → Creep Move Speed`.

The Mage's actual summon resolver still consumes eligible ground nuts and
creates the existing killable, nearest-player-seeking enemy. It retains 40 HP,
the five-second fuse, blast warning, damage, collision and early-kill disarming.
The former static walnut/extra eyes are hidden when the new model is available.
The skeletal model inherits the existing hit/fuse/death pulses, with its feet
anchored to the moving floor. The flame follows the animated head and remains
upright. Dedicated servers skip the skeletal presentation and use the existing
authoritative movement, fuse and damage paths.

## Validation and evidence

`Artifacts/MageSummons_20261016` contains the export/import/connection reports,
successful Development Editor module build logs, genuine solo footage and final
`SoloCheck.json`. The bounded fixture uses the native Mage AI cast and actual
primary Pickaxe input. Two real ground food actors are placed in the encounter;
AI readiness, observer position and review framing are fixture controls. No
summon actors, attack phases or damage results are fabricated.

Checks cover two summons replacing consumed ground nuts, visible animated feet,
faster movement, a real weapon kill disarming one minion, the survivor's timed
detonation, and the retained fuse flame/warning. Position samples account for
20 Hz actor movement updates observed at 60 Hz. The initial sampler incorrectly
divided only moving steps by one observation interval; its diagnostic report
is retained, and the final check uses total travel over the whole time window.

`preview.mp4` is silent genuine L_Mouth SceneCapture footage with copied hero
post-process settings: overview during casting, then a moving close-up. The
Mage is hidden only from that close-up capture to avoid occluding the minions.
Capture is 20 Hz, encoded at 30 fps with repeated frames at normal simulation
speed; only captions are added. No generated or interpolated poses are used.

Original source and all previous Knight/Mage packages remain unchanged; the
profile changes only the added summon speed setting. Editor/build restoration
reports record the final map, clean package state and restored temporary settings.
No multiplayer, full-battle balance, GPU profiling or cook/package was performed.
