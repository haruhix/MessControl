# Food camera collision

Food physics bodies and visible meshes default to Camera = Ignore. Item setup
also applies that response to every primitive component after Blueprint
construction and during item replication, including extra Box/Sphere colliders.
All other collision responses remain intact.

The orbit spring arm's direct detailed-mesh probes now cover teeth only.
Food grip meshes still serve gameplay queries but cannot retract the camera.
SpringArm collision testing remains enabled on the Camera channel.

Validation: MessControlEditor Win64 Development compiled successfully.
The headless engine report in `Tests/index.json` contains two successful tests:
`MessControl.Camera.FoodTransparency` and
`MessControl.Camera.OrbitAndWallCollision`, with zero errors.
Coverage includes whole food and fragments, restored collision profiles, an
extra collider, a food mesh intersecting the camera path, retained gameplay
collision, a wall behind food, and the imported mouth shell. Existing native
test fixtures emit PhysicsControl warnings. No rendered playthrough was run.
