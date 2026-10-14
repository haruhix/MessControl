# Cataclysm raft reference notes

Recorded: 2026-10-14. Status: authoring brief; model, exports and Unreal behavior have not been validated by this document task.

## Construction references

The original source is the Kon-Tiki Museum's [They shrunk the Kon-Tiki raft](https://www.kon-tiki.no/en/news/skalamodelleravkontiki). Its Adam Polinski scale-model construction photographs are construction analogues for this asset, rather than a dimensioned template or a request to reproduce the Kon-Tiki craft. The page credits the photographs to A. Polinski. The parent authoring agent has visually inspected the photographs below; this document records that inspection.

| Reference | Observed construction feature | Adaptation for MessControl |
| --- | --- | --- |
| [AP_2: logs and crossbeams](https://images.squarespace-cdn.com/content/v1/65c3c78a263c667fb481bfb3/1714033295201-THKMLR25X23Z4SYOGZ6V/AP_2.jpg) | Rounded log floats, transverse battens, a separate deck, and pale end grain. | Two rounded longitudinal floats support two cross battens; six broad deck planks sit above the battens. Distinguish pale end grain from darker log sides. |
| [AP_3: bow rope work](https://images.squarespace-cdn.com/content/v1/65c3c78a263c667fb481bfb3/1714033296500-NA0LQ8B6DG1M0AJVEIOV/AP_3.jpg) | Rope wraps form real crossings, working turns, knots and free tails. | Give each visible binding a readable route around the joined parts, with working turns, a knot and short tails. Seat the rope against the timber. |
| [AP_14: aft construction](https://images.squarespace-cdn.com/content/v1/65c3c78a263c667fb481bfb3/1714033312356-MQCTXMLHXLRUF9OL67N8/AP_14.jpg) | Timber lengths are slightly unequal. | Add restrained variation to plank ends and log tips while preserving the intended walking envelope and coherent silhouette. |

Only links and observation notes are retained. No source photographs or source textures are copied, embedded, redistributed, projected onto the asset, or included in exports. Author new geometry and texture work. Dimensions below are project design decisions, not measurements taken from the museum photographs.

## Existing game palette

The parent authoring agent and its coordinating agent inspected [NutFireNutStart.png](../../Artifacts/NutVFXPolish_20261013/NutFireNutStart.png), an existing gameplay screenshot. Its pink tongue, cream teeth and warm ochre walnut establish the surrounding palette.

Choose a warm light-brown deck, pale natural-fiber rope, darker brown log sides and lighter end grain. Keep broad wood grain readable and restrained. Use rounded edges, smooth log silhouettes and visible rounded bindings; this is a polished stylized prop, with no low-poly requirement. Color values and texture resolution remain authoring choices, to be checked against the resulting asset previews.

## Locked model decisions

- Author in meters, with length along X, width along Y and Z up. Put the origin at the raft center on the intended waterline, Z = 0.
- Make six broad deck planks with an approximately 2.50 m by 1.76 m outer deck footprint. The intended inner walking envelope is 2.40 m by 1.70 m, centered on the deck.
- Set the deck top to Z = 0.375 m above the waterline origin. Keep it broadly level; do not fill the inner walking envelope with raised knots or decorative obstacles.
- Use two approximately 2.70 m rounded longitudinal log floats and two transverse battens. Choose their thicknesses and placements to produce visibly supported deck, batten and float contacts.
- Make visible rope bindings with crossing turns, knots and tails. Slightly uneven plank ends and float tips provide hand-built character without compromising the broad walking surface.
- Target approximately 12,000-30,000 evaluated render triangles; hard maximum 45,000, excluding simple collision proxies.
- Provide simple UCX collision: one deck hull and one hull per float. Detailed rope and wood grain are visual features, rather than additional collision detail.

The native capsule dimensions, diameter 68 cm and height 116 cm, were supplied as context only. They have not been measured on the active Blueprint or runtime pawn. The intended two-player capacity is a future gameplay rule; neither the model dimensions nor collision hulls enforce or prove it.

## Delivery and verification

The parent authoring task owns the reproducible authoring script, editable `.blend`, `.fbx`, `.glb`, newly authored PBR atlas, previews and artifact manifests. Use Blender 5.1.0 at `D:/BLENDER_TEST/blender.exe` in headless mode with two threads. Preserve the unrelated live Legogo scene and its unsaved changes.

Deliver a 1024 by 1024 hero image and fixed front, back, left, right, top, bottom and perspective views at 512 by 512 each, assembled into a labeled contact sheet. Keep the asset framing and orthographic scale consistent across opposite views. Collision proxies should not appear in beauty previews.

Record topology, bounds, material and UV metrics for the authored model and for fresh imports of both GLB and FBX. Report collision proxy counts separately from render mesh metrics. Check unit conversion, the waterline pivot, the deck height, missing faces, normals, material references and UV coverage after reimport; a hero render alone does not establish export correctness.

Unreal import, gameplay-camera and game-lighting review, walkability, buoyancy, movement, passenger capacity, multiplayer checks, Windows build/cook/package and runtime cost are deferred. This asset task provides previews and export evidence, with no Unreal behavior validation.

## Nonblocking review questions

- Are the deck, battens and floats visibly supported, and do the rope routes describe credible bindings at their crossings?
- Do the deck, log sides, end grain and rope remain distinct materials in the hero and fixed views?
- Does the broad, rounded raft silhouette read clearly, with controlled variation rather than damaged or detached parts?

`approval_gates` is empty in the modeling contract. These questions guide review and iteration; they do not introduce a permission step.
