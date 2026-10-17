The rolling knight now damages an active arena tooth on a blocking movement
sweep. Previously that contact only triggered the tongue's pain wave. The
contact passes the incoming roll direction and the existing `RollDamage` into
`ReceiveArenaHit`, so ordinary HP, chips, branching cracks, repair and final
tooth loss remain authoritative. The saved profile currently supplies 16 HP.

Each tooth has its own `RollHitGap` (currently 1.1 seconds), independent of
player hit throttling and the tongue wave's global cooldown. Repeated substeps
cannot drain health every frame. The cache clears when an attack starts,
recovers or is cancelled. Only executing rolls damage teeth; warnings,
recovery, lost teeth and teeth consumed for respawn are excluded. The normal
blocking reflection and pain-wave behavior are retained.

Validation:

- Incremental `MessControlEditor Win64 Development` compilation succeeded.
- The focused native test
  `MessControl.CoreLoop.NutBoss.RollCollisionDamagesArenaTeeth` passed. It uses
  actual sphere movement sweeps against tooth bodies and checks warning and
  recovery safety, unrelated teeth, repeated-contact throttling, independent
  tooth cooldowns, later contact and lethal tooth loss. The cube-only fixture
  emits two existing CPU-access warnings; the V10 game meshes have CPU access.
- Short standalone solo PIE passed on an authored V10 tooth. A knight using
  the saved profile, native readiness scheduler, warning and roll struck the
  tooth: 100 to 84 HP, chipped visual and branching cracks. The other 11 teeth
  retained their health. No attack phase or tooth health was fabricated.

The solo fixture pauses directors, removes coffee/food, spawns one knight and
places the observer on the tooth crown as its target. The first placement was
too close to the tongue edge for the warning corridor; that rejected fixture
is retained in `Artifacts/NutRollToothDamage_20261017/Attempt1`. The corrected
approach comes diagonally from inside the tongue. No production movement rules
were changed for that setup. Engine frame-rate/throttling settings were restored.

Evidence, screenshots, compiler log and automation report:
`Artifacts/NutRollToothDamage_20261017`. Unreal MCP tools were unavailable in
this session, so inspection used the editor's loopback Python bridge. The
editor was initially closed; the temporary validation editor was closed after
the check. No game package/cook or multiplayer test was performed.
