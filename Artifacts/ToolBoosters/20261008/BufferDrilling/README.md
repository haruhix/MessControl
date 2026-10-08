# Buffer drilling pose

Buffer now uses a two-handed drill pose instead of the pickaxe's overhead strike. Its +X bit points into the replicated calculus contact; small axial and lateral motor vibration keeps it working without raising the tool between repeated uses. Without a target, it feeds forward from its idle position. Releasing a short click completes the existing use cycle, then blends back to idle. Holding input maintains the drill pose across cycles.

The ordinary pickaxe keeps its existing strike. Buffer damage, hit cadence, calculus removal, and weight use the existing gameplay paths. Carry sway from the previous change is retained.

Validation:

- UE 5.8.1 native editor build succeeded.
- All nine existing `MessControl.Inventory` automation checks passed, including hidden-tool hand contacts and weapon damage/routing (`Tests/index.json`). Four passed with existing engine PhysicsControl warnings.
- Live PIE free drilling, single-click contact, held contact, and idle return passed (`Runtime.json`). Free drilling mesh movement spans stayed around 1 cm, with no overhead swing; idle position returned within 0.1 cm after release.
- Both-hand grip remained precise. The bit axis aligned with the visible enamel normal above 0.99998 dot product. The physical trace checks the enamel beneath the raised mineral geometry; calculus removal is verified through production input and the F3 practice fixture.
- Single-click drilling reduced the fixture from 39 pieces to 4; held drilling cleared all 39 pieces without leaving chipped remnants. Native scene captures show the free, contact, held, and restored idle poses.
- Two-player PIE confirmed that the replicated worker's free and contact drill poses retain their hand contacts on the client. Both peers saw all 39 pieces removed, and working state ended after release (`Network.json`). The fixture initiates actions on the authority and checks the client presentation.
- Saved asset index refreshed successfully with all 1301 assets current and no export errors.

Original editor packages were clean before the build and were preserved. Editor remains open; PIE is stopped, single-player play settings and performance settings are restored. Existing walnut asset/table edits were not changed by this task.
