# Character, tooth and gum camera collision

Character capsules, skeletal meshes (including ragdolls), and extra Blueprint
colliders ignore Camera at startup. Gameplay teeth apply the same response.
The spring arm no longer directly probes tooth brush surfaces; those surfaces
remain available for brushing and other gameplay queries. Normal Camera-channel
sweeps and the revealed-wall volume guard still protect against other walls.

`Tools/Unreal/ignore_teeth_gum_camera.py` saved Camera = Ignore on 15 tooth/gum
components in L_Mouth, including hidden tooth-placement previews. It disables
Use Default Collision on those instances so mesh defaults cannot replace the
custom response on registration. Collision geometry, enabled state, object type
and all other channel responses are preserved. `SavedCollision.json` records
the changes; `SavedReload.json` verifies them after a full editor restart.

Validation: MessControlEditor Win64 Development compiled successfully.
The final headless report in `TestsFinal/index.json` records Success with zero
errors for CharacterAndToothTransparency, FoodTransparency and
OrbitAndWallCollision. Coverage includes standing/fallen teammates, Blueprint
colliders, tooth interaction queries, food/fragments and blocking walls.
`Runtime.json` records the collision audit in PIE on the saved map using actual
Blueprint characters and spawned gameplay teeth. No rendered orbit route or
multiplayer input test was run. Native automation fixtures emit existing
PhysicsControl warnings. Earlier test reports are diagnostic intermediate runs.
