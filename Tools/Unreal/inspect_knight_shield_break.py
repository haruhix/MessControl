"""Read-only saved Knight shield/profile evidence; exports meshes only to Saved.

Safe in an idle editor or editor commandlet. Does not save or modify packages.
FBX export is opt-in with MC_KNIGHT_SHIELD_EXPORT=True, in an editor with a real
RHI only. UE's exporter can require a MeshObject and assert under -NullRHI.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/ManualIntake/ShieldInspection'
PROFILE = '/Game/Gameplay/CoreLoop/DA_NutRain'
PATHS = ['/Game/Gameplay/CoreLoop/NutTank/SK_NutTank',
         '/Game/Gameplay/CoreLoop/NutTank/SK_NutTank_NoShield']


def objpath(obj):
    return obj.get_path_name() if obj else None


def dirty():
    return sorted(p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
                  + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Do not inspect during a player session'
    before = dirty()
    profile = u.load_asset(PROFILE)
    assert profile is not None, PROFILE
    boss = profile.get_editor_property('settings').get_editor_property('boss')
    fields = ['tank_skeletal_mesh', 'tank_unshielded_skeletal_mesh',
              'tank_idle_animation', 'tank_unshielded_idle_animation',
              'tank_shield_break_animation', 'tank_stun_hit_animation']
    bindings, missing = {}, {}
    for key in fields:
        try:
            bindings[key] = objpath(boss.get_editor_property(key))
        except Exception as exc:
            missing[key] = str(exc)
    try:
        stun_seconds = float(boss.get_editor_property('tank_stun_seconds'))
    except Exception as exc:
        stun_seconds = None
        missing['tank_stun_seconds'] = str(exc)
    report = dict(profile=objpath(profile), bindings=bindings, missing_native_fields=missing,
                  tank_stun_seconds=stun_seconds,
                  shield_max_health=float(boss.get_editor_property('shield_max_health')),
                  shield_front_damage_scale=float(boss.get_editor_property('shield_front_damage_scale')),
                  dirty_before=before, meshes=[])
    subs = u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
    loaded = []
    for path in PATHS:
        mesh = u.load_asset(path)
        assert isinstance(mesh, u.SkeletalMesh), path
        loaded.append(mesh)
        entry = dict(asset=objpath(mesh), skeleton=objpath(mesh.get_editor_property('skeleton')),
                     materials=[s.export_text() for s in mesh.get_editor_property('materials')],
                     lods=[], metadata={str(k): str(v) for k, v in u.EditorAssetLibrary.get_metadata_tag_values(mesh).items()})
        source = mesh.get_editor_property('asset_import_data')
        entry['import_filenames'] = list(source.extract_filenames()) if source else []
        try:
            entry['deformer'] = objpath(mesh.get_default_mesh_deformer())
        except Exception as exc:
            entry['deformer_read_error'] = str(exc)
        for lod in range(subs.get_lod_count(mesh)):
            sections = subs.get_num_sections(mesh, lod)
            entry['lods'].append(dict(index=lod, vertices=subs.get_num_verts(mesh, lod), sections=sections,
                                     material_slots=[subs.get_lod_material_slot(mesh, lod, i) for i in range(sections)]))
        report['meshes'].append(entry)
    report['same_skeleton'] = loaded[0].get_editor_property('skeleton') == loaded[1].get_editor_property('skeleton')
    report['variant_is_same_object'] = loaded[0] == loaded[1]
    report['binding_resolves_to_same_object'] = boss.get_editor_property('tank_skeletal_mesh') == boss.get_editor_property('tank_unshielded_skeletal_mesh')
    report['dirty_after'] = dirty()
    assert report['dirty_after'] == before, 'Read-only inspection changed package dirtiness'
    # Persist the complete non-render evidence before any optional exporter call.
    (OUT / 'SavedAssets.json').write_text(json.dumps(report, indent=2, default=str), encoding='utf-8')
    if globals().get('MC_KNIGHT_SHIELD_EXPORT', False):
        for mesh, entry in zip(loaded, report['meshes']):
            filename = OUT / (mesh.get_name() + '.fbx')
            task = u.AssetExportTask()
            task.set_editor_properties(dict(object=mesh, filename=str(filename), automated=True,
                                             prompt=False, replace_identical=True,
                                             exporter=u.SkeletalMeshExporterFBX()))
            options = u.FbxExportOption()
            options.set_editor_properties(dict(ascii=False, level_of_detail=False,
                                               export_morph_targets=False,
                                               bake_material_inputs=u.FbxMaterialBakeMode.DISABLED))
            task.options = options
            assert u.Exporter.run_asset_export_task(task), 'Cannot export saved mesh evidence: ' + objpath(mesh)
            assert filename.is_file() and filename.stat().st_size > 0
            entry['export'] = str(filename)
            entry['export_sha256'] = hashlib.sha256(filename.read_bytes()).hexdigest()
            (OUT / 'SavedAssets.json').write_text(json.dumps(report, indent=2, default=str), encoding='utf-8')
    report['dirty_after'] = dirty()
    assert report['dirty_after'] == before, 'Read-only inspection changed package dirtiness'
    (OUT / 'SavedAssets.json').write_text(json.dumps(report, indent=2, default=str), encoding='utf-8')
    print('KNIGHT_SHIELD_INSPECTION', json.dumps(report, default=str))


if __name__ == '__main__':
    main()
