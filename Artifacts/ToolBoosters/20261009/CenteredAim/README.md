# Centre-screen spray and watergun aim

The crosshair stays at the centre of the local player's screen for the spray slot,
including ordinary spray and both watergun modes. A camera ray acquires the target;
the actual nozzle directs spray and pressure shots toward it. Care patches have no
physical collision, so their visible areas are acquired on the same camera ray.
Tool range and muzzle obstruction checks remain in effect.

Selecting the spray slot smoothly offsets the local camera over the shoulder.
During held use the body turns toward the aim while the arms follow the target.
Other tools, menus and unavailable work contexts hide the marker. The watergun
keeps its charge/cooldown ring.

Validation on the final editor build:

- `TestsCamera/index.json`: Mechanics, HoldSprayAndPreserveProgress and
  OrbitAndWallCollision all succeeded. Reports include physics-control warnings
  and a disabled-physics fixture impulse warning; there are no failed assertions.
- `Runtime.json` and PNGs: 11 rendered gameplay states cover ordinary spray,
  watergun care, charge, shot, cooldown, other tool and menu hiding/restoring.
  The camera target projects to the screen centre within 0.001 px. Both mist
  emitters point at the acquired target and the body faces it during use.
- `Network.json`: the owning client's camera is sent through the native inventory
  tick RPC. Its authoritative target projects within 0.04 px of the client centre.
  The authority drives press/release; client mist, recoil, VFX and health are checked
  after replication. A full shot hits a target beside the body (1000 -> 880 health
  on both peers); a subsequent shot is blocked by a wall. This fixture does not
  validate actual client button input or packet-loss conditions.
- Scoped saved-package index refresh completed for WBP_GameplayHUD and DA_Equipment:
  two current exports, build current, no export errors.

No assets were reauthored. PIE fixtures are temporary. Existing map/assets and
unrelated working-tree changes were preserved. No game packaging was performed.
