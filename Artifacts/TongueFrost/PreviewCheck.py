import json
import time
import traceback
from pathlib import Path
import unreal as u

worlds = list(u.EditorLevelLibrary.get_pie_worlds(False))
assert len(worlds) == 1
world = worlds[0]
pc = u.GameplayStatics.get_player_controller(world, 0)
pc.request_dev_action(u.MCDevAction.ICE_EVENT, 0)
ice = next(x for x in u.GameplayStatics.get_all_actors_of_class(world, u.MCIceEvent) if x.actor_has_tag('MC_DevKeyEvent'))
ice.set_editor_properties({'arrival_seconds': 4.0, 'freeze_seconds': 60.0, 'circle_seconds': 60.0, 'icicle_damage': 0.0})
tongue = ice.get_editor_property('tongue')
floor = tongue.get_editor_property('surface')
frost = ice.get_editor_property('frost_surface')
original = floor.get_material(0)
out = Path(u.Paths.project_dir())/'Artifacts/TongueFrost'
report = {'passed': False, 'samples': [], 'original_tongue_material': original.get_path_name()}
state = {'stage': 'low', 'begin': time.monotonic(), 'at': time.monotonic(), 'camera': False}
handle = None
hero = None

def finish(error=None):
    u.unregister_slate_post_tick_callback(handle)
    report.update(passed=error is None, error=error)
    (out/'PreviewVerification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('TONGUE_FROST_PREVIEW_FINISHED', json.dumps(report))

def camera(mid, both=False):
    global hero
    hero = u.GameplayStatics.get_player_pawn(world, 0)
    assert isinstance(hero, u.MCToothCharacter)
    c = mid.get_vector_parameter_value('Warm Center')
    center = u.Vector(c.r, c.g, c.b)
    if both:
        nc = mid.get_vector_parameter_value('Next Warm Center')
        center = (center+u.Vector(nc.r, nc.g, nc.b))*.5
    hero.get_editor_property('character_movement').stop_movement_immediately()
    hero.get_editor_property('character_movement').disable_movement()
    hero.set_actor_tick_enabled(False)
    hero.get_editor_property('CameraBoom').set_component_tick_enabled(False)
    cam = hero.get_editor_property('Camera')
    cam.set_absolute(True, True, False)
    loc = center+u.Vector(-320, 0, 1500 if both else 1250)
    cam.set_world_location_and_rotation(loc, u.MathLibrary.find_look_at_rotation(loc, center), False, False)
    cam.set_field_of_view(60)
    pc.set_view_target_with_blend(hero, 0)
    report['camera'] = {'location': list(loc.to_tuple()), 'target': list(center.to_tuple())}

def sample(mid):
    assert isinstance(mid, u.MaterialInstanceDynamic)
    assert mid.get_editor_property('parent').get_path_name() == '/Game/Gameplay/Cold/Frost/M_TongueFrost.M_TongueFrost'
    assert mid.get_texture_parameter_value('Frost Mask Texture').get_path_name() == '/Game/Gameplay/Cold/Frost/T_PlayerFrostMask.T_PlayerFrostMask'
    assert floor.get_material(0) == original
    sv, si, sn, su, st = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(floor, 0)
    fv, fi, fn, fu, ft = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(frost, 0)
    assert len(sv) == len(fv) and list(si) == list(fi), (len(sv), len(fv), len(si), len(fi))
    source_transform, frost_transform = floor.get_world_transform(), frost.get_world_transform()
    max_position_error = 0.0
    for i in range(0, len(sv), 97):
        normal = source_transform.transform_direction(sn[i])
        length = sum(v*v for v in normal.to_tuple())**.5
        expected = source_transform.transform_location(sv[i])+normal*(2.5/length)
        actual = frost_transform.transform_location(fv[i])
        error = sum(v*v for v in (expected-actual).to_tuple())**.5
        max_position_error = max(max_position_error, error)
        assert su[i] == fu[i], i
    assert max_position_error < .03, max_position_error
    return {'stage': state['stage'], 'amount': mid.get_scalar_parameter_value('IceAmount'),
        'warm_radius': mid.get_scalar_parameter_value('Warm Radius'), 'warm_enabled': mid.get_scalar_parameter_value('Warm Enabled'),
        'next_warm_enabled': mid.get_scalar_parameter_value('Next Warm Enabled'),
        'fade_width': mid.get_scalar_parameter_value('Warm Fade Width'),
        'vertices': len(fv), 'triangles': len(fi)//3, 'max_surface_offset_error_cm': max_position_error,
        'authored_uvs_preserved': True}

def ready(mid):
    row = sample(mid)
    assert row['warm_enabled'] == 1.0 and row['fade_width'] == 120.0
    report['samples'].append(row)
    u.GameplayStatics.set_game_paused(world, True)
    (out/'PreviewReady.json').write_text(json.dumps(row, indent=2), encoding='utf-8')
    state.update(stage='wait_'+state['stage'], at=time.monotonic())

def tick(dt):
    try:
        assert time.monotonic()-state['begin'] < 100, state
        elapsed = time.monotonic()-state['at']
        mid = frost.get_material(0)
        if state['stage'] == 'low':
            if not isinstance(mid, u.MaterialInstanceDynamic):
                return
            if not state['camera']:
                camera(mid)
                state['camera'] = True
            if elapsed < 1.0:
                return
            assert .05 < mid.get_scalar_parameter_value('IceAmount') < .9
            ready(mid)
        elif state['stage'] == 'full':
            if mid.get_scalar_parameter_value('IceAmount') < .999 or elapsed < .8:
                return
            ready(mid)
        elif state['stage'] == 'two_zones':
            if mid.get_scalar_parameter_value('Next Warm Enabled') != 1.0:
                return
            if not state['camera']:
                camera(mid, True)
                state['camera'] = True
                state['at'] = time.monotonic()
                return
            if elapsed < .8:
                return
            ready(mid)
        elif state['stage'].startswith('wait_'):
            command = out/'PreviewAdvance.txt'
            if not command.exists():
                return
            command.unlink()
            if state['stage'] == 'wait_low':
                state.update(stage='full', at=time.monotonic())
            elif state['stage'] == 'wait_full':
                now = u.GameplayStatics.get_time_seconds(world)
                ice.set_editor_property('circle_started_at', now-56.3)
                state.update(stage='two_zones', at=time.monotonic(), camera=False)
            else:
                pc.request_dev_action(u.MCDevAction.STOP_ICE_EVENT, 0)
                assert frost.get_num_sections() == 0
                assert floor.get_material(0) == original
                report['cleanup'] = 'Frost section removed; original tongue material preserved'
                finish()
            u.GameplayStatics.set_game_paused(world, False)
    except Exception:
        finish(traceback.format_exc())

handle = u.register_slate_post_tick_callback(tick)
print('TONGUE_FROST_PREVIEW_INSTALLED')
