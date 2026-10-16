"""Actual native catalog/front/oblique detail frames; restores review state.

Only the two explicitly labelled inspection views change camera/framing. The
catalog image uses the normal game camera. No packages or source art are saved.
"""
import json
from pathlib import Path
import time
import unreal as u

world = u.EditorLevelLibrary.get_game_world()
assert world and not getattr(u, '_mc_organic_detail_handle', None)
assert not getattr(u, '_mc_pbr_compare_handle', None)
assert not getattr(u, '_mc_isolated_lighting_review', None)
cls = u.load_class(None, '/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
lib = u.get_default_object(u.load_class(None, '/Script/UMG.WidgetBlueprintLibrary'))
widgets = lib.call_method('GetAllWidgetsOfClass', args=(world, cls, False))
assert len(widgets) == 1
widget = widgets[0]
assert widget.get_editor_property('PageIndex') == 1
assert widget.get_editor_property('SelectedDay') == 3 and widget.get_editor_property('Open3') > .98
pc = u.GameplayStatics.get_player_controller(world, 0)
camera = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_Camera')[0]
mouth = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY03')[0]
root = Path(u.Paths.project_dir()).resolve()
request_path = root / 'Tools/Unreal/Menu3D/Evidence/organic_detail_request.json'
request = json.loads(request_path.read_text(encoding='utf8')) if request_path.exists() else {}
run_name = request.get('run_name', 'OrganicDetail_v08_02')
assert Path(run_name).name == run_name and run_name.startswith('OrganicDetail_v08_')
out = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute' / run_name
assert not out.exists()
out.mkdir(parents=True)
previous = {'camera': camera.get_actor_transform(), 'visibility': widget.get_visibility(),
            'pc_tick': pc.is_actor_tick_enabled()}
lights = [u.GameplayStatics.get_all_actors_with_tag(world, tag)[0].get_component_by_class(u.RectLightComponent)
          for tag in ('Menu3D_Key', 'Menu3D_Fill', 'Menu3D_BlockingRim', 'Menu3D_BlockingCavity')]
shadow_previous = [(light, light.get_editor_property('cast_shadows')) for light in lights]
others = [(actor, actor.get_editor_property('hidden')) for actor in
          u.GameplayStatics.get_all_actors_of_class(world, u.Actor)
          if any(str(tag).startswith(('Menu3D_DAY01', 'Menu3D_DAY02')) for tag in actor.tags)]
state = {'started': time.monotonic(), 'index': 0, 'captures': [], 'error': None,
         'scope': 'Actual engine-rendered game camera plus two labelled temporary inspection views; dynamic light shadows disabled only in inspection views to expose geometry',
         'camera_before': str(previous['camera']), 'mesh': mouth.get_component_by_class(u.SkeletalMeshComponent).skeletal_mesh_asset.get_path_name()}
widget.set_visibility(u.SlateVisibility.HIT_TEST_INVISIBLE)

def finish(error=None):
    camera.set_actor_transform(previous['camera'], False, True)
    for actor, hidden in others:
        actor.set_actor_hidden_in_game(hidden)
    for light, flag in shadow_previous:
        light.set_cast_shadows(flag)
    widget.set_visibility(previous['visibility'])
    pc.set_actor_tick_enabled(previous['pc_tick'])
    u.unregister_slate_post_tick_callback(u._mc_organic_detail_handle)
    u._mc_organic_detail_handle = None
    state['error'] = error
    state['camera_restored'] = str(camera.get_actor_transform())
    (out / 'capture_states.json').write_text(json.dumps(state, indent=2), encoding='utf8')
    u.log('MC_ORGANIC_DETAIL_FINISHED ' + str(error))

def shot(name):
    state['captures'].append({'name': name, 'elapsed': time.monotonic()-state['started'],
                              'camera': str(camera.get_actor_transform()), 'open3': widget.get_editor_property('Open3')})
    u.SystemLibrary.execute_console_command(world, 'Shot SHOWUI filename='+(out/(name+'.png')).as_posix()+' -nosuffix')

def tick(delta):
    try:
        elapsed = time.monotonic()-state['started']
        widget.call_method('ResetPointerIntent')
        index = state['index']
        center = mouth.get_actor_location()
        if index == 0 and elapsed > 1.:
            shot('01_catalog_game_camera')
            state['index'] = 1
        elif index == 1 and elapsed > 1.5:
            pc.set_actor_tick_enabled(False)
            widget.set_visibility(u.SlateVisibility.HIDDEN)
            for actor, hidden in others:
                actor.set_actor_hidden_in_game(True)
            for light, flag in shadow_previous:
                light.set_cast_shadows(False)
            location = u.Vector(0., 1400., center.z+15.)
            camera.set_actor_location(location, False, True)
            camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(location, u.Vector(0., 0., center.z+15.)), True)
            state['index'] = 2
        elif index == 2 and elapsed > 3.:
            shot('02_detail_front_inspection')
            state['index'] = 3
        elif index == 3 and elapsed > 3.5:
            location = u.Vector(230., 1380., center.z+70.)
            camera.set_actor_location(location, False, True)
            camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(location, u.Vector(0., 0., center.z-10.)), True)
            state['index'] = 4
        elif index == 4 and elapsed > 5.:
            shot('03_detail_oblique_inspection')
            state['index'] = 5
        elif index == 5 and elapsed > 5.6:
            finish()
    except Exception as error:
        finish(str(error))
        raise

u._mc_organic_detail_handle = u.register_slate_post_tick_callback(tick)
