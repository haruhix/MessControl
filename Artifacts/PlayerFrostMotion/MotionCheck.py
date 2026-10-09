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
ice.set_editor_properties({'arrival_seconds': 1.0, 'freeze_seconds': 60.0, 'thaw_seconds': 1.0, 'circle_seconds': 60.0, 'icicle_damage': 0.0})
out = Path(u.Paths.project_dir())/'Artifacts/PlayerFrostMotion'
report = {'passed': False, 'samples': [], 'max_parameter_error': 0.0, 'walking_stretch': [], 'walking_speed': [], 'jump_stretch': []}
state = {'stage': 'arrival', 'begin': time.monotonic(), 'at': time.monotonic()}
hero = None
originals = []
handle = None

def shells():
    return [c for c in ice.get_components_by_class(u.SkeletalMeshComponent) if c.component_has_tag('MC_PlayerIceCoating')]

def finish(error=None):
    report.update(passed=error is None, error=error)
    u.unregister_slate_post_tick_callback(handle)
    (out/'MotionVerification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('FROST_MOTION_FINISHED', json.dumps(report))

def sample():
    mesh = hero.get_editor_property('mesh')
    parts = shells()
    assert len(parts) == 1, len(parts)
    part = parts[0]
    assert part.get_editor_property('leader_pose_component') == mesh
    slots = []
    mids = []
    for i in range(mesh.get_num_materials()):
        source, coating = mesh.get_material(i), part.get_material(i)
        assert isinstance(coating, u.MaterialInstanceDynamic)
        mids.append(coating.get_path_name())
        values = {}
        for p in ['BodyStretch', 'Damage', 'DamageChipDepth']:
            expected = source.get_scalar_parameter_value(p)
            actual = coating.get_scalar_parameter_value(p)
            error = abs(expected-actual)
            report['max_parameter_error'] = max(report['max_parameter_error'], error)
            assert error < .00001, (p, i, expected, actual)
            values[p] = actual
        slots.append(values)
    assert len(set(mids)) == len(mids), mids
    current = [mesh.get_material(i).get_path_name() for i in range(mesh.get_num_materials())]
    assert current == originals, (originals, current)
    velocity = hero.get_velocity()
    return {'slots': slots, 'speed': (velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z)**.5,
        'body_state': str(hero.get_editor_property('tooth_physics').get_body_state()),
        'animation_settings': str(hero.get_editor_property('animation_settings')),
        'source_materials': current, 'squash_morph': mesh.get_morph_target('Squash'), 'stretch_morph': mesh.get_morph_target('Stretch')}

def camera():
    center = hero.get_actor_location()+u.Vector(0, 0, 5)
    location = center+hero.get_actor_forward_vector()*370+hero.get_actor_right_vector()*75+u.Vector(0, 0, 50)
    cam = hero.get_editor_property('Camera')
    cam.set_world_location_and_rotation(location, u.MathLibrary.find_look_at_rotation(location, center), False, False)

def warm():
    hero.consume_movement_input_vector()
    hero.get_editor_property('character_movement').stop_movement_immediately()
    safe = ice.get_editor_property('tongue').get_actor_transform().transform_location(ice.get_editor_property('safe_anchor'))
    hero.set_actor_location(safe+u.Vector(0, 0, hero.get_editor_property('capsule_component').get_scaled_capsule_half_height()+5), False, False)
    state.update(stage='warm', at=time.monotonic())
    u.GameplayStatics.set_game_paused(world, False)

def tick(dt):
    global hero, originals
    try:
        assert time.monotonic()-state['begin'] < 90, state
        elapsed = time.monotonic()-state['at']
        if state['stage'] == 'arrival':
            if ice.get_editor_property('stage') != u.MCIceEventStage.ACTIVE:
                return
            hero = u.GameplayStatics.get_player_pawn(world, 0)
            mesh = hero.get_editor_property('mesh')
            originals = [mesh.get_material(i).get_path_name() for i in range(mesh.get_num_materials())]
            safe = ice.get_editor_property('tongue').get_actor_transform().transform_location(ice.get_editor_property('safe_anchor'))
            hero.set_actor_location(safe+u.Vector(600, 0, hero.get_editor_property('capsule_component').get_scaled_capsule_half_height()+5), False, False)
            movement = hero.get_editor_property('character_movement')
            movement.set_editor_properties({'max_walk_speed': 160.0, 'orient_rotation_to_movement': False})
            hero.get_editor_property('CameraBoom').set_component_tick_enabled(False)
            cam = hero.get_editor_property('Camera')
            cam.set_absolute(True, True, False)
            cam.set_field_of_view(40)
            camera()
            players = list(ice.get_editor_property('players'))
            assert len(players) == 1
            players[0].set_editor_property('amount', .60)
            ice.set_editor_property('players', players)
            state.update(stage='settle', at=time.monotonic())
        elif state['stage'] == 'settle':
            if elapsed < 2.0 or not shells():
                return
            mesh = hero.get_editor_property('mesh')
            originals = [mesh.get_material(i).get_path_name() for i in range(mesh.get_num_materials())]
            sample()
            state.update(stage='walk', at=time.monotonic())
        elif state['stage'] == 'walk':
            hero.add_movement_input(u.Vector(0, 1 if int(elapsed/.8)%2 == 0 else -1, 0), 1.0, True)
            camera()
            row = sample()
            report['walking_stretch'].append(max(row['slots'], key=lambda slot: abs(slot['BodyStretch']))['BodyStretch'])
            report['walking_speed'].append(row['speed'])
            report['last_walk'] = row
            if elapsed < 3.5:
                return
            assert max(report['walking_speed']) > 10, report
            assert max(report['walking_stretch'])-min(report['walking_stretch']) > .002, report
            report['samples'].append({'stage': 'walking', 'frame_count': len(report['walking_stretch']), 'last': row})
            hero.consume_movement_input_vector()
            hero.jump()
            state.update(stage='jump', at=time.monotonic())
        elif state['stage'] == 'jump':
            camera()
            row = sample()
            report['jump_stretch'].append(max(slot['BodyStretch'] for slot in row['slots']))
            if max(slot['BodyStretch'] for slot in row['slots']) > .05 and hero.get_editor_property('character_movement').is_falling():
                report['samples'].append({'stage': 'jumping', 'last': row})
                u.GameplayStatics.set_game_paused(world, True)
                (out/'MotionPreviewReady.json').write_text(json.dumps(row, indent=2), encoding='utf-8')
                state.update(stage='preview', at=time.monotonic())
            elif elapsed > 6:
                raise AssertionError(('Jump did not produce stretch', report))
        elif state['stage'] == 'preview':
            command = out/'MotionPreviewAdvance.txt'
            if command.exists():
                command.unlink()
                warm()
        elif state['stage'] == 'warm':
            if shells():
                return
            mesh = hero.get_editor_property('mesh')
            assert [mesh.get_material(i).get_path_name() for i in range(mesh.get_num_materials())] == originals
            report['samples'].append({'stage': 'warm', 'coatings': 0})
            pc.request_dev_action(u.MCDevAction.STOP_ICE_EVENT, 0)
            finish()
    except Exception:
        finish(traceback.format_exc())

handle = u.register_slate_post_tick_callback(tick)
print('FROST_MOTION_TEST_INSTALLED')
