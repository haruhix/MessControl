# Hub station collision — final validation, 2026-10-23

The four existing blue wardrobe cabins now block players using two private
collision derivatives. `SM_HubCabinBody_Collision` has four simple boxes (sides,
back and top, leaving the front/bottom open); `SM_HubCabinDoor_Collision` has one
box for each existing closed door. Original artist meshes, triangles, materials
and prop transforms are preserved. The existing vending mesh retains its
original ComplexAsSimple triangle collision; no replacement was needed.

The final fresh standalone Hub run passed **35/35 checks** across all four doors
and the vending machine's visible front (`+localY`). The actual pawn walked from
spawn to each prop, moved into contact and backed away. There were no teleports,
placement fixtures, spawned test blockers or collision overrides. Floor and
capsule-clear approach queries passed; the player-profile capsule sweep hit the
exact intended mesh. Actual movement stopped at approximately 34.074 cm for the
doors and 34.057 cm for the vending front, matching the 34 cm capsule radius.
The owned PIE stopped and temporary play settings were restored cleanly.

Final report: `E:/CodexWork/HubWardrobeRewards_20261022/StationCollision/Solo02/Report.json`.
Five timestamped native clips, exact contact stills, source hashes, decode checks
and the final contact sheet are in
`Artifacts/HubStationCollision_20261023/Final/`. Captures were sampled at 3 Hz;
30 fps videos repeat native images without interpolation or time scaling.
Overhead E prompts are visible in all five final contact stills. These collision
checks did not invoke the E action or validate its modal flow.

This full run supersedes the earlier partial `Solo01` and focused
`VendorSolo02` evidence. Those reports/videos remain intact: the former queried
a Shop stair before reaching the vending mesh; the latter used one explicitly
recorded pawn placement on the real porch. They are historical diagnostics,
not the final five-case result. Final evidence uses the decorative vending front
and proves its walking approach from spawn without that fixture.

Authoring/inspection tools are `Tools/Unreal/Hub/author_hub_station_collision.py`
and `inspect_hub_station_collision.py`; the final smoke is
`check_hub_station_collision_solo.py`. Encode a completed run with
`encode_hub_station_collision.py --mode Full`. The final check covers door fronts
and the vending front; cabin side/back/top blocking, multiplayer, cook and
packaged-game behavior remain outside this focused validation.
