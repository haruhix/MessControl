"""Read-only saved Hub/selector inspection; never starts PIE or saves a package.

Execute after opening MessControl with ``MC_HUB_INSPECT_OPTIONS`` containing an
optional ``maps`` list and ``output`` E: path. An idle, clean editor is required
before changing maps. The original map is restored when requested.
"""
from __future__ import annotations

import json
from pathlib import Path
import traceback

DEFAULT_MAPS = ['/Game/Maps/Hub', '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08']
DEFAULT_OUTPUT = 'E:/CodexWork/MessControlHub_20261019/Inspection/Maps.json'


def vec(value):
    return [round(float(value.x), 4), round(float(value.y), 4), round(float(value.z), 4)]


def obj_path(value):
    return value.get_path_name() if value else None


def inspect_world(u):
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
    settings = world.get_world_settings()
    result = {'world': obj_path(world), 'game_mode': obj_path(settings.get_editor_property('default_game_mode')),
              'actors': [], 'player_starts': [], 'ground_candidates': []}
    try:
        level = u.get_editor_subsystem(u.LevelEditorSubsystem)
        camera = level.get_level_viewport_camera_info(level.get_active_viewport_config_key())
        assert camera, 'No active editor viewport'
        location, rotation = camera[-2:]
        result['viewport'] = {'location': vec(location), 'rotation': str(rotation)}
    except Exception as exc:
        result['viewport_error'] = str(exc)
    for actor in actors:
        center, extent = actor.get_actor_bounds(False)
        item = {'name': actor.get_name(), 'label': actor.get_actor_label(), 'class': obj_path(actor.get_class()),
                'location': vec(actor.get_actor_location()), 'rotation': str(actor.get_actor_rotation()),
                'scale': vec(actor.get_actor_scale3d()), 'bounds_center': vec(center), 'bounds_extent': vec(extent),
                'tags': [str(tag) for tag in actor.tags], 'components': []}
        if isinstance(actor, u.PlayerStart):
            result['player_starts'].append(item['label'])
        if isinstance(actor, u.Pawn):
            item['auto_possess_player'] = str(actor.get_editor_property('auto_possess_player'))
            item['auto_possess_ai'] = str(actor.get_editor_property('auto_possess_ai'))
        for component in actor.get_components_by_class(u.PrimitiveComponent):
            mesh = None
            if isinstance(component, u.StaticMeshComponent):
                mesh = component.static_mesh
            elif isinstance(component, u.SkeletalMeshComponent):
                mesh = component.get_editor_property('skeletal_mesh_asset')
            component_info = {'name': component.get_name(), 'class': component.get_class().get_name(),
                              'mesh': obj_path(mesh), 'transform': str(component.get_world_transform()),
                              'collision': str(component.get_collision_enabled()),
                              'profile': str(component.get_collision_profile_name())}
            if isinstance(mesh, u.StaticMesh):
                component_info['local_bounds'] = str(mesh.get_bounding_box())
            item['components'].append(component_info)
            if mesh and ('plane' in mesh.get_name().lower() or 'floor' in mesh.get_name().lower()):
                result['ground_candidates'].append({'actor': item['label'], 'mesh': obj_path(mesh),
                                                    'center': item['bounds_center'], 'extent': item['bounds_extent']})
        if isinstance(actor, u.CameraActor):
            camera = actor.get_component_by_class(u.CameraComponent)
            item['camera'] = {'fov': camera.field_of_view, 'aspect_ratio': camera.aspect_ratio}
        result['actors'].append(item)
    return result


def run(*, maps=None, output=DEFAULT_OUTPUT, restore_original=True):
    import unreal as u
    output = Path(output).resolve()
    assert output.drive.upper() == 'E:', 'Inspection outputs must be on E:'
    level = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor(), 'Stop PIE before read-only map inspection'
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    assert not dirty, 'Preserve unsaved packages before changing maps: ' + str([obj_path(p) for p in dirty])
    original = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.', 1)[0]
    report = {'status': 'running', 'saved_assets': [], 'original_map': original, 'maps': []}
    try:
        for package in maps or DEFAULT_MAPS:
            assert package.startswith('/Game/') and '..' not in package
            assert level.load_level(package), 'Could not inspect map: ' + package
            report['maps'].append(inspect_world(u))
        if restore_original and original:
            assert level.load_level(original), 'Could not restore original map'
        report['status'] = 'passed'
        return report
    except Exception as exc:
        report.update(status='failed', error=str(exc), traceback=traceback.format_exc())
        raise
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        u.log('MC_HUB_INSPECTION ' + str(output))


if __name__ == '__main__':
    run(**globals().get('MC_HUB_INSPECT_OPTIONS', {}))
