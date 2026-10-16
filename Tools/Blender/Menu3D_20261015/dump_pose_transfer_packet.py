"""Read-only evaluated triangle/loop-UV packet for source-registered atlas transfer."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--out', required=True)
p.add_argument('--include-interior', action='store_true')
opt = p.parse_args(sys.argv[sys.argv.index('--')+1:])
source = Path(opt.candidate).resolve()
out = Path(opt.out).resolve()
out.mkdir(parents=True, exist_ok=True)
before = hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(source))
scene = bpy.context.scene
control = bpy.data.objects['MC_JawsRoot']
control.animation_data_clear()
collections = ['MC_Jaws_Family', 'MenuGingiva_Geometry'] + (['MenuInterior_Geometry'] if opt.include_interior else [])
interior_names = {o.name for o in bpy.data.collections['MenuInterior_Geometry'].all_objects} if opt.include_interior else set()
meshes = sorted({o for col in collections
                 for o in bpy.data.collections[col].all_objects if o.type == 'MESH'}, key=lambda o:o.name)
arrays = {}
report = {'source': str(source), 'sha256': before, 'poses': {}, 'objects': {},
          'camera': {'closed': {'width':14.2,'pixels':[963,257]},
                     'open': {'width':16.,'pixels':[963,410]},
                     'center':[0,0,0], 'view_axis':'+Y', 'image_right':'+X', 'image_up':'+Z'}}
for label, amount in [('closed', 0.), ('open', 1.)]:
    control['OpenAmount'] = amount
    control.update_tag()
    scene.frame_set(42)
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in meshes:
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
        mesh.calc_loop_triangles()
        vertices = np.empty(len(mesh.vertices)*3, np.float32)
        mesh.vertices.foreach_get('co', vertices)
        vertices = vertices.reshape(-1,3)
        M = np.asarray(evaluated.matrix_world, dtype=np.float64)
        world = vertices @ M[:3,:3].T + M[:3,3]
        triangles = np.asarray([tuple(t.vertices) for t in mesh.loop_triangles], np.int32)
        loops = np.asarray([tuple(t.loops) for t in mesh.loop_triangles], np.int32)
        primary_uv = mesh.uv_layers.get('UV0')
        uv_policy = 'EXPLICIT_NAMED_UV0'
        if primary_uv is None and obj.name in interior_names:
            # Frozen V03 interior uses its authored render UV chart. Record
            # the actual name/policy rather than mislabel a projection layer.
            primary_uv = next((layer for layer in mesh.uv_layers if layer.active_render), None)
            uv_policy = 'EXPLICIT_FROZEN_INTERIOR_ACTIVE_RENDER_UV'
        if primary_uv is None:
            raise RuntimeError('Required unique UV0 layer missing: ' + obj.name)
        uv = np.empty(len(mesh.loops)*2,np.float32)
        primary_uv.data.foreach_get('uv', uv)
        uv = uv.reshape(-1,2)[loops]
        prefix = label + '::' + obj.name + '::'
        arrays[prefix+'world_vertices'] = world
        arrays[prefix+'triangles'] = triangles
        arrays[prefix+'UV0_triangles'] = uv
        arrays[prefix+'tri_material_index'] = np.asarray([t.material_index for t in mesh.loop_triangles],np.int16)
        arrays[prefix+'matrix_world'] = M
        report['objects'][obj.name] = {'materials':[m.name if m else None for m in mesh.materials],
                                      'vertices':len(world),'triangles':len(triangles),
                                      'uv_layer':primary_uv.name,
                                      'uv_source_policy':uv_policy,
                                      'uv_layers':[{'name':layer.name,'active_render':layer.active_render} for layer in mesh.uv_layers]}
        evaluated.to_mesh_clear()
    report['poses'][label] = {'OpenAmount':amount,'meshes':len(meshes)}
np.savez_compressed(out/'evaluated_pose_triangle_uv_packet.npz', **arrays)
report['source_unchanged'] = before == hashlib.sha256(source.read_bytes()).hexdigest()
(out/'packet_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if not report['source_unchanged']:
    raise RuntimeError('Source was modified')
print('EXACT_EVALUATED_TRANSFER_PACKET',len(meshes),str(out),flush=True)
