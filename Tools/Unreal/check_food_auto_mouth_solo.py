"""One owned solo PIE: real knife -> ready cut halves -> Food.AutoMouth.

Run only after the current Editor build and after Composer releases the Editor.
Uses saved menu meshes/scales, ordinary primary input and the real L_Mouth
readiness barrier. No content saves, direct HitFood/intake calls or extra peers.
Optional MC_AUTO_OUTPUT overrides the E: report path. Pass MC_AUTO_LOGFILE with
the actual Editor log path to retain accepted native event evidence.
Cancel: builtins.mc_food_auto_mouth_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import hashlib
import json
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUTPUT = Path(globals().get('MC_AUTO_OUTPUT', 'E:/CodexWork/MessControl/Validation/FoodAutoMouth_20261108/Solo'))
RUN = OUTPUT / datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='auto_mouth_support', init_globals={'MC_ENV_OUTPUT': RUN, 'MC_ENV_CAPTURE_HZ': 12})
Base, prop, vec = (support[k] for k in ('ApprovedEnvironmentSolo', 'prop', 'vec'))
KEY = 'mc_food_auto_mouth_solo'
BATCHES = (918081, 918082)
LOGFILE = globals().get('MC_AUTO_LOGFILE')


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def asset_ref(value):
    return value.split("'", 1)[1].rstrip("'") if "'" in value else value


class FoodAutoMouthSolo(Base):
    def __init__(self):
        self.require('mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(),
                     'Use the owned -MCHubTransientProfile Editor; persistent profiles are preserved')
        super().__init__()
        self.require(str(self.output).lower().startswith('e:'), 'Validation output must stay on E:')
        self.editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
        self.original_camera = self.editor.get_level_viewport_camera_info()
        self.original_dirty = self.dirty_packages()
        self.require(not any(self.original_dirty.values()), 'Save/resolve authoring packages before this owned PIE')
        self.scheduler = None
        self.scheduler_tick = None
        self.ready_since = None
        self.video_start = None
        self.row_choices = {}
        self.case = 0
        self.baselines = {}
        self.accepted_at = None
        self.source = None
        self.receipt_seen = set()
        self.niagara_seen = set()
        self.require(LOGFILE and Path(LOGFILE).is_file(), 'Pass MC_AUTO_LOGFILE with the real Editor log path')
        self.log_offset = Path(LOGFILE).stat().st_size if LOGFILE and Path(LOGFILE).is_file() else 0
        self.report.pop('fixture_tuning', None)
        self.report.pop('tuning_reason', None)
        self.report.update(scope='One owned solo L_Mouth PIE; saved scale and real knife primary input',
            mode='food_auto_mouth', run_directory=str(RUN), samples=[], fixtures=[], receipts=[], niagara=[],
            checks={}, log_file=LOGFILE, limitations=[
                'The fixture copies supported reflected row fields and retains the real saved mesh/scale. Native CollisionData and nested Stack/Collision payloads are not roundtripped through JSON or claimed as independently verified.',
                'Food body physics is frozen only while making controlled tool contact; accepted generated halves use production cut/intake code.',
                'The single-day scheduler is paused only in this owned PIE. This is a focused functional/visual check, not a balanced playthrough.',
                'Source C++ WasAutomaticallyConsumed/ConfirmedFoodCutCount and the private LastReceipt payload are unavailable to this Python recipe; accepted native event logs, phase, score, throat count and observed Niagara are used instead. Solo does not independently inspect transport payloads.',
                'Window captures are the actual gameplay camera at up to 12 game-time Hz. Root visual inspection and video encoding are separate recorded steps.',
                'No build, cook/package, network test or content/profile save is performed.'])
        self.report['source_sha256'] = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in (
            'Source/MessControl/Private/MCFoodActor.cpp', 'Source/MessControl/Private/MCFoodCutIntake.cpp',
            'Source/MessControl/Private/MCFoodCutIntakePresentation.cpp', 'Source/MessControl/Private/MCFoodCutPrototype.cpp')}

    def dirty_packages(self):
        return dict(maps=sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
                    content=sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()))

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        text = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)+'\n'
        (self.output/'Report.json').write_text(text, encoding='utf8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'LatestSoloReport.json').write_text(text, encoding='utf8')

    def work_state(self):
        gs = u.GameplayStatics.get_game_state(self.world)
        physics = prop(self.hero, 'tooth_physics')
        state = dict(presentation_ready=bool(self.pc.is_world_presentation_ready()),
            standing=physics.get_body_state() == u.MCBodyState.STANDING,
            alive=bool(prop(self.hero, 'status').is_alive()),
            walking=prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_WALKING,
            working=prop(gs, 'phase') == u.MCShiftPhase.WORKING,
            move_ignored=bool(self.pc.is_move_input_ignored()), look_ignored=bool(self.pc.is_look_input_ignored()),
            reward_menu=bool(self.pc.is_reward_menu_open()), boss_intro=bool(self.pc.is_boss_intro_playing()),
            lobby=bool(prop(gs, 'lobby_waiting')), freezing=bool(self.hero.is_freezing_to_death()),
            mimic=bool(self.hero.is_mimic_captured()), swallowed=bool(prop(self.hero, 'swallowed_by')),
            selected=str(prop(prop(self.hero, 'inventory'), 'selected')))
        state['ready'] = all(state[k] for k in ('presentation_ready', 'standing', 'alive', 'walking', 'working')) \
            and not any(state[k] for k in ('move_ignored', 'look_ignored', 'reward_menu', 'boss_intro', 'lobby', 'freezing', 'mimic', 'swallowed'))
        return state

    def choose_rows(self):
        profile = u.load_asset('/Game/Data/DA_FoodPhysics')
        self.require(valid(profile), 'Saved food profile missing')
        settings = prop(profile, 'settings')
        limits = vec(prop(settings, 'max_auto_consume_dimensions'))
        self.report['saved_auto_portion_limits_cm'] = limits
        self.report['saved_legacy_hand_limits_cm'] = vec(prop(settings, 'max_hand_dimensions'))
        table = u.load_asset('/Game/Data/DT_BreakfastMenu')
        self.require(valid(table), 'Saved breakfast menu missing')
        raw = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(raw, (tuple, list)):
            raw = next(v for v in raw if isinstance(v, str))
        rows = sorted(json.loads(raw), key=lambda r: (r.get('Name') != 'Egg', r.get('Name', '')))
        for row in rows:
            if str(row.get('Kind', 'Food')).split('::')[-1] != 'Food':
                continue
            scale = row.get('FragmentScale', {'X': 1, 'Y': 1, 'Z': 1})
            for reference in row.get('FragmentMeshes', []):
                if not reference or reference == 'None':
                    continue
                mesh = u.load_asset(asset_ref(reference))
                if not valid(mesh):
                    continue
                bounds = mesh.get_bounding_box()
                native = bounds.max-bounds.min
                size = [abs(getattr(native, a.lower())*float(scale[a])) for a in ('X', 'Y', 'Z')]
                if min(size) <= 0:
                    continue
                if 'split' not in self.row_choices and bool(prop(mesh, 'allow_cpu_access')):
                    for axis in range(3):
                        if limits[axis] < size[axis] <= limits[axis]*2 and all(size[a] <= limits[a] for a in range(3) if a != axis):
                            self.row_choices['split'] = (row, mesh, size, axis)
                            break
                if 'ready' not in self.row_choices and all(a <= b for a, b in zip(size, limits)):
                    self.row_choices['ready'] = (row, mesh, size, None)
        self.require(set(self.row_choices) == {'split', 'ready'},
                     'Need saved-scale one-axis oversized and already-ready fragment variants at the actual automatic threshold; do not resize assets for a PASS')

    def fixture(self, kind):
        row, mesh, dimensions, cut_axis = self.row_choices[kind]
        self.case = 0 if kind == 'split' else 1
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        floor = self.floor(center)
        self.require(floor is not None, 'No supported tongue center')
        self.move_to(floor, 0)
        self.pc.set_control_rotation(u.Rotator(pitch=-12, yaw=0))
        self.pc.set_view_target_with_blend(self.hero, 0.)
        # Align the authored long axis with Hero.Right for the native knife
        # plane, matching Composer. Rotation preserves the saved mesh and scale.
        rotation = u.Rotator(yaw=90) if cut_axis == 0 else u.Rotator(roll=90) if cut_axis == 2 else u.Rotator()
        world_dimensions = [dimensions[1], dimensions[0], dimensions[2]] if cut_axis == 0 else \
            [dimensions[0], dimensions[2], dimensions[1]] if cut_axis == 2 else dimensions
        pose = u.Transform(location=floor+u.Vector(world_dimensions[0]*.5+75, 0, world_dimensions[2]*.5+8), rotation=rotation)
        food = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world,
            u.MCFoodActor.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(food), 'Deferred food spawn failed')
        data = prop(food, 'food_data')
        for field, key in (('scale', 'Scale'), ('fragment_scale', 'FragmentScale')):
            value = row[key]
            data.set_editor_property(field, u.Vector(*[float(value[a]) for a in ('X', 'Y', 'Z')]))
        count = max(1, min(64, int(row.get('Fragments', 1))))
        data.set_editor_property('mass', float(row['Mass'])/count)
        data.set_editor_property('health', 25.)
        data.set_editor_property('fragments', count)
        for field, key in (('spoil_seconds', 'SpoilSeconds'), ('absorb_seconds', 'AbsorbSeconds')):
            if key in row:
                data.set_editor_property(field, float(row[key]))
        data.set_editor_property('kind', u.MCFoodKind.FOOD)
        resistance = str(row.get('Resistance', 'Automatic')).split('::')[-1].upper()
        data.set_editor_property('resistance', getattr(u.MCFoodResistance, resistance))
        if 'CrumbColor' in row:
            color = row['CrumbColor']
            data.set_editor_property('crumb_color', u.LinearColor(*[float(color.get(a, 1)) for a in ('R', 'G', 'B', 'A')]))
        food.set_editor_property('food_data', data)
        food.set_editor_property('item_name', row['Name'])
        food.set_editor_property('item_mesh', mesh)
        food.set_editor_property('fragment', True)
        food.set_editor_property('health', 25.)
        food.set_editor_property('batch', BATCHES[self.case])
        self.statics.call_method('FinishSpawningActor', args=(food, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        body = prop(food, 'body')
        body.set_simulate_physics(False)
        prop(food, 'label').set_visibility(False)
        self.food = food
        self.require(food.uses_automatic_cut_intake(), 'Saved profile did not select the automatic pattern')
        self.require(bool(food.fits_automatic_intake_portion()) == (kind == 'ready'), 'Actual automatic portion state differs from the saved-size candidate')
        self.source = prop(u.GameplayStatics.get_game_state(self.world), 'food_cut_intake_presentation')
        self.baselines[kind] = self.counters()
        prop(self.hero, 'inventory').call_method('ServerSelect', args=(u.MCToolSlot.KNIFE,))
        self.report['fixtures'].append(dict(kind=kind, actor=path(food), batch=BATCHES[self.case],
            row=row['Name'], mesh=path(mesh), saved_dimensions_cm=dimensions, actual_hand_size_cm=vec(food.get_hand_portion_size()),
            selected_source_cut_axis=cut_axis, actor_rotation=str(rotation), world_dimensions_cm=world_dimensions,
            saved_fragment_scale=row['FragmentScale'], saved_row=row, physics_frozen_for_contact=True))
        self.accepted_at = None
        self.stage(kind+'_before')

    def counters(self):
        ps = prop(self.hero, 'player_state')
        return dict(points=int(prop(ps, 'points')),
                    swallowed=sum(int(prop(t, 'food_swallowed')) for t in self.actors(u.MCThroat)))

    def observe(self, now):
        for component in u.ObjectIterator(u.NiagaraComponent):
            if not valid(component) or component.get_world() != self.world:
                continue
            asset = component.get_asset()
            if valid(asset) and asset.get_name() == 'NS_Food_AutoMouth':
                key = path(component)
                if key not in self.niagara_seen:
                    self.niagara_seen.add(key)
                    self.report['niagara'].append(dict(time=now, component=key, system=path(asset), active=component.is_active()))
        self.report['samples'].append(dict(time=now, phase=self.phase, counters=self.counters(),
            food_phase=str(prop(self.food, 'phase')) if valid(self.food) else 'expired',
            food_hidden=bool(prop(self.food, 'hidden')) if valid(self.food) else True,
            view_target=path(self.pc.get_view_target()), work=self.work_state()))

    def intake_log_lines(self):
        if not LOGFILE or not Path(LOGFILE).is_file():
            return []
        with Path(LOGFILE).open('rb') as stream:
            stream.seek(self.log_offset)
            lines = stream.read().decode('utf8', errors='replace').splitlines()
        return [line for line in lines if 'MC_FOOD_AUTO_INTAKE' in line and any('batch='+str(b) in line for b in BATCHES)]

    def tick(self, _):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 210, 'Owned solo check exceeded 210 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and 'UEDPIE_0_L_Mouth' in worlds[0].get_path_name(), 'Expected one owned solo L_Mouth')
                self.world = worlds[0]
            self.require(self.owned_pie_is_active(worlds), 'Owned PIE was externally replaced')
            self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
            self.hero = self.pc.get_controlled_pawn() if self.pc else None
            if not valid(self.hero):
                return
            if self.pc.is_reward_menu_open():
                self.pc.choose_reward_perk(0)
                self.report.setdefault('transient_reward_choices', []).append(0)
                return
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            if self.phase == 'waiting_pie':
                ready = self.work_state()
                if not ready['ready']:
                    self.ready_since = None
                    return
                if self.ready_since is None:
                    self.ready_since = now
                if now-self.ready_since < .5:
                    return
                self.report['real_ready_state'] = ready
                gs = u.GameplayStatics.get_game_state(self.world)
                self.scheduler = prop(gs, 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_tick = self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
                self.tongue = next(iter(self.actors(u.MCTongue)), None)
                self.require(valid(self.tongue), 'No real tongue')
                self.require(bool(self.actors(u.MCThroat)), 'No actual saved throat')
                self.choose_rows()
                self.fixture('split')
                self.video_start = now
                return
            if now >= self.sample_at:
                self.sample_at = now+.08
                self.observe(now)
            if now >= self.frame_at:
                self.frame_at = now+1/self.capture_hz
                self.screenshot('%05d.png' % len(self.report['frames']), True)
            elapsed = now-self.phase_at
            kind = 'split' if self.case == 0 else 'ready'
            baseline = self.baselines[kind]
            current = self.counters()
            if self.phase.endswith('_before'):
                self.require(elapsed < 10, 'Normal knife selection/readiness did not settle')
                if prop(prop(self.hero, 'inventory'), 'selected') != u.MCToolSlot.KNIFE:
                    return
                if elapsed >= (2. if kind == 'split' else .7) and self.work_state()['ready']:
                    self.primary_input(True)
                    self.stage(kind+'_cut')
            elif self.phase.endswith('_cut'):
                self.require(elapsed < 3, 'Real knife contact did not finish the expected portion')
                expected = 2 if kind == 'split' else 1
                if current['swallowed']-baseline['swallowed'] == expected and valid(self.food) and prop(self.food, 'phase') == u.MCFoodPhase.DISPOSED:
                    self.accepted_at = now
                    self.report['checks'][kind+'_physical_source_hidden'] = bool(prop(self.food, 'hidden'))
                    self.report['checks'][kind+'_accepted_portions'] = expected
                    self.report['checks'][kind+'_score_delta'] = current['points']-baseline['points']
                    self.stage(kind+'_tail')
                    if kind == 'split':
                        self.primary_input(False)
            elif self.phase == 'split_tail' and elapsed >= 2:
                self.fixture('ready')
            elif self.phase == 'ready_tail':
                if elapsed >= .8 and self.primary:
                    self.primary_input(False)
                    self.report['duplicate_baseline'] = current
                if elapsed >= 2 and now-self.video_start >= 10:
                    self.report['checks']['no_duplicate_delivery_after_repeated_input'] = current == self.report['duplicate_baseline']
                    self.report['checks']['gameplay_camera_retained'] = self.pc.get_view_target() == self.hero
                    accepted = self.intake_log_lines()
                    self.report['checks']['accepted_native_intake_logged'] = all(any('batch='+str(b) in line for line in accepted) for b in BATCHES)
                    self.report['checks']['actual_niagara_observed'] = bool(self.report['niagara'])
                    self.require(self.report['checks']['no_duplicate_delivery_after_repeated_input'], 'Repeated input duplicated delivery')
                    self.require(all(self.report['checks'][k+'_score_delta'] > 0 for k in ('split', 'ready')), 'Actual food score was not credited')
                    self.require(self.report['checks']['accepted_native_intake_logged'], 'No actual accepted native intake log for both fixtures')
                    self.require(self.report['checks']['actual_niagara_observed'], 'No actual AutoMouth Niagara component observed')
                    self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        if valid(self.scheduler) and self.scheduler_tick is not None:
            try:
                self.scheduler.set_actor_tick_enabled(self.scheduler_tick)
            except Exception:
                self.report['scheduler_restore_error'] = traceback.format_exc()
        self.report['dirty_after'] = self.dirty_packages()
        self.report['editor_camera_unchanged'] = self.editor.get_level_viewport_camera_info() == self.original_camera
        if self.report['dirty_after'] != self.original_dirty:
            error = error or 'Package dirty state changed; inspect without saving'
        self.report['intake_log_lines'] = self.intake_log_lines()
        (self.output/'IntakeLog.txt').write_text('\n'.join(self.report['intake_log_lines'])+'\n', encoding='utf8')
        super().finish(error)


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('An owned auto-mouth check is already running; it was preserved')
    probe = FoodAutoMouthSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
