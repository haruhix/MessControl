# Composer and colleague changes integration

Integrated colleague commits `a4af5068e`, `3c1ad3534`, `81f63e104` and `9fda9742143d49735920854c92e9140488c6e1a9` into Composer checkpoint `e3e91e8da322acc1a773ad5c924ade47e7a85254`. The merge preserves the new melee clips, authored weapon transforms, updated mouth assets and our event-driven VFX/SFX presentation. The automatic merge had no textual conflicts.

## Compatibility changes

- Niagara melee trails now receive the same effective animation rate as the native combo trail window, including the colleague's accelerated playback. The authoritative attack cooldown is unchanged.
- Melee regression fixtures observe the first hit before its authoritative cooldown and place late knife targets within the actual contact retry window. Damage, friendly-fire protection, confirmed-hit and knockdown expectations remain unchanged. Spray retains its original observation interval.
- Declared the existing ToolsetRegistry editor dependency explicitly in the project. Game targets do not enable this editor-only plugin.
- Preserved the colleague's 22 incoming LFS resources and verified their bytes against the remote pointers. Our checkpoint's 424 LFS resources were also verified; a live saved-package dependency traversal found no additional untracked gameplay dependency to include.

## Checks actually performed

Forced-unity Editor Build79 succeeded after integration. Build80 succeeded after the final test-fixture correction; comparison of every Source file hash found only `MCWeaponPlayerHitTests.cpp` changed between those builds. No production code or gameplay assets changed in that final rebuild.

Seven focused tests passed on Build79. The remaining fixture initially failed because its fixed 0.65-second wait included a legitimate buffered second attack; it passed on Build80 after the timing correction:

| Test | Successful run |
| --- | --- |
| Inventory.SelectionAndMaterial | Build79 |
| Inventory.HiddenPickaxePreservesHandContacts | Build79 |
| Inventory.PlayerHits.ReachWallsAndLateKnifeContact | Build79 |
| Passives.Batch1.HeavyHitDamageAndAcceptedComboCadence | Build79 |
| Combat.EnemyWeapons.CandySurfaceAndOverlap | Build79 |
| VFX.EditorInspection.RetainedCapturedMeshMismatch | Build79 |
| VFX.FreezeCapture.RealFatalCondition.Default | Build79 |
| Inventory.PlayerHits.KnockbackWithoutFriendlyDamage | Build80 |

All test names have the `MessControl.` prefix. Synthetic actor fixtures reported presentation-not-prepared warnings; these runs are functional regression evidence, not first-use readiness or artistic approval.

The actual saved `DA_ToothAnimation` loads all four incoming clips with the existing tooth skeleton. Food-hit and combo sequences contain all eleven authored weapon transform curves.

The updated `L_Mouth` ran in solo PIE. All six normal-start assertions passed: the real resource barrier and controller became ready; five distinct ingredients belonged to one native batch; the HUD included food plus cleaning; food remained stable; coffee did not start prematurely. Four frames of normal-input knife motion were retained, and the primary agent inspected two. The extended observer reached its 180-second wall limit before the final recovery frame, so its aggregate report remains failed rather than being relabeled a full gameplay pass. The Editor did not crash. Owned PIE stopped, Entry and settings were restored, and no packages remained dirty.

After Build80, Composer was restored to Rocket with a valid preview context, playback stopped, no dirty draft/packages and the original background CPU throttle restored.

## Scope limits and evidence

This merge is a compatibility checkpoint. It does not complete the broader Composer migration's artistic/audio acceptance, Tutorial checks, every normal-map first use, a full earned run, multiplayer or packaged/clean-cache validation. Loading remains slow; see [the migration checkpoint](VFXComposerMigrationCheckpoint_20261103.md).

Detailed local receipts and PNGs remain under `E:/CodexWork/MessControl/Validation/ComposerGitIntegration_20261103`, `ComposerGroupedValidation53_20261102/Runs/integration79_01`, `integration80_failed01` and `ComposerFullMigration_20261101`. Generated media is excluded from Git. Unrelated local ReviewBoard and art work is preserved.
