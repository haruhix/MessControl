"""Root-run, transient-only 64/128/256 renderer lab in one owned solo PIE.

MODE='preview' (default) and MODE='performance' are SEPARATE invocations.
Data/cases.json contains independent native-algorithm rasterizations; there is
no upsampling and no native higher-resolution gameplay claim. A naturally
created 1200cm slime paint mesh is reused, paused, and temporarily given the
same transient rev5+Edge2 material. Accessible native64 wipe masks are hashed;
the protected deposition ledger is unavailable and no native mask is assigned.
Preview: six states, real HUD, gameplay/close views and three 3s micro-orbits.
Performance: uncapped stationary 64/128/64/256/64, 3s warmup+6s CSV each,
no screenshots. No saves/build/network. Cancel:
builtins.mc_coffee_mask_resolution.finish('Stopped by operator').
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import time
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
MODE = str(globals().get('MODE', 'preview')).lower()
if MODE not in ('preview', 'performance'):
    raise RuntimeError('MODE must be preview or performance')
LAB = ROOT/'Saved/CoffeeMaskResolution'
OUTPUT = LAB/('Preview' if MODE == 'preview' else 'Performance')
DATA = Path(globals().get('MC_MASK_RESOLUTION_DATA', LAB/'Data/cases.json')).resolve()
KEY = 'mc_coffee_mask_resolution'
perf = runpy.run_path(str(ROOT/'Tools/Unreal/profile_coffee_film_solo.py'),
    run_name='resolution_perf_support', init_globals={'MC_COFFEE_PERF_OUTPUT': OUTPUT,
                                                     'MC_COFFEE_PERF_SECONDS': 6.})
Performance, Wave, prop, valid, path = (perf[k] for k in
    ('CoffeeFilmPerformance', 'Parent', 'prop', 'valid', 'path'))
helpers = runpy.run_path(str(ROOT/'Tools/Unreal/coffee_mask_resolution_material.py'),
                        run_name='resolution_material_support')



class CoffeeMaskResolution(Performance):
    def __init__(self):
        super().__init__()
        self.lab_ready = False
        self.visibility_restore = []
        self.pause_tick_restore = None
        self.lab_material = self.lab_mid = None
        self.textures = {}
        self.capture_hz = 8. if MODE == 'preview' else 0.
        self.lab_stage_index = -1
        self.required_clips = [f'coffee_mask_{n}_{state}' for n in (64, 128, 256)
                               for state in ('before', 'after', 'orbit')]
        self.report.update(mode='coffee_mask_resolution', lab_mode=MODE, samples=[], clips={}, frames=[],
            screenshots=[], results=[], checks={}, source_sha256={},
            scope='Renderer-only transient-mask density comparison on one actual native paint mesh',
            purpose='Compare independent64/128/256 mask rendering on one paused native1200cm paint mesh',
            baseline='Same transient current rev5 film plus Edge2 material; only actual mask density and normal texel step vary',
            required_clips=self.required_clips if MODE == 'preview' else [],
            clip_labels={name: name.replace('_', ' ') for name in self.required_clips},
            time_clock='wall_seconds', timing_seconds_each=6.,
            native_ledger_verification=dict(available=False,
                reason='LiquidDepositMask is protected and live root API preflight returned no readback',
                verified_scope='Exposed WipeMask SHA and reported care-state snapshot; no native mask writes'),
            limitations=[
                '64/128/256 datasets are independently rasterized by the same native brush/deposit algorithm offline; no 64 upsampling.',
                'Only rendering uses higher-resolution textures. Native gameplay, contacts, completion, upload cost and replication stay64 and are not benchmarked here.',
                'One real1200cm native slime paint mesh is reused without scaling/rebuilding. Other paint meshes are hidden only in owned PIE and restored.',
                'Owned world pauses after normal F3/wave setup. Exposed full paused PC tick updates the real PlayerCameraManager for preview micro-orbits.',
                'Frame time is explicit elapsed wall_seconds; native_game_time is recorded separately and remains paused.',
                'GPUTime is whole Editor RHI frame in one stationary scene, not isolated material cost or multiplayer performance.',
                'No packages, unsaved map changes, native mask assignments, builds or network clients.'])
        self.report['dirty_packages_before'] = helpers['dirty_packages']()
        self.report['protected_packages_before'] = helpers['protected_packages']()
        self.report['source_sha256'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (Path(__file__), ROOT/'Tools/Unreal/profile_coffee_film_solo.py',
                      ROOT/'Tools/Unreal/probe_coffee_wave_stains_solo.py',
                      ROOT/'Tools/Unreal/coffee_mask_resolution_material.py')}

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/('Report_'+MODE+'.json')).write_text(data, encoding='utf-8')

    def start(self):
        self.dataset = json.loads(DATA.read_text(encoding='utf-8'))
        self.require(self.dataset['schema_version'] == 1, 'Unknown mask dataset schema')
        self.require(float(self.dataset['world_size_cm']) == 1200.
                     and float(self.dataset['half_size_cm']) == 600., 'Expected native1200cm tile dataset')
        self.require('no flip' in self.dataset['uv_orientation'].lower(), 'Review mask UV orientation')
        cases = self.dataset['cases']
        self.require(sorted(int(c['resolution']) for c in cases) == [64, 128, 256], 'Need exactly64/128/256')
        self.case_by_n = {int(case['resolution']): case for case in cases}
        for source, expected in self.dataset['native_source_sha256'].items():
            self.require(hashlib.sha256((ROOT/source).read_bytes()).hexdigest() == expected,
                         'Dataset native source changed: '+source)
        self.lab_material, material_report = helpers['build']()
        self.report['transient_material'] = material_report
        self.report['dataset'] = dict(path=str(DATA), sha256=hashlib.sha256(DATA.read_bytes()).hexdigest(),
                                      manifest=self.dataset, imported_textures=[])
        for n, case in self.case_by_n.items():
            for state in ('before', 'after'):
                filename = Path(case[state+'_path'])
                png = filename.read_bytes()
                self.require(hashlib.sha256(png).hexdigest() == case[state+'_sha256'], 'Dataset PNG hash mismatch')
                self.require(png[:8] == b'\x89PNG\r\n\x1a\n'
                             and int.from_bytes(png[16:20], 'big') == n
                             and int.from_bytes(png[20:24], 'big') == n, 'Dataset is not the claimed resolution')
                texture = u.RenderingLibrary.import_buffer_as_texture2d(self.editor_world, list(png))
                self.require(texture is not None and texture.get_path_name().startswith('/Engine/Transient.'),
                             'Expected a transient imported mask')
                for key, value in dict(srgb=False, filter=u.TextureFilter.TF_BILINEAR,
                    address_x=u.TextureAddress.TA_CLAMP, address_y=u.TextureAddress.TA_CLAMP,
                    never_stream=True).items():
                    texture.set_editor_property(key, value)
                self.require(texture.blueprint_get_size_x() == n and texture.blueprint_get_size_y() == n,
                             'Transient imported texture dimensions differ')
                self.require(not prop(texture, 'srgb') and prop(texture, 'filter') == u.TextureFilter.TF_BILINEAR
                             and prop(texture, 'address_x') == u.TextureAddress.TA_CLAMP
                             and prop(texture, 'address_y') == u.TextureAddress.TA_CLAMP,
                             'Texture linear/bilinear/clamp settings did not apply')
                self.textures[n, state] = texture
                self.report['dataset']['imported_textures'].append(dict(resolution=n, state=state,
                    path=path(texture), source=str(filename), source_sha256=case[state+'_sha256'],
                    srgb=bool(prop(texture, 'srgb')), filter=str(prop(texture, 'filter')),
                    address_x=str(prop(texture, 'address_x')), address_y=str(prop(texture, 'address_y')),
                    allocated_bytes=int(texture.blueprint_get_memory_size())))
        self.require(helpers['dirty_packages']() == self.report['dirty_packages_before'],
                     'Transient dataset/material authoring dirtied editor packages')
        # Bypass Performance.start: it builds a different historical baseline.
        Wave.start(self)
        if MODE == 'performance':
            self.engine.set_editor_property('bUseFixedFrameRate', False)
            self.engine.set_editor_property('bSmoothFrameRate', False)
            self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps({'clientFixedFPS': []})),
                         'Could not remove PIE fixedFPS')
            for name, value in (('t.MaxFPS', 0), ('r.VSync', 0), ('r.VSyncEditor', 0),
                ('r.GPUCsvStatsEnabled', 1), ('csv.CompressionMode', 0), ('Slate.bAllowThrottling', 0)):
                self.console(name+' '+str(value))
            self.report['measurement_cvars'] = {name: u.SystemLibrary.get_console_variable_float_value(name)
                                                for name in self.original_cvars}
            self.require(not prop(self.engine, 'bUseFixedFrameRate')
                         and not prop(self.engine, 'bSmoothFrameRate')
                         and self.report['measurement_cvars']['t.MaxFPS'] == 0
                         and self.report['measurement_cvars']['r.VSync'] == 0, 'Uncapping failed')
        self.write()

    def begin_clip(self, *_args):
        self.clip = None  # Do not film F3 setup as a resolution candidate.

    def sample(self, now):
        pass  # Not a wave/deposition proof; avoid sampling unrelated encounter payloads.

    def prepare_baseline(self):
        self.prepared = True  # The lab needs no pre-F3 tooth-cleaning fixture.

    def update_jellies(self, now, elapsed):
        self.require(elapsed < 25., 'Natural F3 wave/tile setup exceeded focused timeout')
        if perf['support']['enum_name'](prop(self.event, 'phase')) != 'JELLY_FIGHT':
            return
        candidates = [a for a in self.actors(u.MCMouthSurface) if prop(a, 'paint_tile')
                      and abs(float(prop(a, 'liquid_half_size'))-600.) < .01
                      and isinstance(prop(a, 'liquid').get_material(0), u.MaterialInstanceDynamic)]
        if not candidates:
            return  # First natural slime deposit creates the actual1200cm mesh.
        center, _extent, _radius = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        candidates.sort(key=lambda a: (a.get_actor_location().x-center.x)**2
                        +(a.get_actor_location().y-center.y)**2)
        self.target = candidates[0]
        self.prepare_lab()

    def native_snapshot(self):
        snapshots = {}
        for actor in self.native_actors:
            self.require(valid(actor), 'Native paint actor disappeared')
            wipe = bytes(int(v) for v in prop(actor, 'wipe_mask'))
            self.require(len(wipe) == 4096, 'Native wipe mask must remain64x64')
            snapshots[path(actor)] = dict(wipe_sha256=hashlib.sha256(wipe).hexdigest(),
                wipe_cells=len(wipe),
                ledger_readback_unavailable='LiquidDepositMask is protected; native root preflight returned no reflected readback',
                care_state=json.loads(u.ToolsetLibrary.get_object_properties(prop(actor, 'status'), ['State'])))
        return snapshots

    def mask_snapshot(self):
        return self.native_snapshot()

    def prepare_lab(self):
        self.primary_input(False)
        self.mesh = prop(self.target, 'liquid')
        self.native_mid = self.mesh.get_material(0)
        self.require(prop(self.native_mid, 'parent') == u.load_asset(helpers['CURRENT']), 'Unexpected native paint parent')
        self.lab_mid = u.MaterialLibrary.create_dynamic_material_instance(self.world, self.lab_material)
        scalars, vectors = {}, {}
        for name in self.report['transient_material']['scalar_parameters']:
            if name != 'MaskResolution':
                value = self.native_mid.get_scalar_parameter_value(name)
                self.lab_mid.set_scalar_parameter_value(name, value)
                scalars[name] = float(value)
        for name in self.report['transient_material']['vector_parameters']:
            value = self.native_mid.get_vector_parameter_value(name)
            self.lab_mid.set_vector_parameter_value(name, value)
            vectors[name] = [value.r, value.g, value.b, value.a]
        self.require(abs(scalars['WorldSize']-1200.) < .01 and scalars['Finish'] == 0.,
                     'Native material physical size/Finish mismatch')
        self.require(abs(scalars['CoffeeEdgeSoftness']-1.) < 1.e-6, 'Expected accepted temporal edge')
        self.material_slots = [(self.target, self.mesh, self.native_mid, self.lab_mid)]
        self.native_actors = [a for a in self.actors(u.MCMouthSurface) if prop(a, 'paint_tile')]
        self.mask_signatures = self.native_snapshot()
        for actor in self.native_actors:
            mesh = prop(actor, 'liquid')
            if actor != self.target:
                visible = mesh.is_visible()
                self.visibility_restore.append((mesh, visible))
                mesh.set_visibility(False, False)
        center, extent, _radius = u.SystemLibrary.get_component_bounds(self.mesh)
        # A full1200cm HUD-safe fit puts the camera above the mouth ceiling.
        # Use the previously reviewed in-mouth film-camera distance instead.
        # This is an honest local crop of the same physical brush crossing,
        # never a rescaled texture, mesh or invented full-footprint view.
        focus_uv = (.50, .43)
        focus = self.target.get_actor_transform().transform_location(u.Vector(
            (focus_uv[0]-.5)*1200., (focus_uv[1]-.5)*1200., 0.))
        focus = self.floor(focus) or focus
        self.close_camera = self.bounded_film_camera(focus, 650., 68.)
        self.game_camera = self.bounded_film_camera(focus, 850., 82.)
        self.report['camera_scope'] = dict(focus_uv=focus_uv,
            full_component_center_cm=perf['support']['vec'](center),
            full_component_extent_cm=perf['support']['vec'](extent),
            crop='Same local native brush crossing in all N; the whole1200cm footprint is not promised in frame',
            rejected_whole_tile_fit='Run045205Z put the camera outside the mouth; telemetry alone did not detect occlusion')
        self.apply_camera('close')
        self.pause_tick_restore = bool(prop(self.pc, 'bShouldPerformFullTickWhenPaused'))
        self.pc.set_editor_property('bShouldPerformFullTickWhenPaused', True)
        self.require(bool(prop(self.pc, 'bShouldPerformFullTickWhenPaused')), 'Paused full camera tick not enabled')
        self.require(not u.GameplayStatics.is_game_paused(self.world), 'Unexpected prepaused owned world')
        self.require(u.GameplayStatics.set_game_paused(self.world, True), 'Could not pause owned renderer fixture')
        self.world_paused_by_probe = True
        self.mesh.set_material(0, self.lab_mid)
        self.lab_ready = self.profile_ready = True
        self.report['fixture'] = dict(actor=path(self.target), component=path(self.mesh),
            component_transform=str(self.mesh.get_world_transform()), native_mid=path(self.native_mid),
            lab_mid=path(self.lab_mid), native_masks=self.mask_signatures,
            half_size_cm=float(prop(self.target, 'liquid_half_size')), uniform_scalars=scalars, uniform_vectors=vectors,
            close_camera=self.close_camera, gameplay_camera=self.game_camera,
            hidden_components=[dict(path=path(m), previous_visible=v) for m, v in self.visibility_restore],
            actual_player_viewport_size=list(self.pc.get_viewport_size()),
            anti_aliasing_method=int(u.SystemLibrary.get_console_variable_int_value('r.AntiAliasingMethod')),
            pc_full_tick_when_paused_before=self.pause_tick_restore)
        self.require(self.report['fixture']['anti_aliasing_method'] == 2, 'Edge2 review requires actual TAA')
        self.report['checks']['real_f3_native1200cm_paint_fixture'] = True
        if MODE == 'performance':
            self.sequence = [('n64_before128', 64), ('n128', 128), ('n64_between', 64),
                             ('n256', 256), ('n64_after256', 64)]
            self.sequence_index = -1
            self.advance_profile()
        else:
            # Keep each resolution's orbit adjacent to its own after state.
            self.preview_steps = [step for n in (64, 128, 256) for step in
                ([(n, state, view, 2.) for state in ('before', 'after') for view in ('gameplay', 'close')]
                 + [(n, 'after', 'orbit', 3.)])]
            self.next_preview()

    def apply_camera(self, view, orbit=0.):
        camera = self.game_camera if view == 'gameplay' else self.close_camera
        eye, target = camera['eye'], camera['target']
        if orbit:
            center = self.close_camera['center']
            x, y = eye[0]-center[0], eye[1]-center[1]
            c, s = math.cos(orbit), math.sin(orbit)
            eye = (center[0]+x*c-y*s, center[1]+x*s+y*c, eye[2])
        self.frame_camera(u.Vector(*eye), u.Vector(*target))
        component = prop(self.camera, 'camera_component')
        component.set_editor_property('FieldOfView', camera['fov'])
        component.set_editor_property('aspect_ratio', 16./9.)
        component.set_editor_property('constrain_aspect_ratio', True)
        self.keep_camera()

    @staticmethod
    def bounded_film_camera(focus, distance, fov):
        center = (float(focus.x), float(focus.y), float(focus.z))
        outward = (.44, -.62, .65)
        # Small pan places the examined edge right of the left gameplay HUD.
        right = (-.62/math.hypot(.62, .44), -.44/math.hypot(.62, .44), 0.)
        return dict(center=center, eye=tuple(center[i]+outward[i]*distance for i in range(3)),
            target=tuple(center[i]-right[i]*distance*.12 for i in range(3)),
            fov=fov, distance_cm=distance, local_crop=True)

    def set_case(self, n, state):
        self.lab_mid.set_texture_parameter_value('WipeMask', self.textures[n, state])
        self.lab_mid.set_scalar_parameter_value('MaskResolution', float(n))
        self.require(self.lab_mid.get_texture_parameter_value('WipeMask') == self.textures[n, state]
                     and abs(self.lab_mid.get_scalar_parameter_value('MaskResolution')-n) < 1.e-6,
                     'Actual candidate MID readback differs')
        self.report.setdefault('candidate_readbacks', []).append(dict(resolution=n, state=state,
            texture=path(self.lab_mid.get_texture_parameter_value('WipeMask')),
            normal_step=1./float(self.lab_mid.get_scalar_parameter_value('MaskResolution')),
            native_masks_unchanged=self.native_snapshot() == self.mask_signatures))
        self.require(self.native_snapshot() == self.mask_signatures, 'Native masks changed during renderer comparison')

    def next_preview(self):
        self.lab_stage_index += 1
        if self.lab_stage_index == len(self.preview_steps):
            self.clip = None
            self.lab_capture_flush = time.monotonic()
            return
        n, state, view, seconds = self.preview_steps[self.lab_stage_index]
        self.set_case(n, state)
        self.apply_camera(view)
        self.clip = f'coffee_mask_{n}_'+('orbit' if view == 'orbit' else state)
        self.report['clips'].setdefault(self.clip, [])
        self.active_candidate = dict(resolution=n, state=state, view=view,
            texture=path(self.textures[n, state]), normal_step=1./n)
        self.preview_wall = time.monotonic()
        self.preview_frame_at = self.preview_wall+.25
        self.report.setdefault('preview_stages', []).append(dict(self.active_candidate,
            seconds=seconds, settle_seconds=.25, clip=self.clip))
        self.write()

    def screenshot(self, name, sequence=False):
        if not self.lab_ready or MODE != 'preview':
            return
        target = self.output/'frames'/name
        target.parent.mkdir(parents=True, exist_ok=True)
        self.console('Shot SHOWUI filename="'+str(target).replace('\\', '/')+'" -nosuffix')
        entry = dict(file=str(target), phase='renderer_resolution_lab',
            time=time.monotonic()-self.started, time_clock='wall_seconds',
            native_game_time=float(u.GameplayStatics.get_time_seconds(self.world)),
            candidate=dict(self.active_candidate), camera_evidence=self.camera_evidence(),
            clip_window=self.clip)
        self.report['frames'].append(entry)
        self.report['clips'][self.clip].append(entry)

    def preview_tick(self):
        if hasattr(self, 'lab_capture_flush'):
            if time.monotonic()-self.lab_capture_flush < .8:
                return
            for frame in self.report['frames']:
                data = Path(frame['file']).read_bytes()
                self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Missing genuine screenshot PNG')
                frame.update(sha256=hashlib.sha256(data).hexdigest(),
                    width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
            self.require(all(len(self.report['clips'].get(name, [])) >= 8 for name in self.required_clips),
                         'A required genuine resolution clip has too few frames')
            self.report['checks'].update(six_independent_resolution_states_captured=True,
                three_real_camera_microorbits_captured=True, native64_wipe_masks_unchanged=True)
            self.finish()
            return
        n, state, view, seconds = self.preview_steps[self.lab_stage_index]
        now = time.monotonic()
        age = now-self.preview_wall-.25
        if age < 0.:
            return
        if view == 'orbit':
            self.apply_camera(view, math.radians(4.)*math.sin(2.*math.pi*min(age/seconds, 1.)))
        camera = self.camera_evidence()
        # Same real PlayerCameraManager gate as the existing genuine video probe.
        if now >= self.preview_frame_at and camera and self.pc.get_view_target() == self.camera \
                and camera['location_error_cm'] < 12. and camera['rotation_error_degrees'] < 5.:
            self.preview_frame_at = now+1./self.capture_hz
            self.screenshot('%05d.png' % len(self.report['frames']), True)
        if age >= seconds:
            self.require(len([f for f in self.report['clips'][self.clip]
                              if f['candidate']['view'] == view]) >= 8, 'Camera view has insufficient genuine frames')
            self.next_preview()

    def advance_profile(self):
        self.sequence_index += 1
        if self.sequence_index == len(self.sequence):
            self.complete_profile()
            return
        name, n = self.sequence[self.sequence_index]
        self.set_case(n, 'after')
        self.apply_camera('close')
        self.profile_phase = name
        self.csv_name = 'MC_CoffeeMask_'+self.profile_id+'_'+name
        self.csv_directory = ROOT/'Saved/Profiling/CSV'
        self.csv_path = None
        self.csv_state = 'warmup'
        self.state_wall = time.monotonic()
        self.report['last_profile_phase'] = name
        self.write()

    def complete_profile(self):
        values = [r['metrics']['GPUTime']['median'] for r in self.report['results']]
        self.require(len(values) == 5, 'Five actual GPU windows required')
        a, b, c, d, e = values
        self.report['comparison'] = dict(unit='ms', sequence=[dict(phase=p[0], resolution=p[1],
            gpu_median_ms=value) for p, value in zip(self.sequence, values)],
            n128_minus_bracketed64_ms=b-(a+c)*.5, n128_bracket64_drift_ms=abs(c-a),
            n256_minus_bracketed64_ms=d-(c+e)*.5, n256_bracket64_drift_ms=abs(e-c),
            interpretation='Only static renderer texture density varies; differences within64 repeat drift are inconclusive. Native CPU/upload/network remain64.')
        self.report['checks'].update(positive_actual_gpu_csv=True, same_native_masks=True,
            stationary_actual_player_camera=True, no_video_sampling_during_measurement=not self.report['frames'])
        self.finish()

    def tick(self, delta):
        if self.done:
            return
        if not self.lab_ready:
            Wave.tick(self, delta)
            return
        try:
            self.require(time.monotonic()-self.started < 240., 'Bounded renderer lab timed out')
            self.require(self.world in u.EditorLevelLibrary.get_pie_worlds(True), 'Owned PIE disappeared')
            if MODE == 'preview':
                self.preview_tick()
            else:
                # Inherited CSV state machine performs no screenshots/mask reads
                # until its completed CSV has flushed outside each timed window.
                self.profile_tick()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        errors = []
        if self.lab_ready:
            try:
                self.require(self.native_snapshot() == self.mask_signatures, 'Native arrays changed during lab')
                self.report['native_masks_after'] = self.native_snapshot()
                self.report['checks']['native64_wipe_masks_unchanged'] = True
            except Exception:
                errors.append(traceback.format_exc())
        for _actor, mesh, native_mid, _lab_mid in self.material_slots:
            try:
                if valid(mesh):
                    mesh.set_material(0, native_mid)
                    self.require(mesh.get_material(0) == native_mid, 'Actual native MID restore failed')
            except Exception:
                errors.append(traceback.format_exc())
        for mesh, visible in self.visibility_restore:
            try:
                if valid(mesh):
                    mesh.set_visibility(visible, False)
                    self.require(mesh.is_visible() == visible, 'PIE visibility restore failed')
            except Exception:
                errors.append(traceback.format_exc())
        if self.pause_tick_restore is not None and valid(self.pc):
            try:
                self.pc.set_editor_property('bShouldPerformFullTickWhenPaused', self.pause_tick_restore)
                self.require(bool(prop(self.pc, 'bShouldPerformFullTickWhenPaused')) == self.pause_tick_restore,
                             'Paused camera tick restore failed')
            except Exception:
                errors.append(traceback.format_exc())
        self.report['renderer_fixture_restore_errors'] = errors
        self.report['dirty_packages_after'] = helpers['dirty_packages']()
        self.report['protected_packages_after'] = helpers['protected_packages']()
        same_packages = self.report['protected_packages_after'] == self.report['protected_packages_before']
        same_dirty = self.report['dirty_packages_after'] == self.report['dirty_packages_before']
        self.report['checks'].update(production_packages_unchanged=same_packages,
            user_dirty_packages_preserved=same_dirty)
        error = error or ('Renderer fixture restoration failed' if errors else None)
        error = error or ('Persistent packages changed' if not same_packages or not same_dirty else None)
        # Restores component MID, world pause, caps, full PIE/editor settings and
        # independently reads settings back; then ends only this owned PIE.
        super().finish(error)


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve running mask resolution lab')
    probe = CoffeeMaskResolution()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
