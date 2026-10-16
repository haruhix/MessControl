"""Seam-driven DAY01/02 UV candidate, unchanged accepted geometry and morphs.

Three logical crown charts: coherent labial/incisal body, hidden lingual panel,
hidden cervical cap. No SmartProject, texture projection or cap fan UVs.
Exports remain candidates until actual checker/atlas/technical and root review.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import sys
import bpy
import bmesh
import numpy as np

assert bpy.app.background
P = argparse.ArgumentParser()
P.add_argument('--day', choices=('DAY01', 'DAY02'), required=True)
P.add_argument('--source-revision', required=True)
P.add_argument('--source-sha', required=True)
P.add_argument('--revision', default='UV_v01')
P.add_argument('--preserve-enamel', action='store_true')
P.add_argument('--posterior-gum-cuts', action='store_true')
P.add_argument('--gum-cap-min-px', type=int, default=0)
P.add_argument('--render', action='store_true')
A = P.parse_args(sys.argv[sys.argv.index('--')+1:])
ROOT = Path(__file__).resolve().parents[3]/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v09_FamilyReferenceForms'
SRC = ROOT/A.day/A.source_revision
OUT = ROOT/A.day/A.revision
assert OUT != SRC and OUT.is_relative_to(ROOT)
OUT.mkdir(parents=True, exist_ok=True)
source = SRC/f'MC_{A.day}_ReferenceForms.blend'
if not source.is_file():
    source = SRC/f'MC_{A.day}_UnrealMorphSource.blend'
assert hashlib.sha256(source.read_bytes()).hexdigest() == A.source_sha
(OUT/'family_finish_uv_export_executed.py').write_bytes(Path(__file__).read_bytes())
meta_path = SRC/('unreal_mouth_handoff.json' if A.preserve_enamel else 'reference_forms_handoff.json')
meta = json.loads(meta_path.read_text())
parts = meta['parts']
bpy.ops.wm.open_mainfile(filepath=str(source))
o = bpy.data.objects['SK_'+A.day]
me = o.data
old = {}
for k in me.shape_keys.key_blocks:
    co = np.empty(len(me.vertices)*3, np.float32)
    k.data.foreach_get('co', co)
    old[k.name] = co.reshape(-1, 3).copy()
    k.value = 0.
co = old['Basis']
me.calc_loop_triangles()
lv = np.empty(len(me.loops), np.int32)
me.loops.foreach_get('vertex_index', lv)
le = np.empty(len(me.loops), np.int32)
me.loops.foreach_get('edge_index', le)
starts, counts, slots = [np.empty(len(me.polygons), np.int32) for unused in range(3)]
for name, array in [('loop_start', starts), ('loop_total', counts), ('material_index', slots)]:
    me.polygons.foreach_get(name, array)
facepart = np.searchsorted([p['vertex_start'] for p in parts], lv[starts], side='right')-1
edge_faces = [[] for unused in me.edges]
for poly in me.polygons:
    for li in poly.loop_indices:
        edge_faces[le[li]].append(poly.index)
assert all(len(f) == 2 for f in edge_faces), 'Closed source solids required'
labels = np.full(len(me.polygons), -1, np.int32)
seams = np.zeros(len(me.edges), dtype=bool)
chart_policy = []
for pi, p in enumerate(parts):
    st, en = p['vertex_start'], p['vertex_start']+p['vertex_count']
    fids = np.flatnonzero(facepart == pi)
    lower = p['row'] == 'Lower'
    if p['material_slot'] == 0:
        structured_human = A.day == 'DAY01'
        if structured_human:
            assert len(fids) == 6*32*32 and p['vertex_count'] == 6146
        else:
            assert len(fids) == 60*64+2*64 and p['vertex_count'] == 61*64+2
        c = co[st:en]
        root, tip = (c[:, 2].min(), c[:, 2].max()) if lower else (c[:, 2].max(), c[:, 2].min())
        t = (c[:, 2]-root)/(tip-root)
        ft = np.array([t[lv[starts[f]:starts[f]+counts[f]]-st].mean() for f in fids])
        allowed = set(fids[ft < .055].tolist())
        seed = int(fids[np.argmin(ft)])
        cap, queue = {seed}, [seed]
        while queue:
            f = queue.pop()
            for li in me.polygons[f].loop_indices:
                for other in edge_faces[le[li]]:
                    if other in allowed and other not in cap:
                        cap.add(other)
                        queue.append(other)
        assert len(cap) > 100
        for local, f in enumerate(fids):
            if structured_human:
                group, cell = divmod(local, 32*32)
                # Source topology records six coherent cube patches in stable
                # order; separator follows the exact local Y=0 edge grid.
                front = (cell//32 >= 16) if group < 2 else (group == 3 if group < 4 else cell % 32 >= 16)
            else:
                # A volumetric crown loft: front half wraps through the curved
                # incisal tip, hidden lingual panel occupies the opposite half.
                front = local % 64 < 32
            labels[f] = pi*3+(2 if f in cap else 0 if front else 1)
        chart_policy.append(dict(part=p['mesh'], cervical_cap_faces=len(cap), cap_root_to_tip_threshold=.055,
                                 policy='coherent labial/incisal body + hidden lingual panel + cervical cap; no front poles/fan charts'))
    else:
        sections, radial = 340, 64
        assert p['vertex_count'] == (sections+1)*radial+2
        labels[fids[:sections*radial]] = pi*3
        labels[fids[sections*radial:sections*radial+radial]] = pi*3+1
        labels[fids[sections*radial+radial:]] = pi*3+2
        cut_rings = []
        if A.posterior_gum_cuts:
            last = max(q['arch_distance'] for q in parts if q['material_slot'] == 0 and q['row'] == p['row'])
            maximum = last+(.52 if A.day == 'DAY01' else .5616)
            first = round((maximum-8.8)/(2*maximum)*sections)
            assert 1 < first < 100
            cut_rings = [first, sections-first]
        for edge in me.edges:
            aa, bb = [int(v)-st for v in edge.vertices]
            if 0 <= aa < (sections+1)*radial and 0 <= bb < (sections+1)*radial:
                if aa % radial == bb % radial == 47:
                    seams[edge.index] = True
                if aa//radial == bb//radial and aa//radial in cut_rings:
                    seams[edge.index] = True
        chart_policy.append(dict(part=p['mesh'], policy='one continuous hero labial body, hidden lingual seam, optional two posterior-return cuts +two closed return caps',
                                 lingual_seam_radial_index=47, posterior_cut_rings=cut_rings))
for edge in me.edges:
    fs = edge_faces[edge.index]
    if labels[fs[0]] != labels[fs[1]]:
        seams[edge.index] = True
assert np.all(labels >= 0)
me.edges.foreach_set('use_seam', seams)
if not me.uv_layers.get('UV0'):
    me.uv_layers.new(name='UV0')
sys.path.insert(0, 'C:/Users/user/.codex/skills/blender-uv-baking/scripts')
import bx_uvbake as ub
bpy.ops.object.select_all(action='DESELECT')
o.select_set(True)
bpy.context.view_layer.objects.active = o
solver = []
enamel_before = ub._get_uv(me, 'UV0')[np.repeat(slots == 0, counts)].copy() if A.preserve_enamel else None
for material, iterations in [(0, 60), (1, 100)]:
    if material == 0 and A.preserve_enamel:
        solver.append(dict(material=material, final_method='BIT_EXACT_PREVIOUS_ACCEPTED_CANDIDATE_UV', source=str(source)))
        continue
    print('FAMILY_UV_SOLVE_BEGIN', material, flush=True)
    with ub._edit_mode([o], 'UV0', select_all=False):
        bm = bmesh.from_edit_mesh(me)
        for f in bm.faces:
            f.select_set(f.material_index == material)
        bmesh.update_edit_mesh(me)
        result = bpy.ops.uv.unwrap(method='ANGLE_BASED', fill_holes=True, correct_aspect=True, margin=.001)
        assert result == {'FINISHED'}
        result = bpy.ops.uv.unwrap(method='MINIMUM_STRETCH', no_flip=True, iterations=iterations,
                                   fill_holes=True, correct_aspect=True, margin=.001)
        assert result == {'FINISHED'}
    data = ub._Arrays(o, 'UV0')
    labs, unused = ub._uv_islands(data)
    uv = data.uv.copy()
    for la in np.unique(labs[slots == material]):
        lm = (labs == la)[data.lf]
        pts, xyz = uv[lm], data.co[data.lv[lm]]
        center = pts.mean(0)
        axis = xyz[:, 2] if material == 0 else xyz[:, 0]
        if np.ptp(axis) > (.35 if material == 0 else 5.):
            direction = np.mean((pts-center)*(axis-axis.mean())[:, None], axis=0)
            # Root-to-tip vertical for tooth bodies; gum body long axis to U.
            owner = parts[int(facepart[np.flatnonzero(labs == la)[0]])]
            target = math.pi*.5*(1. if owner['row'] == 'Lower' else -1.) if material == 0 else 0.
            angle = target-math.atan2(direction[1], direction[0])
            cs, sn = math.cos(angle), math.sin(angle)
            uv[lm] = (pts-center)@np.array([[cs, sn], [-sn, cs]])+center
    ub._set_uv(me, uv, 'UV0')
    data.free()
    with ub._edit_mode([o], 'UV0', select_all=False):
        bm = bmesh.from_edit_mesh(me)
        for f in bm.faces:
            f.select_set(f.material_index == material)
        bmesh.update_edit_mesh(me)
        bpy.ops.uv.average_islands_scale()
        result = bpy.ops.uv.pack_islands(udim_source='CLOSEST_UDIM', rotate=False, scale=True, merge_overlap=False,
                                         margin_method='FRACTION', margin=.00390625)
        assert result == {'FINISHED'}
    if material == 1 and A.gum_cap_min_px:
        data = ub._Arrays(o, 'UV0')
        labs, unused = ub._uv_islands(data)
        newuv = data.uv.copy()
        for la in np.unique(labs[slots == 1]):
            fm = labs == la
            # Exactly64 fan faces form each physically tiny closed return cap;
            # hidden geometry, not a hero crown fan chart.
            if int(fm.sum()) != 64:
                continue
            lm = fm[data.lf]
            pts = newuv[lm]
            minimum = np.ptp(pts, axis=0).min()*4096
            if minimum < A.gum_cap_min_px:
                center = pts.mean(0)
                newuv[lm] = (pts-center)*(A.gum_cap_min_px*1.10/max(minimum, 1e-10))+center
        ub._set_uv(me, newuv, 'UV0')
        data.free()
        with ub._edit_mode([o], 'UV0', select_all=False):
            bm = bmesh.from_edit_mesh(me)
            for f in bm.faces: f.select_set(f.material_index == 1)
            bmesh.update_edit_mesh(me)
            bpy.ops.uv.pack_islands(udim_source='CLOSEST_UDIM', rotate=False, scale=True, merge_overlap=False,
                                   margin_method='FRACTION', margin=.00390625)
    solver.append(dict(material=material, initialization='ANGLE_BASED', final_method='MINIMUM_STRETCH', iterations=iterations,
                        no_flip=True, pack_pair_gap_px=32, pack_border_px=16))
    print('FAMILY_UV_SOLVE_DONE', material, flush=True)
uv = ub._get_uv(me, 'UV0').astype(np.float32)
if A.preserve_enamel:
    assert np.array_equal(uv[np.repeat(slots == 0, counts)], enamel_before), 'Gum repair changed enamel UV'
assert np.isfinite(uv).all() and uv.min() >= 0. and uv.max() <= 1.
for k in me.shape_keys.key_blocks:
    values = np.empty(len(me.vertices)*3, np.float32)
    k.data.foreach_get('co', values)
    assert np.array_equal(values.reshape(-1, 3), old[k.name]), 'UV operation altered accepted geometry/morph'
me.calc_loop_triangles()
tri = np.array([t.loops[:] for t in me.loop_triangles], np.int32)
trimat = np.array([t.material_index for t in me.loop_triangles], np.int32)
tripoly = np.array([t.polygon_index for t in me.loop_triangles], np.int32)
root_weight, tip_weight, margin_weight = [np.zeros(len(co), np.float32) for unused in range(3)]
for p in parts:
    st, en = p['vertex_start'], p['vertex_start']+p['vertex_count']
    c = co[st:en]
    lower = p['row'] == 'Lower'
    if p['material_slot'] == 0:
        root, tip = (c[:, 2].min(), c[:, 2].max()) if lower else (c[:, 2].max(), c[:, 2].min())
        t = np.clip((c[:, 2]-root)/(tip-root), 0, 1)
        r, ttip = np.clip(t/.5, 0, 1), np.clip((t-.7)/.3, 0, 1)
        root_weight[st:en] = 1-r*r*(3-2*r)
        tip_weight[st:en] = ttip*ttip*(3-2*ttip)
    else:
        rings = c[:341*64].reshape(341, 64, 3)
        margin = rings[:, :, 2].max(1) if lower else rings[:, :, 2].min(1)
        margin_weight[st:st+341*64] = np.exp(-((rings[:, :, 2]-margin[:, None])/.25)**2).ravel()
np.savez_compressed(OUT/'texture_mesh_packet.npz', vertices=co, loop_vertex_indices=lv, loop_uv=uv,
                     triangle_loop_indices=tri, triangle_material_indices=trimat, polygon_material_indices=slots,
                     crown_root_weight=root_weight, crown_tip_weight=tip_weight, gum_margin_weight=margin_weight)
np.savez_compressed(OUT/'source_world_poses_cm.npz', **{str({'Basis':1., 'Pose_000':0., 'Pose_025':.25, 'Pose_050':.5, 'Pose_075':.75}[k]):v*100 for k, v in old.items()})
qa = OUT/'QA_Producer'
qa.mkdir(exist_ok=True)
np.savez_compressed(qa/'source_uv_extraction.npz', co=co, uv=uv, loopverts=lv, loopedges=le, starts=starts,
                     counts=counts, material=slots, triloops=tri, tripoly=tripoly)
(qa/'source_uv_extraction.json').write_text(json.dumps(dict(source=str(OUT/f'MC_{A.day}_UnrealMorphSource.blend'),
    material_names=[m.name for m in me.materials], producer_extraction=True, independent=False, read_only=False), indent=2))
uvmeta = dict(status='UNACCEPTED_UV_CANDIDATE_REQUIRES_ACTUAL_ATLAS_CHECKER_AND_TECHNICAL_REVIEW', resolution=4096,
              texture_sets=['MC_Enamel', 'MC_Gum'], policy='Unique UV0 per material, three logical crown charts, three per gum, no SmartProject/repeat stacking/fan UV',
              chart_policy=chart_policy, solver=solver, geometry_morphs='bit-exact accepted shape source five samples',
              enamel_UV_bit_exact_parent=A.preserve_enamel, gum_cap_min_px=A.gum_cap_min_px,
              gum_coverage_policy='Surface stretch, coherent hero face and native texel density take priority over an arbitrary occupancy number')
(OUT/'painter_uv_handoff.json').write_text(json.dumps(uvmeta, indent=2))
meta.update(parent_source=str(source), parent_source_sha256=A.source_sha, source=str(OUT/f'MC_{A.day}_UnrealMorphSource.blend'),
            primary_uv='UV0', status=uvmeta['status'], painter_uv_handoff='painter_uv_handoff.json', source_unit_to_cm=100.,
            fbx_geometry_scale=1., bounds_open_cm=[(co.min(0)*100).tolist(), (co.max(0)*100).tolist()],
            bounds_each_pose_cm={k:dict(min=(v.min(0)*100).tolist(), max=(v.max(0)*100).tolist()) for k,v in old.items()})
bpy.ops.wm.save_as_mainfile(filepath=meta['source'])
meta['source_sha256'] = hashlib.sha256(Path(meta['source']).read_bytes()).hexdigest()
(OUT/'unreal_mouth_handoff.json').write_text(json.dumps(meta, indent=2))
bpy.ops.object.select_all(action='DESELECT')
o.select_set(True)
bpy.data.objects['MC_MenuMouth_Rig'].select_set(True)
bpy.context.view_layer.objects.active = o
bpy.ops.export_scene.fbx(filepath=str(OUT/f'SK_{A.day}.fbx'), use_selection=True, object_types={'MESH','ARMATURE'},
    use_mesh_modifiers=False, add_leaf_bones=False, use_armature_deform_only=True, bake_anim=False, path_mode='STRIP',
    axis_forward='-Y', axis_up='Z', apply_unit_scale=False, apply_scale_options='FBX_SCALE_NONE', mesh_smooth_type='FACE', use_tspace=False)

# Explicit corner normals avoid a static Painter OBJ using a previous pose's normals.
normals = np.empty(len(me.corner_normals)*3, np.float32)
me.corner_normals.foreach_get('vector', normals)
normals = normals.reshape(-1, 3)
with (OUT/f'MC_{A.day}_Painter_Open.obj').open('w', newline='\n') as f:
    f.write(f'# Frozen source geometry and UV0 candidate\nmtllib MC_{A.day}_Painter_Open.mtl\n')
    for v in co: f.write('v %.8f %.8f %.8f\n'%tuple(v))
    for v in uv: f.write('vt %.8f %.8f\n'%tuple(v))
    for v in normals: f.write('vn %.8f %.8f %.8f\n'%tuple(v))
    last = None
    f.write('s 1\n')
    for s, c, slot, pi in zip(starts, counts, slots, facepart):
        if pi != last:
            f.write('g '+parts[pi]['mesh']+'\nusemtl '+('MC_Enamel' if slot == 0 else 'MC_Gum')+'\n')
            last = pi
        f.write('f '+' '.join(f'{lv[i]+1}/{i+1}/{i+1}' for i in range(s, s+c))+'\n')
(OUT/f'MC_{A.day}_Painter_Open.mtl').write_text('newmtl MC_Enamel\nKd .8 .8 .8\nnewmtl MC_Gum\nKd .8 .8 .8\n')
receipt = dict(status=uvmeta['status'], geometry_morphs_bit_exact=True,
    files={p.name:dict(bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in OUT.iterdir() if p.is_file()})
(OUT/'producer_handoff_receipt.json').write_text(json.dumps(receipt, indent=2))
assert hashlib.sha256(source.read_bytes()).hexdigest() == A.source_sha
if A.render:
    for m in me.materials:
        nt = m.node_tree
        bs = nt.nodes.get('Principled BSDF')
        checker = nt.nodes.new('ShaderNodeTexChecker')
        checker.inputs['Color1'].default_value = (.04, .17, .44, 1)
        checker.inputs['Color2'].default_value = (.85, .85, .85, 1)
        checker.inputs['Scale'].default_value = 48
        tc = nt.nodes.new('ShaderNodeTexCoord')
        nt.links.new(tc.outputs['UV'], checker.inputs['Vector'])
        nt.links.new(checker.outputs['Color'], bs.inputs['Base Color'])
        bs.inputs['Roughness'].default_value = .5
    scene = bpy.context.scene
    cam = scene.camera
    scene.render.resolution_x, scene.render.resolution_y = 1400, 788
    for name, loc, typ in [('checker_front_open', (0,35,0), 'ORTHO'), ('checker_oblique_open', (12,30,7), 'PERSP'),
                            ('checker_back_open', (0,-35,0), 'ORTHO')]:
        cam.location = loc
        cam.rotation_euler = (-cam.location).to_track_quat('-Z','Y').to_euler()
        cam.data.type, cam.data.ortho_scale, cam.data.lens = typ, 19.5, 49.5
        scene.render.filepath = str(OUT/(name+'.png'))
        bpy.ops.render.render(write_still=True)
print('FAMILY_UV_CANDIDATE_DONE', str(OUT), flush=True)
