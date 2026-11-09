"""One owned L_Mouth PIE: fresh Apple edge cut, then spoiled Carrot C.

Requires the assigned canonical Juicy profile and idle transient-profile Editor.
Globals MC_JUICY_EDITOR_PID and MC_JUICY_LOGFILE are mandatory; optional
MC_JUICY_OUTPUT overrides the E: run directory. No app/build/asset launch/save,
direct HitFood/intake calls, profile override, hidden effects or extra peers.

Support is the versioned check_food_volume_only_110_solo.py ->
check_food_auto_mouth_solo.py -> check_approved_environment_solo.py chain.
The real saved Apple contact pose and real loading/input/cleanup gates remain.
Cancel: builtins.mc_food_juicy_solo.finish('Stopped by operator').
"""
import builtins
import csv
import hashlib
import io
import json
import math
import os
import re
import runpy
import time
import traceback
from pathlib import Path
import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
TASK = Path(globals().get('MC_JUICY_OUTPUT',
    'E:/CodexWork/MessControl/Validation/FoodJuicyVFX_20261109/Gameplay/Runs'))
PID = int(globals()['MC_JUICY_EDITOR_PID'])
LOG = Path(globals()['MC_JUICY_LOGFILE']).resolve()
assert os.getpid() == PID and TASK.drive.upper() == LOG.drive.upper() == 'E:'
assert LOG.is_file()
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_food_volume_only_110_solo.py'),
    run_name='food_juicy_support',
    init_globals={'MC_VOLUME_OUTPUT': str(TASK), 'MC_VOLUME_LOGFILE': str(LOG)})
Parent, prop, valid, path, vec, asset_ref = (
    support[k] for k in ('FoodVolumeSolo', 'prop', 'valid', 'path', 'vec', 'asset_ref'))
KEY = 'mc_food_juicy_solo'
BATCHES = (918209, 919902)  # First is the unmodified versioned Apple fixture.
ASSET_ROOT = '/Game/Gameplay/Food/VFX/Juicy'
NS = ASSET_ROOT+'/NS_FoodJuicy_Pulp.NS_FoodJuicy_Pulp'
PROFILE = ASSET_ROOT+'/DA_FoodJuicy_Pulp.DA_FoodJuicy_Pulp'
RESOURCE_NAMES = ['NS_FoodJuicy_Pulp', 'DA_FoodJuicy_Pulp', 'M_FoodJuicy_Pulp',
                  'SM_FoodJuicy_Chunk', 'SM_FoodJuicy_Crumb']
RESOURCES = [ASSET_ROOT+'/'+name+'.'+name for name in RESOURCE_NAMES]
USER_NAMES = ['User.Color', 'User.TargetOffset', 'User.Duration', 'User.Radius',
              'User.Intensity', 'User.Seed']


def unpack_json(result):
    values = list(result) if isinstance(result, (tuple, list)) else [result]
    payload = next((str(v) for v in values if isinstance(v, (str, u.Text))
                    and str(v).lstrip().startswith('{')), None)
    assert payload, 'Expected JSON inspection result: '+repr(values)
    errors = [str(v) for v in values if isinstance(v, (str, u.Text))
              and str(v).strip() and str(v) != payload]
    assert not errors, errors
    return json.loads(payload)


