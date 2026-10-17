"""Item 7: uninterrupted held brush moves from self-care to real tongue cleaning.

Idle L_Mouth after root integration. Native F3 CoffeeDirt is the disclosed dirt
fixture before the input sequence. Brush is selected ONCE; C/self-care then LMB
remain ordinary native inputs. No tool reselection, care helper, mode or mask
assignment follows. A supported position-only fixture puts a real puddle within
reach before self-care starts. One standalone authority PIE, genuine 12 Hz video.
Cancel: builtins.mc_brush_selfcare_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import json
import math
import runpy
import time
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_SELFCARE_OUTPUT', ROOT/'Saved/BrushSelfcare/Capture')).resolve()
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_cataclysm_solo.py'),
    run_name='brush_selfcare_support', init_globals={'MC_CATACLYSM_MODE': 'jellies',
        'MC_CATACLYSM_OUTPUT': OUTPUT, 'MC_CATACLYSM_CAPTURE_HZ': 12})
Parent, prop, vec, valid, path = (support[n] for n in ('CataclysmSolo', 'prop', 'vec', 'valid', 'path'))
KEY = 'mc_brush_selfcare_solo'


class BrushSelfcareSolo(Parent):
    def __init__(self):
        super().__init__()
        self.fixture_patches = []
        self.mask_before = {}
        self.primary_started = self.self_clean_at = None
        self.self_target_seen = self.environment_target_seen = False
        self.tool_selections = 0
        self.selected_history = []
        self.report.update(mode='brush_selfcare', samples=[], checks={}, clips={}, frames=[], screenshots=[],
            purpose='Self-care completion returns the same held Brush to environmental cleaning without reselecting',
            fixture_tuning={}, tuning_reason='No timing, care rate, tool or gameplay tuning',
            clip_labels={'selfcare_then_brush': 'Самоочистка → уборка той же щёткой без переключения'},
            limitations=['Native F3 CoffeeDirt soils the hero and creates environmental dirt BEFORE self-care input starts. This is a disclosed setup fixture, not a care-progress helper.',
                'The observer is placed once on supported tongue within reach of a native dirty puddle before input begins; native velocity/movement mode are not assigned.',
                'Exactly one ServerSelect(Brush) call occurs before C/self-care and LMB. No later tool reselection or self-care disable call is issued.',
                'The same held LMB is preserved through native self-care completion into environment cleaning. A reward-menu interruption fails the proof instead of silently restoring input.',
                'Temporary review camera and genuine 12 Hz engine captures; shader/animation quality needs visual review.',
                'Only a short local authority solo check. No package saves, build/cook/package, peers, balance or whole-mouth cleanup.'])
        self.report['source_sha256'] = {source: hashlib.sha256((ROOT/source).read_bytes()).hexdigest()
            for source in ('Tools/Unreal/probe_brush_selfcare_solo.py',
                           'Source/MessControl/Private/MCToothCharacter.cpp',
                           'Source/MessControl/Private/MCInventoryComponent.cpp',
                           'Source/MessControl/Private/MCToothStatusComponent.cpp',
                           'Source/MessControl/Private/MCDevCommands.cpp')}

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_brush_selfcare.json').write_text(data, encoding='utf-8')

    def hero_state(self):
        state = prop(prop(self.hero, 'status'), 'state')
        return dict(coffee_left=int(prop(state, 'coffee_left')), selected=str(prop(self.hero.inventory, 'selected')),
            self_care=bool(prop(self.hero, 'self_care')), brushing=bool(prop(self.hero, 'brushing')),
            target=path(prop(self.hero, 'care_target')), primary_input_requested=self.primary,
            in_coffee=bool(prop(self.hero, 'in_coffee')), hero_cm=vec(self.hero.get_actor_location()))

    def sample(self, now):
        state = self.hero_state()
        self.selected_history.append(state['selected'])
        target = prop(self.hero, 'care_target')
        self.self_target_seen |= target == self.hero
        self.environment_target_seen |= target in self.fixture_patches
        entry = dict(game_seconds=now, phase=self.phase, hero=state,
            environment=[dict(path=path(p), wipe_sum=sum(int(v) for v in prop(p, 'wipe_mask')),
                care_left=int(prop(prop(prop(p, 'status'), 'state'), 'coffee_left')))
                for p in self.fixture_patches if valid(p)])
        self.report['samples'].append(entry)
        if self.primary_started is not None:
            self.require(prop(self.hero.inventory, 'selected') == u.MCToolSlot.BRUSH,
                         'Tool changed during the self-care to environment sequence')
            if state['coffee_left'] == 0 and not state['self_care'] and self.self_clean_at is None:
                self.self_clean_at = now
                self.report['self_clean_completion'] = entry

    def supported_foot(self, near):
        floor = self.floor(near)
        if floor is None:
            return None
        for offset in (u.Vector(34, 0, 0), u.Vector(-34, 0, 0), u.Vector(0, 34, 0), u.Vector(0, -34, 0)):
            edge = self.floor(floor+offset)
            if edge is None or abs(edge.z-floor.z) > 25.:
                return None
        return floor

    def prepare_sequence(self):
        self.tongue = self.actors(u.MCTongue)[0]
        self.report['before_native_dirt_fixture'] = self.hero_state()
        before = {path(p) for p in self.actors(u.MCMouthSurface)}
        self.pc.request_dev_action(u.MCDevAction.COFFEE_DIRT, -1)
        self.fixture_patches = [p for p in self.actors(u.MCMouthSurface)
            if not bool(prop(p, 'ulcer')) and path(p) not in before]
        self.require(self.fixture_patches, 'Native F3 CoffeeDirt created no tongue puddles')
        self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
        self.tool_selections += 1
        self.require(prop(self.hero.inventory, 'selected') == u.MCToolSlot.BRUSH, 'Initial Brush selection failed')
        self.require(self.hero_state()['coffee_left'] > 0, 'Native CoffeeDirt fixture did not soil the hero')
        self.report['dirt_fixture'] = dict(action='F3 CoffeeDirt', after=self.hero_state(),
            patch_paths=[path(p) for p in self.fixture_patches], timing='Before self-care/primary sequence')
        # Each native puddle's centre is inside its seeded footprint. Test four
        # supported approach headings without mutating care or target selection.
        for tile in self.fixture_patches:
            point = self.floor(tile.get_actor_location())
            if point is None:
                continue
            for angle in (0., math.pi/2., -math.pi/2., math.pi):
                foot = self.supported_foot(point-u.Vector(math.cos(angle), math.sin(angle), 0)*60.)
                if foot is None:
                    continue
                yaw = math.degrees(math.atan2(point.y-foot.y, point.x-foot.x))
                center = foot+u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height()+2.)
                self.hero.set_actor_location_and_rotation(center, u.Rotator(yaw=yaw), False, True)
                self.pc.set_control_rotation(u.Rotator(yaw=yaw))
                self.report['position_fixture'] = dict(foot_cm=vec(foot), yaw=yaw, patch=path(tile),
                    timing='Once before self-care/primary', fields_assigned='Actor position/rotation and control facing only')
                self.frame_camera(point+u.Vector(320, -440, 330), point+u.Vector(0, 0, 65))
                prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 68.)
                self.stage('settle_before_input')
                return
        self.require(False, 'No supported approach to a native coffee puddle')

    def begin_inputs(self, now):
        self.require(not self.hero_state()['in_coffee'], 'Water would block self-care; fixture did not reach an isolated dry test')
        self.mask_before = {path(p): [int(v) for v in prop(p, 'wipe_mask')]
            for p in self.fixture_patches if valid(p)}
        self.hero.set_self_care_input(True)
        self.require(prop(self.hero, 'self_care'), 'Normal SetSelfCareInput(true) did not activate self-care')
        self.primary_input(True)
        self.primary_started = now
        self.report['input_sequence'] = dict(start_game_seconds=now, tool_selection_calls=self.tool_selections,
            self_care_enable_calls=1, self_care_disable_calls=0, held_primary=True)
        self.stage('selfcare_then_environment')
        self.begin_clip('selfcare_then_brush', 12.)

    def finish_evidence(self):
        removed = {}
        cleared = 0
        for tile in self.fixture_patches:
            if not valid(tile):
                continue
            before = self.mask_before[path(tile)]
            after = [int(v) for v in prop(tile, 'wipe_mask')]
            removed[path(tile)] = sum(max(0, a-b) for a, b in zip(before, after))
            cleared += sum(a > 100 and b <= 63 for a, b in zip(before, after))
        self.require(self.self_target_seen and self.self_clean_at is not None,
                     'Native self-care did not complete and exit automatically')
        self.require(self.environment_target_seen and sum(removed.values()) > 16 and cleared > 0,
                     'Same held Brush did not erase actual contacted environmental dirt')
        self.require(self.tool_selections == 1 and not prop(self.hero, 'self_care'), 'Unexpected tool reselection or stuck self-care')
        self.report['checks'].update(native_selfcare_target_observed=True, native_selfcare_completed=True,
            selfcare_automatically_exits=True, brush_selected_continuously=True, no_tool_reselection=True,
            uninterrupted_held_primary_reaches_environment=True, actual_environment_cells_cleared=True)
        self.report['environment_checkpoint'] = dict(removed_bytes_by_patch=removed, wet_cells_cleared=cleared,
            native_target_seen=self.environment_target_seen, final_hero=self.hero_state())
        frames = self.report['clips'].get('selfcare_then_brush', [])
        self.require(len(frames) >= 12, 'Too few verified gameplay camera frames')
        for frame in self.report['frames']:
            data = Path(frame['file']).read_bytes()
            self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Capture is missing or not a genuine PNG')
            frame.update(sha256=hashlib.sha256(data).hexdigest(),
                width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
        self.finish()

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 120., 'Short self-care probe exceeded wall-time limit')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one standalone authority PIE')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()):
                    self.world = None
                    return
                self.stage('first_perk')
                return
            self.require(self.world in worlds and len(worlds) == 1, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'Expected live tooth pawn')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.pc.is_reward_menu_open():
                self.require(self.primary_started is None, 'Reward UI interrupted the held-input proof; input was not silently resumed')
                self.pc.choose_reward_perk(0)
                self.stage('perk_resolution')
                return
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.phase_at
            if self.phase == 'first_perk':
                self.require(elapsed < 25., 'First native perk menu did not become ready')
                return
            if self.phase == 'perk_resolution':
                self.prepare_sequence()
                return
            self.require(prop(self.hero, 'status').is_alive(), 'Observer died; native health was not changed')
            self.keep_camera()
            if now >= self.sample_at:
                self.sample_at = now+.05
                self.sample(now)
            if self.clip and now <= self.clip_until and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                camera = self.camera_evidence()
                if camera and self.pc.get_view_target() == self.camera and camera['location_error_cm'] < 12. and camera['rotation_error_degrees'] < 5.:
                    self.screenshot('%05d.png' % len(self.report['frames']), True)
            if self.phase == 'settle_before_input' and elapsed >= .5:
                self.begin_inputs(now)
            elif self.phase == 'selfcare_then_environment':
                self.require(elapsed < 15., 'Self-care did not yield to environmental cleaning within the short native check')
                if self.self_clean_at is not None and now-self.self_clean_at >= 3.5:
                    self.primary_input(False)
                    self.clip = None
                    self.stage('capture_flush')
            elif self.phase == 'capture_flush' and elapsed >= .8:
                self.finish_evidence()
        except Exception:
            self.finish(traceback.format_exc())


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve existing self-care probe')
    probe = BrushSelfcareSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
