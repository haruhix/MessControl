"""Export corrected ARP weights and isolated clips; keep one stable export rig.

Run in background Blender with Auto-Rig Pro enabled. Add actions to clips.json
and rerun; preserve the export settings and rig hierarchy for future clips.
"""
import bpy
import json
import hashlib
import sys
import argparse
from pathlib import Path

ROOT = Path('D:/PROJECT/GAME/WaifuTD/MessControl')
OUT = ROOT / 'ArtSource/BossPhase3ARP'
OUT.mkdir(parents=True, exist_ok=True)
args = argparse.ArgumentParser()
args.add_argument('--source',default=str(OUT / ('BossPhase3ARP_Export.blend' if (OUT/'BossPhase3ARP_Export.blend').exists() else 'BossPhase3ARP_Source.blend')))
parsed = args.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
SOURCE = Path(parsed.source)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene = bpy.context.scene
rig = bpy.data.objects['rig']
meshes = [o for o in scene.objects if o.type == 'MESH' and len(o.data.vertices)
          and any(m.type == 'ARMATURE' and m.object == rig for m in o.modifiers)]
assert len(meshes) == 2
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
for collection in bpy.data.collections:
    collection.hide_viewport = False
for o in scene.objects:
    o.select_set(False)
for o in [rig, *meshes]:
    o.hide_set(False)
    o.hide_viewport = o.hide_select = False
    o.select_set(True)
bpy.context.view_layer.objects.active = rig
rig.animation_data_create()
for track in rig.animation_data.nla_tracks:
    track.mute = True
rig.animation_data.use_nla = False

settings = dict(arp_export_rig_type='UNIVERSAL', arp_engine_type='UNREAL',
    arp_rename_for_ue=True, arp_export_twist=True, arp_full_facial=True,
    arp_ue_root_motion=False, arp_export_rig_name='root', arp_units_x100=True,
    arp_global_scale=1., arp_ge_sel_only=True, arp_export_tex=False,
    arp_ge_add_dummy_mesh=False, arp_ge_force_rest_pose_export=True,
    arp_bake_type='ACTIONS', arp_bake_only_active=True, arp_bake_only_active_slot=True,
    arp_export_use_actlist=False, arp_export_separate_fbx=False,
    arp_simplify_fac=0., arp_ge_bake_sample=1., arp_export_triangulate=True)
for name, value in settings.items():
    setattr(scene, name, value)
scene.arp_bake_anim = False
mesh_file = OUT / 'SK_BossPhase3_ARP.fbx'
result = bpy.ops.arp.arp_export_fbx_panel(filepath=str(mesh_file))
assert 'FINISHED' in result and mesh_file.stat().st_size > 10000
report = {'source':str(SOURCE), 'original_source':'C:/Users/vasil/Desktop/Animation/arp_weights/carries_weights_fixed.blend',
          'fps':scene.render.fps / scene.render.fps_base, 'settings':settings,
          'mesh_file':mesh_file.name, 'mesh_vertices':sum(len(o.data.vertices) for o in meshes),
          'clips':[], 'source_samples':{}}
rest = [{'name':b.name,'parent':b.parent.name if b.parent else None,
         'matrix':[[round(v,6) for v in row] for row in b.matrix_local]} for b in rig.data.bones]
report['source_rig_signature']=hashlib.sha256(json.dumps(rest,sort_keys=True).encode()).hexdigest()
records = json.loads((OUT / 'clips.json').read_text(encoding='utf8'))
for record in records:
    action = bpy.data.actions[record['action']]
    rig.animation_data.action = action
    rig.animation_data.action_slot = action.slots[0]
    scene.arp_bake_anim = True
    scene.arp_frame_range_type = 'CUSTOM'
    scene.arp_export_start_frame = int(record['start'])
    scene.arp_export_end_frame = int(record['end'])
    scene.frame_start = int(record['start'])
    scene.frame_end = int(record['end'])
    samples = []
    for f in range(int(record['start']), int(record['end']) + 1, 3):
        scene.frame_set(f)
        samples.append({'frame':f,'bones':{n:list((rig.matrix_world @ rig.pose.bones[n].matrix).translation)
                       for n in ['root.x','foot.l','foot.r','hand.l','hand.r','head.x']}})
    report['source_samples'][record['name']] = samples
    target = OUT / (record['name'] + '.fbx')
    result = bpy.ops.arp.arp_export_fbx_panel(filepath=str(target))
    assert 'FINISHED' in result and target.stat().st_size > 10000
    report['clips'].append({**record, 'file':target.name,
        'duration_seconds':(record['end'] - record['start']) / report['fps'],
        'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    (OUT / 'ExportReport.json').write_text(json.dumps(report, indent=2))
    print('ARP_CLIP_EXPORTED', record['name'], flush=True)
scene.arp_frame_range_type = 'FULL'
rig.animation_data.action = bpy.data.actions['walk']
rig.animation_data.action_slot = rig.animation_data.action.slots[0]
scene.frame_start, scene.frame_end = 1, 44
scene.frame_set(1)
# Keep an editable copy containing the animator's rig, its meshes and controls.
# The initial complete snapshot preserves all unrelated source contents.
keep = {rig,*meshes}
keep.update(b.custom_shape for b in rig.pose.bones if b.custom_shape)
for obj in list(keep):
    parent=obj.parent
    while parent:
        keep.add(parent)
        parent=parent.parent
for obj in list(bpy.data.objects):
    if obj not in keep:
        bpy.data.objects.remove(obj,do_unlink=True)
for action in list(bpy.data.actions):
    if action.name not in {r['action'] for r in records}:
        bpy.data.actions.remove(action,do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'BossPhase3ARP_Export.blend'))
(OUT / 'ExportReport.json').write_text(json.dumps(report, indent=2))
print('ARP_EXPORT_PASS', json.dumps({'mesh':mesh_file.name,'clips':[r['name'] for r in report['clips']]}),flush=True)
