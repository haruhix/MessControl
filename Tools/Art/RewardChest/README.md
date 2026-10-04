# Reward chest glow

Select a `BP_RewardChest` instance and open **Details > Reward Glow**.
These settings are instance editable and applied by `UserConstructionScript`.

| Setting | Meaning | Default |
| --- | --- | --- |
| Glow Strength | Master multiplier. Zero hides every glow mesh and light. | 1 |
| Rays Strength | Radiating rays behind the chest. | 0.75 |
| Spotlight Glow Strength | Visible beam from above. | 0.6 |
| Sparks Strength | Animated sparks and glints. | 0.6 |
| Seam Glow Strength | Light along the lid seam. | 0.65 |
| Floor Glow Strength | Additional floor halo; zero disables its draw. | 0 |
| Inner Light Intensity | Extra inner point light, in lumens before master multiplier. | 0 |
| Overhead Light Intensity | Downward spotlight, in lumens before master multiplier. | 100 |
| Glow Animation Speed | Animation rate. Zero freezes shader animation. | 1 |
| Glow Tint | Color of the emissive glow. | Gold |

Strength and speed inputs are clamped to 0–10. Light inputs are clamped to
0–10,000. Emissive intensity and light intensity are multiplied by Glow Strength.
Glow Tint controls the emissive materials; the overhead light uses warm gold.
The keyhole glow component has been removed and body/lid use the original
`MI_Chest` material.

The active materials in `/Game/Gameplay/VFX/RewardChest` are `RaysFast`,
`BeamFast`, `SparksFast`, `SeamFast`, and `FloorFast` with the
`M_RewardChest_` prefix. They replace pixel shader trigonometry and exponential
falloff with small baked masks. Animation runs in the vertex shader through
World Position Offset and VertexInterpolator, with no Blueprint tick added.
The ray card is a circular mesh, the beam is one triangle instead of four
nested cones, and sparks use 20 quads instead of 40. Effects have a 3,000-unit
maximum draw distance. Extra floor glow and the inner point light default off.
Legacy assets remain available but are no longer assigned to these components.

`generate_optimized_glow.py` regenerates mask PNGs and compact OBJ meshes.
It requires numpy and Pillow. The ray mask is square-root encoded in grayscale
R8; the material squares the sampled red channel to recover the original mask
with smoother dark gradients. Mask imports use grayscale compression, linear
sampling, clamp addressing, and bilinear filtering. Fast HLSL files contain
the GPU animation source. `chest_glow_construction.dsl` contains the final
construction graph authored through native MCP.

Focused editor and PIE checks passed for defaults, custom intensity/color,
zero master intensity, removal of the keyhole component, and animation while
the landed chest tick is disabled. Material statistics report pixel shader
instructions of 209 → 122 for rays and 169 → 121 for the beam. These are shader
instruction counts, not an FPS benchmark.
