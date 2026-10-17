"""Opt-in, root-run stationary coffee material A/B/A GPU comparison.

Run only in idle L_Mouth, with no other CSV capture active. This owns one solo
PIE, lets the real F3 wave deposit paint, then PAUSES its world for an explicitly
synthetic same-camera material comparison. It is not a gameplay/performance
benchmark of AI, deposition, cleaning or multiplayer. No packages are saved.

The baseline is reconstructed from the original tracked trail(mat) recipe;
it is NOT an exact restored historical binary or the rejected film revision2.
Only PIE component material overrides change. Geometry, native mask texture,
WorldSize and Finish are shared. No screenshots/log sampling during timings.
CSV global GPUTime is actual RHI frame time in ms, not reciprocal capture FPS.

Root run: runpy.run_path(..., run_name='__main__'). Optional globals:
MC_COFFEE_PERF_OUTPUT, MC_COFFEE_PERF_SECONDS (default6, bounded3..8).
Cancel: builtins.mc_coffee_film_perf_solo.finish('Stopped by operator').
"""
import builtins
import csv
import datetime
import hashlib
import json
import math
import runpy
import shutil
import statistics
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_COFFEE_PERF_OUTPUT', ROOT/'Saved/CoffeeFilmPerformance')).resolve()
SECONDS = max(3., min(8., float(globals().get('MC_COFFEE_PERF_SECONDS', 6.))))
KEY = 'mc_coffee_film_perf_solo'
CURRENT = '/Game/Cataclysm/Jelly/M_CataclysmPaintTrail'
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_coffee_wave_stains_solo.py'),
    run_name='coffee_perf_support', init_globals={'MC_COFFEE_WAVE_OUTPUT': OUTPUT/'Fixture'})
Parent, prop, valid, path = (support[k] for k in ('CoffeeWaveStainsSolo', 'prop', 'valid', 'path'))


def summarize_csv(filename):
    """Parse engine CSV data only; footer metadata/repeated headers are ignored."""
    rows = list(csv.reader(Path(filename).read_text(encoding='utf-8-sig').splitlines()))
    headers = [(i, row) for i, row in enumerate(rows) if 'GPUTime' in row and 'FrameTime' in row]
    if not headers:
        raise RuntimeError('CSV lacks global GPUTime/FrameTime columns; no GPU inference is possible')
    # CsvProfiler can discover new stats during recording and append columns.
    # Its final header describes all columns; early rows legitimately have fewer
    # fields. Never require every data row to match the initial header width.
    header_index, initial_header = headers[0]
    final_header_index, header = headers[-1]
    if header[:len(initial_header)] != initial_header:
        raise RuntimeError('CSV headers changed existing column order; unsupported schema')
    end_index = final_header_index if len(headers) > 1 else len(rows)
    frame_column = header.index('FrameTime')
    gpu_column = header.index('GPUTime')
    data = []
    for row in rows[header_index+1:end_index]:
        try:
            if (max(frame_column, gpu_column) < len(row) <= len(header)
                    and math.isfinite(float(row[frame_column]))):
                data.append(row)
        except ValueError:
            pass
    metrics = {}
    for name in ('GPUTime', 'FrameTime', 'GameThreadTime', 'RenderThreadTime', 'RHIThreadTime'):
        if name not in header:
            continue
        column = header.index(name)
        values = []
        for row in data:
            try:
                value = float(row[column])
                if math.isfinite(value) and value > 0.:
                    values.append(value)
            except (IndexError, ValueError):
                pass
        # Exclude start/stop command boundary frames; shader warmup is separate.
        if len(values) >= 50:
            values = values[10:-10]
        if values:
            ordered = sorted(values)
            metrics[name] = dict(unit='ms', samples=len(values), median=statistics.median(values),
                mean=statistics.mean(values), p95=ordered[math.ceil(.95*len(ordered))-1],
                minimum=ordered[0], maximum=ordered[-1])
    if metrics.get('GPUTime', {}).get('samples', 0) < 30:
        raise RuntimeError('Insufficient positive GPUTime samples; GPU timing unavailable/invalid')
    return dict(columns=header, metrics=metrics, raw_frame_rows=len(data),
        appended_stat_columns=len(header)-len(initial_header),
        sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest())