class FoodJuicySolo(Parent):
    def __init__(self):
        super().__init__()
        self.case_index = 0
        self.session_at = None
        self.current = None
        self.sim_caches = []
        self.capture_attempts = {}
        self.component_captured = set()
        self.report.update(mode='food_juicy_canonical_actual_input',
            scope='One owned solo, unchanged gameplay camera/light and saved Food.AutoMouth source; fresh cut and spoiled disposal.',
            canonical_system=NS, canonical_profile=PROFILE, cases=[],
            active_food_vfx=[], legacy_splash=[], particle_samples=[],
            helpers=['Tools/Unreal/check_food_volume_only_110_solo.py',
                     'Tools/Unreal/check_food_auto_mouth_solo.py',
                     'Tools/Unreal/check_approved_environment_solo.py'])
        self.report['limitations'] = [
            'Functional/art capture at a fixed game step and up to12 game-time capture Hz, not a frame-time or GPU benchmark.',
            'Only the owned spawned food body is frozen for controlled real knife contact; food dimensions/settings stay saved.',
            'The day scheduler pauses only in owned PIE. The accepted large Apple sibling is checked, then removed as fixture cleanup before the separate disposal case.',
            'One-frame transient SimCache captures observe current CPU particle ages/counts; advance_simulation=False, no replay or fabricated motion.',
            'Warm Editor readiness/first-use evidence does not certify cold packaged PSO preparation or multiplayer.',
            'Recipe relies on the three explicitly named versioned support files; no old external validation helper is executed.']
        self.report['asset_hashes_before'] = self.asset_hashes()

    def asset_hashes(self):
        files = [ROOT/('Content/'+asset.split('.')[0].removeprefix('/Game/')+'.uasset')
                 for asset in RESOURCES]
        files += [ROOT/'Content/Gameplay/VFX/Profiles/DA_MouthPresentation.uasset',
                  ROOT/'Content/Gameplay/VFX/Tools/DA_ToolPresentation.uasset']
        self.require(all(p.is_file() for p in files), 'Integrate/save Juicy resources before gameplay capture')
        return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}

    def counters(self):
        values = super().counters()
        values['meal_sequence'] = sum(int(prop(t, 'meal_sequence')) for t in self.actors(u.MCThroat))
        return values

    def all_log_lines(self):
        with LOG.open('rb') as stream:
            stream.seek(self.log_offset)
            return stream.read().decode('utf8', errors='replace').splitlines()

    def intake_log_lines(self):
        return [line for line in self.all_log_lines()
                if ('MC_FOOD_AUTO_INTAKE' in line or 'MC_FOOD_AUTO_DISCARD' in line)
                and any(re.search(r'\bbatch='+str(batch)+r'\b', line) for batch in BATCHES)]

    def readiness_resources(self):
        result = json.loads(u.MCGameplayUIEditorLibrary.inspect_pie_visual_resources(
            self.world, [u.SoftObjectPath(p) for p in RESOURCES]))
        self.require(result.get('valid') and result.get('ready'), 'Real location preload is not ready')
        rows = result.get('resources', [])
        self.require(len(rows) == len(RESOURCES) and all(r.get('resident') and r.get('retained') for r in rows),
                     'New Juicy visual resource is outside actual resident/retained preload closure: '+json.dumps(result))
        return result

    def choose_rows(self):
        self.report['resources_before_first_contact'] = self.readiness_resources()
        profile = u.find_object(None, PROFILE)
        library = u.find_object(None, '/Game/Gameplay/VFX/Profiles/DA_MouthPresentation.DA_MouthPresentation')
        self.require(valid(profile) and valid(library), 'Canonical source must already be resident')
        self.require(dict(prop(library, 'profiles'))[u.Name('Food.AutoMouth')] == profile,
                     'Actual scoped library does not bind the saved Juicy profile')
        slots = dict(prop(profile, 'slots'))
        self.require(all(path(prop(slots[u.Name(event)], 'system')) == NS for event in ('Consume', 'Discard')),
                     'Both saved canonical events must use the new system')
        super().choose_rows()
        table = u.load_asset('/Game/Data/DT_BreakfastMenu')
        raw = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(raw, (tuple, list)):
            raw = next(v for v in raw if isinstance(v, str))
        self.carrot = next(row for row in json.loads(raw) if row['Name'] == 'Carrot')
        text = u.DataTableFunctionLibrary.export_data_table_to_csv_string(table)
        if isinstance(text, (tuple, list)):
            text = next(v for v in text if isinstance(v, str))
        rows = list(csv.reader(io.StringIO(text)))
        native = next(row for row in rows[1:] if row and row[0] == 'Carrot')
        self.carrot_native = '('+','.join(k+'='+v for k, v in zip(rows[0][1:], native[1:]) if k != 'Label')+')'
        ref = next(ref for ref in self.carrot['FragmentMeshes'] if 'SM_CarrotSliceC' in ref)
        self.carrot_mesh = u.load_asset(asset_ref(ref))
        self.require(valid(self.carrot_mesh) and bool(prop(self.carrot_mesh, 'allow_cpu_access')),
                     'Saved ready Carrot C must retain readable geometry')

    def fixture(self, _):
        self.session_at = time.monotonic()
        self.setup_case(0)

    def setup_case(self, index):
        self.case_index = index
        if index == 0:
            Parent.fixture(self, None)  # Exact proven AppleRed/scale30 edge pose.
        else:
            center, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
            floor = self.floor(center)
            self.require(floor is not None, 'Supported disposal contact site missing')
            self.move_to(floor, 0.)
            self.pc.set_control_rotation(u.Rotator(pitch=-12, yaw=0))
            self.pc.set_view_target_with_blend(self.hero, 0.)
            pose = u.Transform(location=floor+u.Vector(170., 0., 80.))
            food = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world,
                u.MCFoodActor.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
                None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
            self.require(valid(food), 'Owned spoiled fragment spawn failed')
            data = u.MCFoodRow()
            self.require(data.import_text(self.carrot_native), 'Native saved Carrot row import failed')
            data.set_editor_property('mass', float(self.carrot['Mass'])/max(1, int(self.carrot['Fragments'])))
            data.set_editor_property('health', 25.)
            for field, value in (('food_data', data), ('item_name', 'Carrot'),
                ('item_mesh', self.carrot_mesh), ('fragment', True), ('bSpoiled', True),
                ('health', 25.), ('batch', BATCHES[index])):
                food.set_editor_property(field, value)
            food.set_editor_property('SpoilAt', float(u.GameplayStatics.get_time_seconds(self.world))-1.)
            self.statics.call_method('FinishSpawningActor', args=(food, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
            self.food = food
            origin, extent, _ = u.SystemLibrary.get_component_bounds(prop(food, 'visual'))
            desired = floor+u.Vector(extent.x+70., 0., max(extent.z+8., 75.))
            prop(food, 'body').set_simulate_physics(False)
            food.set_actor_location(food.get_actor_location()+desired-origin, False, True)
            prop(food, 'label').set_visibility(False)
            self.require(food.uses_automatic_cut_intake() and food.fits_automatic_intake_portion(),
                         'Saved spoiled Carrot C must be ready without resizing')
            prop(self.hero, 'inventory').call_method('ServerSelect', args=(u.MCToolSlot.KNIFE,))
        self.current = dict(kind='fresh_apple_cut' if index == 0 else 'spoiled_carrot_discard',
            batch=BATCHES[index], actor=path(self.food), before=self.counters(),
            expected_event='Consume' if index == 0 else 'Discard',
            resource_readiness=self.readiness_resources(), live_components=[], live_particle_components=[])
        self.report['cases'].append(self.current)
        self.stage('case_before')

    def particle_count(self, component, now):
        key = component.get_path_name()
        if key in self.component_captured or self.capture_attempts.get(key, 0) >= 3:
            return
        self.capture_attempts[key] = self.capture_attempts.get(key, 0)+1
        cache = u.NiagaraSimCacheFunctionLibrary.create_niagara_sim_cache(component)
        self.require(valid(cache), 'Current-frame diagnostic cache creation failed')
        parameters = u.NiagaraSimCacheCreateParameters()
        parameters.set_editor_property('allow_data_interface_caching', False)
        captured = u.NiagaraSimCacheFunctionLibrary.capture_niagara_sim_cache_immediate(
            cache, parameters, component, False, .01666)
        values = list(captured) if isinstance(captured, tuple) else [captured]
        if not values or values[0] is False:
            return
        cache = next((value for value in values if isinstance(value, u.NiagaraSimCache)), cache)
        self.sim_caches.append(cache)
        if cache.get_num_frames() < 1:
            return
        counts, namespaced_counts = {}, {}
        for name in cache.get_emitter_names():
            # SimCache uses compiled particle-dataset names, which have their
            # Particles. root namespace removed (NiagaraSimCacheHelper.h).
            # Retain both lookups to expose the previous observer mistake.
            ages = cache.read_float_attribute(u.Name('Age'), name, 0)
            counts[str(name)] = len(ages)
            namespaced_counts[str(name)] = len(cache.read_float_attribute(u.Name('Particles.Age'), name, 0))
        total = sum(counts.values())
        self.report['particle_samples'].append(dict(time=now, case=self.case_index,
            component=key, counts=counts, total=total, attribute='Age',
            diagnostic_wrong_namespace_counts=namespaced_counts,
            capture='Current-frame transient NiagaraSimCache, advance_simulation=False'))
        if total > 0:
            self.component_captured.add(key)
            self.current['live_particle_components'].append(key)

    def observe(self, now):
        for component in u.ObjectIterator(u.NiagaraComponent):
            if not valid(component) or component.get_world() != self.world:
                continue
            asset = component.get_asset()
            if not valid(asset) or not component.is_active():
                continue
            if asset.get_name() == 'NS_FoodCutSplash':
                self.report['legacy_splash'].append(dict(time=now, component=path(component), system=path(asset)))
            if path(asset) != NS or self.phase not in ('case_input', 'case_tail'):
                continue
            inspection = unpack_json(u.MCPerkPreviewEditorLibrary.get_niagara_component_parameter_inspection(
                component, [u.Name(name) for name in USER_NAMES]))
            self.require(inspection.get('world') == self.world.get_path_name() and inspection.get('active'),
                         'Actual component inspection lost owned context')
            params = inspection.get('parameters', [])
            self.require(len(params) == len(USER_NAMES) and all(p.get('present') and p.get('valid') for p in params),
                         'Actual gameplay User.* parameters are missing/invalid: '+json.dumps(inspection))
            values = {p['name']: p['value'] for p in params}
            self.require(values['User.Duration'] > 0 and values['User.Radius'] > 0 and values['User.Intensity'] > 0,
                         'Actual Niagara parameters have no visible duration/radius/intensity')
            self.require(all(math.isfinite(float(v)) for v in values['User.TargetOffset']), 'Nonfinite actual target offset')
            self.report['active_food_vfx'].append(dict(time=now, case=self.case_index,
                component=path(component), system=path(asset), parameters=inspection))
            if path(component) not in self.current['live_components']:
                self.current['live_components'].append(path(component))
            self.particle_count(component, now)
        self.report['samples'].append(dict(time=now, phase=self.phase, case=self.case_index,
            counters=self.counters(), source_phase=str(prop(self.food, 'phase')) if valid(self.food) else 'expired'))

    def finish_case(self):
        lines = [line for line in self.intake_log_lines()
                 if re.search(r'\bbatch='+str(self.current['batch'])+r'\b', line)]
        self.require(len(lines) == 1, 'Expected exactly one native accepted portion for this actual input')
        volumes = [float(re.search(r' volume_cm3=([0-9eE+.-]+)', line).group(1)) for line in lines]
        self.require(all(0 < v <= 110.**3+.01 for v in volumes), 'Accepted portion exceeds actual volume limit')
        before, after = self.current['before'], self.counters()
        if self.case_index == 0:
            self.require('MC_FOOD_AUTO_INTAKE' in lines[0] and after['swallowed']-before['swallowed'] == 1
                and after['meal_sequence']-before['meal_sequence'] == 1, 'Fresh cut did not feed once')
            units = int(re.search(r'\bunits=(\d+)', lines[0]).group(1))
            self.require(after['points'] >= before['points'] and (units != 0 or after['points'] == before['points']),
                         'Fresh score disagrees with zero/nonnegative remaining units')
            children = self.inspect_children()
            large = [c for c in children if not c['disposed'] and not c['fits'] and not c['hidden']]
            self.require(large, 'Original large Apple sibling was not retained')
            self.current['remaining_large_siblings'] = large
            # Owned fixture cleanup prevents the second independent knife case
            # from touching leftover Apple. This is not gameplay completion.
            leftovers = [a for a in self.actors(u.MCFoodActor) if int(prop(a, 'batch')) == BATCHES[0]]
            self.current['fixture_cleanup'] = [path(a) for a in leftovers]
            for actor in leftovers:
                actor.destroy_actor()
        else:
            self.require('MC_FOOD_AUTO_DISCARD' in lines[0] and after == before,
                         'Spoiled disposal gave fresh points/swallowed/meal credit')
        self.require(self.current['live_components'] and self.current['live_particle_components'],
                     'Accepted real input did not produce observed new-system live CPU particles')
        self.require(after == self.current['at_acceptance'], 'No-input tail duplicated progress')
        self.current.update(after=after, native_lines=lines, volumes_cm3=volumes, passed=True)
        if self.case_index == 0:
            self.setup_case(1)
        else:
            self.report['checks'].update(both_real_input_cases=True,
                new_resource_retention_before_first_contact=True,
                actual_live_typed_parameters=True, actual_cpu_particles=True,
                old_knife_splash_absent=not self.report['legacy_splash'],
                no_render_visibility_mask=True, gameplay_camera_retained=self.pc.get_view_target() == self.hero)
            self.require(all(self.report['checks'].values()), 'Final camera/old-splash check failed')
            self.report['resources_after_both_cases'] = self.readiness_resources()
            self.finish()

    def tick(self, dt):
        if self.done:
            return
        if self.phase == 'waiting_pie':
            return super().tick(dt)
        try:
            self.require(time.monotonic()-self.started < 210, 'Owned loading/capture timeout')
            self.require(time.monotonic()-self.session_at < 70, 'Bounded two-case action capture exceeded70s')
            self.require(self.owned_pie_is_active(), 'Owned PIE ended or was replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            if now >= self.sample_at:
                self.sample_at = now+.08
                self.observe(now)
            if now >= self.frame_at:
                self.frame_at = now+1/self.capture_hz
                self.screenshot('%05d.png' % len(self.report['frames']), True)
            elapsed = now-self.phase_at
            if self.phase == 'case_before':
                self.require(elapsed < 10, 'Actual knife readiness/selection did not settle')
                if elapsed >= (1.0 if self.case_index == 0 else .7) and self.work_state()['ready'] \
                    and prop(prop(self.hero, 'inventory'), 'selected') == u.MCToolSlot.KNIFE:
                    self.current['input_world_seconds'] = now
                    self.current['input_log_index'] = len(self.all_log_lines())
                    self.primary_input(True)
                    self.stage('case_input')
            elif self.phase == 'case_input':
                self.require(elapsed < 4, 'Real knife did not accept the saved-scale portion')
                if valid(self.food) and prop(self.food, 'phase') == u.MCFoodPhase.DISPOSED:
                    self.primary_input(False)
                    self.current['at_acceptance'] = self.counters()
                    self.stage('case_tail')
            elif self.phase == 'case_tail' and elapsed >= 2.1:
                self.finish_case()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        self.report['asset_hashes_after'] = self.asset_hashes()
        if self.report['asset_hashes_after'] != self.report['asset_hashes_before']:
            error = error or 'Runtime probe changed saved visual assets'
        self.report['resource_log_lines'] = [line for line in self.all_log_lines()
            if 'MC_PRELOAD_' in line or 'Waiting for static meshes' in line or 'LogShaderCompilers:' in line]
        self.report['resource_log_scope'] = 'Correlate with actual native readiness and recorded input timestamps; loading work is not automatically an interaction hitch.'
        super().finish(error)


old = getattr(builtins, KEY, None)
assert old is None or old.done, 'Preserve existing owned Juicy probe'
probe = FoodJuicySolo()
setattr(builtins, KEY, probe)
try:
    probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
