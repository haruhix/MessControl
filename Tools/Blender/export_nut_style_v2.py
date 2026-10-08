"""Bake saved MC Style v2 production actions onto the existing ARP UE skeleton.

Blender --factory-startup --background --disable-autoexec --python this.py -- --unit Tank|Wizard
Only new FBX files/reports are written; artist, v1/v2 blends and legacy FBX stay read-only.
FBX contains the same selected skinned source meshes for verified bind poses;
Unreal imports animation only and retains its existing meshes/skeletons.
"""
import argparse, hashlib, json, sys
from pathlib import Path
import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_nut_tank import SETTINGS, digest, enable_arp, fbx_inventory, select, sparse_pose_samples

ROOT = Path(__file__).resolve().parents[2]
SPECS = {
    'Tank': dict(source='Tank/Nut_Tank_MC_Style_v2.blend', legacy='NutTank',
                 action_prefix='MC_Tank_', file_prefix='A_NutTank_',
                 meshes=('SM_NutTank', 'SM_Sheld', 'SM_Club'),
                 clips=('Idle', 'Walk', 'WalkLeft', 'WalkRight', 'Melee', 'Jump',
                        'Transform', 'ChargeTell', 'ChargeLoop', 'ChargeRecovery')),
    'Wizard': dict(source='Wizard/NutWizard_Style_v2.blend', legacy='NutWizard',
                   action_prefix='MC_Wizard_', file_prefix='A_NutWizard_',
                   meshes=('SM_NutWizard',),
                   clips=('Idle', 'Walk', 'Cast', 'HeavyCast', 'Summon', 'Rain',
                          'Hit', 'Death', 'Melee')),
}

