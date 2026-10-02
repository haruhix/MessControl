# Food collision authoring

Saved Unreal packages and native LOD geometry are the source of truth. Reports
and generated plans belong in `Saved/FoodCollisionProbe`; they are not assets.
Coordinate values in those reports are asset-local Unreal centimetres. The
geometric QA's separate `game_scale_upper_bounds` applies the current DataTable
scale and the requested actor scale, conservatively for nonuniform scales.

## 1. Audit the current saved meshes

Close other editors and coordinate this launch with native builds and captures.
Use a **full editor** with `-ExecutePythonScript`; a Python commandlet does not
provide the required `StaticMeshEditorSubsystem` authoring path.

```powershell
& 'E:/UE/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe' `
  'E:/DEVGAME/MessControl/MessControl.uproject' `
  -NullRHI -unattended -nosplash -nop4 `
  '-ExecutePythonScript=E:/DEVGAME/MessControl/Tools/Unreal/create_food_collision.py --mode audit --report FoodCollisionBefore.json'
```

Wait for editor exit and require `complete: true`, no errors, and the expected
current menu scope. The audit exports actual render positions/indices, material
assignments, `render_geometry_sha256`, and native convex data through reflected
struct export. Regenerate it if saved meshes or the menu scope changed.

## 2. Generate the exact source plan

The reusable recipe is `plan_food_collision.py --exact-source`, supported by
`food_collision_exact.py`, `food_collision_tetra.py`, and the existing geometry,
component and source-kernel helpers. No saved plan is required as an input.

The tested runtime is **CPython 3.12.14, Windows x64**, with **NumPy 2.3.5,
SciPy 1.18.1, Tetgen 0.8.4**. `food_collision_exact_dependencies.json` records the
versions and checked raw Tetgen binary SHA256; `requirements-food-collision-exact.txt`
pins the wheels. Installation is isolated inside the project's ignored `Saved`
directory, without PyVista/VTK or changes to global Python packages:

```powershell
& C:/Users/user/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe Tools/Unreal/setup_food_collision_exact.py
& C:/Users/user/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe Tools/Unreal/plan_food_collision.py Saved/FoodCollisionProbe/FoodCollisionBefore.json --exact-source
```

The output is `Saved/FoodCollisionProbe/FoodExactSourcePlan.json`. An existing
isolated runtime can be reused with `--dependencies-root` and `--tetgen-library`.
Generated reports, dependencies and binaries are never source assets or commits.

The planner validates the full audit scope, current saved menu scales/uses, and
each recomputed native render SHA. It solves closed-component halfspace kernels;
source-face tetrahedra are merged only into convex unions. Nonstar components
use raw Tetgen PLC with native surface points and triangles preserved, followed
by convex closure merging with volume certificates. Identical source components
share an in-memory cache; source coordinates and render geometry remain unchanged.

Open components require explicit collision-only boundary certificates. Nonplanar
socket caps must be wholly covered by independent closed source unions, checked
by polygon/halfspace subtraction. An exposed planar cap must satisfy the finite
1-Lipschitz nearest-triangle distance bound at the requested item/actor scale;
the default external world-gap budget is 1 cm at ActorScale20. Egg03's tiny
four-edge closure passes this guard; original open render geometry is preserved.

Every hull is reordered to begin with a broad existing face, with indices
remapped and exact face geometry checked. Float32 first-three-plane checks reject
native planar-prism inflation risk. These are raw geometry guards, not evidence
of actual Chaos cooked vertices. Scope, row, source and dependency hashes are
recorded. Unsupported geometry yields `complete:false`, errors and a nonzero exit;
partial reports must never be imported as a completed plan.

The current exact32 candidate has 12,341 hulls across all meshes. Green/Purple
grapes require 2,977/2,974 each, so shape-count performance needs actual runtime
measurement before acceptance. The legacy approximate hybrid remains available
by omitting `--exact-source`: one convex berry envelope plus isolated-stem
V-HACD. Those envelopes have measured error up to .05975 asset-local cm, about
1.195 cm at current Bacon ItemScale20 and 23.9 cm under additional ActorScale20;
they must not be described as source-exact.

## 3. Author and save native collision

```powershell
& 'E:/UE/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe' `
  'E:/DEVGAME/MessControl/MessControl.uproject' `
  -NullRHI -unattended -nosplash -nop4 `
  '-ExecutePythonScript=E:/DEVGAME/MessControl/Tools/Unreal/create_food_collision.py --mode build --plan E:/DEVGAME/MessControl/Saved/FoodCollisionProbe/FoodExactSourcePlan.json --report FoodCollisionRefined.json'
```

The author validates fresh render SHA and plan/menu guards before editing. It
installs reflected source hulls into `BodySetup`, requests native physics
invalidation/rebuild, and saves only food mesh packages. Require editor exit,
`complete:true`, no errors, all expected packages in `saved_packages`, unchanged
render SHA/materials, and raw import round-trip evidence. Path-only resume after
binary merges is unsafe; resume requires current package/source/plan guards.

## 4. Verify saved geometry and runtime physics

```powershell
& C:/Users/user/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe Tools/Unreal/food_collision_geometry.py `
  Saved/FoodCollisionProbe/FoodCollisionRefined.json `
  --output Saved/FoodCollisionProbe/FoodCollisionRefined_GeometryValidation.json `
  --actor-scale 20
```

This measures raw source-hull coverage, exposed convex-union contacts and sampled
filled cavities. Open render meshes cannot support signed cavity classification.
These measurements do not read `FKConvexElem`'s non-reflected Chaos convex pointer;
actual cooked vertices/planes and shape counts require a native runtime exporter.

Refresh the saved asset index after authoring and await a successful current
export. Rebuild native code first if it changed. Verify the runtime's actual
cooked convexes with overlap/ray/drop checks at the current item scale and actor
scale 20, and inspect the normal mesh plus Chaos wire overlay in the recorded
fixture. Cooking tolerances, hull simplification, and contact margins can differ
from the saved source hulls, so geometric QA alone is insufficient acceptance.
