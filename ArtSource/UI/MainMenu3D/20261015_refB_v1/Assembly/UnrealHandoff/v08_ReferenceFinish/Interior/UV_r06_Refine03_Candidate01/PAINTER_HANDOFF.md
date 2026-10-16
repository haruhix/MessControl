# Refine03 tongue — frozen geometry/UV handoff

`release_manifest.json` SHA-256:
`535bf9e152190f64ab2663851278278c81df5214f79d1db547e418e28bac0ccb`.

This is the next native candidate, not final reference/look approval. Root
selected Refine03's continuous raised tongue bed and subtle posterolateral
fullness after reviewing paired front and oblique renders. The new dorsal UV
passed technical and actual checker/layout gates. Packet files are frozen;
author textures in a new `Textures` child directory without changing source,
FBX, OBJ, NPZ or the release manifest.

Combined Painter input: `MC_DAY03_Interior_Painter_Open.obj`, with explicit
per-loop normals and strict decimal MTL values. Texture sets remain:

- Index 0, `MC_Interior`: frozen r05 cavity geometry/UV unchanged.
- Index 1, `MC_Tongue`: approved Refine03 shape; repaired dorsal UV, unchanged
  ventral UV and same equatorial seam. Geometry vertex/loop order is unchanged.

`dorsal_uv_transfer.npz` provides old/new UV correspondence on the same ordered
topology for paint transfer. The tongue dorsal U direction follows source +X;
V follows native/source +Y. The old dorsal paint must be reprojected to the new
UV, and tongue mesh maps must be freshly baked before exporting its PBR maps.
Keep the existing authored warm/coral direction; geometry correction must not
be painted back into a false split valley. Normal output for Unreal remains
DirectX, with no extra runtime channel flip.

Do not re-author or overwrite the old r05 SPP, maps or frozen packet. The cavity
FBX in this folder is a byte-identical copy of the r05 FBX
`1dcb142957e0efe25aba29204593c7d401347b3b7d693e53b7cf0a0fe1b1d8ce`.

The `.blend` contains clean two-object identity transforms in source metres.
Direct binary FBX parsing confirms centimetre metadata UnitScaleFactor 1 and
model scale 100 once, with the existing Z rotation 180. Use the existing native
absolute-node-transform import convention and uniform import scale 1. Do not
add a second 100x conversion.

Separate fresh Blender import checks passed for position, UV/material corner
associations and normals. Tongue position roundtrip error is <0.000086 cm;
character anchor error is <0.000070 cm. Dorsal anisotropy kappa p95 is 1.124,
density p95/p05 is 1.096, angle error p95 is 4.39 degrees. There are two tongue
charts, zero overlaps/collapses/mixed winding, >=16px tile border at 4K and
>74px reported island gap. The reserved footprint/unchanged ventral placement
explains the 35.04% tongue atlas coverage; no nonuniform UV squeeze was used.

Actual images viewed: `uv_layout_SM_DAY03_Tongue.png`,
`checker_interior_front.png`, `checker_interior_oblique.png`,
`checker_interior_back.png`. Reports are in `QA_Independent`; the existing
helper naming means independent fresh-import/check methods, not a claim that
a second agent or user approved the result.

Blender MCP was checked with `get_blendfile_summary_datablocks`, and returned
"Cannot connect to Blender at 127.0.0.1:9876". Root was informed before the
isolated factory-startup Blender 5.1 headless fallback. This agent did not touch
Unreal or Painter. Root owns native integration; Painter authoring should use
the live Painter MCP connection.
