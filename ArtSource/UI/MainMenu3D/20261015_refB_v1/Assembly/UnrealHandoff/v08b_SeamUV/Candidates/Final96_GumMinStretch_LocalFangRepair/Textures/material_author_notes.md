# Final96 physical surface handoff

Outer enamel and gingiva only. This does not certify the full Unreal menu's
final appearance. Native Unreal lighting, subsurface response and all opening
poses still require lead review against the original concept.

The exact approved source/FBX/OBJ/packet identity was verified before authoring.
All anatomy masks were rebuilt from Final96 packet bb42a0c3fd91f8e2a180ec179c61188c9aaac085529806c30ec2b38876b13eea.
No rejected v08 or early v08b UV data, baked lighting or concept radiance is used.

Painter 12.1.5 / Python API 0.3.5, UVTile workflow with one 1001 tile per material.
Native batch bake 12f61a1e11ed4323b44efcca7241031c completed successfully with
all 12 expected mesh maps verified. Settings: 4096, 16px dilation, 2x2 AA,
low-as-high; AO 96 rays, distance0.025 relative to bounding box, smooth
attenuation and no ground; thickness96 rays, distance0.1, per-tile min/max
normalization. Thickness is black thin / white thick, not metric thickness.

Color correctness was checked independently of lighting. Installed Painter
colormanagement.Color documentation states that omitted color_space means sRGB
for a managed BaseColor channel, Raw for non-color channels. Actual readback:

| Set | Intended sRGB | Stored linear Working | Base-only export RGB8 |
|---|---|---|---|
| Enamel | .95, .875, .77 | .8900055, .7388448, .5542217 | 242,223,196 |
| Gum | .835, .185, .235 | .6648600, .0286221, .0450764 | 213,47,60 |

Every pixel of the isolated 512px base-only probe matches the expected rounded
sRGB byte triplet. No manual extra gamma conversion was applied. Roughness
readback is Raw and remains linear.

The first export retained microrelief too quietly to survive normal-map
quantization. Export02 raises enamel Height to0.006 at0.30 opacity and gingiva
Height to0.025 at0.45 opacity. Native 4K exported tangent normal deflection
p50/p95/p99 is enamel0.71/1.59/2.03degrees and gum1.15/2.71/3.55degrees. This
keeps the crowns smooth while retaining fine tissue relief. Both rejected
amplitude export01 and chosen export02 remain separate. Mesh maps01 are reused
because geometry and UVs did not change.

Actual Painter viewport inspection used the computer-use skill's supported
window capture, not an offline reconstruction or fabricated image. Target was
Adobe Substance 3D Painter - MC_DAY03_ReferenceFinish_Final96. Inspected front
with Soft1Front2Backs and oblique/close views with Soft4TubeBank2BlackFlags.
Camera poses: front position[0,26,0],rotation[90,180,0],FOV42; oblique
[4.5,17,0],[90,165,0]; close[3,13,0],[90,167,0]. Continuous facial highlights,
cervical pigment and fine relief were checked across the visible chart joins;
no visible facial UV seam was found in these views. This was a neutral studio
surface inspection; it is not a claim of reference-matched Unreal lighting.

Use shader_binding.json: all ten named texture inputs include absolute paths,
hashes,4K dimensions and color/compression metadata. Use PainterPBR_02 BC,
roughness, combined DirectX normal and mixed AO; thickness is from the native
PainterMeshMaps_01 inward-ray bake. Do not connect exported Height in addition
to the combined normal, which already includes the microrelief. Metallic is0.

Editable source project and deterministic recipes are retained here. The old
SPP checkpoints and rejected UVs remain in their separate v08 revision.
