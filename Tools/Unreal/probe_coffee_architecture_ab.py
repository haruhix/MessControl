"""ROOT-RUN ONLY: transient tongue architecture A/B, separate preview/performance.

MODE='preview': before/after128 masks, close/gameplay views and real camera
micro-orbits. Optional MC_ARCH_DEFORMATION=True (default) adds lockstep pairs:
advance the real native tongue briefly UNPAUSED, then capture both architectures
at the identical paused game-time/geometry. This is qualitative motion evidence.
MODE='performance': static A/B/A, 3s warmup +6s actual CSV GPUTime each, no shots.
A=original tongue material + separate accepted coffee-film mesh.
B=transient original tongue material blended with coffee; only A film is hidden.
Native film actor/tick remain present in BOTH. No CPU-geometry-saving claim.
No package saves, native mask writes, build, Engine calls on support import.
Cancel: builtins.mc_coffee_architecture_ab.finish('Stopped by operator').
"""
import builtins
import hashlib
import json
import math
import runpy
import struct
import time
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
MODE = str(globals().get('MODE', 'preview')).lower()
if MODE not in ('preview', 'performance'):
    raise RuntimeError('MODE must be preview or performance')
OUTPUT = ROOT/'Saved/CoffeeArchitectureAB'/('Preview' if MODE == 'preview' else 'Performance')
KEY = 'mc_coffee_architecture_ab'
DEFORMATION = bool(globals().get('MC_ARCH_DEFORMATION', True)) and MODE == 'preview'
SHADING_MODE = str(globals().get('MC_ARCH_SHADING', 'from_attributes'))
INITIAL_PREHEAT = max(0., min(20., float(globals().get('MC_ARCH_INITIAL_PREHEAT', 0.))))
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_coffee_mask_resolution.py'),
    run_name='architecture_resolution_support', init_globals={'MODE': MODE})
Resolution, prop, valid, path = (support[k] for k in ('CoffeeMaskResolution', 'prop', 'valid', 'path'))
blend = runpy.run_path(str(ROOT/'Tools/Unreal/coffee_surface_blend_material.py'),
                      run_name='architecture_blend_support')


