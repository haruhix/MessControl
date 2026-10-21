# Hub ground painting

The `Ground` actor in `/Game/Maps/Hub` uses
`/Game/Art/Materials/hub/MI_HubGroundPaint`, with its own parent
`M_HubGroundPaint`. The original grass parameters are preserved from `MI_Ground`.

To paint a path:

1. Select `Ground` and enter **Mesh Paint** (Shift+4).
2. Choose **Texture Color**, then **Paint**.
3. Leave the **Red** channel enabled. White **Paint Color** reveals the road;
   black **Erase Color** restores grass.
4. Drag LMB to paint. Hold Shift while dragging LMB to erase. Brackets change
   brush size. Start with Size `0.015`, Strength `0.8`, Falloff `0.5`.
5. Save the Hub level with Ctrl+S. The paint mask belongs to the ground component
   and is stored in the level.

The **Road** parameter group in `MI_HubGroundPaint` exposes **Road Base Color**,
**Road Normal**, **Road ORM**, **Road Tint**, and **Road Tiling**. ORM channels are
ambient occlusion / roughness / metallic. The original defaults use the existing
Megascans `Gravel_Ground_xbnefjm` texture set from `NiagaraExamples/Gallery`;
the instance can override these with other surface textures.

**Road Normal Strength** controls only the road normal map: `0` is flat, `1`
preserves the texture, and values above `1` increase the slope of surface details.
**Road Roughness Strength** multiplies the road roughness map; values below `1`
make the surface smoother and values above `1` make it rougher. **Road Roughness
Offset** adds a constant adjustment after that multiplier; the result is clamped
to 0–1. The existing **Normal Strength** and **Roughness Strength** controls apply
to grass.

For a standalone grayscale roughness texture, enable **Road Use Separate
Roughness** and assign it to **Road Roughness Texture**. This mode uses AO=1 and
metallic=0, so the roughness map does not accidentally make a nonmetallic surface
metallic. For a packed ORM texture, disable that switch and use **Road ORM**.
The user's `ground_08_roughness_2k` map is configured in the separate mode.
Roughness textures must use linear data (`sRGB` off); this map uses Masks
compression. Evaluate road controls on a painted patch in the Hub: the material
instance's preview sphere has no mesh paint mask and displays the grass fallback.

The mesh has a dedicated non-overlapping paint UV in channel 1, generated from
UV0 with the existing mesh build settings. Mesh Paint Texture Coordinate Index
is `1`, the per-instance mask is `2048 x 2048`, and virtual texture support is
enabled. UV0 continues to control the original grass and road texture tiling.
Base color, tangent-space normals, roughness, metallic, specular, and road AO
blend using the mask's red channel. Missing paint textures fall back to grass.

If the ground mesh is reimported with different UVs, retain or regenerate UV1
and keep its mesh paint coordinate index aligned with the material's UV1.

Validation: rebuilt the ground with unchanged vertex count and collision mode,
checked render-data UV1 bounds/area, exercised Texture Color fill and an actual
brush stroke in the editor, and undid the test stroke. The saved level starts
with grass and a clear road mask. The independent road controls were subsequently
compiled and exposed in the material instance while preserving the user's texture
overrides and level painting.
