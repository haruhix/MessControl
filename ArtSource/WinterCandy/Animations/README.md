# Winter candy boss animation source

Source: `D:/PROJECT/GAME/TEETH CONTROL/teeth/candy.blend`.
Only the visible `SM_CandyMan` is exported. The floor and hidden sculpt backup
are excluded. The saved source file is read in a separate Blender process and
is never overwritten.

The artist animated object transforms and shape keys. The exporter bakes those
transforms onto one weighted root bone, with the same pivot and neutral shape,
and preserves the `scream` morph curve alongside `Scream_Atack`. Both geometry
and root animation use centimetres. Rest dimensions are approximately
133.745 x 225.748 x 321.167 cm; the runtime actor has unit scale.

| Source action | Unreal sequence | Gameplay alignment |
| --- | --- | --- |
| idle | A_WinterCandy_Idle | 2-second loop |
| Atack_MiniCandy | A_WinterCandy_MiniCandy | Release at frame 13 / 24; 3-second arc to the warm zone |
| Floor_Hit | A_WinterCandy_FloorHit | Impact at frame 18 / 24; floor wave travels for 2.8 seconds |
| Scream_Atack + scream | A_WinterCandy_Scream | 2-second target warning; morph peak at animation time 0.5 coincides with AOE impact |

Gameplay is implemented by `AMCIceEvent`. Phase two starts at 50% boss health.
Individual cooldowns, measured from attack start, are 16 seconds (MiniCandy),
18 seconds (FloorHit), and 24 seconds (Scream). The shared recovery gate waits
until the cast, projectile flight, or wave ends, then adds 5 seconds in phase
one or 4 seconds in phase two. The oldest eligible attack is selected, so
actual intervals may be longer than the individual cooldown.

Arrival faces the nearest living participant immediately. Between attacks the
boss smoothly follows that player; targeted casts temporarily face their target
zone. The server-selected facing player is replicated to clients.

To rebuild assets, run `ArtSource/WinterCandy/export_candy_animations.py` with
Blender on the saved source, then run `Tools/Unreal/import_candy_animations.py`
inside Unreal Editor with PIE stopped. The import checks bone-motion units
and the paired scream morph before saving the assets under
`/Game/Gameplay/Cold/Candy/Animations`.
