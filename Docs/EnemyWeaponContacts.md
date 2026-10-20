# Enemy weapon contacts

Updated 20 October 2026. Current source and runtime checks are authoritative.

Ordinary enemy strikes use `MCEnemyWeaponContact::CanHit`: distance to the
nearest body surface, the equipped weapon's reach, the existing frontal cone,
vertical separation and a visibility trace. Cleaning's 115 cm care reach no
longer limits a normal 180 cm weapon strike. Facing comes from the enemy body,
so an overlapping nearest point cannot become a zero-direction blind spot.
The same guard is used by nut enemies, the knight/wizard, generic bosses and
both mint candies. Frozen legs and crystal-obstruction rules remain separate.

The knight and wizard block character capsules through their query-only root
sphere. Players cannot step onto that body. Airborne entrance/jump phases do
not act as a descending blocker; landing restores blocking. Transformation
remains solid and death removes the obstacle. Roll and charge ignore players
only for the boss's own movement sweep: their existing segment damage and
knockback handle those contacts, while scenery and teeth still block motion.
Physical food and active-ragdoll limbs retain their previous collision policy.

Chainsaw pulses now include the nut-enemy family, with the existing range,
frontal arc, line of sight and per-target cadence. Shield-only damage produces
accepted-hit feedback. Enemy bodies do not trigger the chainsaw's scenery
crash; real walls still do. The disabled buffer also correctly confirms a
shield-only contact without enabling that upgrade.

The saved `SM_WinterCandy` already has one convex collision hull. Unreal's
primitive collision counter excludes convex hulls; no mesh/package change
was retained after checking both counters and reloading the original asset.

Validation details and preview limitations are recorded in
`Artifacts/EnemyHitboxes_20261020/Validation.json`. Checks are local authority
only; multiplayer/ping behavior and a new Windows cook/package require
separate validation.

The final Editor build succeeded. The combined focused runs confirm 26
relevant native checks, including real capsule movement, both boss roles,
attack knockback, walls, death/transformation and weapon contacts. One
additional pre-existing player-combo test retains exactly the same failure
messages as the earlier general regression; this is not an all-project PASS.

Rendered solo PIE used the normal F3 nut event and its one knight. The
character's capsule contacted and slid around the boss without penetration.
Ordinary Pickaxe/Knife input from 140 cm removed shield durability
`500 -> 460 -> 435`, while boss HP remained 1400. The review clip freezes
the landed boss's AI tick and uses explicit player/camera placements to
isolate contacts; it does not demonstrate a full battle. No content packages
were changed and the probe left no unsaved packages.
