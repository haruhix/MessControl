# Subtle tool carry motion

`MCToothAnimInstance.cpp` adds a small gait-synchronized offset to the upgraded tool's idle wrist target. Translation is limited to 0.2 / 0.4 / 0.7 cm along the actor axes; rotation is limited to 0.45 / 0.25 / 0.5 degrees. The existing support-hand solver follows the same resulting tool pose.

Motion strength follows speed and interpolates at 8/s. It fades when stationary, using a tool, dashing, or leaving the ground. The stationary poses from the reference image remain the targets at rest.

Validation on UE 5.8.1:

- Native editor build succeeded.
- `MessControl.Inventory.HiddenPickaxePreservesHandContacts` succeeded. Its report includes existing PhysicsControl warnings about unregistered control names.
- Live PIE checked running and stopping with MeshaBrush, Chainsaw, Buffer, and Watergun (`Runtime.json`, passed).
- MeshaBrush, Chainsaw, and Watergun mesh-center offsets stayed below 1.5 cm, with maximum rotation below 0.65 degrees. Their stationary pose returned within 0.02 cm.
- Buffer's measured motion spans were 0.98 / 0.36 / 1.29 cm, with maximum rotation 0.52 degrees. Its larger absolute height offset is the existing floor-clearance correction over the curved tongue, so its stop check verifies rotation rather than requiring the initial world-location clearance height.
- Support-hand contact error remained below 1 cm for all four tools (observed floating-point roundoff only).
- Asset index refreshed successfully: 1301 saved assets current, no export errors.

Editor performance settings were restored and PIE stopped. No dirty map or content packages remained. Existing walnut assets and NutRain table changes were preserved.
