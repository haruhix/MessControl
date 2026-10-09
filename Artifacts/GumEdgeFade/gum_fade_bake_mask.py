import json
from collections import Counter, defaultdict
from pathlib import Path
import numpy as np
from PIL import Image

data = json.loads(Path('gum_fade_mesh.json').read_text())
vertices = {int(k): np.array(v, dtype=np.float32) for k, v in data['vertices'].items()}
ids, welded = {}, {}
for key, point in vertices.items():
    position = tuple(np.round(point.astype(np.float64), 3))
    ids[key] = welded.setdefault(position, len(welded))
positions = np.array(list(welded), dtype=np.float32)
edges = Counter()
for tri in data['triangles']:
    a, b, c = [ids[i] for i in tri]
    for x, y in [(a, b), (b, c), (c, a)]:
        if x != y:
            edges[tuple(sorted((x, y)))] += 1
segments = np.array([(positions[a], positions[b]) for (a, b), count in edges.items() if count == 1], dtype=np.float32)
assert len(segments) == 256, len(segments)
start, end = segments[:, 0], segments[:, 1]
delta = end - start
length2 = np.maximum((delta*delta).sum(1), 1e-10)
segment_min, segment_max = np.minimum(start, end), np.maximum(start, end)
size, max_distance = 1024, 200.0
distance_map = np.full((size, size), max_distance, dtype=np.float32)
coverage = np.zeros((size, size), dtype=bool)
for tri, uv in zip(data['triangles'], data['uvs']):
    pts = np.array([vertices[i] for i in tri])
    separation = np.maximum(np.maximum(segment_min-pts.max(0), pts.min(0)-segment_max), 0)
    nearby = (separation*separation).sum(1) <= max_distance**2
    tex = np.array(uv, dtype=np.float32) * size - 0.5
    lo = np.maximum(np.floor(tex.min(0)).astype(int), 0)
    hi = np.minimum(np.ceil(tex.max(0)).astype(int), size-1)
    if np.any(hi < lo):
        continue
    xx, yy = np.meshgrid(np.arange(lo[0], hi[0]+1), np.arange(lo[1], hi[1]+1))
    a, b, c = tex
    denominator = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
    if abs(denominator) < 1e-8:
        continue
    wa = ((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denominator
    wb = ((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denominator
    wc = 1-wa-wb
    inside = (wa >= -1e-5) & (wb >= -1e-5) & (wc >= -1e-5)
    if not inside.any():
        continue
    py, px = yy[inside], xx[inside]
    coverage[py, px] = True
    if not nearby.any():
        continue
    world = (wa[inside, None]*pts[0]+wb[inside, None]*pts[1]+wc[inside, None]*pts[2]).astype(np.float32)
    seg_start, seg_delta, seg_len2 = start[nearby], delta[nearby], length2[nearby]
    dist = np.empty(len(world), dtype=np.float32)
    for offset in range(0, len(world), 1024):
        p = world[offset:offset+1024, None, :] - seg_start[None, :, :]
        t = np.clip((p*seg_delta[None, :, :]).sum(2)/seg_len2[None, :], 0, 1)
        difference = p - t[:, :, None]*seg_delta[None, :, :]
        dist[offset:offset+1024] = np.sqrt((difference*difference).sum(2).min(1))
    distance_map[py, px] = np.minimum(distance_map[py, px], dist)

original_coverage = coverage.copy()
for _ in range(16):
    padded_values = np.pad(distance_map, 1, mode='edge')
    padded_coverage = np.pad(coverage, 1, mode='constant')
    total, count = np.zeros_like(distance_map), np.zeros_like(distance_map)
    for dy in range(3):
        for dx in range(3):
            valid = padded_coverage[dy:dy+size, dx:dx+size]
            total += padded_values[dy:dy+size, dx:dx+size]*valid
            count += valid
    grow = (~coverage) & (count > 0)
    distance_map[grow] = total[grow]/count[grow]
    coverage[grow] = True

pixels = np.round(np.clip(distance_map/max_distance, 0, 1)*255).astype(np.uint8)
dest = Path('D:/PROJECT/GAME/WaifuTD/MessControl/ArtSource/MouthV4/Textures/Gum/SM_Gum_OuterEdgeDistance.png')
dest.parent.mkdir(parents=True, exist_ok=True)
Image.fromarray(pixels).save(dest)
report = {'mesh': data['asset'], 'source': dest.as_posix(), 'size': size, 'maximum_distance': max_distance,
    'boundary_segments': len(segments), 'uv_coverage': float(original_coverage.mean()),
    'texture_min': int(pixels[original_coverage].min()), 'texture_max': int(pixels[original_coverage].max())}
assert report['texture_min'] < 5 and report['texture_max'] == 255, report
Path('gum_fade_mask_report.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report))
