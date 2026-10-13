"""One owned solo PIE: shield, burning walnut, then real wizard summon fuses.

Run only in idle L_Mouth after successful LiveCoding. The existing VFX probe
owns bootstrap/input/settings restoration. Its main is never invoked here.
This subclass intercepts the final fire capture flush and readies the normal
wizard summon scheduler on the remaining real whole nuts. No attack, damage,
detonation or fuse helper is called. Nothing is saved to content packages.

Optional MC_NUT_POLISH_OUTPUT, MC_NUT_POLISH_CAPTURE_HZ (default 12), and
MC_NUT_POLISH_TRY_PICKAXE (default True). An isolated summon may receive actual
held Pickaxe input; unsafe approaches are skipped. Other summons expire normally.
Cancel: builtins.mc_nut_vfx_polish_solo.finish('Stopped by operator').
"""
import builtins
import math
import runpy
import statistics
import time
import traceback
from pathlib import Path

import unreal as u

KEY = 'mc_nut_vfx_polish_solo'
OUTPUT = Path(globals().get('MC_NUT_POLISH_OUTPUT',
    Path(u.Paths.project_saved_dir())/'Checks/NutVFXPolishSolo')).resolve()
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/check_nut_fight_vfx_solo.py'),
    run_name='nut_vfx_polish_base', init_globals={'MC_NUT_VFX_OUTPUT': OUTPUT,
        'MC_NUT_VFX_CAPTURE_HZ': globals().get('MC_NUT_POLISH_CAPTURE_HZ', 12)})
Base, prop, vec, valid = (support[name] for name in ('NutFightVFXSolo', 'prop', 'vec', 'valid'))


