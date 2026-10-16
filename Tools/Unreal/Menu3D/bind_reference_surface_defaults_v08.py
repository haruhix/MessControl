"""Broad lighting test on accepted v08 shape while replacement UV is authored.

Flat physical parameter defaults are explicitly provisional. No projected image
or rejected UV texture is used. Final PBR binding replaces these defaults.
"""
import json
from pathlib import Path
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
base = '/Game/UI/MainMenu3D/ReferenceFinish/Materials'
assets = u.AssetToolsHelpers.get_asset_tools()
mouths = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY03')
assert len(mouths) == 1
component = mouths[0].get_component_by_class(u.SkeletalMeshComponent)
assert component and component.get_num_materials() == 2
report = {'status': 'PHYSICAL_FLAT_DEFAULTS_FOR_LIGHTING_ONLY_UV_REPAIR_PENDING', 'bindings': []}
for index, part in enumerate(('Enamel', 'Gum')):
    name = 'MI_DAY03_' + part + '_v08'
    instance = u.load_asset(base + '/' + name)
    if not instance:
        instance = assets.create_asset(name, base, u.MaterialInstanceConstant,
                                       u.MaterialInstanceConstantFactoryNew())
        u.MaterialEditingLibrary.set_material_instance_parent(instance,
            u.load_asset(base + '/M_Menu' + part + '_v08'))
        assert u.EditorAssetLibrary.save_loaded_asset(instance)
    assert instance
    component.set_material(index, instance)
    report['bindings'].append(instance.get_path_name())
assert levels.save_current_level()
(Path(u.Paths.project_dir()) / 'Tools/Unreal/Menu3D/Evidence/v08_surface_material_defaults.json').write_text(
    json.dumps(report, indent=2), encoding='utf8')
u.log('MC_SURFACE_DEFAULTS ' + json.dumps(report))
