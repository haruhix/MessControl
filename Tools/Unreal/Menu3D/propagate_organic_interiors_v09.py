"""Use the validated detailed cavity/tongue in the two family work-map rows.

The jaws, camera, source packets and character animations are not modified.
Each row receives its own material instances for independent card clipping.
"""
import json
from pathlib import Path
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
out = Path(u.Paths.project_dir()) / 'Tools/Unreal/Menu3D/Evidence/v09_organic_family_binding_report.json'
assert not out.exists()
report = {'status': 'DETAILED_FAMILY_INTERIORS_BOUND_NATIVE_FIT_MOTION_PENDING', 'bindings': [],
          'shared_source': 'Organic_r05/UV_Organic_r05_Candidate01',
          'source_uv_pbr_unchanged': True, 'jaw_camera_character_animation_changed': False}
for day in (1, 2):
    for part in ('Interior', 'Tongue'):
        tag = 'Menu3D_DAY' + str(day).zfill(2) + '_' + part
        actors = u.GameplayStatics.get_all_actors_with_tag(world, tag)
        assert len(actors) == 1
        actor = actors[0]
        component = actor.get_component_by_class(u.StaticMeshComponent)
        mesh = u.load_asset('/Game/UI/MainMenu3D/Meshes/SM_DAY03_' + part + '_Organic_v08')
        assert mesh and component
        source_material = '/Game/UI/MainMenu3D/ReferenceFinish/Materials/MI_DAY03_' + part + '_v08'
        destination = '/Game/UI/MainMenu3D/ReferenceFinish/Materials/MI_DAY' + str(day).zfill(2) + '_' + part + '_v09'
        material = u.load_asset(destination)
        if not material:
            material = u.EditorAssetLibrary.duplicate_asset(source_material, destination)
        assert material
        previous = {'mesh': component.static_mesh.get_path_name(),
                    'material': component.get_material(0).get_path_name(),
                    'relative_location': str(component.get_editor_property('relative_location')),
                    'relative_scale': str(component.get_editor_property('relative_scale3d'))}
        assert component.set_static_mesh(mesh)
        component.set_material(0, material)
        x_scale = .88 if day == 2 and part == 'Interior' else .90
        component.set_relative_scale3d(u.Vector(x_scale, 1., 1.))
        component.set_relative_location(u.Vector(0., 0., 0.), False, False)
        assert component.static_mesh == mesh and component.get_material(0) == material
        assert u.EditorAssetLibrary.save_loaded_asset(material)
        report['bindings'].append({'day': day, 'part': part, 'previous': previous,
                                   'mesh': mesh.get_path_name(), 'material': material.get_path_name(),
                                   'relative_scale': [x_scale, 1., 1.], 'relative_location': [0., 0., 0.]})
assert levels.save_current_level()
out.write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report))
