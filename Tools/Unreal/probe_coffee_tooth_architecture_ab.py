"""ROOT-RUN ONLY: isolated future tooth coffee render prototype, not migration.

MODE='preview': 8 genuine short clips (A/B before, partial, native Chipped,
and matched oblique camera poses). MODE='performance': separate static A/B/A,
3s warmup +6s real CSV GPUTime each; no screenshots/readbacks during CSV.
All masks are independently generated SYNTHETIC 16^3 data in transient textures.
Neither the native GrimeMask nor native coffee brush/contact semantics changes.
One actual runtime tooth is used. Native ReceiveArenaHit(20) is a disclosed
damage-only fixture; its owned PIE is discarded afterwards, not saved.
Root must run the fixture/material preflight first and select the PIE window.
Cancel: builtins.mc_coffee_tooth_architecture_ab.finish('Stopped by operator').
Import under a non-__main__ name is inert; no PIE starts on support import.
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import time
import traceback
import uuid
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
MODE = str(globals().get('MODE', 'preview')).lower()
if MODE not in ('preview', 'performance'):
    raise RuntimeError('MODE must be preview or performance')
OUTPUT = ROOT/'Saved/CoffeeToothArchitectureAB'/('Preview' if MODE == 'preview' else 'Performance')
KEY = 'mc_coffee_tooth_architecture_ab'
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_cataclysm_solo.py'), run_name='tooth_ab_base_support')
Base, CameraSupport, prop, valid, path, vec = (support[k] for k in
    ('Base', 'CataclysmSolo', 'prop', 'valid', 'path', 'vec'))
wave = runpy.run_path(str(ROOT/'Tools/Unreal/probe_coffee_wave_stains_solo.py'), run_name='tooth_ab_camera_support')
performance = runpy.run_path(str(ROOT/'Tools/Unreal/profile_coffee_film_solo.py'),
    run_name='tooth_ab_csv_support', init_globals={'MC_COFFEE_PERF_SECONDS': 6.})
materials = runpy.run_path(str(ROOT/'Tools/Unreal/coffee_surface_blend_material.py'), run_name='tooth_ab_material_support')
fixture_support = runpy.run_path(str(ROOT/'Tools/Unreal/coffee_tooth_ab_support.py'), run_name='tooth_ab_fixture_support')
required_clips = tuple('tooth_'+variant+'_'+state for state in ('before', 'partial', 'damaged', 'orbit')
                      for variant in ('a', 'b'))


def disk_packages():
    names = {'Content/Maps/L_Mouth.umap', 'Content/Art/Materials/M_ArenaTooth.uasset',
             'Content/Gameplay/Care/M_ArenaToothRelief.uasset', 'Content/Data/DA_ArenaTooth.uasset',
             'Content/Gameplay/Arena/Damage/DA_ToothDamage.uasset'}
    for folder in ('Content/Gameplay/Arena/V10', 'Content/Gameplay/Arena/Damage', 'Content/Gameplay/Care/Textures'):
        names.update(str(p.relative_to(ROOT)).replace('\\', '/') for p in (ROOT/folder).rglob('*.uasset'))
    return {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sorted(names) if (ROOT/name).is_file()}


class CoffeeToothArchitectureAB(Base):
    camera_evidence = CameraSupport.camera_evidence
    # This proven state machine does no mask/camera/report reads in its timed
    # interval. It reads them only after the stopped CSV has fully flushed.
    profile_tick = performance['CoffeeFilmPerformance'].profile_tick

    def __init__(self):
        super().__init__()
        self.output = OUTPUT/'Runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
        self.profile_id = self.output.name
        self.fixture = None
        self.tooth = None
        self.ready = False
        self.csv_owned = False
        self.profile_ready = False
        self.pause_tick_restore = self.auto_camera_restore = None
        self.scheduler = None
        self.scheduler_tick = None
        self.world_paused_by_probe = False
        self.original_smooth = bool(prop(self.engine, 'bSmoothFrameRate'))
        self.original_cvars = {name: u.SystemLibrary.get_console_variable_float_value(name)
            for name in ('t.MaxFPS', 'r.VSync', 'r.VSyncEditor', 'r.GPUCsvStatsEnabled',
                         'csv.CompressionMode', 'Slate.bAllowThrottling')}
        self.capture_hz = 8. if MODE == 'preview' else 0.
        self.report.update(mode='coffee_tooth_architecture_ab', lab_mode=MODE,
            run_directory=str(self.output), time_clock='wall_seconds', frames=[], screenshots=[],
            clips={}, samples=[], results=[], checks={}, required_clips=list(required_clips) if MODE == 'preview' else [],
            clip_labels={name: name.replace('_', ' ') for name in required_clips},
            fixture_tuning={}, purpose='Compare synthetic future coffee thin coat A versus material blend B on actual runtime tooth',
            scope='Synthetic future renderer prototype; NOT native GrimeRelief conversion or native brush proof',
            timing_seconds_each=6., limitations=[
                'A full current enamel mesh copy offset0.6cm; B the original tooth enamel graph with coffee inputs blended.',
                'Both use one explicit synthetic ellipsoid, original-Intact local volume and independent16^3/64^2 before/partial atlas data.',
                'Partial erasure reproduces native Stroke math offline. No native brush contact, performance, replication or new tooth gameplay is claimed.',
                'Native GrimeRelief on this tooth is hidden and restored. Its native samples, masks and tick remain intact; no CPU-geometry savings claim.',
                'ReceiveArenaHit20 is used only for a disclosed Chipped render fixture; native coffee arrays are unchanged. Its world is discarded after capture.',
                'Review cameras use real PlayerCameraManager; HUD remains. Twelve matched oblique camera poses are discrete shots, not simulated gameplay movement.',
                'GPUTime is the whole static Editor RHI frame, not an isolated draw pass, live-game FPS or multiplayer result.',
                'No package saves, asset edits, build, cook, network tests or native mask assignments.'])
        self.report['dirty_packages_before'] = materials['dirty_packages']()
        self.report['protected_packages_before'] = disk_packages()
        sources = ('Tools/Unreal/probe_coffee_tooth_architecture_ab.py', 'Tools/Unreal/coffee_tooth_ab_support.py',
                   'Tools/Unreal/coffee_surface_blend_material.py', 'Tools/Unreal/check_approved_environment_solo.py',
                   'Tools/Unreal/probe_cataclysm_solo.py', 'Tools/Unreal/probe_coffee_wave_stains_solo.py',
                   'Tools/Unreal/profile_coffee_film_solo.py', 'Source/MessControl/Public/MCSurfaceWipe.h',
                   'Source/MessControl/Private/MCArenaTooth.cpp', 'Source/MessControl/Private/MCArenaToothGrime.cpp')
        self.report['source_sha256'] = {s: hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources}

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/('Report_'+MODE+'.json')).write_text(data, encoding='utf-8')

    def start(self):
        self.require(all(k in materials for k in ('build_tooth_pair', 'configure_tooth_mid')),
                     'Complete root material/fixture preflight before running')
        super().start()
        if MODE == 'performance':
            self.engine.set_editor_property('bUseFixedFrameRate', False)
            self.engine.set_editor_property('bSmoothFrameRate', False)
            self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps({'clientFixedFPS': []})),
                         'Could not remove PIE frame cap')
            for name, value in (('t.MaxFPS', 0), ('r.VSync', 0), ('r.VSyncEditor', 0),
                                ('r.GPUCsvStatsEnabled', 1), ('csv.CompressionMode', 0), ('Slate.bAllowThrottling', 0)):
                self.console(name+' '+str(value))
            self.report['measurement_cvars'] = {n: u.SystemLibrary.get_console_variable_float_value(n) for n in self.original_cvars}
            self.require(not prop(self.engine, 'bUseFixedFrameRate') and not prop(self.engine, 'bSmoothFrameRate')
                         and self.report['measurement_cvars']['t.MaxFPS'] == 0 and self.report['measurement_cvars']['r.VSync'] == 0,
                         'Performance view must be uncapped')
        self.write()

    def mask_snapshot(self):
        output = {}
        for actor in self.actors(u.MCArenaTooth):
            state = prop(prop(actor, 'status'), 'state')
            mask = bytes(prop(actor, 'grime_mask'))
            output[path(actor)] = dict(mask_sha256=hashlib.sha256(mask).hexdigest(), count=len(mask),
                coffee_left=int(prop(state, 'coffee_left')), coffee_total=int(prop(state, 'coffee_total')))
        return output

    def prepare_fixture(self):
        self.scheduler = u.GameplayStatics.get_game_mode(self.world)
        self.scheduler_tick = bool(self.scheduler.is_actor_tick_enabled())
        self.scheduler.set_actor_tick_enabled(False)
        teeth = [t for t in self.actors(u.MCArenaTooth) if t.is_available()
                 and 'INTACT' in str(t.damage_stage()).upper()]
        teeth.sort(key=lambda t: (int(prop(prop(t, 'state'), 'tooth_id')) not in (3, 4),
                                  abs(int(prop(prop(t, 'state'), 'tooth_id'))-3)))
        self.require(teeth, 'No actual Intact runtime tooth')
        self.tooth = teeth[0]
        self.primary_input(False)
        self.mask_signatures = self.mask_snapshot()
        self.auto_camera_restore = bool(prop(self.pc, 'bAutoManageActiveCameraTarget'))
        self.original_view_target = self.pc.get_view_target()
        self.pause_tick_restore = bool(prop(self.pc, 'bShouldPerformFullTickWhenPaused'))
        self.pc.set_editor_property('bAutoManageActiveCameraTarget', False)
        self.pc.set_editor_property('bShouldPerformFullTickWhenPaused', True)
        self.require(u.GameplayStatics.set_game_paused(self.world, True), 'Could not pause actual solo fixture')
        self.world_paused_by_probe = True
        self.fixture = fixture_support['build_fixture'](self.tooth, self.output/'SyntheticFixture',
            materials['build_tooth_pair'], materials['configure_tooth_mid'])
        self.report['synthetic_fixture'] = self.fixture.report
        location = self.tooth.get_actor_location()
        length = math.hypot(location.x, location.y)
        self.require(length > .001, 'Cannot derive the inward direction of a center-of-arena tooth')
        self.inward = u.Vector(-location.x/length, -location.y/length, 0.)
        origin, extent, _radius = u.SystemLibrary.get_component_bounds(prop(self.tooth, 'visual'))
        # Whole-crown bounds extend far below the tongue. Frame the actual
        # synthetic coat on the exposed crown, rather than the underground AABB.
        focus = u.MathLibrary.transform_location(prop(self.tooth, 'visual').get_world_transform(),
                                                u.Vector(*self.fixture.domain['center']))
        right = u.Vector(-self.inward.y, self.inward.x, 0.)
        eye = focus+self.inward*1050.+u.Vector(0., 0., 240.)
        target = focus-right*105.+u.Vector(0., 0., 30.)
        self.fit = dict(anchor_cm=vec(focus), eye_cm=vec(eye), look_target_cm=vec(target),
                        horizontal_fov_degrees=55., scope='Close review of actual exposed synthetic tooth coat; not gameplay distance')
        self.report['tooth'] = dict(actor=path(self.tooth), tooth_id=int(prop(prop(self.tooth, 'state'), 'tooth_id')),
            original_mesh=path(self.fixture.original_mesh), visual_bounds_min=vec(origin-extent), visual_bounds_max=vec(origin+extent),
            original_native_health=float(prop(prop(self.tooth, 'state'), 'health')), camera_fit=self.fit)
        self.apply_camera()
        self.ready = self.profile_ready = True
        if MODE == 'performance':
            self.sequence = [('separate_A_before', 'A'), ('surface_blend_B', 'B'), ('separate_A_after', 'A')]
            self.sequence_index = -1
            self.advance_profile()
        else:
            self.steps = [(state, architecture) for state in ('before', 'partial', 'damaged', 'orbit') for architecture in ('A', 'B')]
            self.step_index = -1
            self.next_preview()

    def apply_camera(self, angle=0.):
        anchor = u.Vector(*self.fit['anchor_cm'])
        eye = u.Vector(*self.fit['eye_cm'])
        target = u.Vector(*self.fit['look_target_cm'])
        if angle:
            def rotate(v):
                return u.Vector(v.x*math.cos(angle)-v.y*math.sin(angle), v.x*math.sin(angle)+v.y*math.cos(angle), v.z)
            eye, target = anchor+rotate(eye-anchor), anchor+rotate(target-anchor)
        self.frame_camera(eye, target)
        component = prop(self.camera, 'camera_component')
        component.set_editor_property('field_of_view', self.fit['horizontal_fov_degrees'])
        component.set_editor_property('aspect_ratio', 16./9.)
        component.set_editor_property('constrain_aspect_ratio', True)
        self.pc.set_view_target_with_blend(self.camera, 0.)

    def set_variant(self, architecture, state):
        self.require(self.mask_snapshot() == self.mask_signatures, 'Native tooth grime changed in a synthetic renderer lab')
        self.fixture.set_state('before' if state == 'before' else 'partial')
        readback = self.fixture.show_mode(architecture)
        self.active_candidate = dict(architecture=architecture, state=state,
            synthetic_mask_sha256=readback['shared_mask_sha256'], current_mesh=readback['current_mesh'],
            native_game_time=float(u.GameplayStatics.get_time_seconds(self.world)), synthetic=True, native_brush_claim=False)
        self.report.setdefault('architecture_readbacks', []).append(readback)

    def next_preview(self):
        self.step_index += 1
        if self.step_index == len(self.steps):
            self.clip = None
            self.flush_wall = time.monotonic()
            return
        state, architecture = self.steps[self.step_index]
        if state == 'damaged' and not hasattr(self, 'damage_applied'):
            self.clip = None
            self.damage_from = float(u.GameplayStatics.get_time_seconds(self.world))
            before = dict(health=float(prop(prop(self.tooth, 'state'), 'health')), mesh=path(prop(self.tooth, 'visual').get_editor_property('static_mesh')))
            self.require(self.tooth.receive_arena_hit(20., -self.inward), 'Native damage fixture was rejected')
            self.damage_applied = before
            self.damage_wait = True
            self.require(u.GameplayStatics.set_game_paused(self.world, False), 'Could not advance native damage visual')
            return
        self.begin_preview_step()

    def begin_preview_step(self):
        state, architecture = self.steps[self.step_index]
        self.set_variant(architecture, state)
        self.apply_camera()
        self.clip = 'tooth_'+architecture.lower()+'_'+state
        self.report['clips'][self.clip] = []
        self.preview_wall = time.monotonic()
        self.preview_frame_at = self.preview_wall+.35
        self.orbit_index = 0
        self.report.setdefault('preview_stages', []).append(dict(self.active_candidate, clip=self.clip, settle_seconds=.35,
            camera_fit=self.fit, expected_same_camera_as_other_variant=True))
        self.write()

    def screenshot(self, name, sequence=False):
        if MODE != 'preview' or not self.ready:
            return
        camera = self.camera_evidence()
        self.require(camera and self.pc.get_view_target() == self.camera
            and camera['location_error_cm'] < 1. and camera['rotation_error_degrees'] < .1,
            'Actual PlayerCameraManager differs from the requested tooth review view')
        target = self.output/'frames'/name
        target.parent.mkdir(parents=True, exist_ok=True)
        self.console('Shot SHOWUI filename="'+str(target).replace('\\', '/')+'" -nosuffix')
        entry = dict(file=str(target), phase='synthetic_future_tooth_architecture_ab',
            time=time.monotonic()-self.started, time_clock='wall_seconds', native_game_time=float(u.GameplayStatics.get_time_seconds(self.world)),
            candidate=dict(self.active_candidate), camera_evidence=camera, clip_window=self.clip,
            viewport_size=list(self.pc.get_viewport_size()))
        self.report['frames'].append(entry)
        self.report['clips'][self.clip].append(entry)

    def verify_frames(self):
        for frame in self.report['frames']:
            data = Path(frame['file']).read_bytes()
            self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Missing genuine engine PNG')
            width, height = int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')
            self.require((width, height) in ((1280, 720), (1286, 760)), 'Wrong viewport captured; select the floating PIE window')
            frame.update(sha256=hashlib.sha256(data).hexdigest(), width=width, height=height)

    def preview_tick(self):
        now = time.monotonic()
        if getattr(self, 'damage_wait', False):
            if float(u.GameplayStatics.get_time_seconds(self.world))-self.damage_from < .25:
                return
            self.require(u.GameplayStatics.set_game_paused(self.world, True), 'Could not pause native Chipped pose')
            self.require('CHIPPED' in str(self.tooth.damage_stage()).upper(), 'Native20 damage did not produce Chipped stage')
            self.require(prop(self.tooth, 'visual').get_editor_property('static_mesh') != self.fixture.original_mesh, 'Damage visual did not change to its authored variant')
            self.fixture.rebuild_current_mesh()
            self.report['damage_fixture'] = dict(before=self.damage_applied,
                after=dict(health=float(prop(prop(self.tooth, 'state'), 'health')), mesh=path(prop(self.tooth, 'visual').get_editor_property('static_mesh'))),
                native_api='ReceiveArenaHit(20)', advanced_native_seconds=float(u.GameplayStatics.get_time_seconds(self.world))-self.damage_from,
                original_domain_and_masks_retained=True, restoration='Owned PIE is discarded; native health is not manually overwritten')
            self.damage_wait = False
            self.begin_preview_step()
            return
        if hasattr(self, 'flush_wall'):
            if now-self.flush_wall < .8:
                return
            self.verify_frames()
            self.require(all(len(self.report['clips'].get(c, [])) >= 8 for c in required_clips), 'A required clip lacks8 genuine frames')
            self.require(self.mask_snapshot() == self.mask_signatures, 'Native mask state changed')
            self.report['checks'].update(all_eight_synthetic_clips_captured=True, native_tooth_masks_unchanged=True,
                synthetic_masks_shared_both_architectures=True, native_chipped_visual_observed=True)
            self.finish()
            return
        state, _architecture = self.steps[self.step_index]
        if len(self.report['frames']) >= 2 and not hasattr(self, 'dimensions_checked'):
            # Fail early for editor-viewport screenshots rather than recording
            # the remainder as apparently valid camera telemetry.
            first = self.report['frames'][0]['file']
            if Path(first).is_file():
                data = Path(first).read_bytes()
                self.require((int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')) in ((1280,720),(1286,760)),
                             'Shot captured a non-PIE viewport')
                self.dimensions_checked = True
        if state == 'orbit':
            angle = math.radians(-7.+14.*self.orbit_index/11.)
            self.apply_camera(angle)
            camera = self.camera_evidence()
            if now >= self.preview_frame_at and camera and camera['location_error_cm'] < 1. and camera['rotation_error_degrees'] < .1:
                self.active_candidate.update(orbit_index=self.orbit_index, orbit_angle_radians=angle)
                self.screenshot('%05d.png' % len(self.report['frames']), True)
                self.orbit_index += 1
                self.preview_frame_at = now+.28
                if self.orbit_index == 12:
                    self.next_preview()
        else:
            camera = self.camera_evidence()
            if now >= self.preview_frame_at and camera and camera['location_error_cm'] < 1. and camera['rotation_error_degrees'] < .1:
                self.screenshot('%05d.png' % len(self.report['frames']), True)
                self.preview_frame_at = now+1./self.capture_hz
            if now-self.preview_wall >= 2.4:
                self.require(len(self.report['clips'][self.clip]) >= 8, 'Not enough real static tooth frames')
                self.next_preview()

    def advance_profile(self):
        self.sequence_index += 1
        if self.sequence_index == len(self.sequence):
            return self.complete_profile()
        self.profile_phase, architecture = self.sequence[self.sequence_index]
        self.set_variant(architecture, 'partial')
        self.apply_camera()
        self.csv_name = 'MC_CoffeeToothArchitecture_'+self.profile_id+'_'+self.profile_phase
        self.csv_directory = ROOT/'Saved/Profiling/CSV'
        self.csv_path = None
        self.csv_state = 'warmup'
        self.state_wall = time.monotonic()+(12. if self.sequence_index == 0 else 0.)
        self.report['initial_extra_preheat_seconds'] = 12.
        self.report['last_profile_phase'] = self.profile_phase
        self.write()

    def complete_profile(self):
        values = [r['metrics']['GPUTime']['median'] for r in self.report['results']]
        self.require(len(values) == 3, 'Need three actual positive GPU CSV windows')
        a, b, c = values
        baseline = (a+c)*.5
        self.report['comparison'] = dict(unit='ms', separate_A_before=a, surface_blend_B=b, separate_A_after=c,
            separate_repeat_drift_ms=abs(c-a), blend_minus_bracketed_separate_ms=b-baseline,
            separate_repeat_drift_percent=abs(c-a)/baseline*100., blend_minus_bracketed_separate_percent=(b-baseline)/baseline*100.,
            interpretation='Whole static Editor RHI frame only. Differences within repeat-A drift are inconclusive. Native grime geometry/tick retained in both.')
        self.report['checks'].update(positive_actual_gpu_csv=True, no_screenshots_during_measurement=not self.report['frames'],
            native_tooth_masks_unchanged=self.mask_snapshot() == self.mask_signatures)
        self.finish()

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 240., 'Bounded tooth renderer lab timed out')
            if self.world is None:
                worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one actual standalone authority world')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not self.pc or not self.pc.get_controlled_pawn():
                    self.world = None
                    return
                self.hero = self.pc.get_controlled_pawn()
                self.require(isinstance(self.hero, u.MCToothCharacter), 'Expected actual runtime tooth character')
                self.stage('first_perk')
                return
            self.require(self.owned_pie_is_active(), 'Owned PIE ended or was replaced externally')
            if not self.ready:
                now = float(u.GameplayStatics.get_time_seconds(self.world))
                if self.phase == 'first_perk':
                    self.require(time.monotonic()-self.started < 35., 'Normal first-perk setup did not resolve')
                    # The normal reward menu can pause game time before1s.
                    # Its actual open state is the readiness gate.
                    if self.pc.is_reward_menu_open():
                        self.report['checks']['normal_first_perk_ready'] = True
                        self.pc.choose_reward_perk(0)
                        self.stage('perk_resolution')
                elif self.phase == 'perk_resolution' and not self.pc.is_reward_menu_open():
                    self.report['checks']['normal_first_perk_resolved'] = True
                    self.prepare_fixture()
                return
            if MODE == 'preview':
                self.preview_tick()
            else:
                self.profile_tick()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        errors = []
        owned = self.owned_pie_is_active()
        if self.csv_owned:
            try:
                self.console('CsvProfile STOP')
                self.csv_owned = False
            except Exception:
                errors.append(traceback.format_exc())
        if self.fixture and owned:
            try:
                self.require(self.mask_snapshot() == self.mask_signatures, 'Native coffee arrays changed')
                self.report['native_masks_after'] = self.mask_snapshot()
            except Exception:
                errors.append(traceback.format_exc())
            try:
                errors.extend(self.fixture.restore() or [])
                self.report['synthetic_fixture'] = self.fixture.report
            except Exception:
                errors.append(traceback.format_exc())
        if owned:
            for action in (
                lambda: self.pc.set_editor_property('bShouldPerformFullTickWhenPaused', self.pause_tick_restore) if self.pause_tick_restore is not None else None,
                lambda: self.pc.set_editor_property('bAutoManageActiveCameraTarget', self.auto_camera_restore) if self.auto_camera_restore is not None else None,
                lambda: self.pc.set_view_target_with_blend(self.original_view_target, 0.) if hasattr(self, 'original_view_target') else None,
                lambda: self.scheduler.set_actor_tick_enabled(self.scheduler_tick) if valid(self.scheduler) and self.scheduler_tick is not None else None,
                lambda: u.GameplayStatics.set_game_paused(self.world, False) if self.world_paused_by_probe else None):
                try:
                    action()
                except Exception:
                    errors.append(traceback.format_exc())
        for name, value in self.original_cvars.items():
            try:
                self.console(name+' '+str(value))
            except Exception:
                errors.append(traceback.format_exc())
        try:
            self.engine.set_editor_property('bSmoothFrameRate', self.original_smooth)
            self.report['dirty_packages_after'] = materials['dirty_packages']()
            self.report['protected_packages_after'] = disk_packages()
            self.report['checks'].update(user_dirty_packages_preserved=self.report['dirty_packages_after'] == self.report['dirty_packages_before'],
                production_packages_unchanged=self.report['protected_packages_after'] == self.report['protected_packages_before'])
            self.require(self.report['checks']['user_dirty_packages_preserved'] and self.report['checks']['production_packages_unchanged'],
                         'Original saved packages or user dirty state changed')
        except Exception:
            errors.append(traceback.format_exc())
        self.report['fixture_restore_errors'] = errors
        super().finish(error or ('Tooth fixture restoration failed' if errors else None))
        expected = dict(console_variables=dict(self.original_cvars, **{'t.IdleWhenNotForeground': self.old_idle}),
            engine=dict(fixed=bool(self.old_engine[0]), rate=float(self.old_engine[1]), smooth=self.original_smooth),
            editor=dict(throttle=bool(self.old_perf)), play=self.old_play)
        try:
            observed = dict(console_variables={name: u.SystemLibrary.get_console_variable_float_value(name)
                for name in expected['console_variables']},
                engine=dict(fixed=bool(prop(self.engine, 'bUseFixedFrameRate')), rate=float(prop(self.engine, 'FixedFrameRate')),
                            smooth=bool(prop(self.engine, 'bSmoothFrameRate'))),
                editor=dict(throttle=bool(prop(self.perf, 'bThrottleCPUWhenNotForeground'))),
                play=json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(self.old_play))))
            matches = {group+'.'+name: (math.isclose(observed[group][name], value, rel_tol=1.e-6, abs_tol=1.e-6)
                if isinstance(value, (int, float)) and not isinstance(value, bool) else observed[group][name] == value)
                for group, values in expected.items() for name, value in values.items()}
            verified = all(matches.values())
            self.report['post_restore_verification'] = dict(expected=expected, observed=observed, matches=matches, passed=verified)
            self.report['checks']['settings_independently_restored'] = verified
            if not verified:
                self.report.update(status='failed', passed=False, restored_settings=False,
                    error=self.report.get('error') or 'Independent settings restoration readback failed')
        except Exception:
            self.report.update(status='failed', passed=False, restored_settings=False,
                error=self.report.get('error') or 'Independent settings restoration readback failed')
            self.report['post_restore_verification'] = dict(passed=False, error=traceback.format_exc())
        self.write()


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve the existing tooth A/B probe')
    probe = CoffeeToothArchitectureAB()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
