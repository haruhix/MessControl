"""Read-only snapshot of the current solo PIE's actual floor/tooth cleaning debt.

Does not start/stop Play, choose perks, move actors, change care masks or advance
the sequence. Run before/after ordinary brush input. JSON is always saved on E:.
"""
import datetime
import hashlib
import json
import math
import runpy
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
assert ROOT.drive.upper() == 'E:', 'Task output must stay on E:'
WORLD = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
assert WORLD, 'Run during the intended solo PIE; this observer never starts Play'
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_coffee_surface_fit_solo.py'), run_name='cleanup_snapshot_support')
HeightGrid, section = support['HeightGrid'], support['section']


def prop(obj, name, default=None):
    native = ''.join(part[:1].upper()+part[1:] for part in name.split('_'))
    for key in (name, native, 'b'+native):
        try:
            return obj.get_editor_property(key)
        except Exception:
            pass
    return default


def name(obj):
    return obj.get_path_name() if obj else None


BATCH_READABLE = None
BATCH_READ_ERROR = None


def batch_or_unknown(patch):
    global BATCH_READABLE, BATCH_READ_ERROR
    if BATCH_READABLE is False:
        return None
    try:
        result = int(patch.get_editor_property('Batch'))
        BATCH_READABLE = True
        return result
    except Exception as error:
        # Native Batch lacks Edit/BlueprintVisible. A zero fallback would be
        # fabricated evidence; ToolsetLibrary enforces the same read flags.
        BATCH_READABLE, BATCH_READ_ERROR = False, str(error)
        return None


tongues = []
for tongue in u.GameplayStatics.get_all_actors_of_class(WORLD, u.MCTongue):
    vertices, indices, _ = section(prop(tongue, 'surface'))
    tongues.append((tongue, HeightGrid(vertices, indices)))

rows = []
for patch in u.GameplayStatics.get_all_actors_of_class(WORLD, u.MCMouthSurface):
    mask = list(prop(patch, 'wipe_mask', []))
    size = math.isqrt(len(mask))
    tile = bool(prop(patch, 'paint_tile', False))
    status = prop(prop(patch, 'status'), 'state')
    vertices, indices, _ = section(prop(patch, 'liquid'))
    grid = HeightGrid(vertices, indices)
    outside = missing_geometry = 0
    if tile and size and size*size == len(mask):
        half = float(prop(patch, 'liquid_half_size'))
        transform = patch.get_actor_transform()
        for index, value in enumerate(mask):
            if int(value) <= 63:
                continue
            point = transform.transform_location(u.Vector(
                ((index % size+.5)/size-.5)*2*half,
                ((index // size+.5)/size-.5)*2*half, 0))
            xyz = (point.x, point.y, point.z)
            outside += not any(surface.height(xyz) is not None for _, surface in tongues)
            missing_geometry += grid.height(xyz) is None
    rows.append(dict(path=name(patch), batch=batch_or_unknown(patch),
        tags=[str(tag) for tag in prop(patch, 'tags', [])], paint_tile=tile,
        ulcer=bool(prop(patch, 'ulcer')), care_left=int(prop(status, 'coffee_left', 0)),
        wet_bytes=sum(int(value) > 63 for value in mask),
        unsupported_wet_centers=outside, wet_centers_without_mesh=missing_geometry,
        liquid_vertices=len(vertices), liquid_triangles=len(indices)//3,
        liquid_visible=bool(prop(patch, 'liquid').is_visible()),
        location=tuple(float(value) for value in (patch.get_actor_location().x, patch.get_actor_location().y, patch.get_actor_location().z))))

teeth = []
for tooth in u.GameplayStatics.get_all_actors_of_class(WORLD, u.MCArenaTooth):
    state, care = prop(tooth, 'state'), prop(prop(tooth, 'status'), 'state')
    relief = prop(tooth, 'grime_relief')
    vertices, indices, _ = section(relief)
    teeth.append(dict(path=name(tooth), tooth_id=int(prop(state, 'tooth_id', -1)),
        lost=bool(prop(state, 'lost')), consumed=bool(prop(state, 'consumed')),
        coffee_left=int(prop(care, 'coffee_left', 0)),
        relief_visible=bool(relief.is_visible()), relief_vertices=len(vertices), relief_triangles=len(indices)//3))

gs = u.GameplayStatics.get_game_state(WORLD)
sequence = prop(gs, 'single_day_director')
report = dict(scope=__doc__, world=name(WORLD), game_seconds=u.GameplayStatics.get_time_seconds(WORLD),
    surface_batch_read=dict(readable=BATCH_READABLE, error=BATCH_READ_ERROR,
        unreadable_value=None, repeated_access_attempts_suppressed=True),
    sequence_stage=str(prop(sequence, 'stage')), tasks_left=int(prop(gs, 'tasks_left', -1)),
    tasks_total=int(prop(gs, 'tasks_total', -1)), surfaces=rows,
    teeth=sorted(teeth, key=lambda row: row['tooth_id']),
    source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    limitations=['Wet-byte counts for ordinary seeded puddles include decorative/outside seed cells; only painted canvases use every byte as task debt.',
        'Geometry support is sampled at dirty texel centers against procedural mesh triangles; material shading and every pixel still require visual review.',
        'An inaccessible native Batch is reported as null, never zero or an inferred value. The real native first-meal gate remains the batch authority.',
        'This snapshot diagnoses debt; it does not prove real brush completion or run the next-stage gate.'])
out = ROOT/'Saved/Feedback5_11_20261018/Cleanup8'
out.mkdir(parents=True, exist_ok=True)
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
target = out/f'Debt_{stamp}.json'
target.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('MC_CLEANUP_DEBT_SNAPSHOT', str(target), 'surfaces', len(rows), 'teeth', len(teeth),
      'unsupported_wet', sum(row['unsupported_wet_centers'] for row in rows))
