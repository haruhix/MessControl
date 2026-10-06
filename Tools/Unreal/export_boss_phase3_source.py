"""Inspect/export the colleague's phase-three rig and preserve its own Blueprint.

Read-only for the imported meshes and level; the only saved asset is the new
phase-three Blueprint duplicate. Run with the existing Development Editor.
"""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
out = root / 'ArtSource/BossPhase3'
out.mkdir(parents=True, exist_ok=True)
report = {'colleague_commit': 'bfe90a7', 'assets': [], 'level_bosses': []}
for name in ('SK_Boss_stady3', 'SK_Boss_stady_3'):
    path = '/Game/FromBlender6/' + name
    asset = u.load_asset(path)
    assert asset, path
    entry = {'path': asset.get_path_name(), 'class': asset.get_class().get_name()}
    if isinstance(asset, u.SkeletalMesh):
        entry['skeleton'] = asset.get_editor_property('skeleton').get_path_name()
        entry['materials'] = [slot.material_interface.get_path_name() if slot.material_interface else None
                              for slot in asset.get_editor_property('materials')]
        task = u.AssetExportTask()
        task.object = asset
        task.filename = str(out / (name + '_Source.fbx'))
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        task.exporter = u.SkeletalMeshExporterFBX()
        task.options = u.FbxExportOption()
        task.options.set_editor_property('level_of_detail', False)
        task.options.set_editor_property('collision', False)
        assert u.Exporter.run_asset_export_task(task), 'FBX export: ' + path
        entry['fbx'] = task.filename
    report['assets'].append(entry)

original = u.load_asset('/Game/Gameplay/Boss/BP_ZombieBoss')
assert isinstance(original, u.Blueprint)
incoming = u.get_default_object(original.generated_class())
report['incoming_profile'] = str(incoming.get_editor_property('profile'))
mesh = incoming.get_editor_property('mesh')
report['incoming_mesh'] = str(mesh.get_editor_property('skeletal_mesh_asset'))
report['incoming_mesh_transform'] = str(mesh.get_relative_transform())
destination = '/Game/Gameplay/Boss/Phase3/BP_BossPhase3'
duplicate = u.EditorAssetLibrary.load_asset(destination)
if not duplicate:
    duplicate = u.EditorAssetLibrary.duplicate_asset('/Game/Gameplay/Boss/BP_ZombieBoss', destination)
assert isinstance(duplicate, u.Blueprint)
u.BlueprintEditorLibrary.compile_blueprint(duplicate)
assert u.EditorAssetLibrary.save_loaded_asset(duplicate, only_if_is_dirty=False)
report['phase_three_blueprint'] = duplicate.get_path_name()

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/L_Mouth')
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor, u.MCBossCharacter):
        report['level_bosses'].append({'name': actor.get_name(), 'class': actor.get_class().get_path_name(),
                                     'profile': str(actor.get_editor_property('profile'))})
    elif isinstance(actor, u.SkeletalMeshActor):
        component = actor.get_editor_property('skeletal_mesh_component')
        rig = component.get_editor_property('skeletal_mesh_asset')
        if rig and 'Boss' in rig.get_name():
            report['level_bosses'].append({'name': actor.get_name(), 'mesh': rig.get_path_name()})
(out / 'ColleagueImport.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_BOSS_PHASE3_EXPORT_PASS ' + json.dumps(report))
u.SystemLibrary.quit_editor()
