"""CPU actual-pass and input-field evidence for the closed crown disks."""
from pathlib import Path
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Textures/Revisions/v31b_ClosedSourceTransfer_HarmonicOcclusions/Diagnostics'
OUT.mkdir(exist_ok=True)
packet = np.load(OUT.parent / 'actual_closed_camera_first_hit_UV0.npz')
uv, owner = packet['UV0'], packet['object_id']
receipt = json.loads((OUT.parent / 'closed_source_transfer_receipt.json').read_text())
binding = json.loads((ROOT / 'Textures/Revisions/v31_ClosedSourceTransfer_ActualUV0/material_binding_closed_SOURCE_DRAFT.json').read_text())
xs, ys = (uv[..., 0]*2048-.5).astype(np.float32), ((1-uv[..., 1])*2048-.5).astype(np.float32)
enamel_ids = [i for i, name in enumerate(receipt['objects']) if not name.endswith('_Gum')]
enamel = np.isin(owner, enamel_ids)
images, fields = {}, {}
for key in ('base_color', 'roughness', 'coat_weight', 'coat_roughness', 'specular_ior_level', 'subsurface'):
    image = cv2.imread(binding['materials']['MC_Enamel'][key], cv2.IMREAD_UNCHANGED)
    maximum = 65535 if image.dtype == np.uint16 else 255
    if image.ndim == 3:
        image = image[..., 3] if key == 'specular_ior_level' else image[..., :3][..., ::-1]
    sampled = cv2.remap(image.astype(np.float32)/maximum, xs, ys, cv2.INTER_LINEAR)
    fields[key] = sampled
    rgb = sampled if sampled.ndim == 3 else np.repeat(sampled[..., None], 3, axis=2)
    rgb = rgb.copy();rgb[~enamel] = .04
    images[key] = np.uint8(np.clip(rgb, 0, 1)*255)
passes = ROOT / 'Assembly/Review/ClosedColoredPasses_v32'
for key in ('Image', 'DiffCol', 'DiffDir', 'DiffInd', 'GlossDir', 'GlossInd'):
    value = np.load(passes / (key+'_linear.npy'))[..., :3]
    fields[key] = value
    # Display as source-linear radiance, with explicit percentile normalization
    # for light-response maps only. No such control is a new beauty render.
    scale = 1. if key == 'DiffCol' else max(float(np.percentile(value[enamel], 99)), 1e-6)
    value = np.clip(value/scale, 0, 1)
    encoded = np.where(value <= .0031308, value*12.92, 1.055*value**(1/2.4)-.055)
    images[key] = np.uint8(np.clip(encoded, 0, 1)*255)
geometry = np.load(receipt['packet'])
world_triangles, smooth_triangles = [], []
for name in receipt['objects']:
    prefix = 'closed::' + name + '::'
    vertices, triangles = geometry[prefix+'world_vertices'], geometry[prefix+'triangles']
    points = vertices[triangles]
    cross = np.cross(points[:, 1]-points[:, 0], points[:, 2]-points[:, 0])
    vertex_normals = np.zeros_like(vertices)
    for corner in range(3):
        np.add.at(vertex_normals, triangles[:, corner], cross)
    vertex_normals /= np.maximum(np.linalg.norm(vertex_normals, axis=1)[:, None], 1e-12)
    world_triangles.append(points)
    smooth_triangles.append(vertex_normals[triangles])
world_triangles, smooth_triangles = np.concatenate(world_triangles), np.concatenate(smooth_triangles)
triangle_id = packet['triangle_id']
valid = triangle_id >= 0
points = world_triangles[np.maximum(triangle_id, 0)]
p = np.stack(((points[..., 0]/14.2+.5)*963, 128.5-points[..., 2]/14.2*963), axis=-1)
yy, xx = np.mgrid[:257, :963]
e1, e2 = p[..., 1, :]-p[..., 0, :], p[..., 2, :]-p[..., 0, :]
den = e1[..., 0]*e2[..., 1]-e1[..., 1]*e2[..., 0]
den = np.where(abs(den) > 1e-12, den, 1e-12)
dx, dy = xx+.5-p[..., 0, 0], yy+.5-p[..., 0, 1]
a, b = (dx*e2[..., 1]-dy*e2[..., 0])/den, (e1[..., 0]*dy-e1[..., 1]*dx)/den
w = np.stack((1-a-b, a, b), axis=-1)
smooth = np.einsum('hwi,hwik->hwk', w, smooth_triangles[np.maximum(triangle_id, 0)])
smooth /= np.maximum(np.linalg.norm(smooth, axis=-1, keepdims=True), 1e-12)
smooth[~valid] = 0
fields['CPU_ApproxSmoothNormal_XYZ'] = smooth
images['CPU_ApproxSmoothNormal_XYZ'] = np.uint8(np.clip(smooth*.5+.5, 0, 1)*255)
face = np.cross(points[..., 1, :]-points[..., 0, :], points[..., 2, :]-points[..., 0, :])
face /= np.maximum(np.linalg.norm(face, axis=-1, keepdims=True), 1e-12)
face[~valid] = 0
fields['ActualTriangleFaceNormal_XYZ'] = face
images['ActualTriangleFaceNormal_XYZ'] = np.uint8(np.clip(face*.5+.5, 0, 1)*255)
box = (295, 38, 478, 142)
sheet = Image.new('RGB', (183*3*2, (104*2+24)*5), '#16171b')
draw = ImageDraw.Draw(sheet)
for index, (name, image) in enumerate(images.items()):
    x, y = (index%3)*366, (index//3)*232
    draw.text((x+4, y+3), name + (' / p99 normalized' if name in ('Image', 'DiffDir', 'DiffInd', 'GlossDir', 'GlossInd') else ''), fill='white')
    sheet.paste(Image.fromarray(image).crop(box).resize((366, 208), Image.Resampling.NEAREST), (x, y+24))
sheet.save(OUT / 'closed_disks_input_and_actual_passes.png')
yy, xx = np.mgrid[:257, :963]
report = {}
for name, cx, cy in [('LateralL_disk', 325, 89), ('CentralL_disk', 416, 89)]:
    radius = np.sqrt((xx-cx)**2+(yy-cy)**2)
    core = (radius < 10) & enamel
    ring = (radius > 17) & (radius < 22) & enamel
    report[name] = {}
    for key, image in fields.items():
        report[name][key] = {'inside_median': np.median(image[core], axis=0).tolist(), 'outside_ring_median': np.median(image[ring], axis=0).tolist()}
(OUT / 'closed_disks_channel_metrics.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report, indent=2))
