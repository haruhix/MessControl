"""Author one movable fishing-access box from an explicitly inspected pier.

run('plan', pier_actor='<actual actor label>') only inspects the open Hub.
run('apply', pier_actor=...) verifies an E: backup and saves only Hub. Existing
fishing zones keep designer placement unless fit_existing_to_pier=True.
No level load, build, PIE, network connection, SaveAll or mesh edits occur.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import shutil


HUB = '/Game/Maps/Hub'
ZONE_CLASS = '/Script/MessControl.MCHubFishingZone'
ZONE_LABEL = 'Hub_PierFishingZone'
OWNER_TAG = 'MC.Hub.PierFishingZone'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def record(actor):
    rotation = actor.get_actor_rotation()
    return {'name': actor.get_name(), 'label': actor.get_actor_label(),
            'class': actor.get_class().get_path_name(), 'location': vec(actor.get_actor_location()),
            'rotation': [float(rotation.pitch), float(rotation.yaw), float(rotation.roll)],
            'scale': vec(actor.get_actor_scale3d()), 'tags': [str(tag) for tag in actor.tags]}


def dirty(u):
    return sorted({package.get_path_name() for package in
                   list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()) +
                   list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())})


def fit_to_pier(u, component, edge_inset, player_height, half_height):
    minimum, maximum = component.get_local_bounds()
    transform = component.get_world_transform()
    points = [u.MathLibrary.transform_location(transform, u.Vector(x, y, z))
              for x, y, z in itertools.product((minimum.x, maximum.x),
                                               (minimum.y, maximum.y),
                                               (minimum.z, maximum.z))]
    yaw = float(component.get_world_rotation().yaw)
    cosine, sine = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    xs = [point.x * cosine + point.y * sine for point in points]
    ys = [-point.x * sine + point.y * cosine for point in points]
    center_x, center_y = (min(xs) + max(xs)) * .5, (min(ys) + max(ys)) * .5
    extent = [(max(xs) - min(xs)) * .5 - edge_inset,
              (max(ys) - min(ys)) * .5 - edge_inset, half_height]
    require(all(math.isfinite(value) and value >= 25 for value in extent),
            'Pier is too small for the requested inset; inspect bounds/inset')
    return {'location': [center_x * cosine - center_y * sine,
                         center_x * sine + center_y * cosine,
                         max(point.z for point in points) + player_height],
            'yaw': yaw, 'half_extent': extent,
            'pier_component': component.get_path_name(),
            'pier_mesh': component.static_mesh.get_path_name(),
            'edge_inset_cm': edge_inset, 'player_center_above_pier_cm': player_height}


def run(action='plan', *, pier_actor=None, pier_component=None,
        edge_inset=12., player_height=95., half_height=140.,
        fit_existing_to_pier=False,
        output='E:/CodexWork/MessControlHubFishing_20261022/ZoneAuthoring'):
    import unreal as u
    require(action in ('plan', 'apply'), 'Expected plan or apply')
    require(all(math.isfinite(value) for value in (edge_inset, player_height, half_height))
            and 0 <= edge_inset <= 100 and 30 <= player_height <= 200 and 80 <= half_height <= 300,
            'Invalid pier-volume tuning')
    root = Path(u.Paths.project_dir()).resolve()
    out_root = Path(output).resolve()
    require(root.drive.upper() == 'E:' and out_root.drive.upper() == 'E:', 'All working/output files must stay on E:')
    require((root / 'MessControl.uproject').is_file(), 'Wrong project')
    level = u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not level.is_in_play_in_editor(), 'Preserve the current PIE session')
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(world and world.get_path_name().split('.', 1)[0] == HUB, 'Open the saved Hub before running')
    require(not dirty(u), 'Preserve unsaved editor packages before authoring: ' + str(dirty(u)))
    editor = u.get_editor_subsystem(u.EditorActorSubsystem)
    actors = list(editor.get_all_level_actors())
    report = {'action': action, 'map': HUB, 'scope': __doc__, 'changed_assets': [],
              'pier_candidates': [record(actor) for actor in actors
                                  if any(key in (actor.get_actor_label() + ' ' + actor.get_name()).casefold()
                                         for key in ('pier', 'pirs', 'fishing', 'fishrod'))]}
    if pier_actor is None:
        require(action == 'plan', 'Apply requires an explicitly inspected pier actor label/name')
        u.log(json.dumps(report, ensure_ascii=False, indent=2))
        return report
    matches = [actor for actor in actors if pier_actor in (actor.get_name(), actor.get_actor_label())]
    require(len(matches) == 1, 'Choose exactly one inspected pier actor: ' + str(pier_actor))
    pier = matches[0]
    components = [component for component in pier.get_components_by_class(u.StaticMeshComponent)
                  if component.static_mesh and (pier_component is None or component.get_name() == pier_component)]
    require(len(components) == 1, 'Choose the exact pier StaticMeshComponent when more than one exists')
    report['pier'] = record(pier)
    report['placement'] = fit_to_pier(u, components[0], edge_inset, player_height, half_height)
    native = u.load_class(None, ZONE_CLASS)
    require(native, 'Build the Editor with MCHubFishingZone before applying')
    zones = [actor for actor in actors if isinstance(actor, u.MCHubFishingZone)]
    owned = [actor for actor in zones if OWNER_TAG in [str(tag) for tag in actor.tags]
             or actor.get_actor_label() == ZONE_LABEL]
    require(len(owned) <= 1, 'Several owned fishing zones exist; inspect them before authoring')
    require(not zones or owned, 'A designer fishing zone exists; inspect it rather than creating an overlapping box')
    report['existing_fishing_zones'] = [record(actor) for actor in zones]
    report['existing_player_starts'] = [record(actor) for actor in actors if isinstance(actor, u.PlayerStart)]
    report['ready_targets'] = [record(actor) for actor in actors if isinstance(actor, u.MCHubReadyZone)
                               and actor.get_editor_property('bEnabled')]
    report['spawn_orientation'] = 'Native GameMode faces the nearest enabled Ready zone on each spawn/recovery; PlayerStarts stay in place.'
    if action == 'plan':
        u.log(json.dumps(report, ensure_ascii=False, indent=2))
        return report

    out = out_root / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    out.mkdir(parents=True, exist_ok=False)
    source = root / 'Content/Maps/Hub.umap'
    backup = out / 'BeforePackages/Content/Maps/Hub.umap'
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, backup)
    before_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    require(hashlib.sha256(backup.read_bytes()).hexdigest() == before_hash, 'Map backup verification failed')
    report['backup'] = {'source': str(source), 'backup': str(backup), 'sha256': before_hash}
    (out / 'Plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    original = {actor.get_name(): record(actor) for actor in actors if actor not in owned}
    placement = report['placement']
    zone = owned[0] if owned else editor.spawn_actor_from_class(native, u.Vector(*placement['location']),
                                                                u.Rotator(0., placement['yaw'], 0.))
    require(zone, 'Could not create the fishing zone')
    if not owned or fit_existing_to_pier:
        zone.modify()
        zone.set_actor_location(u.Vector(*placement['location']), False, True)
        zone.set_actor_rotation(u.Rotator(0., placement['yaw'], 0.), True)
        zone.set_actor_scale3d(u.Vector(1., 1., 1.))
        zone.set_zone_extent(u.Vector(*placement['half_extent']))
    if not owned:
        zone.set_actor_label(ZONE_LABEL)
        zone.tags = list(zone.tags) + [u.Name(OWNER_TAG), u.Name('MCHubOwned')]
        zone.set_folder_path('Hub/Activities')
    current = {actor.get_name(): record(actor) for actor in editor.get_all_level_actors()}
    require(all(current.get(name) == value for name, value in original.items()), 'An unrelated actor changed')
    if not owned or fit_existing_to_pier:
        require(level.save_current_level(), 'Could not save the Hub fishing zone')
        report['changed_assets'].append(HUB)
    report['zone'] = record(zone)
    report['zone_half_extent'] = vec(zone.get_editor_property('zone').get_unscaled_box_extent())
    report['preserved_designer_placement'] = bool(owned and not fit_existing_to_pier)
    report['dirty_after'] = dirty(u)
    require(not report['dirty_after'], 'Unexpected unsaved packages remain')
    report['status'] = 'authored; runtime validation pending'
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_HUB_FISHING_ZONE_AUTHORED ' + str(out / 'Report.json'))
    return report
