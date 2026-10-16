"""A bounded native lighting/material look-development pass; UV art is separate."""
import json
from pathlib import Path
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
out = Path(u.Paths.project_dir()) / 'Tools/Unreal/Menu3D/Evidence/v08_lighting_calibration01.json'
assert not out.exists(), 'Do not overwrite a previous calibration receipt'
report = {'status': 'BROAD_PHYSICAL_LIGHTING_CANDIDATE_PENDING_NATIVE_REVIEW', 'lights': [], 'materials': [],
          'original_scene_snapshot': 'Tools/Unreal/Menu3D/Evidence/v08_scene_before.json',
          'setter_note': 'UE5.8 MaterialEditingLibrary setters return false even after writing; validate by readback.'}
for tag, intensity, color in [
    ('Menu3D_Key', 420., (1., .82, .68)),
    ('Menu3D_Fill', 95., (1., .42, .35)),
    ('Menu3D_BlockingRim', 100., (.55, .20, 1.)),
    ('Menu3D_BlockingCavity', 35., (1., .16, .20))]:
    actors = u.GameplayStatics.get_all_actors_with_tag(world, tag)
    assert len(actors) == 1, tag
    light = actors[0].get_component_by_class(u.RectLightComponent)
    assert light
    before = {'intensity': light.intensity, 'color': str(light.light_color)}
    light.set_intensity(intensity)
    light.set_light_color(u.LinearColor(*color, 1.))
    report['lights'].append({'tag': tag, 'before': before, 'after': {'intensity': light.intensity,
                             'color': str(light.light_color)}})
base = '/Game/UI/MainMenu3D/ReferenceFinish/Materials/'
for part, tint, density in [
    ('Enamel', (.8, .58, .4), 1.6), ('Gum', (.65, .025, .04), .9),
    ('Tongue', (.32, .008, .018), 1.5)]:
    instance = u.load_asset(base + 'MI_DAY03_' + part + '_v08')
    assert instance
    before = {'SubsurfaceTint': str(u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(instance, 'SubsurfaceTint')),
              'ScatterDensity': u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, 'ScatterDensity')}
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, 'SubsurfaceTint', u.LinearColor(*tint, 1.))
    actual = u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(instance, 'SubsurfaceTint')
    assert max(abs(actual.r-tint[0]), abs(actual.g-tint[1]), abs(actual.b-tint[2])) < 1e-5
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, 'ScatterDensity', density)
    assert abs(u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, 'ScatterDensity')-density) < 1e-5
    assert u.EditorAssetLibrary.save_loaded_asset(instance)
    report['materials'].append({'part': part, 'before': before, 'SubsurfaceTint': tint, 'ScatterDensity': density})
camera = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_Camera')[0].get_component_by_class(u.CameraComponent)
pp = camera.get_editor_property('post_process_settings')
report['post_process_before'] = {'exposure_bias': pp.auto_exposure_bias, 'ao_intensity': pp.ambient_occlusion_intensity}
pp.auto_exposure_bias = -.8
pp.ambient_occlusion_intensity = .3
camera.set_editor_property('post_process_settings', pp)
assert levels.save_current_level()
report['validation'] = 'Only look-dev map lights/postprocess and task material instances changed. Actual rendered comparison pending; texture maps and replacement UV not accepted.'
out.write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('MC_REFERENCE_LIGHTING01 ' + json.dumps(report))
