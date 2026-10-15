"""Focused, read-only mesh-fit evidence on the existing CoffeeSlime v2 solo path.

Run from a saved runner with runpy.run_path(..., run_name='__main__',
init_globals={'MC_PAINT_FIT_EXPECT_CONFORMING': False}) for the old-mesh baseline.
Default True checks the repaired fit. Own reports/frames live below
Saved/CoffeeSurfaceFit/{Baseline|Fixed}/Capture. No build/package/network work.
Cancel: builtins.mc_coffee_surface_fit_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import traceback
import uuid
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
EXPECT = bool(globals().get('MC_PAINT_FIT_EXPECT_CONFORMING', True))
OUTPUT = ROOT/'Saved/CoffeeSurfaceFit'/('Fixed' if EXPECT else 'Baseline')/'Capture'
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_coffee_slime_v2_solo.py'),
                        run_name='coffee_surface_fit_support')
Parent, prop, valid, path, vec = (support[n] for n in ('CoffeeSlimeV2Solo', 'prop', 'valid', 'path', 'vec'))
KEY = 'mc_coffee_surface_fit_solo'


def section(component):
    vertices, indices, normals, uv, tangents = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(component, 0)
    transform = component.get_world_transform()
    return [vec(transform.transform_location(v)) for v in vertices], list(indices), [(v.x, v.y) for v in uv]


def interpolate(points, weights):
    return [sum(p[axis]*w for p, w in zip(points, weights)) for axis in range(len(points[0]))]


def wet(mask, size, uv):
    if not (0. <= uv[0] <= 1. and 0. <= uv[1] <= 1.):
        return False
    x, y = uv[0]*size-.5, uv[1]*size-.5
    ix, iy = math.floor(x), math.floor(y)
    sample = lambda dx, dy: mask[min(size-1, max(0, iy+dy))*size+min(size-1, max(0, ix+dx))]
    a = sample(0, 0)*(1.-x+ix)+sample(1, 0)*(x-ix)
    b = sample(0, 1)*(1.-x+ix)+sample(1, 1)*(x-ix)
    return a*(1.-y+iy)+b*(y-iy) > 63.


class HeightGrid:
    """Topmost exact triangle height in XY; no collision trace approximation."""
    def __init__(self, vertices, indices, cell=100.):
        self.cell, self.triangles, self.grid = cell, [], {}
        for offset in range(0, len(indices), 3):
            p = [vertices[i] for i in indices[offset:offset+3]]
            a, b, c = p
            determinant = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(determinant) < 1.e-8:
                continue
            index = len(self.triangles)
            self.triangles.append((p, determinant))
            for x in range(math.floor(min(v[0] for v in p)/cell), math.floor(max(v[0] for v in p)/cell)+1):
                for y in range(math.floor(min(v[1] for v in p)/cell), math.floor(max(v[1] for v in p)/cell)+1):
                    self.grid.setdefault((x, y), []).append(index)

    def height(self, point):
        highest = None
        for index in self.grid.get((math.floor(point[0]/self.cell), math.floor(point[1]/self.cell)), []):
            (a, b, c), d = self.triangles[index]
            x, y = point[:2]
            wa = ((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/d
            wb = ((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/d
            wc = 1.-wa-wb
            if min(wa, wb, wc) >= -1.e-7:
                height = wa*a[2]+wb*b[2]+wc*c[2]
                highest = height if highest is None else max(highest, height)
        return highest


class CoffeeSurfaceFitSolo(Parent):
    def __init__(self):
        super().__init__()
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        self.output = OUTPUT/'Runs'/(stamp+'_'+uuid.uuid4().hex[:8])
        self.output.mkdir(parents=True, exist_ok=True)
        self.report.update(run_directory=str(self.output), purpose='Actual coffee-paint / deforming tongue triangle fit and hero floor support',
                           fit_expected_conforming=EXPECT, surface_fit_snapshots=[])
        self.report['source_sha256']['Tools/Unreal/probe_coffee_surface_fit_solo.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        self.report['limitations'].append('Two synchronous game-thread snapshots compare actual procedural section-0 triangles and bilinear wet mask >63. '
            'Three interior barycentric samples per paint triangle plus wet mask-cell centers cannot prove every pixel or material WPO. '
            'Existing observer-position fixtures remain disclosed; capsule/floor/indentation measurements distinguish support from visual overlap.')

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_paint.json').write_text(data, encoding='utf-8')

    def snapshot_fit(self, now):
        surface = prop(self.tongue, 'surface')
        vertices, indices, _ = section(surface)
        tongue = HeightGrid(vertices, indices)
        rotation = surface.get_world_rotation()
        entry = dict(game_seconds=now, phase=self.phase, phase_seconds=now-self.phase_at,
                     tongue_vertices=len(vertices), tongue_triangles=len(indices)//3, tiles=[],
                     tongue_surface_transform=dict(location_cm=vec(surface.get_world_location()),
                         rotation_degrees=[float(rotation.pitch), float(rotation.yaw), float(rotation.roll)],
                         scale=vec(surface.get_world_scale())))
        for tile in self.actors(u.MCMouthSurface):
            if tile.get_owner() != self.event or not bool(prop(tile, 'paint_tile')):
                continue
            v, triangles, uv = section(prop(tile, 'liquid'))
            mask = [int(value) for value in prop(tile, 'wipe_mask')]
            size = math.isqrt(len(mask))
            self.require(size > 0 and size*size == len(mask) and len(uv) == len(v), 'Invalid actual paint mesh UV/mask')
            samples, outside = [], 0
            for offset in range(0, len(triangles), 3):
                ids = triangles[offset:offset+3]
                for weights in ((.6, .2, .2), (.2, .6, .2), (.2, .2, .6)):
                    if not wet(mask, size, interpolate([uv[i] for i in ids], weights)):
                        continue
                    point = interpolate([v[i] for i in ids], weights)
                    height = tongue.height(point)
                    if height is None:
                        outside += 1
                    else:
                        samples.append((point[2]-height, point))
            paint = HeightGrid(v, triangles)
            transform, half = tile.get_actor_transform(), float(prop(tile, 'liquid_half_size'))
            centers, missing, outside_centers = [], 0, 0
            for index, value in enumerate(mask):
                if value <= 63:
                    continue
                point = vec(transform.transform_location(u.Vector(((index%size+.5)/size-.5)*2.*half,
                    ((index//size+.5)/size-.5)*2.*half, 0.)))
                base, top = tongue.height(point), paint.height(point)
                if base is None:
                    outside_centers += 1
                elif top is None:
                    missing += 1
                else:
                    centers.append((top-base, point[:2]+[top]))
            both = samples+centers
            lowest = min(both, default=None, key=lambda item: item[0])
            entry['tiles'].append(dict(tile=path(tile), mesh_vertices=len(v), mesh_triangles=len(triangles)//3,
                wet_triangle_samples=len(samples), wet_cell_center_samples=len(centers),
                samples_below_minus_005cm=sum(gap < -.05 for gap, _ in both),
                minimum_clearance_cm=lowest[0] if lowest else None, worst_point_cm=lowest[1] if lowest else None,
                maximum_clearance_cm=max((gap for gap, _ in both), default=None),
                wet_samples_outside_tongue=outside, wet_centers_outside_tongue=outside_centers,
                wet_centers_on_tongue_missing_paint_geometry=missing))
        position = self.hero.get_actor_location()
        bottom = position.z-self.hero.capsule_component.get_scaled_capsule_half_height()
        height = tongue.height(vec(position))
        movement = self.hero.get_movement_component()
        hero = dict(position_cm=vec(position), capsule_bottom_z_cm=bottom, tongue_render_z_cm=height,
                    capsule_clearance_cm=None if height is None else bottom-height,
                    velocity_cm_s=vec(self.hero.get_velocity()), movement_mode=str(prop(movement, 'movement_mode')),
                    movement_base=path(movement.call_method('GetMovementBaseObject', args=())),
                    pressure_indentation_cm=float(self.tongue.indentation_at(position)),
                    pressure_max_depth_cm=float(prop(prop(self.tongue, 'pressure_settings'), 'max_depth')))
        try:
            floor = prop(movement, 'current_floor')
            hero['current_floor'] = dict(floor_distance_cm=float(prop(floor, 'floor_dist')),
                line_distance_cm=float(prop(floor, 'line_dist')), walkable=bool(prop(floor, 'walkable_floor')),
                blocking_hit=bool(prop(floor, 'blocking_hit')))
        except Exception as error:
            hero['current_floor_unavailable'] = str(error)
        else:
            try:
                # GameplayStatics.h BreakHitResult out-parameter order: impact
                # point=5, impact normal=7, actor=9, component=10. FHitResult
                # does not reliably expose its members as Python properties.
                fields = self.statics.call_method('BreakHitResult', args=(prop(floor, 'hit_result'),))
                hero['current_floor'].update(impact_point_cm=vec(fields[5]), impact_normal=vec(fields[7]),
                    actor=path(fields[9]), component=path(fields[10]))
            except Exception as error:
                hero['current_floor']['hit_details_unavailable'] = str(error)
        entry['hero'] = hero
        self.report['surface_fit_snapshots'].append(entry)
        self.write()

    def sample(self, now):
        super().sample(now)
        phases = {entry['phase'] for entry in self.report['surface_fit_snapshots']}
        if (self.phase == 'paint_trail' and now-self.phase_at >= 10.) or (self.phase == 'paint_brush' and now-self.phase_at >= 2.):
            if self.phase not in phases:
                self.snapshot_fit(now)

    def finish(self, error=None):
        if not error:
            snapshots = self.report['surface_fit_snapshots']
            tiles = [tile for entry in snapshots for tile in entry['tiles']]
            count = sum(tile['wet_triangle_samples']+tile['wet_cell_center_samples'] for tile in tiles)
            below = sum(tile['samples_below_minus_005cm'] for tile in tiles)
            self.report['fit_summary'] = dict(snapshots=len(snapshots), wet_comparison_samples=count, below_minus_005cm=below)
            supported = all('MOVE_WALKING' in entry['hero']['movement_mode']
                and entry['hero']['movement_base'].endswith('.TongueSurface')
                and entry['hero']['capsule_clearance_cm'] is not None
                and -.5 <= entry['hero']['capsule_clearance_cm'] <= 5.
                and abs(entry['hero']['velocity_cm_s'][2]) < 1.
                for entry in snapshots)
            if len(snapshots) != 2 or count < 30:
                error = 'Insufficient real paint/tongue surface-fit evidence'
            elif EXPECT and below:
                error = 'Actual wet paint triangles still intersect rendered tongue at '+str(below)+' sampled points'
            elif not supported:
                error = 'Observed hero lost walking support on the tongue'
            else:
                self.report['checks']['real_surface_fit_observed_at_trail_and_brush'] = True
                self.report['checks']['wet_paint_does_not_intersect_rendered_tongue'] = below == 0
                self.report['checks']['hero_remains_supported_on_tongue'] = True
        super().finish(error)


if __name__ == '__main__':
    old = getattr(builtins, KEY, None)
    if old and not getattr(old, 'done', True):
        raise RuntimeError('Preserve the active owned surface-fit probe')
    probe = CoffeeSurfaceFitSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