class CoffeeArchitectureAB(Resolution):
    def __init__(self):
        self.architecture_ready = False
        self.arch_hidden_restore = None
        self.other_hidden_restore = []
        self.deformation_tick_restore = []
        self.arch_phase = None
        super().__init__()
        self.output = OUTPUT/'Fixture'/'Runs'/self.output.name
        self.required_clips = [f'architecture_{a}_{s}' for a in ('A', 'B')
                               for s in ('before', 'after', 'orbit')]
        if DEFORMATION:
            self.required_clips += ['architecture_A_deformation', 'architecture_B_deformation']
        self.report.update(mode='coffee_architecture_ab', lab_mode=MODE,
            run_directory=str(self.output), required_clips=self.required_clips if MODE == 'preview' else [],
            scope='Transient rendering architecture comparison on one native1200cm tongue paint footprint',
            purpose='Compare separate coffee-film geometry A with coffee blended into the original tongue material B',
            baseline='A: original live tongue MID plus separate accepted rev5/Edge2 film; identical independent128 mask for A/B',
            clip_labels={name: name.replace('_', ' ') for name in self.required_clips},
            cpu_geometry_scope='Native film actors/ticks are retained in both variants; CPU-geometry elimination is NOT measured',
            limitations=[
                'Only transient rendering architecture changes. Native masks, cleaning/contact/completion/network remain64.',
                'The same native1200cm film geometry and independent128 before/after data are used; no geometry rescale or upsampling.',
                'B blends coffee into the real tongue graph and hides only the comparison film. Original skin cutouts are retained.',
                'B uses continuous alpha and, in from_attributes mode, native per-pixel SubsurfaceProfile/DefaultLit selection; A uses a separate masked temporal film. Visual acceptance is required.',
                'Other paint components are hidden equally. Native actor/tick state is retained for static A/B/A; no CPU savings claim.',
                'Preview deformation, if requested, releases real native time between paused A/B pairs; it is lockstep evidence, not a continuously simulated gameplay video.',
                'During deformation only unrelated event/jelly/flood actor ticks are temporarily frozen/restored to protect native masks.',
                'GPUTime is the whole Editor RHI frame in one paused view, not an isolated material pass or full-game/multiplayer test.',
                'No package saves, persistent settings, native mask assignments, builds or network clients.'])
        for name in ('Tools/Unreal/probe_coffee_architecture_ab.py',
                     'Tools/Unreal/probe_coffee_mask_resolution.py',
                     'Tools/Unreal/coffee_surface_blend_material.py'):
            self.report['source_sha256'][name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/('Report_'+MODE+'.json')).write_text(data, encoding='utf-8')

    def prepare_lab(self):
        # Parent provides the proven native F3 fixture,128 textures, camera,
        # pause/full-PC-tick, original film MID and restoration bookkeeping.
        # Our next_preview/advance_profile defer until the B graph is ready.
        super().prepare_lab()
        self.tongue_mesh = prop(self.tongue, 'surface')
        self.tongue_native_mid = self.tongue_mesh.get_material(0)
        self.require(self.tongue_native_mid is not None, 'Missing live original tongue interface')
        self.tongue_blend_material, material_report = blend['build_tongue'](
            self.tongue_native_mid, shading_model_mode=SHADING_MODE)
        self.tongue_blend_mid, uniform_report = blend['configure_tongue_mid'](
            self.world, self.tongue_blend_material, self.tongue_native_mid,
            self.textures[128, 'after'], self.tongue_mesh.get_world_transform(),
            self.mesh.get_world_transform(), world_size_cm=1200., resolution=128)
        self.original_uniforms = [(kind, str(name)) for kind, names in (
            ('scalar', u.MaterialEditingLibrary.get_scalar_parameter_names(self.tongue_native_mid)),
            ('vector', u.MaterialEditingLibrary.get_vector_parameter_names(self.tongue_native_mid)),
            ('texture', u.MaterialEditingLibrary.get_texture_parameter_names(self.tongue_native_mid)))
            for name in names]
        self.material_slots.append((self.tongue, self.tongue_mesh, self.tongue_native_mid, self.tongue_blend_mid))
        for mesh, _visible in self.visibility_restore:
            # Native UpdateLiquid resets Visible while the world advances.
            # Keep the existing equal-background fixture hidden in both A/B.
            self.other_hidden_restore.append((mesh, bool(prop(mesh, 'hidden_in_game'))))
            mesh.set_hidden_in_game(True, False)
        self.film_visible_before = bool(self.mesh.is_visible())
        self.arch_hidden_restore = bool(prop(self.mesh, 'hidden_in_game'))
        self.visibility_restore.append((self.mesh, self.film_visible_before))
        self.report['architecture'] = dict(A='original tongue + separate coffee film',
            B='coffee blended into original tongue; separate film hidden', resolution=128,
            tongue_component=path(self.tongue_mesh), original_tongue_mid=path(self.tongue_native_mid),
            blend_material=material_report, blend_uniforms=uniform_report,
            film_actor_tick_enabled=bool(self.target.is_actor_tick_enabled()),
            original_film_hidden_in_game=self.arch_hidden_restore)
        self.require(self.target.is_actor_tick_enabled(), 'Native film actor must remain ticking in A/B')
        self.architecture_ready = True
        if MODE == 'performance':
            self.sequence = [('separate_A_before', 'A'), ('surface_blend_B', 'B'), ('separate_A_after', 'A')]
            self.sequence_index = -1
            self.advance_profile()
        else:
            self.preview_steps = [(architecture, state, view, 2.)
                for state in ('before', 'after') for architecture in ('A', 'B')
                for view in ('gameplay', 'close')]
            self.preview_steps += [(architecture, 'after', 'orbit', 3.) for architecture in ('A', 'B')]
            self.lab_stage_index = -1
            self.next_preview()

    def sync_original_uniforms(self):
        # The native tongue keeps updating its original private MID. Copy those
        # effective uniforms into the transient clone outside timing/capture.
        for kind, name in self.original_uniforms:
            value = getattr(self.tongue_native_mid, 'get_'+kind+'_parameter_value')(name)
            if kind != 'texture' or value is not None:
                getattr(self.tongue_blend_mid, 'set_'+kind+'_parameter_value')(name, value)

    def set_architecture(self, architecture, state='after'):
        self.require(architecture in ('A', 'B'), 'Unknown architecture')
        self.set_case(128, state)  # Same texture and native-mask checks outside CSV windows.
        self.sync_original_uniforms()
        self.tongue_blend_mid.set_texture_parameter_value('AB_CoffeeMask', self.textures[128, state])
        self.tongue_blend_mid.set_scalar_parameter_value('AB_CoffeeEnabled', 1.)
        self.tongue_mesh.set_material(0, self.tongue_native_mid if architecture == 'A' else self.tongue_blend_mid)
        # HiddenInGame survives native UpdateLiquid's SetVisibility on unpaused
        # deformation frames. The actor and its geometry update still run.
        self.mesh.set_hidden_in_game(self.arch_hidden_restore if architecture == 'A' else True, False)
        self.mesh.set_visibility(self.film_visible_before if architecture == 'A' else False, False)
        self.require(self.tongue_mesh.get_material(0) == (
            self.tongue_native_mid if architecture == 'A' else self.tongue_blend_mid), 'Tongue MID swap failed')
        self.require(bool(prop(self.mesh, 'hidden_in_game')) == (
            self.arch_hidden_restore if architecture == 'A' else True), 'Film hide readback failed')
        self.require(self.target.is_actor_tick_enabled(), 'Architecture must not disable native film actor tick')
        self.report.setdefault('architecture_readbacks', []).append(dict(architecture=architecture, state=state,
            tongue_mid=path(self.tongue_mesh.get_material(0)), film_hidden_in_game=bool(prop(self.mesh, 'hidden_in_game')),
            native_film_tick_enabled=bool(self.target.is_actor_tick_enabled()),
            mask=path(self.textures[128, state]), native_game_time=float(u.GameplayStatics.get_time_seconds(self.world))))

    def next_preview(self):
        if not self.architecture_ready:
            return
        self.lab_stage_index += 1
        if self.lab_stage_index == len(self.preview_steps):
            if DEFORMATION:
                self.begin_deformation()
            else:
                self.clip = None
                self.lab_capture_flush = time.monotonic()
            return
        architecture, state, view, seconds = self.preview_steps[self.lab_stage_index]
        self.set_architecture(architecture, state)
        self.apply_camera(view)
        self.clip = f'architecture_{architecture}_'+('orbit' if view == 'orbit' else state)
        self.report['clips'].setdefault(self.clip, [])
        self.active_candidate = dict(architecture=architecture, resolution=128, state=state, view=view,
            texture=path(self.textures[128, state]), normal_step=1./128.)
        self.preview_wall = time.monotonic()
        self.preview_frame_at = self.preview_wall+.25
        self.report.setdefault('preview_stages', []).append(dict(self.active_candidate,
            seconds=seconds, settle_seconds=.25, clip=self.clip))
        self.write()

    def screenshot(self, name, sequence=False):
        if not self.architecture_ready or MODE != 'preview':
            return
        super().screenshot(name, sequence)
        self.report['frames'][-1]['phase'] = 'renderer_architecture_ab'

    def preview_tick(self):
        if self.arch_phase:
            self.deformation_tick()
            return
        if hasattr(self, 'lab_capture_flush'):
            if time.monotonic()-self.lab_capture_flush < .8:
                return
            for frame in self.report['frames']:
                data = Path(frame['file']).read_bytes()
                self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Missing genuine screenshot PNG')
                frame.update(sha256=hashlib.sha256(data).hexdigest(),
                    width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
            self.require(all(len(self.report['clips'].get(name, [])) >= 8 for name in self.required_clips),
                         'A required architecture clip has too few genuine frames')
            self.report['checks'].update(before_after_both_architectures=True,
                both_actual_player_camera_orbits=True, native64_wipe_masks_unchanged=True)
            self.finish()
            return
        _architecture, _state, view, seconds = self.preview_steps[self.lab_stage_index]
        now = time.monotonic(); age = now-self.preview_wall-.25
        if age < 0.:
            return
        if view == 'orbit':
            self.apply_camera(view, math.radians(4.)*math.sin(2.*math.pi*min(age/seconds, 1.)))
        camera = self.camera_evidence()
        if now >= self.preview_frame_at and camera and self.pc.get_view_target() == self.camera \
                and camera['location_error_cm'] < 12. and camera['rotation_error_degrees'] < 5.:
            self.preview_frame_at = now+1./self.capture_hz
            self.screenshot('%05d.png' % len(self.report['frames']), True)
        if age >= seconds:
            self.require(len([f for f in self.report['clips'][self.clip]
                              if f['candidate']['view'] == view]) >= 8, 'A camera view lacks genuine frames')
            self.next_preview()

    def geometry_snapshot(self):
        output = {}
        for label, mesh in (('tongue', self.tongue_mesh), ('film', self.mesh)):
            section = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(mesh, 0)
            vertices = section[0]
            self.require(vertices, 'Native deformation geometry unavailable: '+label)
            packed = b''.join(struct.pack('<ddd', float(v.x), float(v.y), float(v.z)) for v in vertices)
            output[label] = dict(vertices=len(vertices), position_sha256=hashlib.sha256(packed).hexdigest(),
                transform=str(mesh.get_world_transform()))
        return output

    def begin_deformation(self):
        self.clip = None
        for cls in (u.MCCataclysmEvent, u.MCCataclysmJelly, u.MCCoffeeFlood):
            for actor in self.actors(cls):
                enabled = bool(actor.is_actor_tick_enabled())
                self.deformation_tick_restore.append((actor, enabled))
                actor.set_actor_tick_enabled(False)
        self.report['deformation_fixture'] = dict(
            kind='Native unpaused advances, paired A/B captured at identical paused time/geometry',
            step_game_seconds=.12, steps=10, statistical_cpu_claim=False,
            tick_fixture=[dict(actor=path(a), previous_tick_enabled=v) for a,v in self.deformation_tick_restore], pairs=[])
        # An actual short native yawn gives a visible, repeatable local-Z bend.
        # Both render variants are captured at each identical paused geometry.
        started = bool(self.tongue.start_yawn(2.))
        self.require(started, 'Native yawn fixture could not start')
        self.report['deformation_fixture']['native_motion'] = dict(
            kind='StartYawn', seconds=2., started=started, max_source_local_height_cm=45.)
        self.deformation_step = 0
        self.deformation_initial_geometry = self.geometry_snapshot()
        self.advance_deformation_time()

    def advance_deformation_time(self):
        self.clip = None
        self.deformation_native_from = float(u.GameplayStatics.get_time_seconds(self.world))
        self.require(u.GameplayStatics.set_game_paused(self.world, False), 'Could not release native tongue time')
        self.arch_phase = 'native_advance'

    def deformation_tick(self):
        now = time.monotonic()
        if self.arch_phase == 'native_advance':
            if float(u.GameplayStatics.get_time_seconds(self.world))-self.deformation_native_from < .12:
                return
            self.require(u.GameplayStatics.set_game_paused(self.world, True), 'Could not pause native A/B pair')
            self.require(self.native_snapshot() == self.mask_signatures, 'Native masks changed during deformation fixture')
            self.deformation_pair_time = float(u.GameplayStatics.get_time_seconds(self.world))
            self.deformation_pair_geometry = self.geometry_snapshot()
            self.set_deformation_variant('A')
        elif self.arch_phase in ('pair_A', 'pair_B'):
            age = now-self.deformation_wall
            if age >= .22 and not self.deformation_captured:
                camera = self.camera_evidence()
                self.require(camera and camera['location_error_cm'] < 1. and camera['rotation_error_degrees'] < .1,
                             'Native deformation pair actual camera drifted')
                self.require(float(u.GameplayStatics.get_time_seconds(self.world)) == self.deformation_pair_time
                    and self.geometry_snapshot() == self.deformation_pair_geometry, 'A/B pair native time or geometry changed')
                self.screenshot('%05d.png' % len(self.report['frames']), True)
                self.report['frames'][-1]['paired_native_geometry'] = self.deformation_pair_geometry
                self.deformation_captured = True
            if age >= .45:
                if self.arch_phase == 'pair_A':
                    self.set_deformation_variant('B')
                else:
                    self.report['deformation_fixture']['pairs'].append(dict(step=self.deformation_step,
                        native_game_time=self.deformation_pair_time, geometry=self.deformation_pair_geometry,
                        matching_A_B_time_geometry=True))
                    self.deformation_step += 1
                    if self.deformation_step == 10:
                        moved = self.deformation_pair_geometry['tongue']['position_sha256'] != \
                            self.deformation_initial_geometry['tongue']['position_sha256']
                        self.require(moved, 'Native tongue did not deform during the fixture')
                        self.report['checks'].update(native_deformation_observed=True, paired_identical_native_geometry=True)
                        self.restore_deformation_ticks()
                        self.arch_phase = None;self.clip = None;self.lab_capture_flush = now
                    else:
                        self.advance_deformation_time()

    def set_deformation_variant(self, architecture):
        self.set_architecture(architecture, 'after')
        self.apply_camera('close')
        self.clip = f'architecture_{architecture}_deformation'
        self.report['clips'].setdefault(self.clip, [])
        self.active_candidate = dict(architecture=architecture, resolution=128, state='after', view='deformation',
            deformation_step=self.deformation_step, paired_native_game_time=self.deformation_pair_time,
            texture=path(self.textures[128, 'after']))
        self.deformation_wall = time.monotonic();self.deformation_captured = False
        self.arch_phase = 'pair_'+architecture

    def restore_deformation_ticks(self):
        errors = []
        for actor, enabled in self.deformation_tick_restore:
            try:
                if valid(actor):
                    actor.set_actor_tick_enabled(enabled)
                    self.require(bool(actor.is_actor_tick_enabled()) == enabled, 'Native tick fixture restore failed')
            except Exception:
                errors.append(traceback.format_exc())
        self.deformation_tick_restore = []
        self.report.setdefault('deformation_tick_restore_errors', []).extend(errors)
        return errors

    def advance_profile(self):
        if not self.architecture_ready:
            return
        self.sequence_index += 1
        if self.sequence_index == len(self.sequence):
            self.complete_profile();return
        name, architecture = self.sequence[self.sequence_index]
        self.set_architecture(architecture, 'after')
        self.apply_camera('close')
        self.profile_phase = name;self.csv_name = 'MC_CoffeeArchitecture_'+self.profile_id+'_'+name
        self.csv_directory = ROOT/'Saved/Profiling/CSV';self.csv_path = None;self.csv_state = 'warmup'
        extra = INITIAL_PREHEAT if self.sequence_index == 0 else 0.
        self.state_wall = time.monotonic()+extra
        self.report['initial_extra_preheat_seconds'] = INITIAL_PREHEAT
        self.report['last_profile_phase'] = name;self.write()

    def complete_profile(self):
        values = [r['metrics']['GPUTime']['median'] for r in self.report['results']]
        self.require(len(values) == 3, 'Need actual A/B/A GPU windows')
        a,b,c = values;baseline=(a+c)*.5
        self.report['comparison'] = dict(unit='ms', separate_A_before=a, surface_blend_B=b, separate_A_after=c,
            separate_repeat_drift_ms=abs(c-a), separate_repeat_drift_percent=abs(c-a)/baseline*100.,
            blend_minus_bracketed_separate_ms=b-baseline,
            blend_minus_bracketed_separate_percent=(b-baseline)/baseline*100.,
            interpretation='Static whole-Editor RHI rendering only. Native film actor/ticks retained; no CPU-geometry saving or full-game claim.')
        self.report['checks'].update(positive_actual_gpu_csv=True, same_native_masks=True,
            stationary_actual_player_camera=True, no_video_sampling_during_measurement=not self.report['frames'],
            native_film_actor_tick_retained=True)
        self.finish()

    def finish(self, error=None):
        if getattr(self, 'done', False):
            return
        errors = self.restore_deformation_ticks()
        for mesh, hidden in self.other_hidden_restore:
            try:
                if valid(mesh):
                    mesh.set_hidden_in_game(hidden, False)
                    self.require(bool(prop(mesh, 'hidden_in_game')) == hidden, 'Other film hidden state restore failed')
            except Exception:
                errors.append(traceback.format_exc())
        if self.arch_hidden_restore is not None and hasattr(self, 'mesh') and valid(self.mesh):
            try:
                self.mesh.set_hidden_in_game(self.arch_hidden_restore, False)
                self.require(bool(prop(self.mesh, 'hidden_in_game')) == self.arch_hidden_restore,
                             'Native film HiddenInGame restore failed')
            except Exception:
                errors.append(traceback.format_exc())
        self.report['architecture_restore_errors'] = errors
        super().finish(error or ('Architecture restoration failed' if errors else None))


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve existing architecture probe')
    probe = CoffeeArchitectureAB();setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
