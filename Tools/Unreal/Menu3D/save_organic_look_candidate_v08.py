"""Save the reviewed physical tissue-tone candidate in the isolated work map.

Keeps dynamic shadows enabled for the separate row-isolation investigation.
Does not alter texture pixels, jaw geometry, camera or production startup map.
"""
import json
from pathlib import Path
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
out = Path(u.Paths.project_dir()) / 'Tools/Unreal/Menu3D/Evidence/v08_organic_look_candidate01.json'
assert not out.exists()
recipes = {
    'Enamel': {'BaseColorTint': [.85, .65, .7], 'SubsurfaceTint': [.8, .4, .4], 'ScatterDensity': 1.6, 'RoughnessScale': 1.},
    'Gum': {'BaseColorTint': [.85, 2.2, 1.9], 'SubsurfaceTint': [.35, .075, .09], 'ScatterDensity': 1.5, 'RoughnessScale': 1.15},
    'Interior': {'BaseColorTint': [.45, .8, .85], 'SubsurfaceTint': [.015, .002, .003], 'ScatterDensity': 3., 'RoughnessScale': 1.},
    'Tongue': {'BaseColorTint': [.7, .9, .9], 'SubsurfaceTint': [.12, .008, .015], 'ScatterDensity': 3., 'RoughnessScale': 1.},
}
report = {'status': 'SAVED_WORK_LOOK_CANDIDATE_NOT_FINAL_REFERENCE_ACCEPTANCE',
          'evidence': 'PBRCalibration_v08b_06_Interior/D_burgundy_depth_warm_enamel.png',
          'shadow_policy': 'Keep all four source light shadow flags; row isolation review pending',
          'materials': [], 'lights': [], 'camera_geometry_texture_pixels_changed': False}
for day in (1, 2, 3):
    for part, values in recipes.items():
        suffix = 'v08' if day == 3 else 'v09'
        material = u.load_asset('/Game/UI/MainMenu3D/ReferenceFinish/Materials/MI_DAY%02d_%s_%s' % (day, part, suffix))
        assert material
        before = {}
        for name, value in values.items():
            if isinstance(value, list):
                before[name] = str(u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(material, name))
                u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(material, name, u.LinearColor(*value, 1.))
                actual = u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(material, name)
                assert max(abs(actual.r-value[0]), abs(actual.g-value[1]), abs(actual.b-value[2])) < 1e-5
            else:
                before[name] = u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(material, name)
                u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material, name, value)
                assert abs(u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(material, name)-value) < 1e-5
        assert u.EditorAssetLibrary.save_loaded_asset(material)
        report['materials'].append({'path': material.get_path_name(), 'before': before, 'after': values})
for tag, intensity, color in (
    ('Menu3D_Key', 420., [1., .72, .65]), ('Menu3D_Fill', 110., [1., .9, .88]),
    ('Menu3D_BlockingRim', 40., [.55, .2, 1.]), ('Menu3D_BlockingCavity', 22., [1., .8, .8])):
    light = u.GameplayStatics.get_all_actors_with_tag(world, tag)[0].get_component_by_class(u.RectLightComponent)
    before = {'intensity': light.intensity, 'color': str(light.light_color), 'cast_shadows': light.cast_shadows,
              'source_width': light.source_width, 'source_height': light.source_height}
    light.set_intensity(intensity)
    light.set_light_color(u.LinearColor(*color, 1.))
    if tag == 'Menu3D_Key':
        light.set_source_width(160.)
        light.set_source_height(850.)
    report['lights'].append({'tag': tag, 'before': before, 'intensity': intensity, 'color': color,
                             'cast_shadows': light.cast_shadows, 'source_width': light.source_width,
                             'source_height': light.source_height})
assert levels.save_current_level()
out.write_text(json.dumps(report, indent=2), encoding='utf8')
print('MC_ORGANIC_LOOK_CANDIDATE_SAVED')