def rest_signature(rig):
    return [(b.name, b.parent.name if b.parent else None, b.use_deform,
             [list(row) for row in b.matrix_local]) for b in rig.data.bones]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--unit', choices=tuple(SPECS), required=True)
    parser.add_argument('--source', type=Path, help='Explicit saved production copy; never modifies the blend')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    spec = SPECS[args.unit]
    source = (args.source if args.source is not None else ROOT / 'ArtSource/NutAnimationStyle' / spec['source']).resolve()
    assert source.is_file() and source.suffix.lower() == '.blend', source
    out = ROOT / 'ArtSource/NutAnimationStyle/Exports' / args.unit
    out.mkdir(parents=True, exist_ok=True)
    legacy_file = ROOT / 'ArtSource' / spec['legacy'] / 'ExportReport.json'
    legacy = json.loads(legacy_file.read_text(encoding='utf8'))
    reference_name = legacy.get('mesh_file', 'SK_NutTank.fbx')
    legacy_reference = next(f for f in legacy['files'] if f['file'] == reference_name)['fbx']
    legacy_bind = {p['name']: p['matrix'] for p in legacy_reference['bind_poses']}
    original_sha = digest(source)
    addon = enable_arp()
    bpy.ops.wm.open_mainfile(filepath=str(source))
    # The saved Tank also contains an independent review clone. Export uses
    # only the original source scene/rig/meshes, never the review instance.
    rig = bpy.data.objects['rig']
    scene = next(s for s in bpy.data.scenes if rig.name in s.objects)
    if bpy.context.window:
        bpy.context.window.scene = scene
    meshes = [bpy.data.objects[n] for n in spec['meshes']]
    assert all(any(m.type == 'ARMATURE' and m.object == rig for m in o.modifiers) for o in meshes)
    rig.animation_data_create()
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    rig.animation_data.use_nla = False
    rig.animation_data.action = None
    properties = {b.name: {k: b[k] for k in b.keys() if isinstance(b[k], (bool, int, float))}
                  for b in rig.pose.bones}
    signature = rest_signature(rig)
    for name, value in SETTINGS.items():
        if not hasattr(scene, name):
            raise RuntimeError('Installed ARP lacks setting ' + name)
        setattr(scene, name, value)
    fps = scene.render.fps / scene.render.fps_base
    report = {'schema': 2, 'status': 'exporting', 'unit': args.unit,
              'source': str(source), 'source_sha256_before': original_sha,
              'blender_version': bpy.app.version_string, 'arp': addon, 'fps': fps,
              'export_settings': SETTINGS, 'legacy_reference': str(legacy_file),
              'legacy_hierarchy_sha256': legacy_reference['hierarchy_sha256'],
              'files': [], 'clips': [], 'source_pose_samples': {},
              'import_scope': 'Animation only against existing Unreal skeleton. No mesh or reference pose replacement.'}
    report_path = out / 'ExportReport.json'
    def save():
        report_path.write_text(json.dumps(report, indent=2), encoding='utf8')
    save()
    for label in spec['clips']:
        rig.animation_data.action = None
        for b in rig.pose.bones:
            b.matrix_basis = Matrix.Identity(4)
            for k, value in properties[b.name].items():
                b[k] = value
        action = bpy.data.actions[spec['action_prefix'] + label]
        rig.animation_data.action = action
        slots = [s for s in action.slots if s.target_id_type == 'OBJECT']
        assert len(slots) == 1, action.name
        rig.animation_data.action_slot = slots[0]
        start, end = map(round, action.frame_range)
        scene.arp_bake_anim = True
        scene.arp_frame_range_type = 'CUSTOM'
        scene.arp_export_start_frame, scene.arp_export_end_frame = start, end
        scene.frame_start, scene.frame_end = start, end
        report['source_pose_samples'][label] = sparse_pose_samples(scene, rig, start, end)
        scene.frame_set(start)
        bpy.context.view_layer.update()
        select([rig, *meshes], rig)
        filename = spec['file_prefix'] + label + '.fbx'
        path = out / filename
        result = bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
        assert 'FINISHED' in result and path.is_file() and path.stat().st_size > 10000, filename
        info = fbx_inventory(path)
        assert info['hierarchy_sha256'] == legacy_reference['hierarchy_sha256'], ('Hierarchy differs', filename)
        assert info['root_animation_constant'], ('Root moves', filename)
        current_bind = {p['name']: p['matrix'] for p in info['bind_poses']}
        assert set(legacy_bind) == set(current_bind), ('Bind nodes differ', filename)
        maximum = max(abs(a - b) for n in legacy_bind for a, b in zip(legacy_bind[n], current_bind[n]))
        assert maximum < .0001, ('Bind matrix differs', filename, maximum)
        report['files'].append({'file': filename, 'bytes': path.stat().st_size,
                                'sha256': digest(path), 'fbx': info, 'bind_max_delta': maximum})
        report['clips'].append({'source_action': action.name, 'label': label, 'file': filename,
                               'start_frame': start, 'end_frame': end, 'fps': fps,
                               'duration_seconds': (end - start) / fps,
                               'loop': label in ('Idle', 'Walk', 'WalkLeft', 'WalkRight', 'ChargeLoop')})
        save()
        print('NUT_STYLE_V2_EXPORTED', args.unit, label, info['bone_count'], flush=True)
    report['source_sha256_after'] = digest(source)
    report['source_preserved'] = original_sha == report['source_sha256_after']
    report['source_reference_pose_unchanged'] = signature == rest_signature(rig)
    report['reference_unchanged'] = all(f['bind_max_delta'] < .0001 for f in report['files'])
    report['hierarchy_consistent'] = all(f['fbx']['hierarchy_sha256'] == legacy_reference['hierarchy_sha256'] for f in report['files'])
    report['root_animation_constant'] = all(f['fbx']['root_animation_constant'] for f in report['files'])
    assert all(report[k] for k in ('source_preserved', 'source_reference_pose_unchanged',
                                   'reference_unchanged', 'hierarchy_consistent', 'root_animation_constant'))
    report['status'] = 'complete'
    save()
    print('NUT_STYLE_V2_EXPORT_PASS', args.unit, len(report['clips']), flush=True)

if __name__ == '__main__':
    main()
