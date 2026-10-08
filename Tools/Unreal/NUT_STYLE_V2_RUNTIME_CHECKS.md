# Nut Style v2 — targeted runtime checks

Completed on 2026-10-08: the requested `MessControlEditor` build succeeded;
all 19 clips were imported onto the existing two skeletons and connected in the
saved `DA_NutRain`. [Integration report](../../Saved/Checks/NutStyleV2Integration.json)
confirms preserved encounter tuning, original art packages and skeleton reference
poses. Root motion is disabled with reference-pose root lock.

The current Wizard source is `NutWizard_Style_v2_Game.blend`. At the imported
base release pose, palm-to-preserved-emitter distance is 0.075 cm for Cast,
0.408 cm for HeavyCast and 3.81 cm for Summon/Rain. These measurements precede
native aim/release overlays and palm IK; they do not prove the full aimed pose.

All three native automation tests passed and the headless process exited with
status 0: `ChargePhasesAndServerClock`, `OptionalChargeFallback` and
`MageMeleePreservesShove` under `MessControl.CoreLoop.NutBoss.Animation`.
The [final test log](../../Saved/Logs/NutStyleV2TestsFinal.log) records the result.
These tests exercise animation snapshots with transient fixtures, including
phase timing, legacy fallback and the melee overlay weights.

The combat, arena-contact and multiplayer checks below have not been run.
Use saved DA_NutRain values when comparing time, distance and damage; C++ defaults
are not proof of the profile's current tuning.

## Package acceptance

- `Saved/Checks/NutStyleV2Import.json` passes for exactly 10 Tank and 9 Wizard
  AnimSequences on the existing two skeletons. Legacy package hashes and skeleton
  reference transforms remain equal. Root motion is disabled and root lock is
  `REF_POSE`; constant root tracks remain present.
- After connection, `NutStyleV2Integration.json` records all 19 references as
  applied. Every other boss setting and every encounter setting is preserved.
- Cast/Summon/Rain release palm measurements are reviewed together with the
  preserved emitter offset. An imported base-pose distance is not the final
  aimed hand position: runtime applies the existing palm IK afterwards.

## Charge phase and obstacle checks

Run one full straight charge and one charge interrupted by a blocking obstacle.
Keep one player in the corridor and one outside it.

1. Telegraph plays `ChargeTell` once, from start to end over `ChargeWindup`.
   The Tank remains at its locked start while its feet perform authored
   preparation steps. Whole-clip foot planting is intentionally not required.
2. Executing selects `ChargeLoop`, starting at the authored phase that matches
   the end of Tell. It loops while the actor moves along the fixed locked
   corridor; moving the target afterwards does not redirect the charge.
3. A clear run and the obstacle case both enter `ChargeRecovery` at its start.
   The Tank finishes the 1.4 s recovery with a stable stance and no scale jump.
   Authored recovery steps are allowed. Check both an early obstacle hit and a
   full-length run, because they can end on different loop phases.
4. The corridor player is hit at most once by the charge; the off-corridor player
   remains unharmed. Damage, push, cooldown and path limits match the saved
   settings. Animation changes must not gate collision or server movement.
5. Compare slow playback with the default `ChargeSpeed` and inspect foot contact
   against the floor. If a stride-rate adjustment is necessary, derive it from
   the authored foot displacement and actor speed, then repeat both charge cases.

## Mage melee and spell separation

1. Put a player within melee range with the three spell cooldowns unavailable.
   The Mage selects `MageMelee`, compresses its preparation into `MeleeWindup`,
   reaches the contact pose at the server resolve tick and recovers in 0.9 s.
   The right hand follows its authored shove rather than the spell emitter IK.
2. A player in the forward sector is hit; a player behind the Mage or behind a
   blocker is not. Damage and push retain the original values. No fireball,
   summon or rain effect is produced by the melee animation.
3. Check Fireball with a target at both sides of the Mage's aim range. The palm
   and visual release remain near the projectile origin at resolve, and the
   spell retains the locked target and existing travel/collision behavior.
4. Check Summon at its release tick, then continue to the recovery end. Spawn
   count, live-creep cap and creep AI remain unchanged; no third character's
   animation assets are required by this change.
5. Check the full Rain channel, including its first and final drops. The
   55–62% channel pose remains stable through the four-second default hold, then
   recovers. Actual drop count, cadence and repeated-hit gap follow the profile.
   `HeavyCast` remains a fallback clip, not a fourth spell.

## Other Tank attacks and Roll

- Melee reaches its contact pose at `TankMeleeImpactFraction` exactly when the
  server resolves damage. The shield and club retain their assigned side; feet
  remain supported during the planted part of the strike.
- Jump reaches takeoff and landing at the configured fractions. The authored
  body pose does not add a second vertical trajectory to native actor movement.
  Check one entrance fall as well: it uses the existing takeoff-to-impact segment.
- Roll plays Transform during windup, reaches the compact end pose before the
  static ball fully replaces the Tank, and reverses Transform during recovery.
  The ball's rotation, chase turn limit, repeat-hit gap, push and reflection at
  blockers/arena edges remain native behavior. Inspect shape/scale continuity at
  both 0.18 s mesh substitutions, including after a ricochet.
- Interrupt an active Roll by defeating the boss. No executing mesh or attack
  actor should remain visible after the existing defeat presentation finishes.

## Loops, reactions and replication

- Observe both characters idle, walking, changing direction and stopping. Tank
  Walk/WalkLeft/WalkRight keep common phase and duration while blended. Check
  Mage retreat movement for sliding; it currently shares the existing Walk slot.
- Hit both bosses during telegraph, executing and recovery. Reactions must not
  restart an ability, change its resolve time, or produce non-finite poses. Mage
  Hit remains a lightweight overlay; the shield keeps its original front-sector
  damage rule and its attack-state exceptions.
- Defeat the Mage during a cast and during Rain. Death plays once over the saved
  `DeathSeconds`, and existing attack effects are cleaned up.
- In the existing two-player PIE/network check, compare authority and one remote
  player during Charge, Mage Melee, Rain and Roll. They should select the same
  clip/phase from replicated attack/state times, with no client root movement or
  cosmetic bone evaluation influencing server damage and projectile origins.
- Repeat spawning the encounter after the test. Cached references must include
  the four new clips and retain the correct Tank/Mage role when presented.

Record actual test evidence separately. Imported base-pose QA and the Blender
videos do not establish gameplay, collision, procedural overlay or network
acceptance.
