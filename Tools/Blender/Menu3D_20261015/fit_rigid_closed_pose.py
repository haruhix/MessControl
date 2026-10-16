"""CPU diagnostic: one rigid 3D upper-jaw pose against closed source silhouettes.

Uses actual canonical mesh vertices; does not modify Blender files or certify fit.
The objective is convex support distance, followed by a separate actual mask gate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

os.environ['OPENBLAS_NUM_THREADS']='2'
os.environ['OMP_NUM_THREADS']='2'
import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
parser = argparse.ArgumentParser()
parser.add_argument('--vertices', required=True)
parser.add_argument('--out', required=True)
args = parser.parse_args()
vertex_path = Path(args.vertices)
out = Path(args.out)
out.mkdir(parents=True, exist_ok=True)
source = json.loads((ROOT/'Evidence/SourceAnalysis/ClosedPoseFit/upper_pose_fit.json').read_text())
archive = np.load(vertex_path)
angles = np.linspace(0, 2*np.pi, 24, endpoint=False)
directions = np.column_stack((np.cos(angles), np.sin(angles)))
parts = []
full_vertices = {}
golden = np.pi*(3-np.sqrt(5))
sphere_y = 1-2*(np.arange(2048)+.5)/2048
sphere_r = np.sqrt(1-sphere_y*sphere_y)
sphere_directions = np.column_stack((sphere_r*np.cos(np.arange(2048)*golden),
                                    sphere_y,sphere_r*np.sin(np.arange(2048)*golden)))
for part in source['parts']:
    name = part['name']
    if name not in archive:
        continue
    vertices = archive[name].astype(np.float64)
    full_vertices[name] = vertices
    # A deterministic convex support subset accelerates the finite-difference
    # iterations. The final report is always recomputed using every mesh vertex.
    selected = set()
    for block in np.array_split(sphere_directions,16):
        selected.update(np.argmax(vertices @ block.T,axis=0).tolist())
    vertices = vertices[sorted(selected)]
    target = np.asarray(part['closed_polygon_xy'], dtype=np.float64)
    parts.append((name, vertices, target, np.max(target @ directions.T, axis=0)))
if len(parts) != 6:
    raise RuntimeError(f'Expected all six upper crowns, got {[p[0] for p in parts]}')


def rotation(p):
    x,y,z = p[:3]
    cx,sx,cy,sy,cz,sz = np.cos(x),np.sin(x),np.cos(y),np.sin(y),np.cos(z),np.sin(z)
    rx=np.array([[1,0,0],[0,cx,-sx],[0,sx,cx]])
    ry=np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]])
    rz=np.array([[cz,-sz,0],[sz,cz,0],[0,0,1]])
    return rz @ ry @ rx


def project(vertices, p):
    v=vertices @ rotation(p).T
    scale=963/p[3]
    return np.column_stack(((v[:,0]+p[4])*scale+481.5,
                            128.5-(v[:,2]+p[5])*scale))


def residual(p):
    return np.concatenate([np.max(project(v,p) @ directions.T,axis=0)-target
                           for _,v,_,target in parts])


lower=np.array([np.deg2rad(-75),np.deg2rad(-20),np.deg2rad(-8),12.,-6.,-6.])
upper=np.array([np.deg2rad(75),np.deg2rad(20),np.deg2rad(8),18.,6.,6.])
steps=np.array([1e-4,1e-4,1e-4,1e-3,1e-4,1e-4])


def optimize(initial, active):
    p=np.clip(initial,lower,upper).copy()
    damping=.01
    best=float('inf')
    iterations=0
    for i in range(90):
        iterations=i+1
        r=residual(p)
        score=float(r@r)
        jac=[]
        for j in active:
            q=p.copy();q[j]+=steps[j]
            jac.append((residual(q)-r)/steps[j])
        jac=np.column_stack(jac)
        gram=jac.T@jac
        diagonal=np.maximum(np.diag(gram),1e-8)
        delta=np.linalg.solve(gram+damping*np.diag(diagonal),-jac.T@r)
        q=p.copy();q[active]+=delta;q=np.clip(q,lower,upper)
        next_score=float(residual(q)@residual(q))
        if next_score<score:
            p=q;damping=max(damping*.4,1e-7)
            if abs(score-next_score)<1e-6:
                break
        else:
            damping=min(damping*5,1e8)
            if damping>=1e8:
                break
        best=min(best,score)
    r=residual(p)
    return p,{'support_rmse_px':float(np.sqrt(np.mean(r*r))),
              'support_p95_abs_px':float(np.percentile(np.abs(r),95)),
              'support_max_abs_px':float(np.max(np.abs(r))), 'iterations':iterations}


models=[]
for degrees in (0,-20,20,-40,40,-60,60):
    # Closed source crowns are taller; initial global framing from measured widths.
    p=np.array([np.deg2rad(degrees),0.,0.,14.2,0.,-1.55])
    fitted,stats=optimize(p,[0,3,4,5])
    models.append((fitted,stats,'pitch_camera_translation'))
best_pitch=min(models,key=lambda p:p[1]['support_rmse_px'])
for p,_,_ in sorted(models,key=lambda p:p[1]['support_rmse_px'])[:3]:
    fitted,stats=optimize(p,[0,1,2,3,4,5])
    models.append((fitted,stats,'full_rigid_camera_translation'))
best=min(models,key=lambda p:p[1]['support_rmse_px'])
for i,(name,_,target,support) in enumerate(parts):
    parts[i]=(name,full_vertices[name],target,support)
for _,stats,_ in models:
    stats['optimization_support_sampling']='2048 deterministic world directions; final best rechecked with all vertices'
full_residual=residual(best[0])
best[1].update({'support_rmse_px':float(np.sqrt(np.mean(full_residual*full_residual))),
                'support_p95_abs_px':float(np.percentile(np.abs(full_residual),95)),
                'support_max_abs_px':float(np.max(np.abs(full_residual)))})
canvas=Image.open(source['source_closed']['path']).convert('RGB')
draw=ImageDraw.Draw(canvas)
part_stats=[]
for name,vertices,target,support in parts:
    projected=project(vertices,best[0])
    hull=cv2.convexHull(projected.astype(np.float32)).reshape(-1,2)
    draw.line([tuple(p) for p in np.vstack((hull,hull[0]))],fill=(255,50,205),width=2)
    draw.line([tuple(p) for p in np.vstack((target,target[0]))],fill=(75,255,211),width=1)
    error=np.max(projected@directions.T,axis=0)-support
    part_stats.append({'part':name,'support_rmse_px':float(np.sqrt(np.mean(error*error))),
                       'support_max_abs_px':float(np.max(np.abs(error)))})
canvas.save(out/'rigid_support_fit_overlay.png')
def describe(model):
    p,stats,kind=model
    return {'model':kind,**stats,'rotation_xyz_degrees':np.rad2deg(p[:3]).tolist(),
            'rotation_matrix_world':rotation(p).tolist(),'camera_ortho_width':float(p[3]),
            'jaw_translation_world':[float(p[4]),0.,float(p[5])],
            'closed_camera_center_world':[0.,0.,0.],
            'projection':'x=481.5+(Rx+Tx)*963/W; y=128.5-(Rz+Tz)*963/W'}
report={'schema':'rigid_closed_support_fit.v1','status':'CPU_DIAGNOSTIC_NOT_ACTUAL_RENDER_ACCEPTANCE',
        'vertex_file':str(vertex_path),'vertex_sha256':hashlib.sha256(vertex_path.read_bytes()).hexdigest(),
        'objective':'24 convex silhouette supports per crown, equal weights; no per-crown deformation',
        'limitations':['Convex support ignores concave boundaries and label occlusion.',
                      'Actual Blender silhouette render and cervical gum attachment still require verification.',
                      'Unobserved depth is a modeled hypothesis. No proof of unique/global optimum.'],
        'best':describe(best),'best_pitch_only':describe(best_pitch),'parts':part_stats,
        'attempts':[describe(m) for m in models]}
(out/'rigid_support_fit.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'best':report['best'],'out':str(out)}))
