"""Read the reopened editor state and verify the material bindings without UI input."""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
report = dict(map=world.get_path_name(), materials={}, bindings=[], effects={})
for tissue in ['Gum', 'Palate', 'Exit']:
    path = '/Game/Gameplay/MouthV3/M_' + tissue + 'V3'
    material = u.load_asset(path)
    assert material, path
    report['materials'][path] = dict(expressions=u.MaterialEditingLibrary.get_num_material_expressions(material))
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    for component in actor.get_components_by_class(u.MeshComponent):
        materials = [component.get_material(i).get_path_name() for i in range(component.get_num_materials()) if component.get_material(i)]
        if any('MouthV3' in m or 'LivingTissue' in m or 'ThroatSculpt' in m for m in materials):
            report['bindings'].append(dict(actor=actor.get_actor_label(), component=component.get_name(), materials=materials))
for effect in ['NS_BrushFoam', 'NS_IceShatter']:
    system = u.load_asset('/Game/Gameplay/VFX/' + effect)
    assert system, effect
    report['effects'][effect] = system.get_path_name()
assert 'L_Mouth' in report['map']
assert report['bindings']
path = root / 'Artifacts/Approval/Editor_Verification.json'
path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_APPROVAL_EDITOR_READY ' + str(path))
