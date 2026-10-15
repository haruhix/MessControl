# Food chopping and enemy combat

Ordinary knife and pickaxe attacks select their mode from the reachable target:

- Food: repeat the existing overhead pose while primary input is held. Further
  swings stop on release or loss of an eligible food target. Tool resistance
  rules remain in effect. This pose has a separate reliable swing-event tag.
- Small enemies and bosses: actual primary press edges drive the three-step
  combat combo, including the wider drawn-back second stroke.
- Reachable enemies take priority over loose food in front of the player.
  A held food attack does not automatically become an enemy attack. Combat
  sweeps cannot cut food sideways when it enters their reach mid-wind-up.

Food chops clear the combat press buffer. Contact resolution shares target
queries with input mode selection and keeps the swing's food/combat intent.
The existing upper tool-byte nibble carries 1..3 for combat or 4 for food;
reflected RPC signatures are unchanged.

## Verification

`EditorIntegration.log` records the successful cold Development Editor module
compile and restart. `SoloCheck.json` records a single short solo PIE using
real primary press/release edges and the real food, mob and boss damage paths:

1. Held knife cuts soft food three times, then stops after release (75 damage).
2. Held pickaxe cuts hard food three times, then stops after release (120 damage).
3. Holding on a mob causes one attack, with nearby food receiving no damage.
4. Three clicks on a mob cause the three-step combo (75 damage).
5. Three clicks on the saved phase-three boss Blueprint cause the combo
   (120 damage), with nearby food receiving no damage.
6. Switching from food to a reachable mob during a hold causes no automatic
   enemy attack. Only the already-started overhead food chop was accepted.

`ModeSequenceCheck.json` and `ModeEvents.log` verify three overhead-only events
for each food hold, steps 1/2/3 for mob and boss combos, and no unintended combo
events during the target transition. `TargetModePoses.jpg` and `preview.mp4`
contain genuine gameplay-camera captures. PNG normalization preserves every
pixel before encoding; all clips and the joined video are fully decoded for
verification. The preview is silent.

`EditorRestored.json` records the original L_Mouth map, no active PIE or dirty
packages, restored performance settings and the preserved food-table contents.
The user explicitly authorized saving the unsaved food table before restart;
its generated collision data was preserved as well. `AuthorizedMenuSave.json`
and `AuthorizedSaveDetails.json` record that save. `Before/` contains source
baselines and saved-asset backups. Earlier combat work is preserved in the
neighboring artifact folders. No multiplayer, cook or packaging test was run.