class CoffeeFilmPerformance(Parent):
    def __init__(self):
        super().__init__()
        self.capture_hz = 0.
        self.profile_ready = False
        self.csv_owned = False
        self.world_paused_by_probe = False
        self.material_slots = []
        self.csv_state = None
        self.original_smooth = prop(self.engine, 'bSmoothFrameRate')
        self.original_cvars = {name: u.SystemLibrary.get_console_variable_float_value(name)
            for name in ('t.MaxFPS', 'r.VSync', 'r.VSyncEditor', 'r.GPUCsvStatsEnabled',
                         'csv.CompressionMode', 'Slate.bAllowThrottling')}
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        self.profile_id = stamp+'_'+str(time.time_ns()%1000000)
        self.report.update(mode='coffee_film_performance', samples=[], clips={}, frames=[],
            scope='Synthetic paused-world, same-camera A/B/A material comparison in one owned solo PIE',
            baseline='Original trail(mat) recipe reconstructed transiently; not an exact old package or film revision2',
            timing_seconds_each=SECONDS, results=[],
            hardware=globals().get('MC_COFFEE_PERF_GPU_INFO', 'Not supplied; raw CSV metadata retained'),
            limitations=[
                'Owned PIE world is paused after actual native wave deposition. CPU times do not describe live gameplay.',
                'A is reconstructed original pre-film trail recipe; B is the currently saved shared coffee-film material.',
                'Swaps affect only captured PIE paint components. Same native geometry, wipe textures and camera are retained.',
                'CSV global GPUTime measures the whole Editor RHI frame, not an isolated shader pass or packaged build.',
                'Only one arena close-up is measured; screen coverage, GPU, resolution and scene complexity affect cost.',
                'External driver limits, GPU clock/thermal changes and background applications can affect results.',
                'No screenshots or report/mask sampling occur inside timed windows; A/B/A drift is reported.',
                'No package saves, build/cook/package, multiplayer tests or persistent configuration writes.'])

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_performance.json').write_text(data, encoding='utf-8')

    def begin_clip(self, *_args):
        self.clip = None

    def screenshot(self, *_args, **_kwargs):
        pass

    def start(self):
        recipe = ROOT/'Tools/Unreal/author_coffee_slime_surfaces_v2.py'
        self.report['baseline_recipe_sha256'] = hashlib.sha256(recipe.read_bytes()).hexdigest()
        author = runpy.run_path(str(recipe), run_name='coffee_perf_original_recipe')
        # PythonScriptPlugin NewObject defaults its outer to GetTransientPackage.
        self.baseline = u.new_object(u.Material, name='MCPerfOriginalCoffee_'+self.profile_id)
        self.require(self.baseline.get_path_name().startswith('/Engine/Transient.'),
                     'Baseline material must be transient')
        author['trail'](self.baseline)
        errors = list(u.MaterialEditingLibrary.recompile_material(self.baseline))
        self.require(not errors, 'Transient original recipe compilation failed: '+str(errors))
        self.report['baseline_material'] = path(self.baseline)
        self.report['current_material'] = CURRENT
        package = ROOT/'Content/Cataclysm/Jelly/M_CataclysmPaintTrail.uasset'
        self.report['current_package_sha256'] = hashlib.sha256(package.read_bytes()).hexdigest()
        self.report['compiled_material_statistics'] = {}
        for label, material in (('reconstructed_original', self.baseline),
                                ('current', u.load_asset(CURRENT))):
            try:
                stats = u.MaterialEditingLibrary.get_statistics(material)
                self.report['compiled_material_statistics'][label] = {name: int(prop(stats, name))
                    for name in ('num_vertex_shader_instructions', 'num_pixel_shader_instructions',
                                 'num_samplers', 'num_vertex_texture_samples', 'num_pixel_texture_samples')}
            except Exception as caught:
                self.report['compiled_material_statistics'][label] = dict(unavailable=str(caught))
        super().start()
        # The existing video parent requests fixed60; undo it before async PIE
        # begins. Base.finish restores its saved fixed-rate/PIE settings.
        self.engine.set_editor_property('bUseFixedFrameRate', False)
        self.engine.set_editor_property('bSmoothFrameRate', False)
        self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps({'clientFixedFPS': []})),
                     'Could not remove PIE client fixed FPS')
        for name, value in (('t.MaxFPS', 0), ('r.VSync', 0), ('r.VSyncEditor', 0),
                            ('r.GPUCsvStatsEnabled', 1), ('csv.CompressionMode', 0),
                            ('Slate.bAllowThrottling', 0)):
            self.console(name+' '+str(value))
        self.report['measurement_cvars'] = {name: u.SystemLibrary.get_console_variable_float_value(name)
            for name in self.original_cvars}
        self.report['measurement_cvars']['t.IdleWhenNotForeground'] = \
            u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        self.require(not prop(self.engine, 'bUseFixedFrameRate')
                     and not prop(self.engine, 'bSmoothFrameRate')
                     and self.report['measurement_cvars']['t.MaxFPS'] == 0
                     and self.report['measurement_cvars']['r.VSync'] == 0,
                     'Frame caps were not removed')
        self.write()

    def sample(self, now):
        if not self.profile_ready:
            super().sample(now)

    def update_jellies(self, now, elapsed):
        if self.phase == 'wait_branch':
            self.require(elapsed < 25., 'Natural wave exceeded focused setup')
            if support['enum_name'](prop(self.event, 'phase')) != 'JELLY_FIGHT':
                return
            self.prepare_wave_brush()
            # Allow the actual PlayerCameraManager to catch the new close-up
            # before pausing. Paused camera-manager behavior is not assumed.
            self.stage('perf_camera_settle')
        elif self.phase == 'perf_camera_settle' and elapsed >= .35:
            self.prepare_profile()

    def prepare_profile(self):
        self.primary_input(False)
        current = u.load_asset(CURRENT)
        self.require(current is not None, 'Saved current film material unavailable')
        for actor in self.actors(u.MCMouthSurface):
            if not prop(actor, 'paint_tile'):
                continue
            mesh = prop(actor, 'liquid')
            material = mesh.get_material(0)
            if not isinstance(material, u.MaterialInstanceDynamic):
                continue
            self.require(prop(material, 'parent') == current,
                         'Paint fixture uses an unexpected parent; do not silently compare unrelated material')
            baseline_mid = u.MaterialLibrary.create_dynamic_material_instance(self.world, self.baseline)
            texture = material.get_texture_parameter_value('WipeMask')
            self.require(texture is not None, 'Native wipe texture unavailable')
            baseline_mid.set_texture_parameter_value('WipeMask', texture)
            for name in ('WorldSize', 'Finish'):
                baseline_mid.set_scalar_parameter_value(name, material.get_scalar_parameter_value(name))
            self.material_slots.append((actor, mesh, material, baseline_mid))
        self.require(self.material_slots, 'No actual native paint materials for comparison')
        self.mask_signatures = self.mask_snapshot()
        self.report['fixture'] = dict(camera=self.camera_evidence(),
            visible_paint_components=[dict(actor=path(a), component=path(m),
                visible=m.is_visible(), native_mid=path(b), baseline_mid=path(o),
                component_transform=str(m.get_world_transform()),
                native_mask_texture=path(b.get_texture_parameter_value('WipeMask')))
                for a, m, b, o in self.material_slots], masks=self.mask_signatures,
            viewport='Standalone floating game window requested1280x720; entire Editor GPU frame measured',
            actual_player_viewport_size=list(self.pc.get_viewport_size()),
            horizontal_fov_degrees=float(prop(prop(self.camera, 'camera_component'), 'field_of_view')),
            screen_percentage=u.SystemLibrary.get_console_variable_float_value('r.ScreenPercentage'))
        self.require(not u.GameplayStatics.is_game_paused(self.world), 'Owned fixture unexpectedly paused')
        self.require(u.GameplayStatics.set_game_paused(self.world, True), 'Could not pause owned fixture')
        self.world_paused_by_probe = True
        self.profile_ready = True
        self.sequence = [('original_before', False), ('current', True), ('original_after', False)]
        self.sequence_index = -1
        self.advance_profile()

    def mask_snapshot(self):
        return {path(actor): hashlib.sha256(bytes(int(v) for v in prop(actor, 'wipe_mask'))).hexdigest()
                for actor, _mesh, _current, _old in self.material_slots}

    def advance_profile(self):
        self.sequence_index += 1
        if self.sequence_index == len(self.sequence):
            self.complete_profile()
            return
        name, current = self.sequence[self.sequence_index]
        for _actor, mesh, native, baseline in self.material_slots:
            mesh.set_material(0, native if current else baseline)
        self.profile_phase = name
        self.csv_name = 'MC_CoffeeFilm_'+self.profile_id+'_'+name
        self.csv_directory = ROOT/'Saved/Profiling/CSV'
        self.csv_path = None
        self.csv_state = 'warmup'
        self.state_wall = time.monotonic()
        self.report['last_profile_phase'] = name
        self.write()

    def profile_tick(self):
        elapsed = time.monotonic()-self.state_wall
        self.require(time.monotonic()-self.started < 240., 'Bounded material profile exceeded wall timeout')
        self.require(self.world in u.EditorLevelLibrary.get_pie_worlds(True), 'Owned profile PIE disappeared')
        if self.csv_state == 'warmup' and elapsed >= 3.:
            # Do not alter CsvProfile's process-global STARTFILE override: its
            # previous value is not exposed. Detect the one written file instead.
            self.csv_directory.mkdir(parents=True, exist_ok=True)
            self.csv_before = {str(p): (p.stat().st_size, p.stat().st_mtime_ns)
                               for p in self.csv_directory.glob('*.csv')}
            self.console('CsvProfile START')
            self.csv_owned = True
            self.csv_state = 'timing'
            self.state_wall = time.monotonic()
        elif self.csv_state == 'timing' and elapsed >= SECONDS:
            self.console('CsvProfile STOP')
            self.csv_owned = False
            self.csv_state = 'flush'
            self.state_wall = time.monotonic()
            self.last_file_size = None
            self.file_stable_wall = self.state_wall
        elif self.csv_state == 'flush':
            self.require(elapsed < 25., 'CSV did not finish writing')
            if self.csv_path is None:
                changed = [p for p in self.csv_directory.glob('*.csv')
                           if self.csv_before.get(str(p)) != (p.stat().st_size, p.stat().st_mtime_ns)]
                if not changed:
                    return
                self.require(len(changed) == 1, 'More than one CSV changed; capture ownership ambiguous')
                self.csv_path = changed[0]
            size = self.csv_path.stat().st_size
            if size != self.last_file_size:
                self.last_file_size = size
                self.file_stable_wall = time.monotonic()
                return
            if time.monotonic()-self.file_stable_wall < .75:
                return
            summary = summarize_csv(self.csv_path)
            self.require(self.mask_snapshot() == self.mask_signatures, 'Native coverage changed across comparison')
            camera = self.camera_evidence()
            self.require(camera and camera['location_error_cm'] < 1.
                         and camera['rotation_error_degrees'] < .1,
                         'Actual player view drifted from stationary comparison camera')
            raw = self.output/'Raw'/(self.csv_name+'.csv')
            raw.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.csv_path, raw)
            self.report['results'].append(dict(phase=self.profile_phase, csv=str(raw),
                                               camera=camera, **summary))
            self.advance_profile()

    def complete_profile(self):
        a, b, c = self.report['results']
        old_a, new, old_b = (x['metrics']['GPUTime']['median'] for x in (a, b, c))
        baseline = (old_a+old_b)*.5
        self.report['comparison'] = dict(unit='ms', original_before_median=old_a,
            current_median=new, original_after_median=old_b,
            original_repeat_drift_ms=abs(old_b-old_a),
            original_repeat_drift_percent=abs(old_b-old_a)/baseline*100.,
            current_minus_original_ms=new-baseline,
            current_minus_original_percent=(new-baseline)/baseline*100.,
            interpretation='Whole Editor RHI frame in this paused close-up; small differences within A/A drift are inconclusive')
        self.report['checks'].update(positive_actual_gpu_csv=True,
            same_native_masks=True, stationary_actual_player_camera=True,
            no_video_sampling_during_measurement=True)
        self.finish()

    def tick(self, delta):
        if self.done:
            return
        if not self.profile_ready:
            super().tick(delta)
            return
        try:
            self.profile_tick()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        errors = []
        if self.csv_owned:
            try:
                self.console('CsvProfile STOP')
                self.csv_owned = False
            except Exception:
                errors.append(traceback.format_exc())
        for _actor, mesh, material, _old in self.material_slots:
            try:
                if valid(mesh):
                    mesh.set_material(0, material)
            except Exception:
                errors.append(traceback.format_exc())
        if self.world_paused_by_probe:
            try:
                if self.world in u.EditorLevelLibrary.get_pie_worlds(True):
                    u.GameplayStatics.set_game_paused(self.world, False)
            except Exception:
                errors.append(traceback.format_exc())
        for name, value in self.original_cvars.items():
            try:
                self.console(name+' '+str(value))
            except Exception:
                errors.append(traceback.format_exc())
        try:
            self.engine.set_editor_property('bSmoothFrameRate', self.original_smooth)
        except Exception:
            errors.append(traceback.format_exc())
        self.report['performance_restore_errors'] = errors
        super().finish(error or ('Performance fixture restoration failed' if errors else None))
        # Setter calls completing is not proof that console priorities/settings
        # accepted the old values. Read them independently after Base restored
        # its frame, foreground and PIE settings. This does not write config.
        expected = dict(
            console_variables=dict(self.original_cvars,
                **{'t.IdleWhenNotForeground': self.old_idle}),
            engine=dict(use_fixed_frame_rate=bool(self.old_engine[0]),
                fixed_frame_rate=float(self.old_engine[1]), smooth_frame_rate=bool(self.original_smooth)),
            editor=dict(throttle_cpu_when_not_foreground=bool(self.old_perf)),
            play=self.old_play)
        observed, read_errors = {}, []
        try:
            observed = dict(
                console_variables={name: u.SystemLibrary.get_console_variable_float_value(name)
                    for name in expected['console_variables']},
                engine=dict(use_fixed_frame_rate=bool(prop(self.engine, 'bUseFixedFrameRate')),
                    fixed_frame_rate=float(prop(self.engine, 'FixedFrameRate')),
                    smooth_frame_rate=bool(prop(self.engine, 'bSmoothFrameRate'))),
                editor=dict(throttle_cpu_when_not_foreground=bool(prop(self.perf, 'bThrottleCPUWhenNotForeground'))),
                play=json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(self.old_play))))
        except Exception:
            read_errors.append(traceback.format_exc())
        matches = {}
        for group, values in expected.items():
            for name, value in values.items():
                actual = observed.get(group, {}).get(name)
                matches[group+'.'+name] = (
                    math.isclose(actual, value, rel_tol=1.e-6, abs_tol=1.e-6)
                    if isinstance(value, (int, float)) and not isinstance(value, bool)
                    and isinstance(actual, (int, float)) else actual == value)
        verified = not read_errors and all(matches.values())
        self.report['post_restore_verification'] = dict(expected=expected, observed=observed,
            matches=matches, read_errors=read_errors, passed=verified)
        self.report['checks']['settings_independently_restored'] = verified
        if not verified:
            self.report.update(status='failed', passed=False, restored_settings=False,
                error=self.report.get('error') or 'Independent post-restore settings readback failed')
        self.write()
        u.log('MC_COFFEE_PERF_SETTINGS_READBACK '+('PASS' if verified else 'FAIL'))


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve existing material performance probe')
    probe = CoffeeFilmPerformance()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
