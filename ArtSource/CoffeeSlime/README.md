# Coffee slime

Original coffee creature for MessControl, authored from the generated `v1/concept_v1.png` and the game's Tooth character screenshot. References inform wet material readability and animation principles; no external models, textures or creator geometry are included. See `reference_notes.md` for sources and the applied design choices.

`v1/CoffeeSlime.blend` is the editable source. It has a single joined skinned mesh, an export collection, a five-bone rig, five baked actions, packed image textures, and a separate review stage. Blender 5.1.0 (`D:/BLENDER_TEST/blender.exe`, build `adfe2921d5f3`) authored it in isolated background processes. The unrelated unsaved live Blender project was preserved.

## Export contract

The editable `.blend` and GLB use meters, +X forward, +Z up, with root/pivot at the floor. Rest bounds are 1.68 × 1.58 × 1.30 m, 18,236 triangles, six material slots and a maximum of two normalized bone influences. UE FBX uses a disposable centimeter-coordinate derivation: mesh/bone rest coordinates and pose-location keys ×100, scene unit length .01, object/reference root scales1, FBX unit factor1. Unreal import uses unit conversion and uniform scale1; no component-scale compensation.

The corrected set in `v1/exports` passed root's actual UE import: 17 owned packages saved, five bones, reference root scale1, face at +X58/Z62cm and Hop lift33.998cm. CanonicalPromotion.json verifies that promotion copied the exact tested candidate bytes. Candidate audit/image proof remains; duplicate candidate FBXs and extracted .fbm textures are ignored. The older meter-space FBX set was rejected and replaced; source `.blend`/GLB/textures are unaffected. Actual gameplay/lighting validation remains root-owned.

| File | Purpose | Duration |
|---|---|---|
| SK_CoffeeSlime.fbx | Skeletal mesh and skeleton, no embedded animation | — |
| A_CoffeeSlime_Idle.fbx | Looping small gel/crema settling motion | 1.20 s |
| A_CoffeeSlime_Hop.fbx | Squash, takeoff, aerial stretch, impact squash, settling | 0.90 s |
| A_CoffeeSlime_Hurt.fbx | Brief recoil with lifted footprint and return | 0.28 s |
| A_CoffeeSlime_PreBurst.fbx | Growing tense balloon with bounded wobble | 0.60 s |
| A_CoffeeSlime_Burst.fbx | Matching swollen start, rapid flattened collapse | 0.18 s |
| SM_CoffeeSlime.fbx | Neutral static reference plus UCX_SM_CoffeeSlime_00 hull | — |
| CoffeeSlime.glb | Review mesh, PBR textures and all five actions | — |

Bones: root → gel_body → gel_top; face is a child of gel_body and foam_tip is a child of gel_top. No added leaf bones or root motion. Blender actions are baked at 100 fps, including both endpoints. Mesh/clip FBX exports temporarily name the object `Armature`, because the UE5.8 importer removes that Blender wrapper specifically; `.blend`/GLB retain `SK_CoffeeSlime_Rig`. Combined with the explicit centimeter derivation, removing the wrapper does not remove a required scale100. Actual UE skeleton and orientation acceptance belongs to the engine import validation.

The native actor owns world motion/collision. Attach the new presentation at `-BodyRadius-4` cm relative to the actor, with scale 1. Remove the old green sphere material, separate eyes, procedural body squash/lift for the new presentation. Retain native wandering, health, paint, GroundBurst and Droplets. The static UCX hull is an optional reference and is not a replacement for the gameplay collision sphere or a verified PhysicsAsset.

## Materials

Opaque, lit materials; no transmission or glass opacity. Values below are linear RGB. Default dielectric specular is .5 / IOR 1.5, metallic 0. Body and foam have UV-mapped 512² PNG BaseColor and Roughness images under `v1/textures`; the images are also packed into the source and GLB. These are original deterministic textures with subtle variation, not Blender-only procedural effects. BaseColor uses sRGB; Roughness uses linear/non-color.

| Slot | Base Color | Roughness | Coat / coat roughness |
|---|---|---|---|
| 0 M_CoffeeGel | .075, .025, .009 | .20 | .40 / .10 |
| 1 M_MilkFoam | .80, .68, .43 | .46 | 0 |
| 2 M_Ivory | .88, .83, .68 | .28 | .15 / .10 |
| 3 M_MauveLid | .26, .12, .15 | .36 | .08 / .10 |
| 4 M_EspressoDark | .006, .003, .002 | .16 | .23 / .10 |
| 5 M_CaramelLip | .23, .08, .02 | .27 | .25 / .10 |

FBX material translation can differ between importers; the slot table and PNGs define the intended UE material setup. Fresh Blender FBX and GLB renders retain the intended appearance; this does not prove Unreal shader parity.

## Review and reproduction

Use the absolute Blender executable above with `--background --factory-startup --disable-autoexec --threads 2 --python-exit-code 1 --python author_coffee_slime.py -- --stage author`. Run stages `export`, `stills`, `motion` sequentially after inspecting the source render. `validate_export.py --asset <file> --report <json>` runs under a separate Blender process for each exported file. Optional `--render <png>` captures the fresh imported materials without repairing them. `make_review.py` assembles unchanged renders with labels.

`--stage export` defaults to canonical centimeter FBX under `v1/exports`; `--fbx-subdir <owned-subdirectory>` is available for isolated comparison exports without replacing the accepted set. Do not compare freshly generated FBX bytes as a deterministic mesh/animation test: Blender5.1 includes wall-clock timestamps and process-hashed IDs. Exact tested-byte promotion hashes are retained separately; fresh reproduction is checked by physical bounds, hierarchy, normalized weights, clip timing and evaluated poses. The default export does not alter/save the meter-space `.blend`.

Technical reports and images are in `v1/review`. Fresh import checks cover closed geometry (glTF seam splits welded only in a temporary topology copy), degenerate triangles, UV presence, material count, dimensions, hierarchy, normalized weights, root stability and clip duration. Pose sheets inspect silhouette/deformation/frame containment separately from numeric checks. The refinement note records a corrected first PreBurst scale, not an accepted older version.

Unreal import/runtime validation and integration evidence are owned separately by the root workflow. No network or GPU-cost claims are made from Blender evidence.
