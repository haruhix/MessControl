"""Read saved mesh/material evidence for the Tank roll/shield iteration."""
import json
from pathlib import Path
import unreal as u

mesh = u.load_asset('/Game/Gameplay/CoreLoop/NutTank/SK_NutTank')
subs = u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
report = dict(mesh=mesh.get_path_name(), materials=[],
              skeleton=mesh.get_editor_property('skeleton').get_path_name(),
              dirty_packages=[p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
                              + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())],
              subsystem_methods=[x for x in dir(subs) if any(w in x for w in ('section', 'material', 'lod'))])
for material in mesh.get_editor_property('materials'):
    report['materials'].append(dict(slot=str(material.get_editor_property('material_slot_name')),
        imported=str(material.get_editor_property('imported_material_slot_name')),
        material=str(material.get_editor_property('material_interface'))))
try:
    report['lod_info'] = [str(x) for x in mesh.get_editor_property('lod_info')]
except Exception as error:
    report['lod_info_error'] = str(error)
worlds = list(u.EditorLevelLibrary.get_pie_worlds(False))
report['runtime'] = []
for world in worlds:
    for boss in u.GameplayStatics.get_all_actors_of_class(world, u.MCNutBoss):
        if 'TANK' in str(boss.get_editor_property('boss_role')).upper():
            model = boss.get_editor_property('tank_model')
            report['runtime'].append(dict(boss=boss.get_name(), model=str(model.get_editor_property('skeletal_mesh_asset')),
                bones=[str(model.get_bone_name(i)) for i in range(model.get_num_bones())],
                shield_health=float(boss.get_editor_property('shield_health'))))
output = Path(u.Paths.project_dir(), 'Saved/Checks/TankV4MeshInspection.json')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2), encoding='utf8')
u.log(json.dumps(report))
