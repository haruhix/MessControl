# Cataclysm — short solo validation

Status: prepared, **not run**. Root owns Editor/build/PIE execution. Script: `Tools/Unreal/probe_cataclysm_solo.py`.

Each run requires idle `L_Mouth`, starts one standalone authority PIE with one local player, and restores the original play/frame/foreground settings. It preserves an existing PIE, never changes or saves the editor map, and stops only its owned session. The existing first-perk UI is resolved before the real F3 action runs.

Run once with `MC_CATACLYSM_MODE='rafts'`, once with `'jellies'`. Default mode is rafts; globals can be passed using `runpy.run_path(..., run_name='__main__', init_globals={...})` through the owned editor Python connection.

Rafts: observe Warning → FirstWave → BranchWarning → Flooding; exactly two rafts and no jellies. Two stationary allied NPC pawns in the same world are load fixtures, not network clients. Check two actual deck riders while a third pawn swims nearby; cancel a real 0.7-second overload warning by removing the third rider; then retain three riders and observe the native two-second dip/return. Verify passengers survive, ordinary deep swimming spends reserve, real held RMB creates a brace contact and boards through the movement component's capsule sweeps, and dry deck recovers reserve. Wait for natural draining and require dirty Cleanup to remain active. Deep-water hold is explicitly tuned to 26 seconds, fill/drain to 3 seconds, for this short fixture.

Jellies: exactly the jelly branch with no raft actors. Assert both the event default and each spawned jelly's actual health are 360 HP. Held normal Pickaxe input must decrease health; lethal warning remains an outstanding enemy until natural rupture. Persistent brushable event grime must remain after the rupture. Kill the native pair, then verify transition to Cleanup.

Both modes: dirty Cleanup persists for three seconds without finishing from elapsed time. Select one real event patch and hold normal brush input; its actual CoffeeLeft must decrease. Full cleanup of all patches, players and teeth, Complete at zero consequences, balance during free movement, continuing-overload repeat and drowning exhaustion remain explicit manual checkpoints. No direct damage, care, zero-dirt, clock/fuse manipulation or generated pass result is permitted.

Evidence: each run writes `Saved/Checks/CataclysmSolo/Runs/<UTC_mode_id>/Report.json`, source hashes, actual phase/input/resource/base/health/raft telemetry, and genuine 1280×720 `Shot SHOWUI` PNGs at requested 8 Hz. Clip windows cover common wave, capacity/warning, dip/return, RMB boarding, jelly rupture and brushing. Frames carry game timestamps and actual camera evidence; missing frames fail capture validation. Use actual timestamp duration when encoding; no interpolation or invented frames. Rendered clips still require visual inspection before claiming warning/foam/rupture quality.

Read-only source review: delayed Cataclysm-owned waves pass the flood's director guard; director tickets wait for actual Event.IsComplete/bFailed; variants are exclusive and cleanup has no completion timer. Root was notified of the three TObjectPtr range-loop compile errors and participant scaling that counted unpossessed practice pawns. No shared gameplay files were edited by this probe author.
