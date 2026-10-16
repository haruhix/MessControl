"""Capture bounded physical look candidates in this task's solo PIE only.

No source textures, asset materials, camera framing or saved lights are changed.
"""
import json
from pathlib import Path
import time
import unreal as u

root = Path(u.Paths.project_dir())
request_path = root / 'Tools/Unreal/Menu3D/Evidence/pbr_compare_request.json'
request = json.loads(request_path.read_text(encoding='utf8')) if request_path.is_file() else {}
run_name = request.get('run_name', 'PBRCalibration_v08b_01')
assert Path(run_name).name == run_name and run_name.startswith('PBRCalibration_v08b_')
out = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute' / run_name
out.mkdir(parents=True, exist_ok=True)
assert not (out / 'candidate_states.json').exists()
world = u.EditorLevelLibrary.get_game_world()
assert world
cls = u.load_class(None, '/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
lib = u.get_default_object(u.load_class(None, '/Script/UMG.WidgetBlueprintLibrary'))
widgets = lib.call_method('GetAllWidgetsOfClass', args=(world, cls, False))
assert len(widgets) == 1
widget = widgets[0]
previous_visibility = widget.get_visibility()
widget.set_visibility(u.SlateVisibility.HIT_TEST_INVISIBLE)
widget.call_method('ShowPage', args=(0,))
widget.get_editor_property('PlayButton').on_clicked.broadcast()
widget.call_method('SelectDay', args=(3,))
mouth = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY03')[0]
component = mouth.get_component_by_class(u.SkeletalMeshComponent)
other_primitives = []
for actor in u.GameplayStatics.get_all_actors_of_class(world, u.Actor):
    if any(str(tag).startswith(('Menu3D_DAY01', 'Menu3D_DAY02')) for tag in actor.tags):
        for primitive in actor.get_components_by_class(u.PrimitiveComponent):
            other_primitives.append((primitive, primitive.get_editor_property('cast_shadow')))
gum = component.create_dynamic_material_instance(1)
enamel = component.create_dynamic_material_instance(0)
assert gum and enamel
interior_mids = {}
for part in ('Interior', 'Tongue'):
    part_actor = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY03_' + part)[0]
    part_component = part_actor.get_component_by_class(u.StaticMeshComponent)
    mid = part_component.create_dynamic_material_instance(0)
    assert mid
    interior_mids[part.lower()] = mid
interior_defaults = {name: {
    'tint': mid.get_vector_parameter_value('BaseColorTint'),
    'scatter': mid.get_vector_parameter_value('SubsurfaceTint'),
    'density': mid.get_scalar_parameter_value('ScatterDensity'),
    'rough': mid.get_scalar_parameter_value('RoughnessScale'),
} for name, mid in interior_mids.items()}
lights = {}
for tag in ('Menu3D_Key', 'Menu3D_Fill', 'Menu3D_BlockingRim', 'Menu3D_BlockingCavity'):
    light = u.GameplayStatics.get_all_actors_with_tag(world, tag)[0].get_component_by_class(u.RectLightComponent)
    lights[tag] = light

candidates = [
    dict(name='A_bound_baseline', density=.9, scatter=[.65, .025, .04], tint=[1, 1, 1], rough=1.,
         fill=[1., .42, .35], fill_intensity=95., rim_intensity=100.),
    dict(name='B_density_neutral_fill', density=3., scatter=[.4, .06, .08], tint=[1, 1, 1], rough=1.,
         fill=[1., .85, .8], fill_intensity=95., rim_intensity=45.),
    dict(name='C_coral_pigment', density=3., scatter=[.4, .06, .08], tint=[.78, 1.65, 1.5], rough=1.12,
         fill=[1., .85, .8], fill_intensity=95., rim_intensity=45.),
    dict(name='D_soft_coral', density=1.5, scatter=[.35, .075, .09], tint=[.85, 2.2, 1.9], rough=1.2,
         fill=[1., .85, .8], fill_intensity=110., rim_intensity=45.),
]
candidates = request.get('candidates', candidates)
state = {'started': time.monotonic(), 'index': 0, 'captures': [],
         'scope': 'Runtime-only physical material/light candidates; Organic_r05 with actual released interior PBR',
         'mesh': component.get_editor_property('skeletal_mesh_asset').get_path_name(), 'candidates': candidates}
assert not getattr(u, '_mc_pbr_compare_handle', None)


def apply(candidate):
    gum.set_scalar_parameter_value('ScatterDensity', candidate['density'])
    gum.set_vector_parameter_value('SubsurfaceTint', u.LinearColor(*candidate['scatter'], 1.))
    gum.set_vector_parameter_value('BaseColorTint', u.LinearColor(*candidate['tint'], 1.))
    gum.set_scalar_parameter_value('RoughnessScale', candidate['rough'])
    lights['Menu3D_Fill'].set_light_color(u.LinearColor(*candidate['fill'], 1.))
    lights['Menu3D_Fill'].set_intensity(candidate['fill_intensity'])
    lights['Menu3D_BlockingRim'].set_intensity(candidate['rim_intensity'])
    lights['Menu3D_Key'].set_light_color(u.LinearColor(*candidate.get('key_color', [1., .82, .68]), 1.))
    lights['Menu3D_Key'].set_intensity(candidate.get('key_intensity', 420.))
    lights['Menu3D_Key'].set_editor_property('source_width', candidate.get('key_width', 500.))
    lights['Menu3D_Key'].set_editor_property('source_height', candidate.get('key_height', 700.))
    lights['Menu3D_BlockingCavity'].set_light_color(u.LinearColor(*candidate.get('cavity_color', [1., .16, .2]), 1.))
    lights['Menu3D_BlockingCavity'].set_intensity(candidate.get('cavity_intensity', 35.))
    for tag, key in (('Menu3D_Key', 'key_shadows'), ('Menu3D_Fill', 'fill_shadows'),
                     ('Menu3D_BlockingRim', 'rim_shadows'), ('Menu3D_BlockingCavity', 'cavity_shadows')):
        lights[tag].set_cast_shadows(candidate.get(key, True))
    component.set_cast_shadow(candidate.get('mouth_cast_shadow', True))
    component.set_editor_property('self_shadow_only', candidate.get('mouth_self_shadow_only', False))
    component.set_editor_property('cast_inset_shadow', candidate.get('mouth_self_shadow_only', False))
    for primitive, previous in other_primitives:
        primitive.set_cast_shadow(False if candidate.get('disable_other_row_casters', False) else previous)
    enamel.set_vector_parameter_value('BaseColorTint', u.LinearColor(*candidate.get('enamel_tint', [1., 1., 1.]), 1.))
    enamel.set_vector_parameter_value('SubsurfaceTint', u.LinearColor(*candidate.get('enamel_scatter', [.8, .58, .4]), 1.))
    enamel.set_scalar_parameter_value('ScatterDensity', candidate.get('enamel_density', 1.6))
    for name, mid in interior_mids.items():
        defaults = interior_defaults[name]
        for key, parameter in (('tint', 'BaseColorTint'), ('scatter', 'SubsurfaceTint')):
            value = candidate.get(name + '_' + key)
            mid.set_vector_parameter_value(parameter, u.LinearColor(*value, 1.) if value is not None else defaults[key])
        for key, parameter in (('density', 'ScatterDensity'), ('rough', 'RoughnessScale')):
            mid.set_scalar_parameter_value(parameter, candidate.get(name + '_' + key, defaults[key]))


def finish(error=None):
    apply(candidates[0])
    widget.set_visibility(previous_visibility)
    u.unregister_slate_post_tick_callback(u._mc_pbr_compare_handle)
    u._mc_pbr_compare_handle = None
    state['error'] = error
    (out / 'candidate_states.json').write_text(json.dumps(state, indent=2), encoding='utf8')
    u.log('MC_PBR_COMPARE_FINISHED ' + str(error))


def tick(delta):
    try:
        elapsed = time.monotonic() - state['started']
        widget.call_method('ResetPointerIntent')
        index = state['index']
        if index >= len(candidates):
            if elapsed >= 2. + len(candidates) * 1.5:
                finish()
            return
        begin = 1. + index * 1.5
        if elapsed >= begin and not state.get('applied'):
            apply(candidates[index])
            state['applied'] = True
        if elapsed >= begin + 1.:
            path = out / (candidates[index]['name'] + '.png')
            u.SystemLibrary.execute_console_command(world, 'Shot SHOWUI filename=' + path.as_posix() + ' -nosuffix')
            state['captures'].append({'name': candidates[index]['name'], 'time': elapsed,
                                      'open3': widget.get_editor_property('Open3'),
                                      'gum_mid_authored': gum.get_path_name(),
                                      'gum_mid_current': component.get_material(1).get_path_name(),
                                      'enamel_mid_authored': enamel.get_path_name(),
                                      'enamel_mid_current': component.get_material(0).get_path_name(),
                                      'gum_tint_readback': str(gum.get_vector_parameter_value('BaseColorTint')),
                                      'enamel_tint_readback': str(enamel.get_vector_parameter_value('BaseColorTint'))})
            state['index'] += 1
            state['applied'] = False
    except Exception as error:
        finish(str(error))
        raise


u._mc_pbr_compare_handle = u.register_slate_post_tick_callback(tick)
u.log('MC_PBR_COMPARE_STARTED')
