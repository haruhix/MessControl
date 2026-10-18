# Wizard rain and fireball — 2026-10-18

## Changes

- The wizard's rain replays every zone of both opening Nutageddon patterns from the active encounter profile. It uses the same reachable-footprint resolver, physical walnuts and individual landing warnings. Earlier debris does not relocate these indexed drops. The spell retains its existing active duration.
- Fireball contact removes exactly 80% of the player's health at impact, independently of maximum health and ordinary incoming-damage perk multipliers. Normal damage validation, feedback and single-impact handling remain active.
- An accepted fireball hit applies 820 cm/s of horizontal knockback along the projectile's flight and 260 cm/s of lift through the existing character physics component.

## Validation

- Gameplay changes compiled and loaded into the running Unreal Editor with Live Coding. A temporary compiler workaround was restored afterward, together with the original editor performance and play settings.
- Solo PIE: all 18 wizard rain landings matched the 18 opening landings, with 0 cm maximum XY error.
- Solo PIE: real projectile sweeps against fresh possessed player characters after spawn protection expired produced 100 → 20 HP and 37.5 → 7.5 HP. The player entered ragdoll and moved approximately 115 cm along the projectile direction in the following 0.2 seconds. Fireball contacts were isolated above the arena to avoid debris and teleport-placement interference.
- `MessControl.CoreLoop.NutVFX.ServerSweepHitsOnceAndCancels` passed, including current-health damage, knockdown and no repeated terminal damage.
- The initial combined encounter regression passed the new opening-footprint comparisons, but its later free-placement check was crowded by the additional test nuts, and its completion check did not wait for the wizard's death animation. The fixture now disposes its coverage nuts before free placement and waits for `DeathSeconds`. The corrected test translation unit compiled successfully; that combined test was not rerun.
- No multiplayer or packaged-build validation was performed.

Runtime measurements: `Artifacts/WizardRainFireball_20261018/SoloVerification.json`.
