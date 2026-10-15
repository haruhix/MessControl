"""Bake actual five world-space mouth poses into a one-bone FBX for Unreal.

No character conversion: Unreal reuses its existing character and animations.
Open is the Basis. Four absolute sampled poses are relative morph targets;
the runtime interpolates adjacent hats, including zero keys at fully Open.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('--source', required=True)
p.add_argument('--out', required=True)
p.add_argument('--native-y-flip',action='store_true')
p.add_argument('--geometry-scale',type=float,default=1.)
opt = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
assert bpy.app.background, 'Never replace an unrelated GUI document'
source = Path(opt.source).resolve()
out = Path(opt.out).resolve()
task = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
assert out.is_relative_to(task / 'Assembly/UnrealHandoff')
assert not (out / 'SK_DAY03.fbx').exists(), 'Use a fresh revision'
out.mkdir(parents=True, exist_ok=True)
source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(source))
scene = bpy.context.scene
root = bpy.data.objects['MC_JawsRoot']
root.animation_data_clear()
mouth = sorted({o for name in ('MC_Jaws_Family', 'MenuGingiva_Geometry')
                for o in bpy.data.collections[name].all_objects if o.type == 'MESH'}, key=lambda o:o.name)
assert len(mouth) == 34, [o.name for o in mouth]
poses = {}
records = []
faces, uv_loops, mat_indices = [], [], []
materials = ['MC_Enamel', 'MC_Gum']
offset = 0
for amount in (1., 0., .25, .5, .75):
    root['OpenAmount'] = amount
    root.update_tag()
    scene.frame_set(42)
    bpy.context.view_layer.update()
    print('SAMPLE', amount, flush=True)
    dg = bpy.context.evaluated_depsgraph_get()
    parts = []
    for ob in mouth:
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh(preserve_all_data_layers=True, depsgraph=dg)
        coords = np.empty(len(me.vertices) * 3, dtype=np.float32)
        me.vertices.foreach_get('co', coords)
        matrix = np.asarray(ev.matrix_world)
        world = coords.reshape(-1, 3) @ matrix[:3, :3].T + matrix[:3, 3]
        if opt.native_y_flip:
            world[:,1] *= -1.
        parts.append(world.astype(np.float32) * opt.geometry_scale)
        if amount == 1.:
            uv = me.uv_layers.get('UV0')
            assert uv, ob.name
            # Bulk RNA access matters: repeatedly requesting uv.data in 5.x
            # recreates a legacy UV collection and is prohibitively slow.
            loop_vertices = np.empty(len(me.loops),dtype=np.int32)
            me.loops.foreach_get('vertex_index',loop_vertices)
            starts = np.empty(len(me.polygons),dtype=np.int32)
            counts = np.empty(len(me.polygons),dtype=np.int32)
            slots = np.empty(len(me.polygons),dtype=np.int32)
            me.polygons.foreach_get('loop_start',starts)
            me.polygons.foreach_get('loop_total',counts)
            me.polygons.foreach_get('material_index',slots)
            coords_uv = np.empty(len(me.loops)*2,dtype=np.float32)
            uv.data.foreach_get('uv',coords_uv)
            polygon_indices = np.split(loop_vertices+offset,np.cumsum(counts)[:-1])
            uv_polygons = np.split(coords_uv.reshape(-1,2),np.cumsum(counts)[:-1])
            faces.extend(tuple(row[::-1] if opt.native_y_flip else row) for row in polygon_indices)
            uv_loops.extend(np.concatenate([row[::-1] if opt.native_y_flip else row for row in uv_polygons]))
            slot_map = [materials.index(m.name.split('.')[0]) for m in me.materials]
            mat_indices.extend(np.asarray(slot_map,dtype=np.int32)[slots].tolist())
            records.append({'mesh':ob.name, 'vertex_start':offset, 'vertex_count':len(me.vertices)})
            offset += len(me.vertices)
        ev.to_mesh_clear()
        print('PART', ob.name, amount, flush=True)
    poses[amount] = np.concatenate(parts)
assert all(a.shape == poses[1.].shape for a in poses.values())
np.savez_compressed(out / 'source_world_poses_cm.npz', **{str(k):v*(100./opt.geometry_scale) for k,v in poses.items()})
bpy.ops.wm.read_factory_settings(use_empty=True)
mesh = bpy.data.meshes.new('MC_DAY03_Surface')
mesh.from_pydata(poses[1.].tolist(), [], faces)
mesh.update()
ob = bpy.data.objects.new('SK_DAY03', mesh)
bpy.context.scene.collection.objects.link(ob)
for name in materials:
    mesh.materials.append(bpy.data.materials.new(name))
for poly, material in zip(mesh.polygons, mat_indices):
    poly.material_index = material
    poly.use_smooth = True
uv = mesh.uv_layers.new(name='UV0')
uv.data.foreach_set('uv', np.asarray(uv_loops, dtype=np.float32).ravel())
ob.shape_key_add(name='Basis')
for amount, name in ((0.,'Pose_000'),(.25,'Pose_025'),(.5,'Pose_050'),(.75,'Pose_075')):
    key = ob.shape_key_add(name=name)
    key.data.foreach_set('co', poses[amount].ravel())
    key.value = 0.
arm = bpy.data.armatures.new('MC_MenuMouth_Skeleton')
rig = bpy.data.objects.new('MC_MenuMouth_Rig', arm)
bpy.context.scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
bone = arm.edit_bones.new('root')
bone.head, bone.tail = (0.,0.,0.), (0.,0.,100.)
bpy.ops.object.mode_set(mode='OBJECT')
group = ob.vertex_groups.new(name='root')
group.add(list(range(len(mesh.vertices))), 1., 'REPLACE')
modifier = ob.modifiers.new('RootSkin', 'ARMATURE')
modifier.object = rig
ob.parent = rig
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.context.scene.unit_settings.system = 'NONE'
bpy.ops.wm.save_as_mainfile(filepath=str(out / 'MC_DAY03_UnrealMorphSource.blend'))
bpy.ops.export_scene.fbx(filepath=str(out / 'SK_DAY03.fbx'), use_selection=True,
    object_types={'MESH','ARMATURE'}, use_mesh_modifiers=False,
    add_leaf_bones=False, use_armature_deform_only=True,
    bake_anim=False, path_mode='STRIP', axis_forward='-Y', axis_up='Z',
    apply_unit_scale=False, apply_scale_options='FBX_SCALE_NONE',
    mesh_smooth_type='FACE', use_tspace=False)
# Frozen physical cheek/tongue volumes are separate opening-only StaticMeshes.
interior_source = task / 'Interior/V03/MouthInterior.blend'
with bpy.data.libraries.load(str(interior_source), link=False) as (src, dst):
    dst.collections = ['MenuInterior_Geometry']
bpy.context.scene.collection.children.link(dst.collections[0])
interior_records = []
for source_ob in sorted((o for o in dst.collections[0].all_objects if o.type == 'MESH'),key=lambda o:o.name):
    ev = source_ob.evaluated_get(bpy.context.evaluated_depsgraph_get())
    me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=bpy.context.evaluated_depsgraph_get())
    data = np.empty(len(me.vertices) * 3, dtype=np.float32)
    me.vertices.foreach_get('co', data)
    matrix = np.asarray(ev.matrix_world)
    data = (data.reshape(-1,3) @ matrix[:3,:3].T + matrix[:3,3]) * opt.geometry_scale
    if opt.native_y_flip:
        data[:,1] *= -1.
        # Flip mesh winding and matching loop data together in Blender.
        me.flip_normals()
    me.vertices.foreach_set('co', data.astype(np.float32).ravel())
    layer = me.uv_layers.get('UV0') or me.uv_layers.active
    keep = layer.name
    for other in list(me.uv_layers):
        if other.name != keep:
            me.uv_layers.remove(other)
    me.uv_layers[0].name = 'UV0'
    name = 'SM_DAY03_Tongue' if 'Tongue' in source_ob.name else 'SM_DAY03_Interior'
    copy = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(copy)
    bpy.ops.object.select_all(action='DESELECT')
    copy.select_set(True)
    bpy.context.view_layer.objects.active = copy
    bpy.ops.export_scene.fbx(filepath=str(out / (name + '.fbx')),use_selection=True,
        object_types={'MESH'},bake_anim=False,path_mode='STRIP',axis_forward='-Y',axis_up='Z',
        apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE')
    interior_records.append({'asset':name,'source':source_ob.name,'uv0_source':keep})
report = {'status':'Unreal skeletal import handoff; actual native validation required',
    'source':str(source), 'source_sha256':source_sha,
    'source_unit_to_cm':100., 'fbx_geometry_scale':opt.geometry_scale, 'native_y_flip':opt.native_y_flip, 'vertices':len(mesh.vertices), 'polygons':len(mesh.polygons),
    'triangles':sum(len(p.vertices)-2 for p in mesh.polygons),
    'materials':materials, 'primary_uv':'UV0', 'parts':records, 'interior':interior_records,
    'morphs':['Pose_000','Pose_025','Pose_050','Pose_075'],
    'control':'w_i = max(0, 1-abs(OpenAmount-sample_i)/.25); Open is Basis',
    'bounds_open_cm':[(poses[1.].min(axis=0)*(100./opt.geometry_scale)).tolist(),(poses[1.].max(axis=0)*(100./opt.geometry_scale)).tolist()],
    'fbx_length_policy':'Blender unit is meter; FBX metadata converts to native centimeters. Do not pre-multiply vertices by 100 a second time.',
    'source_unchanged':hashlib.sha256(source.read_bytes()).hexdigest() == source_sha}
assert report['source_unchanged']
(out / 'unreal_mouth_handoff.json').write_text(json.dumps(report,indent=2), encoding='utf8')
print('MC_UNREAL_MOUTH_HANDOFF ' + json.dumps(report))
