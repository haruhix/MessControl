import json
import time
import traceback
from pathlib import Path
import unreal as u

worlds = list(u.EditorLevelLibrary.get_pie_worlds(False))
assert len(worlds) == 1, len(worlds)
world = worlds[0]
pc = u.GameplayStatics.get_player_controller(world, 0)
hero = u.GameplayStatics.get_player_pawn(world, 0)
assert isinstance(hero, u.MCToothCharacter)
pc.request_dev_action(u.MCDevAction.ICE_EVENT, 0)
ice = next(a for a in u.GameplayStatics.get_all_actors_of_class(world, u.MCIceEvent) if a.actor_has_tag('MC_DevKeyEvent'))
ice.set_editor_properties({'arrival_seconds': 1.0, 'freeze_seconds': 60.0, 'thaw_seconds': 1.0, 'circle_seconds': 60.0, 'icicle_damage': 0.0})
out = Path(u.Paths.project_dir())/'Artifacts/PlayerFrostMask'
out.mkdir(parents=True, exist_ok=True)
originals = [hero.get_editor_property('mesh').get_material(i).get_path_name() for i in range(hero.get_editor_property('mesh').get_num_materials())]
report = {'passed': False, 'samples': [], 'camera': None}
state = {'stage': 'arrival', 'started': time.monotonic(), 'at': time.monotonic()}
handle = None

def shells():
    return [c for c in ice.get_components_by_class(u.SkeletalMeshComponent) if c.component_has_tag('MC_PlayerIceCoating')]

def materials_unchanged():
    assert originals == [hero.get_editor_property('mesh').get_material(i).get_path_name() for i in range(hero.get_editor_property('mesh').get_num_materials())]

def seed(amount):
    players = list(ice.get_editor_property('players'))
    assert len(players) == 1
    players[0].set_editor_property('amount', amount)
    ice.set_editor_property('players', players)

def finish(error=None):
    report.update(passed=error is None, error=error)
    u.unregister_slate_post_tick_callback(handle)
    (out/'PreviewVerification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('FROST_PREVIEW_FINISHED', json.dumps(report))

def tick(dt):
    global hero, originals
    try:
        if time.monotonic()-state['started'] > 600:
            raise AssertionError(('Frost preview timed out', state))
        elapsed = time.monotonic()-state['at']
        if state['stage'] == 'arrival':
            if ice.get_editor_property('stage') != u.MCIceEventStage.ACTIVE:
                return
            # Restarting the dev event may respawn the pawn; use the possessed one.
            hero = u.GameplayStatics.get_player_pawn(world, 0)
            assert isinstance(hero, u.MCToothCharacter)
            originals = [hero.get_editor_property('mesh').get_material(i).get_path_name() for i in range(hero.get_editor_property('mesh').get_num_materials())]
            safe = ice.get_editor_property('tongue').get_actor_transform().transform_location(ice.get_editor_property('safe_anchor'))
            half_height = hero.get_editor_property('capsule_component').get_scaled_capsule_half_height()
            hero.set_actor_location(safe+u.Vector(600, 0, half_height+5), False, False)
            hero.get_editor_property('character_movement').stop_movement_immediately()
            hero.get_editor_property('character_movement').disable_movement()
            hero.set_actor_tick_enabled(False)
            hero.get_editor_property('CameraBoom').set_component_tick_enabled(False)
            center = hero.get_actor_location()+u.Vector(0, 0, 5)
            location = center+hero.get_actor_forward_vector()*370+hero.get_actor_right_vector()*75+u.Vector(0, 0, 50)
            rotation = u.MathLibrary.find_look_at_rotation(location, center)
            camera = hero.get_editor_property('Camera')
            camera.set_absolute(True, True, False)
            camera.set_world_location_and_rotation(location, rotation, False, False)
            camera.set_field_of_view(40)
            pc.set_view_target_with_blend(hero, 0)
            report['camera'] = {'location': list(location.to_tuple()), 'rotation': list(rotation.to_tuple())}
            state.update(stage='setup', at=time.monotonic())
        elif state['stage'] == 'setup':
            if elapsed < .5:
                return
            seed(.25)
            state.update(stage='low', at=time.monotonic())
        elif state['stage'] in ['low', 'high']:
            parts = shells()
            if elapsed < .8 or not parts:
                return
            mid = parts[0].get_material(0)
            amount = mid.get_scalar_parameter_value('IceAmount')
            if amount < (.20 if state['stage'] == 'low' else .70):
                return
            assert mid.get_texture_parameter_value('Frost Mask Texture').get_path_name() == '/Game/Gameplay/Cold/Frost/T_PlayerFrostMask.T_PlayerFrostMask'
            materials_unchanged()
            report['samples'].append({'stage': state['stage'], 'amount': amount, 'mask': mid.get_texture_parameter_value('Frost Mask Texture').get_path_name()})
            u.GameplayStatics.set_game_paused(world, True)
            (out/'PreviewReady.json').write_text(json.dumps({'stage': state['stage'], 'amount': amount, 'camera': report['camera']}, indent=2), encoding='utf-8')
            state.update(stage='wait_'+state['stage'], at=time.monotonic())
        elif state['stage'] in ['wait_low', 'wait_high']:
            command = out/'PreviewAdvance.txt'
            if not command.exists():
                return
            command.unlink()
            if state['stage'] == 'wait_low':
                seed(.78)
                state.update(stage='high', at=time.monotonic())
            else:
                safe = ice.get_editor_property('tongue').get_actor_transform().transform_location(ice.get_editor_property('safe_anchor'))
                hero.set_actor_location(safe+u.Vector(0, 0, hero.get_editor_property('capsule_component').get_scaled_capsule_half_height()+5), False, False)
                state.update(stage='warm', at=time.monotonic())
            u.GameplayStatics.set_game_paused(world, False)
        elif state['stage'] == 'warm':
            if shells():
                return
            materials_unchanged()
            report['samples'].append({'stage': 'warm', 'coatings': 0})
            pc.request_dev_action(u.MCDevAction.STOP_ICE_EVENT, 0)
            finish()
    except Exception:
        finish(traceback.format_exc())

handle = u.register_slate_post_tick_callback(tick)
print('FROST_PREVIEW_INSTALLED')
