"""Owned solo: actual saved Apple knife cut, then walk into its large sibling.

Run in the rebuilt, idle L_Mouth -MCHubTransientProfile Editor. Mandatory globals:
MC_WALKING_EDITOR_PID and MC_WALKING_LOGFILE (the actual E: Editor log). Optional
MC_WALKING_OUTPUT defaults to the task's E: validation directory. No launch,
build, asset/profile save, direct HitFood/intake, collision or gameplay tuning.
Only fixture placement/body freeze is controlled; all blocking is production.
Cancel: builtins.mc_food_walking_solo.finish('Stopped by operator').
"""
import builtins
import hashlib
import math
import os
import re
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUTPUT = Path(globals().get('MC_WALKING_OUTPUT',
    'E:/CodexWork/MessControl/Validation/FoodWalkingCollision_20261109/Solo')).resolve()
PID = int(globals()['MC_WALKING_EDITOR_PID'])
LOG = Path(globals()['MC_WALKING_LOGFILE']).resolve()
assert os.getpid() == PID and OUTPUT.drive.upper() == LOG.drive.upper() == 'E:'
assert LOG.is_file()
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_food_volume_only_110_solo.py'),
    run_name='food_walking_support',
    init_globals={'MC_VOLUME_OUTPUT': str(OUTPUT), 'MC_VOLUME_LOGFILE': str(LOG)})
Parent, prop, valid, path, vec = (
    support[k] for k in ('FoodVolumeSolo', 'prop', 'valid', 'path', 'vec'))
KEY, BATCH = 'mc_food_walking_solo', support['BATCH']


def dot_xy(a, b):
    return float(a.x*b.x+a.y*b.y)