class NutVFXPolishSolo(Base):
    def __init__(self):
        super().__init__()
        self.report['clips']['wizard_summon'] = []
        self.report['scope'] = 'One owned standalone PIE; real shield hits, scheduled fire nut, scheduled summon and unchanged native fuses'
        self.report['capture_hz'] = self.capture_hz
        self.report['summon_samples'] = []
        self.report['runtime_patch_evidence'] = {'fire': [], 'shield_chips': []}
        self.report['limitations'].extend([
            'Summon readiness and observer/review-camera positions are explicit PIE fixtures; no summon speed, health, damage, radius or fuse is changed.',
            'Pickaxe kill is optional and attempted only at a supported isolated approach; the remaining summons must detonate naturally.',
            'ProceduralMeshLibrary exposes vertices/triangles but no vertex colors; spark alpha and visual quality require inspection of the genuine footage.',
            'Pulse intervals are measured from actual visual-scale peaks; inadequate sampling is reported instead of inventing acceleration evidence.'])
        self.try_pickaxe = bool(globals().get('MC_NUT_POLISH_TRY_PICKAXE', True))
        self.summon_refs = {}
        self.summon_history = {}
        self.expired_paths = set()
        self.retired_at = {}
        self.hidden_retired = set()
        self.spark_paths = set()
        self.summon_cast_seen = False
        self.summon_focus = None
        self.detail_camera_at = 0.
        self.kill_started_at = None
        self.optional_kill = {'status': 'not_attempted'}

    def section(self, component, index=0):
        if not valid(component) or int(component.get_num_sections()) <= index:
            return 0, 0
        section = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(component, index)
        return len(section[0]), len(section[1])//3

    def sample(self, now):
        if self.phase.startswith('polish_'):
            self.sample_summons(now)
            return
        super().sample(now)
        for shot in self.actors(u.MCNutSpellProjectile):
            if prop(shot, 'source_actor') != self.mage or prop(prop(shot, 'impact'), 'impacted'):
                continue
            flames, core = prop(shot, 'flames'), prop(shot, 'core')
            flame_count, _ = self.section(flames)
            core_count, _ = self.section(core)
            if not flame_count or not core_count:
                continue  # Before the first native presentation tick.
            self.require(flame_count == 380 and core_count == 100,
                'Active native fire patch was not observed: expected Flames380/Core100')
            self.report['runtime_patch_evidence']['fire'].append(dict(game_seconds=now,
                actor=shot.get_path_name(), flames_vertices=flame_count, core_vertices=core_count,
                flames_visible=bool(flames.is_visible()), core_visible=bool(core.is_visible()),
                no_collision=flames.get_collision_enabled() == u.CollisionEnabled.NO_COLLISION
                    and core.get_collision_enabled() == u.CollisionEnabled.NO_COLLISION))
            self.report['checks']['live_fire_flames_380_core_100'] = True
        if not valid(self.tank) or float(prop(self.tank, 'shield_health')) > 0:
            return
        for cue in self.actors(u.MCNutCombatEffect):
            state = prop(cue, 'cue')
            if prop(cue, 'source_actor') != self.tank or prop(state, 'type') != u.MCNutCombatCue.SHIELD_HIT:
                continue
            chips = prop(cue, 'flames')
            count, triangles = self.section(chips)
            if not count or not chips.is_visible():
                continue
            material = chips.get_material(0)
            parent = material.get_base_material().get_path_name() if material else None
            no_collision = chips.get_collision_enabled() == u.CollisionEnabled.NO_COLLISION
            self.require(count == 360 and parent == '/Game/Gameplay/VFX/NutCombat/M_NutShieldFracture.M_NutShieldFracture'
                         and no_collision, 'Broken shield lacks native 360-vertex collision-free shell chips/material')
            self.report['runtime_patch_evidence']['shield_chips'].append(dict(game_seconds=now,
                actor=cue.get_path_name(), vertices=count, triangles=triangles, material_parent=parent,
                visible=True, no_collision=no_collision))
            self.report['checks']['live_shield_break_chips_360_material_no_collision'] = True

    def begin_summon(self, now):
        self.primary_input(False)
        self.ready(None, now)
        remaining = self.foods()
        self.require(len(remaining) >= 2, 'Need at least two real surviving whole nuts for summon/expiry footage')
        self.before_summon = [food.get_path_name() for food in remaining]
        self.report['pre_summon_nuts'] = [self.food_support(food) for food in remaining]
        self.report['summon_scheduler_fixture'] = dict(game_seconds=now,
            available_whole_nuts=len(remaining), native_mage_state=str(prop(self.mage, 'state')),
            changes='NextSummonAt/NextSpecialAt readiness only; native scheduler/food conversion/fuse remain unchanged')
        self.overview()
        self.dodge(now)
        self.clip = 'wizard_summon'
        self.stage('polish_summon_ready')

    def choose_pickaxe(self, armed, now):
        if not self.try_pickaxe:
            self.optional_kill = {'status': 'skipped', 'reason': 'MC_NUT_POLISH_TRY_PICKAXE=False'}
            return
        candidates = []
        for actor in armed:
            point = actor.get_actor_location()
            peers = [other.get_actor_location() for other in armed if other != actor]
            for dx, dy in ((130., 0.), (-130., 0.), (0., 130.), (0., -130.)):
                floor = self.supported(point+u.Vector(dx, dy, 0), 50.)
                if floor is None:
                    continue
                clearance = min((math.hypot(floor.x-other.x, floor.y-other.y) for other in peers), default=10000.)
                if clearance >= 330. and actor.get_fuse_seconds_remaining() > 3.:
                    candidates.append((clearance, actor, floor))
        if not candidates:
            self.optional_kill = {'status': 'skipped', 'reason': 'No isolated supported native Pickaxe approach'}
            return
        _, self.kill_target, floor = max(candidates, key=lambda item: item[0])
        self.move_to(floor, math.degrees(math.atan2(self.kill_target.get_actor_location().y-floor.y,
            self.kill_target.get_actor_location().x-floor.x)))
        prop(self.hero, 'inventory').server_select(u.MCToolSlot.PICKAXE)
        self.primary_input(True)
        self.kill_started_at = now
        self.optional_kill = dict(status='input_started', actor=self.kill_target.get_path_name(),
            started_at=now, initial_health=float(prop(self.kill_target, 'health')),
            initial_remaining=float(self.kill_target.get_fuse_seconds_remaining()), approach_floor_cm=vec(floor))

    def observe_pickaxe(self, now):
        if self.kill_started_at is None or self.optional_kill['status'] != 'input_started':
            return False
        target = self.kill_target
        if not valid(target) or prop(target, 'defeated'):
            self.primary_input(False)
            self.require(not valid(target) or not prop(target, 'exploded'), 'Optional Pickaxe target exploded before the observed kill')
            self.optional_kill.update(status='killed_by_held_primary', observed_at=now)
            self.report['checks']['optional_real_pickaxe_disarms_summon'] = True
            return False
        if now-self.kill_started_at >= .95 or target.get_fuse_seconds_remaining() <= 2.5:
            self.primary_input(False)
            self.optional_kill.update(status='attempt_ended_without_kill', ended_at=now,
                remaining=float(target.get_fuse_seconds_remaining()), health=float(prop(target, 'health')))
            return False
        point = target.get_actor_location()
        offset = self.hero.get_actor_location()-point
        length = math.hypot(offset.x, offset.y)
        floor = self.supported(point+u.Vector(offset.x/max(1., length)*130., offset.y/max(1., length)*130., 0), 45.)
        if floor is None:
            self.primary_input(False)
            self.optional_kill.update(status='approach_lost_support', ended_at=now)
            return False
        self.move_to(floor, math.degrees(math.atan2(point.y-floor.y, point.x-floor.x)))
        return True

    def detail_camera(self, now, armed):
        if now < self.detail_camera_at or not armed:
            return
        self.detail_camera_at = now+.30
        if not valid(self.summon_focus) or not prop(self.summon_focus, 'armed'):
            self.summon_focus = min(armed, key=lambda actor: actor.get_distance_to(self.hero))
        target = self.summon_focus.get_actor_location()+u.Vector(0, 0, 35.)
        for offset in (u.Vector(90, 460, 240), u.Vector(-90, -460, 240),
                       u.Vector(460, 90, 240), u.Vector(-460, -90, 240)):
            if self.supported(target+offset, 60.) is None:
                continue
            self.frame_camera(target+offset, target)
            prop(self.camera, 'camera_component').set_editor_property('field_of_view', 78.)
            self.keep_review_camera()
            self.frame_at = max(self.frame_at, now+.04)
            self.report.setdefault('summon_camera_fixtures', []).append(dict(game_seconds=now,
                actor=self.summon_focus.get_path_name(), location_cm=vec(target+offset), target_cm=vec(target),
                fov=78., fixture='Position-only review camera; native moving/armed actor remains untouched'))
            return

    def sample_summons(self, now):
        actors = self.summons()
        rows = []
        for actor in actors:
            path = actor.get_path_name()
            self.summon_refs[path] = actor
            armed, defeated, exploded = (bool(prop(actor, name)) for name in ('armed', 'defeated', 'exploded'))
            started, expires = (float(prop(actor, name)) for name in ('fuse_started_at', 'fuse_explodes_at'))
            remaining = float(actor.get_fuse_seconds_remaining())
            duration = expires-started
            expected = max(0., expires-now) if armed and not defeated else 0.
            self.require(.799 <= duration <= 12.001 and abs(remaining-expected) < .15,
                'Observed summon fuse duration/remaining disagrees with its unchanged native clock')
            component = prop(actor, 'fuse_flame')
            count, triangles = self.section(component, 1)
            self.require(count <= 48 and triangles <= 24, 'Spark section exceeded the native bounded six-spark budget')
            visible = bool(component.is_visible())
            spark_visible = visible and int(component.get_num_sections()) > 1 and component.is_mesh_section_visible(1)
            mat = component.get_material(1)
            parent = mat.get_base_material().get_path_name() if mat else None
            if spark_visible:
                self.require(count == 48 and parent == '/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire',
                    'Active spark section does not use native geometry and the actual shared flame material')
                self.spark_paths.add(path)
            if exploded:
                self.expired_paths.add(path)
            if defeated or not armed:
                self.retired_at.setdefault(path, now)
                if not visible:
                    self.hidden_retired.add(path)
                elif now-self.retired_at[path] > .15:
                    self.require(False, 'Retired summon still displays its fuse/spark component')
            scale = prop(prop(actor, 'visual'), 'relative_scale3d')
            mesh_scale = prop(actor, 'mesh_scale')
            ratio = float(scale.x)/max(.0001, float(mesh_scale.x))
            row = dict(path=path, location_cm=vec(actor.get_actor_location()), armed=armed, defeated=defeated,
                exploded=exploded, health=float(prop(actor, 'health')), remaining=remaining,
                fuse_started_at=started, fuse_explodes_at=expires, duration=duration,
                visual_scale=vec(scale), visual_scale_x_ratio=ratio,
                fuse_visible=visible, spark_section_visible=bool(spark_visible), spark_vertices=count,
                spark_triangles=triangles, spark_material_parent=parent,
                blast_radius_cm=float(prop(actor, 'blast_radius')), blast_damage=float(prop(actor, 'blast_damage')),
                push_speed=float(prop(actor, 'push_speed')))
            rows.append(row)
            if armed and not defeated:
                self.summon_history.setdefault(path, []).append(dict(time=now,
                    progress=max(0., min(1., 1.-remaining/duration)), scale_ratio=ratio))
        for path, actor in self.summon_refs.items():
            if not valid(actor):
                self.hidden_retired.add(path)
        sample = dict(game_seconds=now, phase=self.phase, primary_held=self.primary,
            player_cm=vec(self.hero.get_actor_location()), player_health=float(prop(prop(self.hero, 'status'), 'state').health),
            mage_attack=str(prop(self.mage, 'attack')), mage_state=str(prop(self.mage, 'state')),
            summons=rows, camera_evidence=self.camera_evidence())
        self.report['summon_samples'].append(sample)
        self.report['samples'].append(sample)

    def conclude_summons(self, now):
        self.require(self.summon_cast_seen and self.summon_refs, 'No real native Summon cast/conversion was observed')
        self.require(self.spark_paths, 'No actual visible 48-vertex fuse spark section was observed')
        self.require(self.expired_paths, 'No native bExploded expiry was observed on an unchanged summon fuse')
        self.require(self.expired_paths <= self.hidden_retired, 'Expired summons did not hide/remove their fuse visuals')
        if self.optional_kill['status'] == 'killed_by_held_primary':
            self.require(self.optional_kill['actor'] in self.hidden_retired, 'Killed summon kept its fuse visible')
        self.report['optional_pickaxe_kill'] = self.optional_kill
        self.report['natural_expired_summons'] = sorted(self.expired_paths)
        self.report['hidden_or_removed_summons'] = sorted(self.hidden_retired)
        pulse = []
        for path, rows in self.summon_history.items():
            if self.optional_kill.get('actor') == path:
                continue
            peaks = [b for a, b, c in zip(rows, rows[1:], rows[2:])
                if b['scale_ratio'] > a['scale_ratio'] and b['scale_ratio'] > c['scale_ratio']]
            early = [row['time'] for row in peaks if .05 < row['progress'] < .60]
            late = [row['time'] for row in peaks if row['progress'] > .72]
            early_gaps = [b-a for a, b in zip(early, early[1:])]
            late_gaps = [b-a for a, b in zip(late, late[1:])]
            pulse.append(dict(actor=path, observed_peak_times=[row['time'] for row in peaks],
                early_median_interval=statistics.median(early_gaps) if early_gaps else None,
                late_median_interval=statistics.median(late_gaps) if late_gaps else None,
                faster_late=statistics.median(late_gaps) < statistics.median(early_gaps)
                    if early_gaps and late_gaps else None))
        self.report['observed_visual_pulse_intervals'] = pulse
        self.report['checks'].update(native_summon_clock_remaining_verified=True,
            native_spark_section_bounded_visible_material_verified=True,
            unchanged_native_fuses_expire_and_remove_visuals=True,
            actual_visual_pulse_acceleration_observed=any(row['faster_late'] is True for row in pulse))
        self.require(self.report['checks'].get('live_fire_flames_380_core_100')
                     and self.report['checks'].get('live_shield_break_chips_360_material_no_collision'),
                     'Runtime geometry evidence did not confirm both active LiveCoding visual patches')
        self.screenshot('03_summon_native_expiry.png')
        self.report['summon_recorded_seconds_after_arming'] = now-self.arm_seen_at
        self.primary_input(False)
        self.stage('polish_capture_flush')

    def tick(self, delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 140., 'Polish probe exceeded 140 wall seconds')
            if not self.phase.startswith('polish_'):
                if self.phase == 'capture_flush':
                    self.begin_summon(float(u.GameplayStatics.get_time_seconds(self.world)))
                else:
                    super().tick(delta)
                return
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            self.require(len(worlds) == 1 and self.world in worlds, 'Owned standalone PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(valid(self.hero) and isinstance(self.hero, u.MCToothCharacter), 'Owned observer was replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            age = now-self.phase_at
            armed = [actor for actor in self.summons() if prop(actor, 'armed') and not prop(actor, 'defeated')]
            picking = self.observe_pickaxe(now)
            if not picking and self.phase != 'polish_capture_flush':
                self.dodge(now)
            if self.phase == 'polish_summon_live':
                self.detail_camera(now, armed)
            self.keep_review_camera()
            self.require(prop(self.hero, 'status').is_alive(), 'Observer died; actual native damage was preserved')
            if now >= self.sample_at:
                self.sample_at = now+.05
                self.sample_summons(now)
            if self.capture_hz and self.clip and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                camera = self.camera_evidence()
                if self.pc.get_view_target() == self.camera and camera['location_error_cm'] < 2. and camera['rotation_error_degrees'] < 2.:
                    self.screenshot('%05d.png'%len(self.report['frames']), True)
                    self.report['frames'][-1]['camera_evidence'] = camera
                else:
                    self.report.setdefault('skipped_camera_requests', []).append(dict(game_seconds=now,
                        phase=self.phase, clip_window=self.clip, camera_evidence=camera,
                        reason='Actual player camera manager has not adopted the owned pose'))
            if self.phase == 'polish_summon_ready':
                self.require(age < 6., 'Mage did not finish the actual fire recovery before summon')
                if age >= .65 and prop(self.mage, 'state') == u.MCNutBossState.IDLE:
                    self.ready('NextSummonAt', now+.20)
                    self.stage('polish_summon_cast')
            elif self.phase == 'polish_summon_cast':
                self.require(age < 8., 'Native wizard summon scheduler created no armed creeps')
                if prop(self.mage, 'attack') == u.MCNutBossAttack.SUMMON and prop(self.mage, 'state') in (
                    u.MCNutBossState.TELEGRAPH, u.MCNutBossState.EXECUTING):
                    self.summon_cast_seen = True
                if armed:
                    self.require(self.summon_cast_seen and len(armed) == len(self.before_summon),
                        'Native summon did not convert the observed remaining whole nuts')
                    self.arm_seen_at = now
                    self.report['summon_count'] = dict(before=len(self.before_summon), actual=len(armed))
                    self.choose_pickaxe(armed, now)
                    self.stage('polish_summon_live')
            elif self.phase == 'polish_summon_live' and now-self.arm_seen_at >= 7.:
                self.conclude_summons(now)
            elif self.phase == 'polish_capture_flush' and age >= .65:
                self.clip = None
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    for key in (KEY, 'mc_nut_fight_vfx_solo', 'mc_nut_fight_redesign_solo', 'mc_approved_environment_solo'):
        previous = getattr(builtins, key, None)
        if previous is not None and not getattr(previous, 'done', True):
            raise RuntimeError('An owned probe is already active: '+key)
    probe = NutVFXPolishSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
