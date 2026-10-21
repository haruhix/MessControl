# Hub swimming

The water in `/Game/Maps/Hub` supports controlled surface swimming using the existing tooth movement and procedural stroke animation.

- Surface: `Plane` (`StaticMeshActor_93`), with `MI_Water_Stylized_Plane` and no collision.
- Gameplay volume: `Hub_SwimmingWater`, an ordinary `PhysicsVolume` in `Hub/Water`, with **Water Volume** enabled. Its top is aligned to the visible surface at Z -116.509 cm.
- Move with the existing WASD / movement stick input. Floating holds the face above the water. Shallow water retains ordinary walking.
- At a tall bank, hold the existing **E** climb control and move upward with **W**. The normal mantle restores walking on top.
- The volume's `SafeSwimming` actor tag enables energy recovery while swimming, so this Hub pond does not introduce exhaustion or drowning. Untagged water retains normal swim energy behavior.

`MCToothMovementComponent` samples either the existing coffee flood or a standard water PhysicsVolume. The volume brush supplies its footprint and surface height; a floor trace rejects shallow water. The same sampled height drives surface movement, the animated hand stroke targets and ragdoll recovery placement. Existing coffee movement/current settings remain in use for coffee actors.

To make another horizontal pond swimmable, place a water PhysicsVolume with its top at the rendered surface, fit its brush to the pond footprint, and disable collision on the visual water mesh. Add `SafeSwimming` only for water intended to be safe indefinitely.

Validation on 2026-10-21: normal `MessControlEditor` module compilation and a fresh editor startup passed. The focused solo PIE check passed 12 assertions: dry start, walking into water, stable floating, movement input travel, animation instance, water/coffee separation, safe energy, climbing out, walking restoration and finite movement. The stroke traversed over 2.7 m in 1.5 seconds. Actual gameplay frames and the detailed results are in `Saved/HubSwimming_20261021`. The authoring and solo-check scripts are in `Tools/Unreal/Hub`. No multiplayer or packaged-build validation was requested.

The existing `MessControl.Coffee.ControlledSwimming` regression test also passed (one test, no errors or warnings), confirming that the original coffee swimming/current path still works after the shared surface-query change. Final evidence: `Saved/HubSwimming_20261021/complete.json` and `coffee_regression.json`.
