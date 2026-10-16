"""Isolate the observed mid-pose gum blotches in a settled native pose.

Runtime-only diagnostics. This is not a gameplay/input or final-look test.
"""
import json
from pathlib import Path
import time
import unreal as u

root = Path(u.Paths.project_dir())
out = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute/MidPoseShading_v08b_01'
out.mkdir(parents=True, exist_ok=True)
assert not (out / 'diagnostic_states.json').exists()
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
pc = u.GameplayStatics.get_player_controller(world, 0)
mouth = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY03')[0]
component = mouth.get_component_by_class(u.SkeletalMeshComponent)
gum = component.create_dynamic_material_instance(1)
assert gum
folder = '/Game/UI/MainMenu3D/ReferenceFinish/Textures/'
textures = {name: u.load_asset(folder + 'DAY03/T_DAY03_Gum_' + name + '_v08')
            for name in ('Normal', 'AO', 'Thickness')}
flat_normal = u.load_asset(folder + 'T_Default_Normal')
white = u.load_asset(folder + 'T_Default_WhiteMask')
interior = sum([list(u.GameplayStatics.get_all_actors_with_tag(world, tag))
                for tag in ('Menu3D_DAY03_Interior', 'Menu3D_DAY03_Tongue')], [])
previous_hidden = [(actor, actor.get_editor_property('hidden')) for actor in interior]
state = {'started': time.monotonic(), 'index': 0, 'captures': [],
         'scope': 'Settled fixed midpose .5, PC Tick temporarily disabled, native surface isolation only',
         'mesh': component.get_editor_property('skeletal_mesh_asset').get_path_name()}
names = ['A_held_midpose', 'B_flat_normal', 'C_opaque_thickness', 'D_flat_normal_opaque_noAO', 'E_original_maps_no_interior']
assert not getattr(u, '_mc_midpose_handle', None)


def apply(index):
    gum.set_texture_parameter_value('NormalTexture', flat_normal if index in (1, 3) else textures['Normal'])
    gum.set_texture_parameter_value('AOTexture', white if index == 3 else textures['AO'])
    gum.set_texture_parameter_value('ThicknessTexture', white if index in (2, 3) else textures['Thickness'])
    gum.set_scalar_parameter_value('ScatterDensity', 10. if index in (2, 3) else .9)
    for actor, original in previous_hidden:
        actor.set_actor_hidden_in_game(index == 4 or original)


def finish(error=None):
    apply(0)
    pc.set_actor_tick_enabled(True)
    widget.get_editor_property('Day3CavityControls').set_visibility(u.SlateVisibility.HIDDEN)
    widget.set_visibility(previous_visibility)
    u.unregister_slate_post_tick_callback(u._mc_midpose_handle)
    u._mc_midpose_handle = None
    state['error'] = error
    (out / 'diagnostic_states.json').write_text(json.dumps(state, indent=2), encoding='utf8')
    u.log('MC_MIDPOSE_DIAG_FINISHED ' + str(error))


def tick(delta):
    try:
        elapsed = time.monotonic() - state['started']
        widget.call_method('ResetPointerIntent')
        if elapsed < 1.:
            return
        if not state.get('held'):
            pc.set_actor_tick_enabled(False)
            component.clear_morph_targets()
            component.set_morph_target('Pose_050', 1., False)
            widget.get_editor_property('Day3CavityControls').set_visibility(u.SlateVisibility.HIDDEN)
            state['held'] = True
        index = state['index']
        if index >= len(names):
            if elapsed > 2.5 + len(names) * 1.5:
                finish()
            return
        begin = 1. + index * 1.5
        if elapsed >= begin and not state.get('applied'):
            apply(index)
            state['applied'] = True
        if elapsed >= begin + 1.:
            path = out / (names[index] + '.png')
            u.SystemLibrary.execute_console_command(world, 'Shot SHOWUI filename=' + path.as_posix() + ' -nosuffix')
            state['captures'].append({'name': names[index], 'elapsed': elapsed,
                                      'pose050': component.get_morph_target('Pose_050'),
                                      'material_identity_matches': gum == component.get_material(1),
                                      'pc_tick': pc.is_actor_tick_enabled()})
            state['index'] += 1
            state['applied'] = False
    except Exception as error:
        finish(str(error))
        raise


u._mc_midpose_handle = u.register_slate_post_tick_callback(tick)
u.log('MC_MIDPOSE_DIAG_STARTED')
