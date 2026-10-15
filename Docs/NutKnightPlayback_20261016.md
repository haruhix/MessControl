# Knight playback, size and turn-axis correction

Jump and combo retain their existing windup, flight and contact timing. Their
remaining authored recovery now advances at the source rate to the final frame,
at full clip weight. A 0.25-second end-pose hold precedes the existing 0.25-second
transition to idle. The jump's recovery is no longer hardcoded to one second;
`TankJumpAnimationSeconds` records the complete 154/30-second source length.
Combo recovery is no longer faded before the last source frame. Cooldown
readiness includes the complete recovery. Normal melee also keeps its final pose.

The animation proxy replaces non-ball sample scales with the skeleton reference
scales, then applies the same constraint after the final cached-pose crossfade.
Consequently an old scaled pose cannot leak size changes into damage, walking,
jump, combo, stun or death. Only the Roll transform/exit samples permit authored
scale; ordinary reaction slots do not become exceptions by reusing a scaled
clip. Component scale stays fixed outside the two ball handoffs. The Knight's
death no longer shrinks the model before despawning. Mage animation and size
behavior are unchanged.

`TankTurnPivot` is a new floor-level scene component under the collision body.
The skeletal model is attached to it. Its horizontal anchor comes from the
midpoint of the reference feet; the vertical anchor is the reference sole level.
Shield and club bounds no longer determine the turn center. The same anchor and
size are used for shielded and unshielded meshes. Existing serialized attachment
parents are repaired on presentation refresh. The collision sphere stays at its
existing gameplay height; its vertical yaw axis passes through the floor pivot.

`Artifacts/NutKnightPlayback_20261016/SoloCheck.json` passed all 11 checks:

- Combo and jump reach their actual final source bone poses; minimum maximum
  endpoint error is 0.00553 cm and 0.00329 cm respectively over five sampled bones.
- Recovery lasts 3.65 seconds for combo and 4.15 seconds for jump, including the
  last-pose hold. Three original combo damage contacts remain intact.
- A native 180-degree turn keeps the ground pivot stationary and the body center
  within 2.89 cm of its horizontal axis.
- Real rear Pickaxe input triggers damage. A second PIE-only reaction routes
  BallEnd through the damage slot to stress non-unit animation scale keys.
- Non-ball bone scale matches the reference through playback and mixing; maximum
  observed error is 1.58e-16. Component-scale variation is zero, including damage
  and the final death frames.
- Native roll entry and exit retain their scale exceptions. A one-second roll
  is used only to shorten this fixture; event tuning stays unchanged.

The fixture also controls observer placement/HP, cooldown readiness and chase
speed during turn observation. It never fabricates attack phases, damage results
or evaluated poses. The source profile, animations and skeleton are not edited
by the test. `InitialSetupFailure.json` retains an initial zero-sample fixture
failure from an unavailable Python scale getter; the corrected fixture uses the
reflected property and its final run passed. The successful Editor module build
and source inspection reports are saved beside the gameplay evidence.

`preview.mp4` is genuine solo SceneCapture footage, normal simulation speed,
15 Hz capture / 30 fps encode with repeated frames. The review camera follows
jump and roll to keep the complete character in frame; it stays fixed for
turning. Captions are added. No generated or interpolated poses are used.
The temporary scale-key stress case is excluded from this ordinary gameplay
preview; its metrics and captures remain in the report. No multiplayer,
cook/package, full encounter balance test or GPU profile was run.

All existing animation/mesh/material packages, source blends, unrelated saved
assets and user configuration are preserved. Editor and compiler settings are
restored; PIE is stopped and no content or maps remain dirty.
