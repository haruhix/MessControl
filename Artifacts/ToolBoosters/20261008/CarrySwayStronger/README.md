# Stronger carry motion

User requested more visible motion. Doubled the translation and rotation amplitudes in `MCToothAnimInstance.cpp`, retaining the existing gait phase, speed weighting, smoothing, and shared two-hand grip.

Wrist translation amplitudes: 0.4 / 0.8 / 1.4 cm. Rotation amplitudes: 0.9 / 0.5 / 1.0 degrees.

Native editor build succeeded. Live PIE running/stopping checks passed for all four tools (`Runtime.json`):

- MeshaBrush, Chainsaw, and Watergun mesh-center offsets stayed below 3 cm; maximum rotation was about 1.30 degrees, approximately twice the previous values.
- Buffer rotation reached 1.04 degrees. Its translation includes the existing floor-clearance adjustment over the curved tongue.
- Both-hand grip contact remained precise for every tool; sway settled after stopping.
- Asset index refresh succeeded with 1301 current assets and no export errors.

No new automation tests were added for the amplitude adjustment. The previous hand-contact regression result is preserved in `../CarrySway/Tests/`.
