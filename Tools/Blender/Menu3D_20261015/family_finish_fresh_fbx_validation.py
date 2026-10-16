"""Read-only nine-pose source/fresh-FBX candidate verification.

Producer verification, not an independent art-direction acceptance claim.
Does not save either input or touch Unreal/Painter/GUI scenes.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys
import bpy
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

assert bpy.app.background
P = argparse.ArgumentParser()
P.add_argument('--handoff', type=Path, required=True)
A = P.parse_args(sys.argv[sys.argv.index('--')+1:])
OUT = A.handoff.resolve()
meta = json.loads((OUT/'unreal_mouth_handoff.json').read_text())
day = meta['day']
source, fbx = OUT/f'MC_{day}_UnrealMorphSource.blend', OUT/f'SK_{day}.fbx'
before = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, fbx)}


def nearest_mapping(a, b):
    tree = KDTree(len(b))
    for i, co in enumerate(b):
        tree.insert(Vector(co), i)
    tree.balance()
    mapping, errors = [], []
    for co in a:
        unused, i, error = tree.find(Vector(co))
        mapping.append(i)
        errors.append(error)
    return np.array(mapping), float(max(errors))


def read_scene(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if path.suffix == '.blend':
        bpy.ops.wm.open_mainfile(filepath=str(path))
    else:
        bpy.ops.import_scene.fbx(filepath=str(path), use_anim=False)
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    assert len(meshes) == 1
    o = meshes[0]
    m = o.data
    keys = m.shape_keys.key_blocks
    values = {}
    matrix = np.array(o.matrix_world)
    for k in keys:
        co = np.empty(len(m.vertices)*3, np.float32)
        k.data.foreach_get('co', co)
        co = co.reshape(-1,3).astype(float)
        values[k.name] = co@matrix[:3,:3].T+matrix[:3,3]
    samples = {}
    for amount in np.linspace(0.,1.,9):
        a = values['Basis'].copy()
        for name, t in [('Pose_000',0.),('Pose_025',.25),('Pose_050',.5),('Pose_075',.75)]:
            a += max(0.,1.-abs(amount-t)/.25)*(values[name]-values['Basis'])
        samples[str(amount)] = a
    arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    root = o.vertex_groups.get('root')
    weights_ok = root is not None and all(any(g.group == root.index and abs(g.weight-1.) < 1e-6 for g in v.groups) for v in m.vertices)
    m.calc_loop_triangles()
    triangles = np.array([t.vertices[:] for t in m.loop_triangles], np.int32)
    basis = values['Basis']
    q = basis[triangles]
    areas = np.linalg.norm(np.cross(q[:,1]-q[:,0],q[:,2]-q[:,0]),axis=1)*.5
    edgecoords = basis[np.array([e.vertices[:] for e in m.edges], np.int32)]
    edge_use = np.zeros(len(m.edges),np.int32)
    for l in m.loops: edge_use[l.edge_index] += 1
    uv = np.empty(len(m.loops)*2,np.float32)
    m.uv_layers['UV0'].data.foreach_get('uv',uv)
    info = dict(asset=str(path),sha256=before[str(path)],vertices=len(m.vertices),triangles=len(triangles),
                materials=[mat.name for mat in m.materials],UV_layers=[l.name for l in m.uv_layers],
                morphs=list(values), armatures=[dict(name=a.name,bones=[b.name for b in a.data.bones]) for a in arms],
                all_root_weights_one=weights_ok, all_polygons_smooth=all(p.use_smooth for p in m.polygons),
                nonmanifold_edges=int((edge_use != 2).sum()), minimum_triangle_area_source_units2=float(areas.min()),
                zero_length_edges=int((np.linalg.norm(edgecoords[:,1]-edgecoords[:,0],axis=1)<1e-9).sum()),
                UV_finite=bool(np.isfinite(uv).all()), UV_in_0_1=bool(uv.min()>=0 and uv.max()<=1),
                poses={str(a):dict(min=c.min(0).tolist(),max=c.max(0).tolist()) for a,c in samples.items()})
    return values, samples, info


source_values, source_poses, source_report = read_scene(source)
source_pair_errors = {}
for amount, c in source_poses.items():
    errors = []
    for pair in meta['mirror_pairs']:
        r, l = [meta['parts'][pair[key]] for key in ('right_part','left_part')]
        right = c[r['vertex_start']:r['vertex_start']+r['vertex_count']].copy()
        left = c[l['vertex_start']:l['vertex_start']+l['vertex_count']]
        right[:,0] *= -1.
        errors.append(float(np.linalg.norm(right-left,axis=1).max()))
    for p in [p for p in meta['parts'] if p['material_slot'] == 1]:
        g = c[p['vertex_start']:p['vertex_start']+p['vertex_count']]
        mapping = np.r_[np.arange(341*64).reshape(341,64)[::-1].ravel(),341*64+1,341*64]
        reflected = g.copy()
        reflected[:,0] *= -1.
        errors.append(float(np.linalg.norm(reflected-g[mapping],axis=1).max()))
    source_pair_errors[amount] = max(errors)
fresh_values, fresh_poses, fresh_report = read_scene(fbx)
mapping, basis_error = nearest_mapping(fresh_values['Basis'],source_values['Basis'])
mirror = fresh_values['Basis'].copy()
mirror[:,0] *= -1.
mirror_mapping, unused = nearest_mapping(mirror,fresh_values['Basis'])
pose_checks = {}
for amount, fresh in fresh_poses.items():
    expected = source_poses[amount][mapping]
    reflected = fresh.copy()
    reflected[:,0] *= -1.
    pose_checks[amount] = dict(fresh_vs_source_max_cm=float(np.linalg.norm(fresh-expected,axis=1).max()*100),
                               fresh_reflection_max_cm=float(np.linalg.norm(reflected-fresh[mirror_mapping],axis=1).max()*100),
                               source_paired_crown_gum_max_cm=source_pair_errors[amount]*100)
gates = dict(one_root=all(len(r['armatures'])==1 and r['armatures'][0]['bones']==['root'] for r in (source_report,fresh_report)),
             shape_key_contract=all(r['morphs']==['Basis','Pose_000','Pose_025','Pose_050','Pose_075'] for r in (source_report,fresh_report)),
             all_weights_root=all(r['all_root_weights_one'] for r in (source_report,fresh_report)),
             positive_surface_area=all(r['minimum_triangle_area_source_units2']>1e-12 for r in (source_report,fresh_report)),
             closed_no_zero_edges=all(r['nonmanifold_edges']==0 and r['zero_length_edges']==0 for r in (source_report,fresh_report)),
             UV_finite_0_1=all(r['UV_finite'] and r['UV_in_0_1'] for r in (source_report,fresh_report)),
             nine_pose_source_fbx_agreement=max(r['fresh_vs_source_max_cm'] for r in pose_checks.values())<.001,
             nine_pose_reflection=max(r['fresh_reflection_max_cm'] for r in pose_checks.values())<.001,
             source_pair_reflection=max(source_pair_errors.values())<1e-5,
             inputs_unchanged=all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==sha for p,sha in before.items()))
report = dict(status='PRODUCER_TECHNICAL_PASS' if all(gates.values()) else 'FAILED', independent=False,read_only=True,
              source=source_report,fresh_fbx=fresh_report,poses=pose_checks,gates=gates,
              limitations=['Actual Unreal runtime/material/clip review remains required','This is producer verification; independent review and actual checker/atlas gates remain separate'])
(OUT/'QA_Producer/fresh_fbx_nine_pose_validation.json').write_text(json.dumps(report,indent=2))
assert all(gates.values()),gates
print('FAMILY_FRESH_FBX_TECHNICAL_PASS',json.dumps(dict(gates=gates,max_errors_cm={k:max(p[k] for p in pose_checks.values()) for k in next(iter(pose_checks.values()))})))
