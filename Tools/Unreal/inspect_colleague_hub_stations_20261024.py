"""Read-only inspection of real Hub stations, nearest decor, keycaps and roof rays.

Never loads another map, starts/stops PIE, moves actors, saves packages or edits
collision. It reads the open Hub editor world, or its existing PIE world when
use_pie=True. Optional capsule_radius_cm reproduces the real preview ray offsets;
without PIE or this argument the report labels its CDO-radius probe as such.
"""
from datetime import datetime, timezone
import json
from pathlib import Path
import re


def path(value):
    return value.get_path_name() if value else None


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def label(actor):
    try:
        return actor.get_actor_label()
    except Exception:
        return actor.get_name()


def dirty(u):
    return {'content':sorted(path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
            'maps':sorted(path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}


def collision(u, component):
    return {'enabled':str(component.get_collision_enabled()),
            'profile':str(component.get_collision_profile_name()),
            'pawn':str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN)),
            'visibility':str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_VISIBILITY)),
            'camera':str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA))}


def transform(component):
    return {'location':vec(component.get_world_location()), 'transform':str(component.get_world_transform())}


def bounds_distance(point, center, extent):
    return sum(max(abs(a-b)-e, 0.)**2 for a,b,e in zip(vec(point),vec(center),vec(extent)))**.5


def run(output='E:/CodexWork/ColleagueCosmeticsReconcile_20261024/HubStationInspection.json',
        use_pie=False, capsule_radius_cm=None):
    import unreal as u
    destination = Path(output).resolve()
    assert destination.drive.upper() == 'E:'
    assert Path(u.Paths.project_dir()).resolve().drive.upper() == 'E:'
    before = dirty(u)
    pie = list(u.EditorLevelLibrary.get_pie_worlds(True))
    if use_pie:
        assert len(pie) == 1, 'Expected one existing local PIE world; inspector never starts it'
        world = pie[0]
    else:
        assert not pie, 'Choose use_pie=True to inspect an already running Play session'
        world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world and re.search(r'/(?:UEDPIE_\d+_)?Hub$', world.get_path_name().split('.',1)[0]), 'Open the actual Hub first'
    actors = list(u.GameplayStatics.get_all_actors_of_class(world, u.Actor))
    heroes = [a for a in actors if isinstance(a, u.MCToothCharacter)]
    if capsule_radius_cm is not None:
        radius = float(capsule_radius_cm);radius_source = 'explicit inspected runtime radius'
    elif heroes:
        radius = float(heroes[0].capsule_component.get_scaled_capsule_radius());radius_source = path(heroes[0])
    else:
        cdo = u.get_default_object(u.MCToothCharacter)
        radius = float(cdo.capsule_component.get_scaled_capsule_radius())
        radius_source = 'native CDO only; actual Hub pawn scale not proven by an idle-editor probe'
    assert radius > 0
    candidates = []
    keywords = re.compile(r'wardrobe|locker|cabinet|cabin|booth|closet|shop|store|buster|cube_006|vending', re.I)
    for actor in actors:
        for component in actor.get_components_by_class(u.StaticMeshComponent):
            mesh = component.static_mesh
            if not mesh:
                continue
            identity = ' '.join((actor.get_name(), label(actor), mesh.get_path_name()))
            if not keywords.search(identity):
                continue
            center, extent = actor.get_actor_bounds(False)
            candidates.append({'actor':actor, 'component':component, 'center':center, 'extent':extent,
                               'record':{'actor':path(actor), 'label':label(actor), 'mesh':path(mesh),
                                         'actor_transform':str(actor.get_actor_transform()),
                                         'actor_bounds_center':vec(center), 'actor_bounds_extent':vec(extent),
                                         'component':path(component), 'collision':collision(u,component)}})
    stations = [a for a in actors if isinstance(a, (u.MCHubWardrobeStation, u.MCHubShopStation))]
    ignored = heroes+[a for a in actors if isinstance(a, u.MCWardrobePreviewActor)]
    breaker = u.get_default_object(u.GameplayStatics)

    def trace(start, end):
        hit = u.SystemLibrary.line_trace_single(world,start,end,u.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                               False,ignored,u.DrawDebugTrace.NONE,True)
        if not hit:
            return {'blocking':False, 'start':vec(start), 'end':vec(end)}
        data = breaker.call_method('BreakHitResult', args=(hit,))
        return {'blocking':bool(data[0]), 'actor':path(data[9]), 'component':path(data[10]),
                'point':vec(data[5]), 'normal':vec(data[7]), 'start':vec(start), 'end':vec(end)}

    report = {'captured_utc':datetime.now(timezone.utc).isoformat(), 'world':path(world), 'use_pie':use_pie,
              'capsule_radius_cm':radius, 'radius_source':radius_source, 'stations':[],
              'limits':['Metadata and floor-ray inspection only; no movement/overlap or UI interaction test.',
                        'Roof suspicion compares ray heights; a high horizontal surface is not automatically a roof.',
                        'Named decor candidates are heuristic; report contains exact matching mesh/actor identities.'],
              'dirty_before':before}
    for station in stations:
        volume = station.get_editor_property('InteractionVolume')
        prompt = station.get_editor_property('InteractionPrompt')
        origin = station.get_actor_location()
        item = {'actor':path(station), 'label':label(station), 'class':path(station.get_class()),
                'enabled':bool(station.get_editor_property('bEnabled')), 'actor_transform':str(station.get_actor_transform()),
                'interaction':dict(transform(volume), extent=vec(volume.get_unscaled_box_extent()), collision=collision(u,volume)),
                'E_prompt':dict(transform(prompt), visible=bool(prompt.is_visible()),
                        widget_class=path(prompt.get_editor_property('widget_class')), space=str(prompt.get_editor_property('space'))),
                'nearest_decor':[]}
        ranked = sorted(candidates, key=lambda candidate:bounds_distance(origin,candidate['center'],candidate['extent']))
        for candidate in ranked[:10]:
            record = dict(candidate['record'])
            record['distance_to_actor_bounds_cm'] = bounds_distance(origin,candidate['center'],candidate['extent'])
            record['decor_center_in_interaction_local'] = vec(u.MathLibrary.inverse_transform_location(
                volume.get_world_transform(),candidate['center']))
            item['nearest_decor'].append(record)
        item['floor_rays'] = []
        for step in range(6):
            approach = origin+station.get_actor_right_vector()*(radius*(3.+step*2.))
            # Exact current BeginPresentation floor ray, plus a low-start probe
            # that can expose a roof hit by the high-start ray inside a store.
            high = trace(approach+u.Vector(0,0,500),approach-u.Vector(0,0,2000))
            low = trace(approach+u.Vector(0,0,80),approach-u.Vector(0,0,2000))
            row = {'step':step, 'approach':vec(approach), 'current_preview_trace':high, 'low_start_probe':low}
            if high.get('blocking') and low.get('blocking'):
                row['high_minus_low_surface_z_cm'] = high['point'][2]-low['point'][2]
            item['floor_rays'].append(row)
        report['stations'].append(item)
    report['dirty_after'] = dirty(u)
    assert report['dirty_after'] == before, 'Read-only inspection unexpectedly changed package state'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('MC_COLLEAGUE_HUB_STATIONS_INSPECTED '+str(destination))
    return report


if __name__ == '__main__':
    run()
