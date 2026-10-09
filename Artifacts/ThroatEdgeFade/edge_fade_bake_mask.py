import json
from collections import Counter, defaultdict
from pathlib import Path
import numpy as np
from PIL import Image

data = json.loads(Path('edge_fade_mesh.json').read_text())
vertices = {int(k): np.array(v) for k, v in data['vertices'].items()}
# Weld identical geometry so a UV seam is not treated as an outer edge.
ids = {}
welded = {}
for key, point in vertices.items():
    pos = tuple(np.round(point, 3))
    ids[key] = welded.setdefault(pos, len(welded))
positions = np.array(list(welded.keys()))
edges = Counter()
for tri in data['triangles']:
    a, b, c = [ids[i] for i in tri]
    for x, y in [(a, b), (b, c), (c, a)]:
        edges[tuple(sorted((x, y)))] += 1
adjacency = defaultdict(list)
for (a, b), count in edges.items():
    if count == 1:
        adjacency[a].append(b)
        adjacency[b].append(a)
loops = []
remaining = set(adjacency)
while remaining:
    start = min(remaining)
    loop = []
    stack = [start]
    while stack:
        i = stack.pop()
        if i not in remaining:
            continue
        remaining.remove(i)
        loop.append(i)
        stack.extend(adjacency[i])
    loops.append(loop)
report = []
for loop in loops:
    points = positions[loop]
    report.append({'count': len(loop), 'min': points.min(0).tolist(), 'max': points.max(0).tolist(), 'extent': np.ptp(points, axis=0).tolist()})
print('BOUNDARIES', json.dumps(report))
assert loops
# The broad external perimeter is distinct from the smaller inner aperture.
outer = max(loops, key=lambda loop: np.ptp(positions[loop], axis=0)[0] * np.ptp(positions[loop], axis=0)[2])
outer_set = set(outer)
segments = [(positions[a], positions[b]) for (a, b), count in edges.items() if count == 1 and a in outer_set and b in outer_set]
Path('edge_fade_boundary.json').write_text(json.dumps({'loops': report, 'outer_segment_count': len(segments), 'outer_vertices': positions[outer].tolist()}, indent=2))
if '--inspect' in __import__('sys').argv:
    raise SystemExit(0)

size = 1024
max_distance = 200.0
distance_map = np.full((size, size), max_distance, dtype=np.float32)
coverage = np.zeros((size, size), dtype=bool)
for tri, uv in zip(data['triangles'], data['uvs']):
    tex = np.array(uv) * size - 0.5
    lo = np.maximum(np.floor(tex.min(0)).astype(int), 0)
    hi = np.minimum(np.ceil(tex.max(0)).astype(int), size - 1)
    if np.any(hi < lo):
        continue
    xx, yy = np.meshgrid(np.arange(lo[0], hi[0]+1), np.arange(lo[1], hi[1]+1))
    a, b, c = tex
    denom = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
    if abs(denom) < 1e-8:
        continue
    wa = ((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denom
    wb = ((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denom
    wc = 1-wa-wb
    inside = (wa >= -1e-5) & (wb >= -1e-5) & (wc >= -1e-5)
    if not inside.any():
        continue
    pts = wa[inside, None]*vertices[tri[0]] + wb[inside, None]*vertices[tri[1]] + wc[inside, None]*vertices[tri[2]]
    dist = np.full(len(pts), max_distance)
    for p, q in segments:
        delta = q-p
        t = np.clip(((pts-p) @ delta) / max(float(delta @ delta), 1e-10), 0, 1)
        dist = np.minimum(dist, np.linalg.norm(pts-(p+t[:, None]*delta), axis=1))
    py, px = yy[inside], xx[inside]
    distance_map[py, px] = np.minimum(distance_map[py, px], dist)
    coverage[py, px] = True
# Dilate island borders to avoid bright fringes from filtering and mipmaps.
for _ in range(16):
    padded_values = np.pad(distance_map, 1, mode='edge')
    padded_coverage = np.pad(coverage, 1, mode='constant')
    total = np.zeros_like(distance_map)
    count = np.zeros_like(distance_map)
    for dy in range(3):
        for dx in range(3):
            valid = padded_coverage[dy:dy+size, dx:dx+size]
            total += padded_values[dy:dy+size, dx:dx+size] * valid
            count += valid
    grow = (~coverage) & (count > 0)
    distance_map[grow] = total[grow] / count[grow]
    coverage[grow] = True
pixels = np.round(np.clip(distance_map/max_distance, 0, 1)*255).astype(np.uint8)
dest = Path('D:/PROJECT/GAME/WaifuTD/MessControl/ArtSource/MouthV4/Textures/Exit/SK_Exit_OuterEdgeDistance.png')
dest.parent.mkdir(parents=True, exist_ok=True)
Image.fromarray(pixels).save(dest)
print('BAKED', dest.as_posix(), 'segments', len(segments), 'coverage', float(coverage.mean()))
