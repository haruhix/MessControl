"""Bind physical cavity materials for the v08 native look-dev scene.

Uses scalar defaults while the revised cavity mesh/maps are authored. This is a
saved look-development step, not approval of the old cavity or the final look.
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
report = {'status': 'CAVITY_MATERIAL_DEFAULTS_PENDING_REVISED_GEOMETRY_AND_TEXTURES',
          'world': world.get_path_name(), 'bindings': []}
for part in ('Interior', 'Tongue'):
    parent = u.load_asset(base + '/M_Menu' + part + '_v08')
    assert parent
    name = 'MI_DAY03_' + part + '_v08'
    instance = u.load_asset(base + '/' + name)
    if not instance:
        instance = assets.create_asset(name, base, u.MaterialInstanceConstant,
                                       u.MaterialInstanceConstantFactoryNew())
        u.MaterialEditingLibrary.set_material_instance_parent(instance, parent)
        assert u.EditorAssetLibrary.save_loaded_asset(instance)
    assert instance
    actors = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY03_' + part)
    assert len(actors) == 1
    component = actors[0].get_component_by_class(u.StaticMeshComponent)
    assert component
    before = component.get_material(0).get_path_name()
    component.set_material(0, instance)
    report['bindings'].append({'actor': actors[0].get_actor_label(),
                               'previous': before, 'material': instance.get_path_name()})
assert levels.save_current_level()
out = Path(u.Paths.project_dir()) / 'Tools/Unreal/Menu3D/Evidence/v08_interior_material_defaults.json'
out.write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('MC_REF_INTERIOR_DEFAULTS ' + json.dumps(report))
