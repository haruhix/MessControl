"""Export one repaired Wizard mesh and Cast against the unchanged native49 rig.

Run in a separate background Blender process. The source blend is read-only;
only ExportsV7/Wizard FBX files and its manifest are written. Any helper/control
reparenting that changes ARP's exported hierarchy fails before UE import.
"""
import argparse
import json
import sys
from pathlib import Path

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_nut_tank import SETTINGS, digest, enable_arp, fbx_inventory, mesh_inventory, select

ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=ROOT / 'ArtSource/NutAnimationStyle/Wizard/NutWizard_Performance_v7.blend')
    parser.add_argument('--action', default='MC_Wizard_Cast')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'ArtSource/NutAnimationStyle/ExportsV7/Wizard')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    source, out = args.source.resolve(), args.output_dir.resolve()
    require(source.is_file() and source.suffix.lower() == '.blend', 'Missing saved performance source: ' + str(source))
    out.mkdir(parents=True, exist_ok=True)
    original_sha = digest(source)
    legacy_path = ROOT / 'ArtSource/NutWizard/ExportReport.json'
    legacy = json.loads(legacy_path.read_text(encoding='utf8'))
    reference = next(row['fbx'] for row in legacy['files'] if row['file'] == legacy['mesh_file'])
    require(reference['bone_count'] == 49, 'Expected the existing native49 Wizard skeleton')
    legacy_bind = {row['name']: row['matrix'] for row in reference['bind_poses']}
    addon = enable_arp()
    bpy.ops.wm.open_mainfile(filepath=str(source))
    rig, mesh = bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    scene = next(scene for scene in bpy.data.scenes if rig.name in scene.objects)
    if bpy.context.window:
        bpy.context.window.scene = scene
    require(any(mod.type == 'ARMATURE' and mod.object == rig for mod in mesh.modifiers), 'Wrong skinned source mesh')
    rig.animation_data_create()
    rig.animation_data.use_nla = False
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    properties = {bone.name: {key: bone[key] for key in bone.keys() if isinstance(bone[key], (bool, int, float))} for bone in rig.pose.bones}
    for name, value in SETTINGS.items():
        require(hasattr(scene, name), 'ARP lacks export setting: ' + name)
        setattr(scene, name, value)
    # Nondeforming source controls are baked into the old deform hierarchy.
    if hasattr(scene, 'arp_ge_export_parent_def'):
        scene.arp_ge_export_parent_def = False
    action = bpy.data.actions.get(args.action)
    require(action is not None, 'Missing Cast Action: ' + args.action)
    start, end = map(round, action.frame_range)
    fps = scene.render.fps / scene.render.fps_base
    require(end > start and fps > 0, 'Cast must have a positive saved frame range')
    report = dict(schema=3, status='exporting', unit='Wizard', source=str(source), source_sha256_before=original_sha,
                  blender_version=bpy.app.version_string, arp=addon, fps=fps, export_settings=SETTINGS,
                  legacy_reference=str(legacy_path), legacy_hierarchy_sha256=reference['hierarchy_sha256'],
                  mesh_file='SK_NutWizard_PerformanceV7.fbx', source_meshes=[mesh_inventory(mesh)], files=[], clips=[],
                  import_scope='One new skeletal mesh on the existing skeleton, one new Cast; no legacy package replacement.')
    manifest_path = out / 'ExportReport.json'

    def save():
        manifest_path.write_text(json.dumps(report, indent=2), encoding='utf8')

    def reset():
        rig.animation_data.action = None
        for bone in rig.pose.bones:
            bone.matrix_basis = Matrix.Identity(4)
            for key, value in properties[bone.name].items():
                bone[key] = value
        rig.update_tag()
        bpy.context.view_layer.update()

    def export(filename):
        select([rig, mesh], rig)
        path = out / filename
        result = bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
        require('FINISHED' in result and path.is_file(), 'ARP export failed: ' + filename)
        info = fbx_inventory(path)
        require(info['bone_count'] == 49 and info['hierarchy_sha256'] == reference['hierarchy_sha256'],
                'Native49 hierarchy changed; keep c_shoulder parents unchanged and drive their evaluated transforms: ' + filename)
        current = {row['name']: row['matrix'] for row in info['bind_poses']}
        require(set(current) == set(legacy_bind), 'Native49 bind nodes changed: ' + filename)
        delta = max(abs(a - b) for name in legacy_bind for a, b in zip(legacy_bind[name], current[name]))
        require(delta < .0001, 'Reference bind changed: ' + filename + ', delta=' + str(delta))
        report['files'].append(dict(file=filename, bytes=path.stat().st_size, sha256=digest(path), fbx=info, bind_max_delta=delta))
        save()
        return info

    save()
    reset()
    scene.arp_bake_anim = False
    export(report['mesh_file'])
    reset()
    rig.animation_data.action = action
    slots = [slot for slot in action.slots if slot.target_id_type == 'OBJECT']
    require(len(slots) == 1, 'Cast must have one object Action slot')
    rig.animation_data.action_slot = slots[0]
    scene.arp_bake_anim = True
    scene.arp_frame_range_type = 'CUSTOM'
    scene.arp_export_start_frame, scene.arp_export_end_frame = start, end
    scene.frame_start, scene.frame_end = start, end
    scene.frame_set(start)
    bpy.context.view_layer.update()
    info = export('A_NutWizard_Cast.fbx')
    require(info['root_animation_constant'], 'Exported root motion changed')
    report['clips'].append(dict(source_action=action.name, label='Cast', file='A_NutWizard_Cast.fbx', start_frame=start,
                               end_frame=end, fps=fps, duration_seconds=(end-start)/fps, loop=False))
    report.update(source_sha256_after=digest(source), source_preserved=digest(source) == original_sha,
                  reference_unchanged=True, hierarchy_consistent=True, root_animation_constant=True)
    require(report['source_preserved'], 'Source blend changed during export')
    report['status'] = 'complete'
    save()
    print('MC_WIZARD_PERFORMANCE_V7_EXPORT_PASS', json.dumps(dict(mesh=report['mesh_file'], cast='A_NutWizard_Cast.fbx', native_bones=49)), flush=True)


if __name__ == '__main__':
    main()
