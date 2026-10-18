"""Owned solo video: two large painters, Large -> Medium -> Small, real pops.

Run only in an idle L_Mouth after the matching Editor build. Inherits the real
F3/input/capture harness; no packages are saved and no health/AI/clocks are tuned.
The unrelated day scheduler is paused only in this owned PIE and restored.
The position-only melee observer stops following when a lethal warning begins,
so the actual coffee pop pushes the freely moving pawn. No network clients.
MC_JELLY_SPLIT_OUTPUT and MC_JELLY_SPLIT_CAPTURE_HZ are optional E: overrides.
Cancel: builtins.mc_jelly_split_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import math
import runpy
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_JELLY_SPLIT_OUTPUT', ROOT/'Saved/Feedback_20261018/JellySplit')).resolve()
if OUTPUT.drive.upper() != 'E:':
    raise RuntimeError('Jelly evidence must be written to E:')
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_jelly_paint_solo.py'),
    run_name='jelly_split_support', init_globals={'MC_PAINT_OUTPUT': OUTPUT,
        'MC_PAINT_CAPTURE_HZ': globals().get('MC_JELLY_SPLIT_CAPTURE_HZ', 8)})
Paint, Cataclysm = support['JellyPaintSolo'], support['Parent']
prop, vec, path, valid, enum_name = (support[k] for k in ('prop', 'vec', 'path', 'valid', 'enum_name'))
wave_support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_wave_observer_support.py'), run_name='jelly_wave_observer_support')
KEY = 'mc_jelly_split_solo'


class JellySplitSolo(Paint):
    def __init__(self):
        super().__init__()
        self.tier = 'LARGE'
        self.pre_kill_paths = set()
        self.pop_before = None
        self.pop_snapshot = None
        self.wave_observer_held = False
        self.next_melee_press_at = self.melee_release_at = -100.
        self.report.update(mode='jelly_split', checks={}, clips={}, frames=[], samples=[],
            tier_checkpoints=[], game_time_limit_seconds=120,
            tuning_reason='Jelly health, wave, rupture timing and native paint remain unchanged; only the unrelated owned-PIE day scheduler is paused.',
            scope='One standalone authority PIE; real F3, native three-tier painter and normal Pickaxe input',
            limitations=[
                'Position-only melee observer follows a moving native jelly until lethal warning, then remains free for the push.',
                'Combat uses ordinary repeated LMB press/release edges every .90 s; the native combo still decides swing acceptance, recovery and damage.',
                'The observer holds a real fixed tooth with normal RMB during the opening wave, then releases before jelly input.',
                'The unrelated day scheduler is temporarily paused; actual Cataclysm/hero/jelly/paint ticks and health remain native.',
                'This PIE checks the native initial pair, then kills one lineage through Large -> Medium -> Small. The focused native regression exercises one 1 -> 3 -> 9 branch; a full simultaneous 2 -> 6 -> 18 population is not runtime-tested here.',
                'No network clients, package saves, health edits, clock jumps or broad test suite.',
                'Camera frames establish visible behavior only after human/lead visual review; GPU cost and final balance are unmeasured.'])
        for name in ('Tools/Unreal/probe_jelly_split_solo.py', 'Tools/Unreal/probe_wave_observer_support.py',
                     'Source/MessControl/Private/MCCataclysmJelly.cpp'):
            self.report['source_sha256'][name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()

    def jelly_metrics(self, jelly):
        result = super().jelly_metrics(jelly)
        result.update(size=enum_name(prop(jelly, 'jelly_size')),
            children_spawned=int(prop(jelly, 'children_spawned')),
            players_pushed=int(prop(jelly, 'rupture_players_pushed')),
            paint_floor_rejections=int(prop(jelly, 'paint_floor_rejections')))
        return result

    def approach_jelly(self, now):
        if now < self.next_follow_at:
            return
        self.next_follow_at = now+.10
        center = self.kill_target.get_actor_location()
        radius = float(prop(self.kill_target, 'settings').body_radius)
        heading = self.kill_target.get_actor_forward_vector()
        base = math.atan2(heading.y, heading.x)+math.pi
        for turn in (0., math.pi/2., -math.pi/2., math.pi):
            direction = u.Vector(math.cos(base+turn), math.sin(base+turn), 0)
            foot = self.supported_foot(center+direction*(radius+50.))
            if foot is None:
                continue
            yaw = math.degrees(math.atan2(center.y-foot.y, center.x-foot.x))
            self.place_observer(foot, yaw, 'Position-only melee follow; released for native rupture knockback')
            return
        self.require(False, 'No supported melee approach for '+self.tier)

    def begin_tier(self, jelly):
        self.tracked_jelly = jelly
        self.tier = enum_name(prop(jelly, 'jelly_size'))
        self.trail_before = dict(jelly=self.jelly_metrics(jelly), paint=self.paint_metrics())
        self.trail_history = []
        self.camera_for_jelly(jelly, True)
        self.begin_clip('jelly_'+self.tier.lower()+'_trail', 6.2)
        self.stage('paint_trail')

    def update_jellies(self, now, elapsed):
        if self.phase == 'wait_branch':
            self.require(elapsed < 45., 'Natural wave did not reach the jelly branch')
            if enum_name(prop(self.event, 'phase')) != 'JELLY_FIGHT':
                return
            alive = [j for j in prop(self.event, 'jellies') if valid(j) and float(prop(j, 'health')) > 0]
            self.require(len(alive) == 2 and all(enum_name(prop(j, 'jelly_size')) == 'LARGE' for j in alive),
                         'Native event must begin with exactly two large jellies')
            self.report['checks']['exactly_two_large_sources'] = True
            if self.wave_observer_held:
                wave_support['release_wave_observer'](self)
                self.wave_observer_held = False
            self.begin_tier(alive[0])
        elif self.phase == 'paint_trail':
            self.camera_for_jelly(self.tracked_jelly, True)
            if elapsed < 6.:
                return
            after = self.jelly_metrics(self.tracked_jelly)
            before = self.trail_before['jelly']
            self.require(after['wander_distance_cm']-before['wander_distance_cm'] > 70., self.tier+' did not wander')
            self.require(after['paint_stroke_requests'] > before['paint_stroke_requests'], self.tier+' did not request a trail')
            probes = []
            stride = max(1, len(self.trail_history)//32)
            for entry in self.trail_history[::stride]:
                point = u.Vector(*entry['floor_cm'])
                probes.append(dict(point_cm=entry['floor_cm'], wet=bool(self.event.is_paint_wet_at(point))))
            self.require(probes and sum(p['wet'] for p in probes)/len(probes) >= .7,
                         self.tier+' observed path lacks real persistent wet coverage')
            self.report['tier_checkpoints'].append(dict(size=self.tier, trail_before=before,
                trail_after=after, actual_path_probes=probes, paint=self.paint_metrics()))
            self.report['checks'][self.tier.lower()+'_leaves_real_trail'] = True
            self.kill_target = self.tracked_jelly
            self.pre_kill_paths = {path(j) for j in prop(self.event, 'jellies') if valid(j)}
            self.pop_before = dict(paint=self.paint_metrics(),
                body_state=enum_name(self.hero.tooth_physics.get_body_state()),
                hero_cm=vec(self.hero.get_actor_location()))
            self.pop_snapshot = None
            self.last_jelly_health = float(prop(self.kill_target, 'health'))
            self.health_changes = []
            self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
            self.primary_input(True)
            self.melee_release_at = now+.55
            self.next_melee_press_at = now+.90
            self.report.setdefault('real_melee_press_edges', []).append(dict(time=now, size=self.tier, held=True))
            self.begin_clip('jelly_'+self.tier.lower()+'_split_pop', 22.)
            self.stage('split_kill')
        elif self.phase == 'split_kill':
            self.require(elapsed < 21. and valid(self.kill_target), 'Ordinary Pickaxe could not kill '+self.tier)
            self.camera_for_jelly(self.kill_target)
            health = float(prop(self.kill_target, 'health'))
            if health < self.last_jelly_health-.001:
                self.health_changes.append(dict(time=now, health=health))
            self.last_jelly_health = health
            if bool(prop(self.kill_target, 'pending_rupture')):
                self.primary_input(False)
                self.pop_before['body_at_warning'] = enum_name(self.hero.tooth_physics.get_body_state())
                self.pop_before['hero_at_warning_cm'] = vec(self.hero.get_actor_location())
                self.stage('split_pop')
            else:
                self.approach_jelly(now)
                # Current combat combos consume press edges; only food chopping
                # repeats while held. Release after the native contact window,
                # then make the next normal press beyond the default .80 s max
                # recovery. No swing/damage helper or private cooldown is used.
                if self.primary and now >= self.melee_release_at:
                    self.primary_input(False)
                    self.report['real_melee_press_edges'].append(dict(time=now, size=self.tier, held=False))
                if now >= self.next_melee_press_at:
                    self.primary_input(True)
                    self.melee_release_at = now+.55
                    self.next_melee_press_at = now+.90
                    self.report['real_melee_press_edges'].append(dict(time=now, size=self.tier, held=True))
        elif self.phase == 'split_pop':
            self.require(elapsed < 3., 'Native rupture did not complete')
            if valid(self.kill_target) and float(prop(self.kill_target, 'ruptured_at')) >= 0 and self.pop_snapshot is None:
                expected = 0 if self.tier == 'SMALL' else 3
                children = [j for j in prop(self.event, 'jellies') if valid(j) and path(j) not in self.pre_kill_paths]
                metric = self.jelly_metrics(self.kill_target)
                self.require(metric['rupture_count'] == 1 and metric['children_spawned'] == expected and len(children) == expected,
                             self.tier+' pop did not produce the exact child count')
                self.require(metric['players_pushed'] > 0, self.tier+' pop did not push the nearby observer')
                body = enum_name(self.hero.tooth_physics.get_body_state())
                self.require(body == 'STANDING' and self.pop_before['body_at_warning'] == 'STANDING',
                             'The pop started or inherited a non-standing body state')
                self.pop_snapshot = dict(jelly=metric, paint=self.paint_metrics(), body_after=body,
                    hero_after_cm=vec(self.hero.get_actor_location()), children=[path(j) for j in children])
                self.next_tier_jelly = children[0] if children else None
                self.report['tier_checkpoints'][-1].update(pop_before=self.pop_before, pop=self.pop_snapshot,
                    real_pickaxe_health_changes=self.health_changes)
                self.report['checks'][self.tier.lower()+'_splits_and_pushes_without_stun'] = True
            if elapsed >= 1.6:
                self.require(self.pop_snapshot is not None, 'Rupture was missed before actor retirement')
                if self.tier != 'SMALL':
                    self.begin_tier(self.next_tier_jelly)
                else:
                    self.clip = None
                    self.stage('split_capture_flush')
        elif self.phase == 'split_capture_flush' and elapsed >= 1.:
            required = ['jelly_'+tier+'_'+part for tier in ('large', 'medium', 'small') for part in ('trail', 'split_pop')]
            if self.capture_hz:
                self.require(all(len(self.report['clips'].get(name, [])) >= 8 for name in required), 'A required native clip is incomplete')
            for entry in self.report['frames']:
                data = Path(entry['file']).read_bytes()
                self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Missing/invalid engine screenshot')
                entry.update(sha256=hashlib.sha256(data).hexdigest(), width=int.from_bytes(data[16:20], 'big'),
                    height=int.from_bytes(data[20:24], 'big'))
            self.finish()

    def tick(self, delta):
        if self.done:
            return
        try:
            Cataclysm.tick(self, delta)
            if not self.done and self.phase == 'wait_branch' and not self.wave_observer_held:
                wave_support['brace_observer_for_wave'](self)
                self.wave_observer_held = True
            if not self.done and self.phase == 'wait_branch' and self.episode_started_at is None:
                self.episode_started_at = float(u.GameplayStatics.get_time_seconds(self.world))
                self.scheduler = prop(u.GameplayStatics.get_game_state(self.world), 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_tick = self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
            if self.world is not None:
                self.require(float(u.GameplayStatics.get_time_seconds(self.world)) < 120., 'Jelly split check exceeded 120 game seconds')
        except Exception:
            self.finish(traceback.format_exc())


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('A jelly split probe is already running')
    probe = JellySplitSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
