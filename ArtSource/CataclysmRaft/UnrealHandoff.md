# Cataclysm raft Unreal handoff

Recorded: 2026-10-14. Target: MessControl, Unreal Engine 5.8. This is a standalone asset handoff; Unreal import and gameplay behavior have not been validated in this task.

The current [authoring script](../../Tools/Blender/author_cataclysm_raft.py) and [manifest](manifest.json) are the source for the exported asset. The [modeling contract](modeling_contract.json) describes the design envelope, and [reference notes](reference_notes.md) record construction and palette provenance. Final fresh GLB and FBX reimport geometry reports are available; their results are summarized below. The parent authoring agent completed visual review of the authored hero, fresh GLB hero, final fresh FBX hero and updated seven-view contact sheet and accepted those previews, including solid end-grain caps. Unreal import and gameplay checks remain deferred.

Final authoring-source SHA-256: `6c813859f8a34dba77690d3f4890be31f39873871d113084aedd4b5c41290343`.

## Delivered source and maps

| File | Role |
| --- | --- |
| [SM_CataclysmRaft.blend](SM_CataclysmRaft.blend) | Editable Blender source, authored in meters. |
| [SM_CataclysmRaft.fbx](SM_CataclysmRaft.fbx) | Unreal handoff: one render mesh and three UCX collision hulls in the same FBX. |
| [SM_CataclysmRaft.glb](SM_CataclysmRaft.glb) | Render-mesh interchange and preview export; UCX hulls are not included. |
| [T_CataclysmRaft_BaseColor.png](Textures/T_CataclysmRaft_BaseColor.png) | 1024 by 1024 Base Color atlas. |
| [T_CataclysmRaft_Roughness.png](Textures/T_CataclysmRaft_Roughness.png) | 1024 by 1024 linear roughness atlas. |
| [T_CataclysmRaft_Normal.png](Textures/T_CataclysmRaft_Normal.png) | 1024 by 1024 tangent-space OpenGL (+Y) normal atlas. |

The render mesh is named `SM_CataclysmRaft` and uses `M_CataclysmRaft_Atlas`. The current manifest records 22,000 authored render triangles. Count collision separately; its topology, UVs and material coverage are not render-mesh evidence.

## Dimensions and pivot

| Feature | Current value and meaning |
| --- | --- |
| Origin | Raft center at Z = 0, the **intended waterline**. It does not establish runtime buoyancy. |
| Deck top | Z = **37.5 cm** above the origin. This supersedes the earlier 34 cm value. |
| Intended inner walking rectangle | **240 by 170 cm**, centered on the deck; a native design envelope rather than a runtime walkability result. |
| Outside deck planks | Approximately **250 by 176 cm**, with restrained variation at the plank ends. |
| Port float | **270 cm** authored length. |
| Starboard float | **276 cm** authored length. |
| Full fresh-import render bounds | **277.7 by 195.6 by 67 cm**, including rope tails and other outer details; this is not the walking rectangle. |

Blender authoring uses X for raft length, Y for width and Z up. The current FBX exporter applies unit scale and writes axis conversion (`-Z` forward, `Y` up) and FBX unit metadata. Unreal is expected to resolve the mesh into centimeter units. Start with an import scale of 1, then **measure the imported mesh and collision bounds** against the values above; do not assume that either a 100 multiplier or a 0.01 multiplier is needed. Check actual axis orientation and the Z = 0 pivot after import. Overall bounds include float tips, rounded details and rope, so the walking rectangle is not the whole render mesh bounding box.

## FBX import settings

