"""Short layout-only subset of the genuine first-meal solo PIE probe.

Run in idle L_Mouth after the matching Editor build. The existing one-portion
private PIE profile, normal perk selection, opening wave and real RMB brace are
reused. Native producers choose every dirt position/mask. Stop after a three
second recorded overview: food delivery, brushing and cleanup completion are NOT
tested. Two runs are descriptive layout samples, not a randomness proof.

Output: E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/DirtScatter.
Cancel: builtins.mc_dirt_scatter_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import json
import math
import runpy
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = (ROOT/'Saved/Feedback5_11_20261018/DirtScatter').resolve()
if OUTPUT.drive.upper() != 'E:':
    raise RuntimeError('DirtScatter evidence must remain on E:')
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_cleanup_first_relax_solo.py'),
    run_name='dirt_scatter_cleanup_support', init_globals={
        'MC_CLEANUP_OUTPUT': OUTPUT,
        'MC_CLEANUP_CAPTURE_HZ': globals().get('MC_DIRT_SCATTER_CAPTURE_HZ', 8)})
Parent, prop, valid, path, vec = (support[key] for key in
    ('CleanupFirstRelaxSolo', 'prop', 'valid', 'path', 'vec'))
KEY = 'mc_dirt_scatter_solo'


class DirtScatterSolo(Parent):
    def __init__(self):
        super().__init__()
        self.report.update(mode='dirt_scatter_layout_subset', scope=__doc__,
            cleanup_passed=None, subset_passed=False, layout_samples=[],
            flood_sources={},
            wave_seed_scope='Native wave seed also depends on StartedAt/ArenaCenter; RunSeed alone does not reproduce this layout.',
            completed_subset=False, visual_approval='Pending root review of genuine frames',
            limitations=[
                'Only initial native first-meal dirt placement and a three-second overview are observed.',
                'No food cutting/delivery, brushing, FirstMeal completion, whole-mouth cleanliness or performance claim.',
                'Only the private PIE one-portion profile is tuned; normal perk, native wave, health and dirt remain authoritative.',
                'Real held RMB anchors the observer during the wave; the review camera does not hide geometry.',
                'Paint canvas origins are allocation coordinates, not individual stain placement positions. Actual wet-cell positions are recorded separately.',
                'Spacing and two-run nearest-centre comparisons describe observed geometry; they do not establish statistical randomness.',
                'Camera projection/LOS screening is geometric evidence, not visual approval or a pixel-occlusion proof.',
                'No package saves, gameplay state/mask edits, network clients, build or broad tests.'])
        for source in ('Tools/Unreal/probe_dirt_scatter_solo.py',
                       'Source/MessControl/Private/MCCoffeeFlood.cpp',
                       'Source/MessControl/Private/MCDayDirector.cpp',
                       'Source/MessControl/Public/MCDirtPlacement.h'):
            target = ROOT/source
            if target.is_file():
                self.report['source_sha256'][source] = hashlib.sha256(target.read_bytes()).hexdigest()

    def write(self):
        # The inherited teardown says passed only for its supplied scope. Make
        # the subset boundary machine-readable as well as explicit in prose.
        if self.report.get('status') == 'passed':
            self.report.update(status='subset_passed', subset_passed=True, passed=None)
        elif self.report.get('status') in ('failed', 'interrupted'):
            self.report['subset_passed'] = False
        self.report['cleanup_passed'] = None
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_scatter.json').write_text(data, encoding='utf-8')

    def sample(self, now):
        super().sample(now)
        for flood in self.actors(u.MCCoffeeFlood):
            self.report['flood_sources'][path(flood)] = dict(
                observed_game_seconds=now, started_at=float(prop(flood, 'started_at')),
                arena_center_cm=vec(prop(flood, 'arena_center')),
                half_size_cm=vec(prop(flood, 'half_size')),
                active=bool(prop(flood, 'active')), wave=int(prop(flood, 'wave')))

    def layout_snapshot(self, label):
        rows, review_points = [], []
        for patch in self.surfaces():
            if self.is_clean(patch):
                continue
            tile = bool(prop(patch, 'paint_tile'))
            half = float(prop(patch, 'liquid_half_size'))
            transform = patch.get_actor_transform()
            row = dict(actor=path(patch), paint_tile=tile, native_origin_cm=vec(patch.get_actor_location()),
                origin_is_stain_placement=not tile, half_size_cm=half,
                seed=int(prop(patch, 'liquid_seed')))
            if tile:
                mask = [int(value) for value in prop(patch, 'wipe_mask')]
                size = math.isqrt(len(mask))
                self.require(size and size*size == len(mask), 'Invalid native mask during layout census')
                wet = [transform.transform_location(u.Vector(
                    ((i % size+.5)/size-.5)*2*half,
                    ((i // size+.5)/size-.5)*2*half, 0)) for i, value in enumerate(mask) if value > 63]
                if not wet:
                    continue
                # Preserve every sampled wet position as evidence. Do not
                # substitute canvas centres for the layout actually painted.
                centroid = [sum(getattr(point, axis) for point in wet)/len(wet) for axis in ('x', 'y', 'z')]
                extrema = []
                for axis in ('x', 'y'):
                    extrema.extend((min(wet, key=lambda p: getattr(p, axis)), max(wet, key=lambda p: getattr(p, axis))))
                row.update(wet_bytes=len(wet), wet_centroid_cm=centroid,
                    actual_wet_positions_cm=[vec(point) for point in wet],
                    actual_wet_xy_bounds=dict(min_x=min(p.x for p in wet), max_x=max(p.x for p in wet),
                        min_y=min(p.y for p in wet), max_y=max(p.y for p in wet)))
                middle = min(wet, key=lambda point: math.dist(vec(point), centroid))
                points = extrema+[middle]
            else:
                points = [transform.transform_location(u.Vector(x*half, y*half, 0))
                    for x, y in ((-1, -1), (-1, 1), (1, 1), (1, -1), (0, 0))]
                row['full_corners_cm'] = [vec(point) for point in points[:4]]
            row_points = []
            for point in points:
                floor = self.floor(point)
                self.require(floor is not None, 'Actual dirt review point lacks native tongue tissue')
                point = floor+u.Vector(0, 0, 12.)
                review_points.append(point)
                row_points.append(vec(point))
            row['actual_tissue_review_points_cm'] = row_points
            rows.append(row)
        self.require(rows and review_points, 'No real native floor dirt for the short layout subset')
        origins = [row['wet_centroid_cm'] if row['paint_tile'] else row['native_origin_cm'] for row in rows]
        for index, row in enumerate(rows):
            others = [math.dist(origins[index][:2], other[:2]) for j, other in enumerate(origins) if j != index]
            row['nearest_observed_centroid_distance_xy_cm'] = min(others) if others else None
        sample = dict(label=label, game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            native_run_seed=int(prop(self.gs, 'run_seed')), native_food_paths=sorted(self.native_food_paths),
            sources=rows, central_work_bounds=self.central_work_bounds(65.)[0],
            review_point_count=len(review_points), centre_spacing_is_not_a_minimum_gap_claim=True)
        self.report['layout_samples'].append(sample)
        return sample, review_points

    def projected_point(self, point, fov=100.):
        rotation = self.camera.get_actor_rotation()
        forward, right, up = (u.MathLibrary.get_forward_vector(rotation),
            u.MathLibrary.get_right_vector(rotation), u.MathLibrary.get_up_vector(rotation))
        delta = point-self.camera.get_actor_location()
        dot = lambda a, b: a.x*b.x+a.y*b.y+a.z*b.z
        depth = dot(delta, forward)
        if depth <= 1.:
            return None
        horizontal = math.tan(math.radians(fov*.5))
        return [.5+.5*dot(delta, right)/(depth*horizontal),
            .5-.5*dot(delta, up)/(depth*horizontal*(720./1280.))]

    def clear_subject_ray(self, eye, point):
        ignore = [self.hero]+self.actors(u.MCFoodActor)
        if valid(self.camera):
            ignore.append(self.camera)
        trace = u.SystemLibrary.line_trace_single(self.world, eye, point,
            u.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignore, u.DrawDebugTrace.NONE, True)
        hit = next((value for value in (list(trace) if isinstance(trace, (tuple, list)) else [trace])
            if isinstance(value, u.HitResult)), None)
        if hit is None:
            return True
        fields = self.statics.call_method('BreakHitResult', args=(hit,))
        return not fields[0] or fields[3] >= math.dist(vec(eye), vec(point))-15.

    def central_overview_camera(self, snapshot):
        sample, points = self.layout_snapshot('before_overview')
        target = u.Vector(*[sum(getattr(point, axis) for point in points)/len(points) for axis in ('x', 'y', 'z')])
        fov = 100.
        candidates = []
        # Fit the real wet extents, including every paint canvas. Both ends and
        # several heights are screened because a fixed high camera can enter
        # the palate or miss a widened foreground footprint.
        for offset in (u.Vector(-2400, 0, 1000), u.Vector(2400, 0, 1000),
                       u.Vector(-2800, -250, 800), u.Vector(2800, 250, 800),
                       u.Vector(-1800, 0, 1500), u.Vector(1800, 0, 1500),
                       u.Vector(-1300, -500, 1800), u.Vector(-1300, 500, 1800),
                       u.Vector(0, 0, 2100)):
            eye = target+offset
            self.frame_camera(eye, target)
            prop(self.camera, 'camera_component').set_editor_property('field_of_view', fov)
            projected = [self.projected_point(point, fov) for point in points]
            framed = sum(value is not None and .06 <= value[0] <= .94 and .08 <= value[1] <= .92 for value in projected)
            clear = sum(self.clear_subject_ray(eye, point) for point in points)
            candidates.append(dict(eye_cm=vec(eye), projected_uv=projected,
                framed_points=framed, clear_rays=clear, total_points=len(points)))
            if framed == len(points) and clear == len(points):
                break
        chosen = max(candidates, key=lambda row: (min(row['framed_points'], row['clear_rays']), row['framed_points'], row['clear_rays']))
        self.frame_camera(u.Vector(*chosen['eye_cm']), target)
        prop(self.camera, 'camera_component').set_editor_property('field_of_view', fov)
        self.report['central_overview_camera'] = dict(method='External genuine review camera; geometry/materials remain visible',
            target_cm=vec(target), field_of_view_degrees=fov, expected_client_aspect_ratio=1280./720.,
            actual_floor_review_points_cm=[vec(point) for point in points], candidates=candidates, chosen=chosen,
            all_review_points_project_inside=chosen['framed_points'] == len(points),
            all_sampled_subject_rays_clear=chosen['clear_rays'] == len(points),
            visual_approval='Pending root review; projection and sampled rays do not prove pixel visibility')
        self.require(chosen['framed_points'] == len(points), 'No candidate frames every actual dirt review extent; inspect camera report')

    def compare_previous_layout(self):
        previous = []
        for candidate in (OUTPUT/'Runs').glob('*/Report.json'):
            if candidate.parent == self.output:
                continue
            try:
                data = json.loads(candidate.read_text(encoding='utf-8'))
                if data.get('subset_passed') and data.get('layout_samples'):
                    previous.append((candidate.stat().st_mtime, candidate, data))
            except (OSError, ValueError):
                continue
        if not previous:
            self.report['layout_comparison'] = dict(previous_run=None, note='First observed sample; no comparison yet')
            return
        _, previous_file, baseline = max(previous, key=lambda item: item[0])
        old, new = baseline['layout_samples'][-1], self.report['layout_samples'][-1]
        positions = lambda sample: [row['wet_centroid_cm'] if row['paint_tile'] else row['native_origin_cm'] for row in sample['sources']]
        before, after = positions(old), positions(new)
        distances = [min(math.dist(point[:2], other[:2]) for other in before) for point in after]
        self.report['layout_comparison'] = dict(previous_run=str(previous_file),
            previous_report_sha256=hashlib.sha256(previous_file.read_bytes()).hexdigest(),
            previous_native_run_seed=old['native_run_seed'], current_native_run_seed=new['native_run_seed'],
            previous_flood_sources=baseline.get('flood_sources'), current_flood_sources=self.report['flood_sources'],
            previous_observed_centres_cm=before, current_observed_centres_cm=after,
            nearest_previous_observed_centre_distances_xy_cm=distances,
            proof_scope='Two independently produced layouts with current code; not a statistical randomness or old/new performance test')

    def begin_native_cut(self, now):
        # Parent calls this only after its actual three-second overview. End
        # before the cut/collection/throw/cleaning half of the larger scenario.
        self.require(self.central_overview_done, 'The native overview subset did not complete')
        self.layout_snapshot('after_overview')
        frames = self.report['clips'].get('08_central_floor_placement', [])
        self.require(self.capture_hz and len(frames) >= 8, 'Short overview has too few genuine game-window frames')
        for entry in frames:
            data = Path(entry['file']).read_bytes()
            self.require(data.startswith(b'\x89PNG\r\n\x1a\n'), 'An actual source PNG is missing')
            self.require(entry['sha256'] == hashlib.sha256(data).hexdigest(), 'Source PNG changed after capture')
        self.report.update(completed_subset=True, native_stages=self.native_stages,
            review_clip='08_central_floor_placement', cleanup_passed=None)
        self.report['checks']['three_second_native_layout_overview_recorded'] = True
        self.compare_previous_layout()
        self.finish()


def main():
    for key in (KEY, 'mc_cleanup_first_relax_solo'):
        old = getattr(builtins, key, None)
        if old is not None and not getattr(old, 'done', True):
            raise RuntimeError('An owned dirt/cleanup probe is already running; it was left intact')
    probe = DirtScatterSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
