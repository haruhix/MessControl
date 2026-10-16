"""Read-only shape-candidate UV deformation audit; no unwrap or export."""
import bpy
import hashlib
import json
import sys
from pathlib import Path
import numpy as np

P = Path(sys.argv[sys.argv.index('--')+1])
SOURCE = P/'MC_DAY03_InteriorSource.blend'
before = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
ob = bpy.data.objects['SM_DAY03_Tongue']
me = ob.data
me.calc_loop_triangles()
co = np.array([v.co[:] for v in me.vertices], float)
loopv = np.array([l.vertex_index for l in me.loops])
uv = np.array([l.uv[:] for l in me.uv_layers['UV0'].data], float)
tri = np.array([t.loops[:] for t in me.loop_triangles])
v = co[loopv[tri]]
u = uv[tri]
e1 = v[:, 1]-v[:, 0]
e2 = v[:, 2]-v[:, 0]
area3 = np.linalg.norm(np.cross(e1, e2), axis=1)/2
b1 = u[:, 1]-u[:, 0]
b2 = u[:, 2]-u[:, 0]
areaUV = abs(b1[:, 0]*b2[:, 1]-b1[:, 1]*b2[:, 0])/2
density = np.sqrt(areaUV/np.maximum(area3, 1e-30))*4096
length = np.linalg.norm(e1, axis=1)
xx = np.sum(e1*e2, axis=1)/np.maximum(length, 1e-30)
yy = 2*area3/np.maximum(length, 1e-30)
j1 = b1/np.maximum(length[:, None], 1e-30)
j2 = (b2-j1*xx[:, None])/np.maximum(yy[:, None], 1e-30)
trace = (j1*j1+j2*j2).sum(1)
det = j1[:, 0]*j2[:, 1]-j1[:, 1]*j2[:, 0]
disc = np.sqrt(np.maximum(trace*trace-4*det*det, 0))
kappa = np.sqrt((trace+disc)/np.maximum(trace-disc, 1e-30))


def quant(a, w, q):
    order = np.argsort(a)
    cumulative = np.cumsum(w[order])
    return float(a[order[min(np.searchsorted(cumulative, q*cumulative[-1]), len(order)-1)]])


def angles(p):
    result = []
    for i in range(3):
        a = p[:, (i+1)%3]-p[:, i]
        b = p[:, (i+2)%3]-p[:, i]
        result.append(np.degrees(np.arccos(np.clip(np.sum(a*b, axis=1)/np.maximum(np.linalg.norm(a, axis=1)*np.linalg.norm(b, axis=1), 1e-30), -1, 1))))
    return np.stack(result, axis=1)


ae = abs(angles(v)-angles(u)).max(1)
scope = ('Dorsal UV0 repaired on approved Refine03; ventral unchanged; actual per-triangle geometric distortion measured.'
         if (P/'uv_repair_report.json').exists()
         else 'UV0 preserved from frozen r05; new geometric stretch measured after changing only dorsal Z')
report = {'source': str(SOURCE), 'source_sha256': before, 'read_only': True, 'scope': scope, 'surfaces': []}
for name, mask in [('dorsal', v[:, :, 2].mean(1) > -3.43), ('ventral', v[:, :, 2].mean(1) <= -3.43)]:
    weights = area3[mask]
    lo = quant(density[mask], weights, .05)
    median = quant(density[mask], weights, .5)
    hi = quant(density[mask], weights, .95)
    report['surfaces'].append({'surface': name, 'triangle_count': int(mask.sum()), 'area_source_units2': float(weights.sum()),
                               'density_p05_median_p95_px_per_source_unit': [lo, median, hi],
                               'density_intrachart_spread': hi/lo,
                               'kappa_p95': quant(kappa[mask], weights, .95), 'kappa_p99': quant(kappa[mask], weights, .99),
                               'angle_error_degrees_p95': quant(ae[mask], weights, .95)})
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == before
(P/'uv_deformation_review.json').write_text(json.dumps(report, indent=2))
print('LOW_TONGUE_UV_REVIEW', json.dumps(report))
