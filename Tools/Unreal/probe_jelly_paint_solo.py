"""One owned solo proof of wandering gel, connected paint, balloon rupture and brush.

Run in idle L_Mouth after the native Editor integration/material save. The real
F3 CataclysmJellies path, game clocks, jelly ticks, default 360 HP, tool damage
and ordinary held LMB remain active. The initial wave advances naturally.
Only observer positions/camera and the unrelated single-day scheduler are
fixtures. No jelly/AI freeze, native damage/care helper or gameplay tuning.
Cancellation: builtins.mc_jelly_paint_solo.finish('Stopped by operator').
Optional globals: MC_PAINT_OUTPUT and MC_PAINT_CAPTURE_HZ (default 8, up to 12).
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

KEY = 'mc_jelly_paint_solo'
ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_PAINT_OUTPUT',
    Path(u.Paths.project_saved_dir()) / 'Checks/JellyPaintSolo')).resolve()
support = runpy.run_path(str(ROOT / 'Tools/Unreal/probe_cataclysm_solo.py'),
    run_name='jelly_paint_support', init_globals={'MC_CATACLYSM_MODE': 'jellies',
        'MC_CATACLYSM_OUTPUT': OUTPUT,
        'MC_CATACLYSM_CAPTURE_HZ': globals().get('MC_PAINT_CAPTURE_HZ', 8)})
Parent, prop, vec, valid, path, enum_name = (
    support[name] for name in ('CataclysmSolo', 'prop', 'vec', 'valid', 'path', 'enum_name'))


def xy_distance(a, b):
    return math.hypot(a.x-b.x, a.y-b.y)


def source_function(source, name):
    start = source.index('AMCCataclysmJelly::' + name + '(')
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth and end < len(source):
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if depth:
        raise RuntimeError('Could not bound current native source function ' + name)
    return source[start:end]


class JellyPaintSolo(Parent):
    def __init__(self):
        super().__init__()
        self.output = self.output.with_name(self.output.name.replace('_jellies_', '_paint_'))
        self.scheduler = None
        self.scheduler_tick = None
        self.episode_started_at = None
        self.tracked_jelly = None
        self.trail_before = None
        self.trail_history = []
        self.next_follow_at = 0.
        self.rupture_snapshot = None
        self.death_before = None
        self.brush_point = None
        self.brush_wipe_before_cells = []
        self.brush_paint_metrics_before = None
        self.brush_contact_seen = False
        self.report.update(mode='paint', clips={}, frames=[], screenshots=[], samples=[], checks={},
            run_directory=str(self.output), game_time_limit_seconds=60,
            scope='One owned standalone authority PIE; real F3 CataclysmJellies, native wandering/paint and normal Pickaxe/brush LMB',
            fixture_tuning={}, tuning_reason='No Cataclysm, jelly, health, damage, AI or clock tuning',
            clip_labels={'jelly_paint_trail': 'Желе хаотично рисует очищаемый след',
                         'jelly_paint_rupture': 'Кирка, раздувание и разрыв шара с краской',
                         'jelly_paint_brush': 'Настоящая щётка стирает участок следа'},
            limitations=[
                'The initial F3 wave and 12-second wandering observation advance naturally; no jelly tick, steering, health, attack damage or game clock is assigned.',
                'The unrelated single-day scheduler is paused in this owned PIE and restored; the Cataclysm event/liquid/jellies and hero continue ticking.',
                'After the wandering observation, the hero is a disclosed position-only fixture following one moving jelly at approximately 88 cm on verified supported floor. Normal held Pickaxe LMB deals all damage.',
                'A temporary review camera follows the action. The hero is positioned beside an actually wet paint cell for normal held brush LMB; the surviving jelly keeps painting.',
                'Source policy plus actual empty Target, goal changes and travel corroborate independent wandering. Player proximity is not treated as a reliable pursuit test.',
                'The native exact-commit death counters identify burst-added wet cells; ordinary global coverage changes alone are not attributed to the rupture.',
                'Only one jelly is killed. Full encounter cleanup, balance, network behavior, GPU cost and final visual quality remain separate checks.',
                'No editor map changes, package saves, build/cook/package, native damage/care helpers or broad test suite.'
            ])
        for source in ('Tools/Unreal/probe_jelly_paint_solo.py',
                       'Source/MessControl/Public/MCCataclysmJelly.h',
                       'Source/MessControl/Public/MCCataclysmEvent.h',
                       'Source/MessControl/Private/MCCataclysmEvent.cpp',
                       'Source/MessControl/Public/MCMouthSurface.h',
                       'Source/MessControl/Private/MCMouthLiquid.cpp',
                       'Source/MessControl/Private/MCCoffeeWipe.cpp'):
            self.report['source_sha256'][source] = hashlib.sha256((ROOT/source).read_bytes()).hexdigest()
        source = (ROOT/'Source/MessControl/Private/MCCataclysmJelly.cpp').read_text(encoding='utf-8')
        movement = '\n'.join(source_function(source, name) for name in
                            ('ResetWandering', 'PickWanderGoal', 'WanderOnTongue', 'Tick'))
        damage = source_function(source, 'ReceiveToolDamage')
        self.source_policy = not any(token in movement for token in
            ('RetargetJelly(', 'RandomGameplayInterestPoint(', 'Target->', 'TActorIterator<AMCToothCharacter>')) and 'Target=nullptr;' in damage
        self.report['source_policy'] = dict(no_player_selection_in_movement_functions=self.source_policy,
            inspected_functions=['ResetWandering', 'PickWanderGoal', 'WanderOnTongue', 'Tick', 'ReceiveToolDamage'],
            basis='Current hashed source plus runtime travel/goal/Target telemetry; no inference from proximity correlation')

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_paint.json').write_text(data, encoding='utf-8')

    def paint_metrics(self):
        return {name: float(prop(self.event, name)) if name == 'paint_distance_cm' else int(prop(self.event, name))
                for name in ('paint_tiles_created', 'paint_tile_count', 'paint_strokes_accepted',
                             'paint_coverage_cells', 'paint_wet_cells', 'paint_distance_cm',
                             'death_paint_bursts', 'death_paint_coverage_added_cells', 'death_paint_teeth_touched')}

    def jelly_metrics(self, jelly):
        return dict(path=path(jelly), position_cm=vec(jelly.get_actor_location()),
            target=path(prop(jelly, 'target')), actor_tick_enabled=bool(jelly.is_actor_tick_enabled()),
            health=float(prop(jelly, 'health')), pending=bool(prop(jelly, 'pending_rupture')),
            death_started_at=float(prop(jelly, 'death_started_at')),
            ruptured_at=float(prop(jelly, 'ruptured_at')), rupture_count=int(prop(jelly, 'rupture_count')),
            wander_goal_cm=vec(prop(jelly, 'wander_goal')), wander_distance_cm=float(prop(jelly, 'wander_distance')),
            wander_goal_changes=int(prop(jelly, 'wander_goal_changes')),
            paint_stroke_requests=int(prop(jelly, 'paint_stroke_requests')),
            trail_discontinuities=int(prop(jelly, 'trail_discontinuities')))

    def sample(self, now):
        super().sample(now)
        if not valid(self.event):
            return
        entry = self.report['samples'][-1]
        entry['paint'] = self.paint_metrics()
        entry['jelly_diagnostics'] = [self.jelly_metrics(j) for j in prop(self.event, 'jellies') if valid(j)]
        if self.phase == 'paint_trail' and valid(self.tracked_jelly):
            floor = self.floor(self.tracked_jelly.get_actor_location())
            self.require(floor is not None, 'Wandering gel left the supported tongue')
            self.trail_history.append(dict(time=now, floor_cm=vec(floor),
                diagnostics=self.jelly_metrics(self.tracked_jelly)))
        if self.phase == 'paint_brush' and valid(self.brush_target):
            # The hero exposes the authoritative care target; the component's
            # replicated implementation fields are intentionally Python-protected.
            target = prop(self.hero, 'care_target')
            self.brush_contact_seen = self.brush_contact_seen or target == self.brush_target
            entry['brush_contact'] = dict(target=path(target), intended_tile=path(self.brush_target),
                native_primary_brushing=bool(prop(self.hero, 'brushing')),
                target_basis='Authoritative hero CareTarget set by AdvanceCare before BrushLiquid')

    def camera_for_jelly(self, jelly, wider=False):
        center = jelly.get_actor_location()
        offset = u.Vector(420, -590, 430) if wider else u.Vector(330, -440, 330)
        self.frame_camera(center+offset, center-u.Vector(0, 0, 35))
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 82. if wider else 75.)

    def supported_foot(self, near):
        foot = self.floor(near)
        if foot is None:
            return None
        for offset in (u.Vector(34, 0, 0), u.Vector(-34, 0, 0), u.Vector(0, 34, 0), u.Vector(0, -34, 0)):
            edge = self.floor(foot+offset)
            if edge is None or abs(edge.z-foot.z) > 25.:
                return None
        return foot

    def place_observer(self, foot, yaw, reason):
        # Set only the observer transform/control facing. Native movement mode,
        # velocity, footing, jelly motion and held-input processing remain live.
        center = foot+u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height()+2.)
        self.hero.set_actor_location_and_rotation(center, u.Rotator(yaw=yaw), False, True)
        self.pc.set_control_rotation(u.Rotator(yaw=yaw))
        self.report.setdefault('position_fixtures', []).append(dict(
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            pawn=path(self.hero), foot_cm=vec(foot), yaw=yaw, reason=reason,
            native_movement_mode=str(prop(self.hero.get_movement_component(), 'movement_mode'))))

    def approach_jelly(self, now):
        if now < self.next_follow_at:
            return
        self.next_follow_at = now + .08
        center = self.kill_target.get_actor_location()
        forward = self.kill_target.get_actor_forward_vector()
        base_angle = math.atan2(forward.y, forward.x) + math.pi
        for offset in (0., math.pi/2., -math.pi/2., math.pi):
            direction = u.Vector(math.cos(base_angle+offset), math.sin(base_angle+offset), 0)
            foot = self.supported_foot(center+direction*88.)
            if foot is None:
                continue
            yaw = math.degrees(math.atan2(center.y-foot.y, center.x-foot.x))
            self.place_observer(foot, yaw, 'Position-only follow for ordinary held Pickaxe input')
            self.report['position_fixtures'][-1].update(
                jelly=path(self.kill_target), jelly_distance_xy_cm=xy_distance(center, foot))
            return
        self.require(False, 'No supported 88 cm melee approach to the moving native jelly')

    def begin_paint_kill(self):
        self.kill_target = self.tracked_jelly
        self.require(valid(self.kill_target) and float(prop(self.kill_target, 'health')) > 0., 'Observed jelly is no longer live')
        self.last_jelly_health = float(prop(self.kill_target, 'health'))
        self.death_before = self.paint_metrics()
        self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
        self.primary_input(True)
        self.begin_clip('jelly_paint_rupture', 23.)
        self.stage('paint_kill')

    def inspect_trail(self, now):
        after = self.jelly_metrics(self.tracked_jelly)
        paint_after = self.paint_metrics()
        before, before_paint = self.trail_before['jelly'], self.trail_before['paint']
        observed = [h['diagnostics'] for h in self.trail_history]
        self.require(len(observed) >= 30 and all(h['target'] is None and h['actor_tick_enabled'] for h in observed),
                     'Observed wandering acquired a Target or stopped native ticking')
        self.require(self.source_policy, 'Current source policy still selects a player for jelly movement')
        self.require(after['wander_distance_cm']-before['wander_distance_cm'] > 250.
                     and after['wander_goal_changes']-before['wander_goal_changes'] >= 2,
                     'Twelve natural seconds did not produce native travel and independent goal changes')
        self.require(after['paint_stroke_requests'] > before['paint_stroke_requests']
                     and paint_after['paint_strokes_accepted'] > before_paint['paint_strokes_accepted']
                     and paint_after['paint_coverage_cells'] > before_paint['paint_coverage_cells'],
                     'Native wandering did not add accepted persistent paint coverage')
        self.require(0 < paint_after['paint_tile_count'] <= int(prop(self.event, 'max_paint_tiles')),
                     'Actual paint tiles exceeded their finite budget or no tile was created')
        probes = []
        history = [h for h in self.trail_history if self.phase_at+.4 < h['time'] < now-.4]
        # Read actual wet coverage at midpoints of short observed movements,
        # not just at landing dots. Cap this read-only diagnostic at 64 probes.
        stride = max(1, (len(history)+63)//64)
        for index in range(1, len(history), stride):
            a, b = (u.Vector(*h['floor_cm']) for h in (history[index-1], history[index]))
            distance = xy_distance(a, b)
            if distance < 1. or distance > 30.:
                continue
            midpoint = (a+b)*.5
            probes.append(dict(point_cm=vec(midpoint), segment_length_cm=distance,
                wet=bool(self.event.is_paint_wet_at(midpoint))))
        self.require(len(probes) >= 10 and sum(p['wet'] for p in probes)/len(probes) >= .80,
                     'Actual wet coverage does not connect the observed short movement path')
        self.report['trail_checkpoint'] = dict(before=self.trail_before, after=dict(jelly=after, paint=paint_after),
            observed_seconds=now-self.phase_at, wet_midpoints=probes,
            wet_midpoint_ratio=sum(p['wet'] for p in probes)/len(probes))
        self.report['checks'].update(source_and_runtime_show_independent_wandering=True,
            real_travel_adds_bounded_paint_tiles=True, observed_path_has_connected_wet_coverage=True)

    def begin_paint_brush(self):
        self.primary_input(False)
        live = [j for j in prop(self.event, 'jellies') if valid(j) and float(prop(j, 'health')) > 0.]
        self.require(live, 'The remaining native jelly unexpectedly died')
        candidates = []
        for tile in self.actors(u.MCMouthSurface):
            if tile.get_owner() != self.event or not bool(prop(tile, 'paint_tile')):
                continue
            mask = list(prop(tile, 'wipe_mask'))
            size = int(math.sqrt(len(mask)))
            if size*size != len(mask) or not size:
                continue
            half = float(prop(tile, 'liquid_half_size'))
            pose = tile.get_actor_transform()
            for index, value in enumerate(mask):
                if int(value) <= 180:
                    continue
                point = pose.transform_location(u.Vector(((index%size+.5)/size-.5)*2*half,
                    ((index//size+.5)/size-.5)*2*half, 0))
                separation = min(xy_distance(point, jelly.get_actor_location()) for jelly in live)
                candidates.append((separation, tile, point))
        candidates.sort(key=lambda item: item[0], reverse=True)
        for separation, tile, near in candidates[:40]:
            point = self.floor(near)
            if point is None or not tile.is_paint_wet_at(point):
                continue
            for angle in (0., math.pi/2., -math.pi/2., math.pi):
                foot = self.supported_foot(point-u.Vector(math.cos(angle), math.sin(angle), 0)*60.)
                if foot is None:
                    continue
                self.brush_target, self.brush_point = tile, point
                self.brush_before = int(prop(prop(tile, 'status'), 'state').coffee_left)
                self.brush_wipe_before_cells = [int(value) for value in prop(tile, 'wipe_mask')]
                self.brush_paint_metrics_before = self.paint_metrics()
                yaw = math.degrees(math.atan2(point.y-foot.y, point.x-foot.x))
                self.place_observer(foot, yaw, 'Supported approach to an actual occupied paint-mask cell')
                self.report['position_fixtures'][-1].update(
                    paint_tile=path(tile), paint_point_cm=vec(point), surviving_jelly_distance_xy_cm=separation)
                self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
                self.frame_camera(point+u.Vector(240, -340, 285), point+u.Vector(0, 0, 20))
                prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 73.)
                self.primary_input(True)
                self.begin_clip('jelly_paint_brush', 4.6)
                self.stage('paint_brush')
                return
        self.require(False, 'No supported approach to a real wet paint cell for ordinary brushing')

    def finish_captures(self):
        missing = [f['file'] for f in self.report['frames'] if not Path(f['file']).is_file()]
        self.require(not missing, 'Genuine engine capture frames missing: ' + str(missing[:3]))
        if self.capture_hz:
            required = ('jelly_paint_trail', 'jelly_paint_rupture', 'jelly_paint_brush')
            self.require(all(len(self.report['clips'].get(name, [])) >= 8 for name in required),
                         'A required paint clip has too few verified camera frames')
        for entry in self.report['frames']:
            frame = Path(entry['file']).read_bytes()
            self.require(frame[:8] == b'\x89PNG\r\n\x1a\n', 'Engine screenshot is not a PNG')
            entry.update(sha256=hashlib.sha256(frame).hexdigest(),
                width=int.from_bytes(frame[16:20], 'big'), height=int.from_bytes(frame[20:24], 'big'))
        self.report['observed_event_phases'] = self.observed_phases
        self.report['manual_checkpoints'] = [
            'Inspect the real trail/balloon/brush clips in gameplay lighting; telemetry does not establish visual quality.',
            'Finish every event paint tile and new player/tooth coating after killing the remaining jelly; completion-to-zero is outside this short probe.',
            'Assess default difficulty without the disclosed position-following observer fixture.']
        self.finish()

    def update_jellies(self, now, elapsed):
        if self.phase == 'wait_branch':
            self.require(elapsed < 45., 'Natural first wave did not enter jelly branch')
            if enum_name(prop(self.event, 'phase')) != 'JELLY_FIGHT':
                return
            jellies = [j for j in prop(self.event, 'jellies') if valid(j)]
            self.require(not prop(self.event, 'rafts') and len(jellies) >= 2, 'Native jelly branch lacks its pair or mixed in rafts')
            healths = [float(prop(j, 'health')) for j in jellies]
            self.require(all(abs(h-360.) < .001 for h in healths), 'Jelly default 360 HP was changed')
            self.tracked_jelly = jellies[0]
            self.report['native_jelly_spawn_health'] = healths
            self.report['checks']['natural_real_f3_jelly_branch_with_default_health'] = True
            self.trail_before = dict(jelly=self.jelly_metrics(self.tracked_jelly), paint=self.paint_metrics())
            self.camera_for_jelly(self.tracked_jelly, True)
            self.begin_clip('jelly_paint_trail', 12.1)
            self.stage('paint_trail')
        elif self.phase == 'paint_trail':
            self.require(valid(self.tracked_jelly), 'Observed jelly retired before the untouched wandering interval ended')
            self.camera_for_jelly(self.tracked_jelly, True)
            if elapsed >= 12.:
                self.inspect_trail(now)
                self.begin_paint_kill()
        elif self.phase == 'paint_kill':
            self.require(elapsed < 20. and valid(self.kill_target), 'Normal Pickaxe input did not kill the moving native jelly in time')
            health = float(prop(self.kill_target, 'health'))
            if health < self.last_jelly_health-.001:
                self.health_changes.append(dict(time=now, before=self.last_jelly_health, after=health, primary=self.primary,
                    hero_distance_xy_cm=xy_distance(self.hero.get_actor_location(), self.kill_target.get_actor_location())))
            self.last_jelly_health = health
            self.camera_for_jelly(self.kill_target)
            if bool(prop(self.kill_target, 'pending_rupture')):
                self.primary_input(False)
                self.require(health <= 0. and int(prop(self.event, 'jellies_left')) > 0 and self.health_changes,
                             'Real lethal warning failed to retain the native encounter source')
                self.report['pickaxe_health_changes'] = self.health_changes
                self.report['checks']['ordinary_pickaxe_input_starts_native_balloon_warning'] = True
                self.begin_clip('jelly_paint_rupture', 2.2)
                self.stage('paint_rupture')
            else:
                self.approach_jelly(now)
        elif self.phase == 'paint_rupture':
            self.require(elapsed < 3., 'Native balloon warning/rupture never completed')
            if valid(self.kill_target):
                self.camera_for_jelly(self.kill_target)
                if float(prop(self.kill_target, 'ruptured_at')) >= 0. and self.rupture_snapshot is None:
                    self.rupture_snapshot = dict(time=now, jelly=self.jelly_metrics(self.kill_target), paint=self.paint_metrics())
                    self.require(self.rupture_snapshot['jelly']['rupture_count'] == 1, 'Jelly rupture was not committed exactly once')
                    self.require(self.rupture_snapshot['jelly']['ruptured_at']-self.rupture_snapshot['jelly']['death_started_at'] >= .59,
                                 'Native rupture skipped its balloon warning interval')
                    self.require(self.rupture_snapshot['paint']['death_paint_bursts']-self.death_before['death_paint_bursts'] == 1,
                                 'Native event did not record exactly one balloon-paint commit')
                    self.require(self.rupture_snapshot['paint']['death_paint_coverage_added_cells'] > self.death_before['death_paint_coverage_added_cells'],
                                 'Native balloon commit added no persistent wet paint cells')
            if elapsed >= 2.:
                self.require(self.rupture_snapshot is not None, 'Missed the native rupture before its cosmetic retirement')
                self.require(not valid(self.kill_target), 'Native jelly cosmetic lifetime did not retire after the committed rupture')
                paint = self.paint_metrics()
                self.require(paint['death_paint_bursts'] == self.rupture_snapshot['paint']['death_paint_bursts'],
                             'Retirement repeated the native balloon-paint commit')
                self.require(paint['paint_wet_cells'] > 0, 'Paint disappeared with the retired jelly')
                self.report['rupture_checkpoint'] = dict(before=self.death_before, committed=self.rupture_snapshot,
                    after_cosmetic_retirement=paint, actor_still_valid=valid(self.kill_target))
                self.report['checks'].update(balloon_rupture_commits_once_with_persistent_deposit=True,
                    paint_survives_cosmetic_retirement=True)
                self.begin_paint_brush()
        elif self.phase == 'paint_brush' and elapsed >= 4.5:
            self.primary_input(False)
            self.require(valid(self.brush_target), 'Selected persistent paint tile retired while brushing')
            after = [int(value) for value in prop(self.brush_target, 'wipe_mask')]
            before = self.brush_wipe_before_cells
            reduced = sum(max(0, a-b) for a, b in zip(before, after))
            cleared = sum(a > 100 and b <= 63 for a, b in zip(before, after))
            self.require(len(after) == len(before) and reduced > 16 and cleared >= 1 and self.brush_contact_seen,
                         'Ordinary held brush did not erase actual wet cells on its contacted paint tile')
            self.report['brush_checkpoint'] = dict(tile=path(self.brush_target), paint_point_cm=vec(self.brush_point),
                care_before=self.brush_before, care_after=int(prop(prop(self.brush_target, 'status'), 'state').coffee_left),
                fixed_cell_wipe_byte_reduction=reduced, previously_wet_cells_now_dry=cleared,
                wipe_sum_before=sum(before), wipe_sum_after=sum(after), actual_brush_contact_seen=self.brush_contact_seen,
                event_paint_metrics_before=self.brush_paint_metrics_before,
                deposit_observation='Public event coverage counters; the private tile deposit mask is not read. Fixed-cell brush proof uses the public live wipe mask.',
                surviving_native_jellies=sum(valid(j) and float(prop(j, 'health')) > 0. for j in prop(self.event, 'jellies')),
                paint=self.paint_metrics())
            self.report['checks']['ordinary_brush_erases_persistent_paint_cells'] = True
            self.stage('paint_capture_flush')
        elif self.phase == 'paint_capture_flush' and elapsed >= .9:
            self.finish_captures()

    def tick(self, delta):
        if self.done:
            return
        try:
            if self.world is not None:
                now = float(u.GameplayStatics.get_time_seconds(self.world))
                self.require(now < 60., 'Focused paint check exceeded 60 game seconds')
            super().tick(delta)
            if not self.done and self.phase == 'wait_branch' and self.episode_started_at is None:
                self.episode_started_at = float(u.GameplayStatics.get_time_seconds(self.world))
                self.scheduler = prop(u.GameplayStatics.get_game_state(self.world), 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_tick = self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
                    self.report['scheduler_fixture'] = dict(actor=path(self.scheduler), original_tick=self.scheduler_tick,
                        scope='Unrelated single-day scheduler only; native Cataclysm/liquid/jelly ticks unchanged')
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if not self.done and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                if valid(self.scheduler) and self.scheduler_tick is not None:
                    self.scheduler.set_actor_tick_enabled(self.scheduler_tick)
                    self.report['scheduler_restored'] = self.scheduler.is_actor_tick_enabled() == self.scheduler_tick
                now = float(u.GameplayStatics.get_time_seconds(self.world))
                self.report['total_game_seconds'] = now
                if self.episode_started_at is not None:
                    self.report['episode_game_seconds'] = now-self.episode_started_at
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('A paint probe is already running; it was left intact')
    probe = JellyPaintSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