Import the FBX as one Static Mesh asset named `SM_CataclysmRaft`. If the importer presents multiple render mesh entries, enable **Combine Meshes** / **Combine Static Meshes**. UCX entries must be recognized as custom collision, rather than rendered or delivered as separate playable props. Epic documents these controls in the [FBX import options](https://dev.epicgames.com/documentation/unreal-engine/fbx-import-options-reference-in-unreal-engine) and [Interchange import reference](https://dev.epicgames.com/documentation/unreal-engine/interchange-import-reference-in-unreal-engine?lang=en-US).

| Setting | Requested value |
| --- | --- |
| Mesh type | Static Mesh; skeletal mesh and animation import off. |
| Combine Meshes | On if the importer presents separate mesh entries; resulting render asset count must be one. |
| Auto Generate Collision | **Off**. |
| Import Collision / custom collision | **On**; with Interchange, enable collision import according to mesh name. |
| One Convex Hull Per UCX | **On**. |
| Normal Import Method | **Import Normals and Tangents**. |
| Import Uniform Scale | Begin at **1**, then verify bounds in centimeters. |

The custom collision meshes are:

- `UCX_SM_CataclysmRaft_00`: deck hull, 240 by 170 by 9 cm; its top is Z = 37.5 cm.
- `UCX_SM_CataclysmRaft_01`: port float hull, 270 by 42 by 42 cm.
- `UCX_SM_CataclysmRaft_02`: starboard float hull, 276 by 42 by 42 cm.

These are simple convex proxies. After import, inspect the Static Mesh's simple collision and confirm exactly three hulls, with no UCX geometry contributing to the visible mesh. UCX objects are nonvisual, so absence of their render material or UV coverage is not a visual-material failure.

**Normals and tangents:** the final script triangulates an export copy, explicitly exports GLB tangents and enables FBX tangent export with `use_tspace=True`. Keep the requested Unreal setting **Import Normals and Tangents**. Unreal's import of those attributes and the resulting shading are still unverified; confirm them during the later engine import. Epic distinguishes imported normals with engine-computed tangents from [imported normals and tangents](https://dev.epicgames.com/documentation/unreal-engine/fbx-import-options-reference-in-unreal-engine).

## Texture and material setup

Use one opaque wood-and-rope PBR atlas material for the render mesh. FBX material auto-wiring may be incomplete: inspect the imported material and manually connect the atlas maps where needed.

| Texture | Unreal interpretation and connection |
| --- | --- |
| `T_CataclysmRaft_BaseColor` | **sRGB on**; connect RGB to Base Color. |
| `T_CataclysmRaft_Roughness` | **sRGB off**, linear/non-color data; connect one grayscale channel to Roughness. |
| `T_CataclysmRaft_Normal` | **sRGB off**, Normal Map compression and normal sampler; connect to Normal after convention conversion. |

The supplied normal map is **OpenGL (+Y)**. For Unreal's normal-map convention, enable **Flip Green Channel** unless the chosen importer explicitly converts the source convention already. Apply that conversion once, and verify that wood grain and rope relief respond to light in the expected direction. Both the [Texture Asset Editor](https://dev.epicgames.com/documentation/unreal-engine/texture-asset-editor-in-unreal-engine) and [Interchange texture settings](https://dev.epicgames.com/documentation/unreal-engine/interchange-import-reference-in-unreal-engine?lang=en-US) expose a green-channel flip.

Set Metallic to 0 for wood and rope. The Blender preview uses normal strength 0.42; material translation may not preserve that value, so match the intended restrained surface detail during the later Unreal material review. Keep the source maps as authored rather than using the construction photographs as texture inputs.

## Evidence and deferred work

Parent-authored reports and previews belong under `Artifacts/CataclysmRaft_20261014`. The final [fresh GLB report](../../Artifacts/CataclysmRaft_20261014/fresh_glb_metrics.json), [fresh FBX report](../../Artifacts/CataclysmRaft_20261014/fresh_fbx_metrics.json) and [collision report](../../Artifacts/CataclysmRaft_20261014/collision_metrics.json) record:

| Check | Fresh GLB | Fresh FBX |
| --- | --- | --- |
| Render meshes / triangles | 1 / **22,000** | 1 / **22,000** |
| Vertex count | 15,768, including attribute-seam splits | 11,044 |
| Full dimensions | 277.7 by 195.6 by 67 cm | 277.7 by 195.6 by 67 cm |
| UV maps / named materials | 1 / 1 | 1 / 1 |
| Degenerate faces / invalid vertices / missing material assignments | 0 / 0 / 0 | 0 / 0 / 0 |
| Boundary / non-manifold edge counters | 8,704 / 8,704 from split attribute seams | **0 / 0**, geometrically closed |
| Report result | Passed for its recorded metrics | Passed for its recorded metrics |

The render bounds run approximately from (-137.5, -97.8, -27.0) cm to (140.2, 97.8, 40.0) cm. The origin remains (0, 0, 0), with intended waterline Z = 0 and the deck top at Z = 37.5 cm. Outer rope details extend beyond the deck's dimensions and height.

GLB attribute splits increase its vertex and boundary-edge counters; those counters are not evidence of visible cracks on their own. The parent authoring agent's final preview comparison is complete and accepted, including solid end-grain caps. The metric reports and offline visual review do not certify Unreal texture translation, tangent shading, or gameplay appearance.

All **three UCX hulls pass** the separate closed, convex and positive-volume checks, with zero boundary/non-manifold edges. Their material-coverage status is `not_applicable_nonvisual_collision`; the report's missing-material counter is not a render-material failure for these nonvisual proxies. Engine recognition of the UCX hulls remains unverified.

This task does not import the asset into Unreal or validate game-camera appearance, game lighting, walkability, physical collision, buoyancy, movement, passenger capacity, multiplayer behavior, or gameplay. The two-player limit remains future gameplay logic. The native 68 cm diameter by 116 cm height capsule was context only and was not measured on the active Blueprint/runtime pawn. Windows build, cook and package are also outside this asset task.

Geometry and texture pixels are newly authored. Museum photographs were inspected as construction analogues; no source photograph or source texture is copied into the delivered asset. No new skills were installed. The standalone model has **no approval gate** under the root task's explicit policy; construction contacts, material separation and silhouette remain review questions for iteration.