class FoodWalkingSolo(Parent):
    def __init__(self):
        super().__init__()
        self.remnant = self.blocker = None
        self.contact = self.walk_start = self.direction = None
        self.max_progress = 0.
        self.walk_samples = []
        self.capture_hz = 6.
        self.report.update(mode='food_walking_saved_cut_actual_movement',
            scope='One genuine saved-scale Apple knife cut; ordinary movement into its actual large cut sibling.',
            walking_samples=self.walk_samples, checks={}, editor_pid=PID,
            helpers=['Tools/Unreal/check_food_volume_only_110_solo.py',
                     'Tools/Unreal/check_food_auto_mouth_solo.py',
                     'Tools/Unreal/check_approved_environment_solo.py'])
        self.report['limitations'] = [
            'Only an owned standalone PIE is created. No persistent profile/package is saved, and existing PIE is rejected.',
            'The source is frozen for the unchanged validated knife pose. The actual generated large sibling must land into Free/Stuck naturally before its body is frozen for a reproducible walking check.',
            'AddMovementInput and SetPrimaryInputHeld are real production input handlers; this is not physical keyboard automation.',
            'The native settled flag is not written or directly invoked. Its QueryOnly/Pawn-block proxy and exact-component capsule hit are observed before real CMC movement.',
            'The object query uses the live project FoodWalkingObstacle enum alias and must hit the exact FoodWalkingBlocker; real CMC movement is checked independently.',
            'A frozen remnant does not validate dynamic Chaos jitter, networking, small-debris nudges or performance. Focused native tests cover these collision policies separately; this recipe claims only its actual solo observations.',
            'Screenshots retain the real gameplay camera. Root visual review and video encoding are separate steps.']
        for name in ('Source/MessControl/Private/MCFoodWalkingCollision.cpp',
                     'Source/MessControl/Private/MCPeerMovementCollision.cpp',
                     'Tools/Unreal/check_food_walking_collision_solo.py'):
            self.report['source_sha256'][name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()

    def choose_rows(self):
        super().choose_rows()
        config = (ROOT/'Config/DefaultEngine.ini').read_text(encoding='utf8')
        channels = re.findall(r'^\+DefaultChannelResponses=\(([^\n]+)\)', config, re.M)
        objects = [re.search(r'Name="([^"]+)"', row).group(1) for row in channels
                   if 'bTraceType=False' in row]
        self.require(objects == ['BrushTool', 'FoodFragment', 'FoodWalkingObstacle'],
                     'Project object-channel mapping changed; update the read-only query mapping before running')
        self.object_type = u.ObjectTypeQuery.ECC_FOOD_WALKING_OBSTACLE
        self.report['walking_object_query'] = dict(project_object_channels=objects,
            object_type=str(self.object_type), purpose='Exact proxy geometry query; real movement is tested separately')

    def health(self):
        return float(prop(prop(prop(self.hero, 'status'), 'state'), 'health'))

    def blocker_is_active(self):
        return (valid(self.blocker)
                and self.blocker.get_collision_enabled() == u.CollisionEnabled.QUERY_ONLY
                and self.blocker.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN)
                    == u.CollisionResponseType.ECR_BLOCK)

    def floor_record(self):
        floor = self.hero.character_movement.get_editor_property('CurrentFloor')
        hit = self.statics.call_method('BreakHitResult', args=(prop(floor, 'hit_result'),))
        return dict(blocking=bool(prop(floor, 'blocking_hit')), walkable=bool(prop(floor, 'walkable_floor')),
                    actor=path(hit[9]), component=path(hit[10]), normal=vec(hit[7]))

    def capsule_query(self, start, end):
        hit = u.SystemLibrary.capsule_trace_single_for_objects(self.world, start, end,
            self.radius, self.half_height, [self.object_type], False, [self.hero], u.DrawDebugTrace.NONE, True)
        self.require(hit is not None and hit is not False, 'No query hit on the actual food walking obstacle')
        parts = self.statics.call_method('BreakHitResult', args=(hit,))
        self.require(parts[0] and not parts[1] and parts[9] == self.remnant and parts[10] == self.blocker,
                     'Capsule query must hit the exact active remnant proxy without initial overlap')
        return parts

    def prepare_remnant(self):
        candidates = [part for part in self.actors(u.MCFoodActor)
                      if part != self.food and int(prop(part, 'batch')) == BATCH
                      and prop(part, 'phase') != u.MCFoodPhase.DISPOSED
                      and not part.fits_automatic_intake_portion()]
        self.require(len(candidates) == 1, 'The genuine edge cut must retain exactly one large sibling')
        part = candidates[0]
        if prop(part, 'phase') not in (u.MCFoodPhase.FREE, u.MCFoodPhase.STUCK):
            return False  # Landing, not a scripted phase override, owns this transition.
        self.remnant, self.blocker = part, prop(part, 'walking_blocker')
        body, visual = prop(part, 'body'), prop(part, 'cut_visual')
        self.require(valid(self.blocker) and valid(visual), 'Rebuilt generated food must have its real proxy and cut mesh')
        body.set_physics_linear_velocity(u.Vector())
        body.set_physics_angular_velocity_in_degrees(u.Vector())
        body.set_simulate_physics(False)
        center, extent, _ = u.SystemLibrary.get_component_bounds(visual)
        capsule = self.hero.capsule_component
        self.radius = float(capsule.get_scaled_capsule_radius())
        self.half_height = float(capsule.get_scaled_capsule_half_height())
        approach = self.floor(center-u.Vector(extent.x+self.radius+155., 0., 0.))
        self.require(approach is not None, 'The real cut has no supported approach; keep the diagnostic, do not move through the food')
        self.move_to(approach, 0.)
        self.pc.set_control_rotation(u.Rotator(pitch=-12., yaw=0.))
        self.report['large_sibling'] = dict(actor=path(part), proxy=path(self.blocker),
            phase=str(prop(part, 'phase')), cut_visual=path(visual), visual_center_cm=vec(center),
            visual_extent_cm=vec(extent), body_frozen_after_natural_landing=True,
            hand_size_cm=vec(part.get_hand_portion_size()), approach_cm=vec(self.hero.get_actor_location()))
        self.stage('obstacle_settle')
        return True

    def prepare_contact(self):
        start = self.hero.get_actor_location()+u.Vector(0., 0., 3.)
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.remnant, 'cut_visual'))
        hit = self.capsule_query(start, u.Vector(center.x+extent.x+self.radius+150., start.y, start.z))
        normal = hit[7]
        length = math.hypot(normal.x, normal.y)
        self.require(length > .6, 'The selected real surface does not provide a substantial walking front')
        normal = u.Vector(normal.x/length, normal.y/length, 0.)
        approach = self.floor(hit[5]+normal*(self.radius+155.))
        self.require(approach is not None, 'Exact remnant front has no supported ordinary walking approach')
        self.direction = -normal
        yaw = math.degrees(math.atan2(self.direction.y, self.direction.x))
        self.move_to(approach, yaw)
        self.pc.set_control_rotation(u.Rotator(pitch=-12., yaw=yaw))
        self.report['front_normal'] = vec(normal)
        self.stage('approach_settle')

    def begin_walk(self):
        self.require(self.work_state()['ready'] and self.blocker_is_active(), 'Ordinary walking/proxy readiness was lost')
        floor = self.floor_record()
        self.require(floor['walkable'] and floor['actor'] != path(self.remnant), 'Approach must start on the actual world floor')
        start = self.hero.get_actor_location()
        hit = self.capsule_query(start+u.Vector(0., 0., 3.), start+self.direction*650.+u.Vector(0., 0., 3.))
        self.walk_start, self.contact = start, hit[4]
        self.expected_progress = dot_xy(self.contact-start, self.direction)
        self.require(60. < self.expected_progress < 400., 'Query front is not within the controlled ordinary approach')
        self.health_before = self.health()
        self.anchor_before = self.blocker.get_world_transform()
        self.report['contact_query'] = dict(actor=path(hit[9]), component=path(hit[10]),
            initial_overlap=bool(hit[1]), capsule_center_cm=vec(hit[4]), impact_point_cm=vec(hit[5]),
            impact_normal=vec(hit[7]), expected_progress_cm=self.expected_progress, approach_floor=floor)
        self.stage('walk_into_remnant')

    def observe_walk(self, now):
        position = self.hero.get_actor_location()
        progress = dot_xy(position-self.walk_start, self.direction)
        self.max_progress = max(self.max_progress, progress)
        floor = self.floor_record()
        row = dict(time=now, phase=self.phase, position_cm=vec(position),
            velocity_cm_s=vec(self.hero.get_velocity()), progress_cm=progress, floor=floor,
            health=self.health(), standing=prop(self.hero, 'tooth_physics').get_body_state() == u.MCBodyState.STANDING,
            walking=prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_WALKING,
            proxy_active=self.blocker_is_active(),
            proxy_anchor_fixed=self.blocker.get_world_transform().is_near_equal(self.anchor_before, .01, .01, .001))
        self.walk_samples.append(row)
        self.require(row['proxy_active'] and row['proxy_anchor_fixed'], 'Walking proxy was retired or moved during this frozen-fixture approach')
        self.require(row['standing'] and row['walking'] and abs(row['health']-self.health_before) < .01,
                     'Walking contact damaged, knocked down or ungrounded the worker')
        self.require(floor['walkable'] and floor['actor'] != path(self.remnant), 'Front contact left the world floor or used raw food as support')

    def tick(self, delta):
        if self.done:
            return
        if self.phase == 'waiting_pie':
            return super().tick(delta)
        try:
            self.require(time.monotonic()-self.started < 210, 'Owned walking solo exceeded 210 wall seconds')
            self.require(self.owned_pie_is_active(), 'Owned PIE stopped or was replaced')
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
            if self.phase == 'volume_before':
                self.require(elapsed < 10, 'Ordinary knife selection/readiness did not settle')
                if elapsed >= 2. and self.work_state()['ready'] and prop(prop(self.hero, 'inventory'), 'selected') == u.MCToolSlot.KNIFE:
                    self.primary_input(True)
                    self.stage('volume_cut')
            elif self.phase == 'volume_cut':
                self.require(elapsed < 3., 'The unchanged genuine knife contact did not cut; inspect the diagnostic')
                if prop(self.food, 'phase') == u.MCFoodPhase.DISPOSED:
                    self.primary_input(False)
                    self.report['children_at_acceptance'] = self.inspect_children()
                    self.report['accepted_baseline'] = self.counters()
                    self.stage('cut_landing')
            elif self.phase == 'cut_landing':
                self.require(elapsed < 8., 'Generated large sibling did not naturally finish landing')
                if elapsed >= .8:
                    self.prepare_remnant()
            elif self.phase == 'obstacle_settle':
                self.require(elapsed < 4., 'Production walking obstacle did not arm after settling in a clear footprint')
                if elapsed >= .6 and self.blocker_is_active():
                    self.prepare_contact()
            elif self.phase == 'approach_settle' and elapsed >= .4:
                self.begin_walk()
            elif self.phase == 'walk_into_remnant':
                self.observe_walk(now)
                self.hero.add_movement_input(self.direction, 1., False)
                if elapsed >= 1.8:
                    self.hero.get_movement_component().stop_movement_immediately()
                    self.stage('blocked_tail')
            elif self.phase == 'blocked_tail':
                self.observe_walk(now)
                if elapsed >= .3:
                    progress = dot_xy(self.hero.get_actor_location()-self.walk_start, self.direction)
                    self.require(progress > 40. and abs(progress-self.expected_progress) <= 25., 'Worker did not reach the actual queried front')
                    self.require(self.max_progress <= self.expected_progress+10., 'Worker crossed the exact large-food blocking surface')
                    lines = self.intake_log_lines()
                    self.require(len(lines) == 1 and 'volume_cm3=' in lines[0], 'Retain one genuine accepted-cap native log before reporting success')
                    self.require(self.counters() == self.report['accepted_baseline'], 'Walking alone changed intake/score counters')
                    self.report['checks'].update(real_primary_cut_completed=True,
                        large_sibling_preserved=True, production_walking_proxy_observed=True,
                        exact_proxy_capsule_query=True, ordinary_input_reached_food=True,
                        large_food_blocks_actual_character=True, world_ground_preserved=True,
                        health_and_standing_preserved=True, no_extra_intake_without_input=True)
                    self.report.update(accepted_native_log_lines=lines, end_progress_cm=progress,
                                       maximum_progress_cm=self.max_progress, expected_progress_cm=self.expected_progress,
                                       result_scope='saved_apple_cut_large_sibling_actual_solo_walking_only')
                    self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    previous = getattr(builtins, KEY, None)
    if previous is not None and not getattr(previous, 'done', True):
        raise RuntimeError('An owned food walking probe is already active; it was preserved')
    probe = FoodWalkingSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
    return probe


if __name__ == '__main__':
    main()
