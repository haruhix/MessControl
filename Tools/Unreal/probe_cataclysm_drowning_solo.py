"""Optional, focused swimming survival proof in one owned standalone PIE.

Run only after the Editor build, in idle L_Mouth. Reuses CataclysmSolo helpers
without invoking its main or creating NPC peers. Default: real circular CMC
movement input, normal reserve drain, unchanged native six-second grace.
MC_DROWN_RATE=50 opts into a documented, PIE-only idle-drain acceleration.
If the native flood cannot fit the predicted normal depletion + grace, only
SwimEnergyPerSecond is accelerated to 50 on the PIE movement component.
MC_DROWN_CAPTURE_HZ defaults to 6 (0 disables sequence capture).
Cancel: builtins.mc_cataclysm_drowning_solo.finish('Stopped by operator').
No damage helpers, health/energy assignment, clock jumps or asset saves.
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

KEY = 'mc_cataclysm_drowning_solo'
ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_DROWN_OUTPUT', Path(u.Paths.project_saved_dir()) /
                          'Checks/CataclysmDrowningSolo')).resolve()
REQUESTED_RATE = globals().get('MC_DROWN_RATE')
if REQUESTED_RATE is not None:
    REQUESTED_RATE = float(REQUESTED_RATE)
    if not math.isfinite(REQUESTED_RATE) or not 1. <= REQUESTED_RATE <= 100.:
        raise RuntimeError('MC_DROWN_RATE must be a finite rate between 1 and 100')
support = runpy.run_path(str(ROOT / 'Tools/Unreal/probe_cataclysm_solo.py'),
    run_name='cataclysm_drowning_support', init_globals={
        'MC_CATACLYSM_MODE': 'rafts', 'MC_CATACLYSM_OUTPUT': OUTPUT,
        'MC_CATACLYSM_CAPTURE_HZ': globals().get('MC_DROWN_CAPTURE_HZ', 6)})
Parent, prop, vec, valid, path, enum_name = (
    support[name] for name in ('CataclysmSolo', 'prop', 'vec', 'valid', 'path', 'enum_name'))


class CataclysmDrowningSolo(Parent):
    def __init__(self):
        super().__init__()
        self.subject = self.movement = None
        self.circle = None
        self.circle_radius = 120.
        self.drive = False
        self.old_rate = self.old_auto_camera = None
        self.drowning_seen_at = None
        self.terminal_eta = None
        self.previous_health = None
        self.sample_before_death = None
        self.previous_energy = None
        self.surface_breach_at = None
        self.low_seen = self.red_seen = False
        self.report.update(mode='drowning', scope='One owned standalone authority PIE; real F3 raft event, CMC input and native water death',
            fixture_tuning={}, clips={'swim_fatigue': [], 'drowning_countdown': [], 'water_death': []},
            samples=[], checks={}, limitations=[
                'One initial position fixture places the local pawn in supported deep water; no repeated repositioning or peers.',
                'Circular AddMovementInput exercises normal CMC acceleration; this is not a hardware-input or balance playthrough.',
                'Default drain is preferred. Any accelerated SwimEnergyPerSecond is recorded and confined to this PIE pawn.',
                'Drowning grace, health, energy, flood clock, collision and damage are never assigned or advanced by the probe.',
                'Native wave crests may briefly lift the swimmer into air. Such breaches are recorded, bounded to 2.5 s, cannot recover reserve, and never receive a new position fixture.',
                'Water death attribution uses the native countdown, deep water at the terminal transition and health/damage timestamp; Status exposes no reflected last-source actor.',
                'No network clients, packages saved, build, editor map switch or broad test suite.'])
        self.report['source_sha256']['Tools/Unreal/probe_cataclysm_drowning_solo.py'] = hashlib.sha256(
            (ROOT / 'Tools/Unreal/probe_cataclysm_drowning_solo.py').read_bytes()).hexdigest()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output / 'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT / 'Report_drowning.json').write_text(data, encoding='utf-8')

    def set_rate(self, rate, reason):
        if self.old_rate is None:
            self.old_rate = float(prop(self.movement, 'swim_energy_per_second'))
        self.movement.set_editor_property('SwimEnergyPerSecond', rate)
        self.report['fixture_tuning']['SwimEnergyPerSecond'] = dict(original=self.old_rate, value=rate, reason=reason,
            target=path(self.movement), world=self.world.get_path_name(), game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)))

    def start_swimmer(self, now):
        liquid = prop(self.event, 'liquid')
        self.require(valid(liquid) and not prop(liquid, 'river_flood'), 'Expected the real deep raft branch')
        rafts = [r for r in prop(self.event, 'rafts') if valid(r)]
        self.require(len(rafts) == 2 and not prop(self.event, 'jellies'), 'Raft branch mixed in another encounter')
        # These are the same live bounds used by CoffeeFlood::Start; its native
        # ArenaCenter/HalfSize fields are intentionally protected from Python.
        center, half, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        level = prop(liquid, 'surface').get_world_location().z
        if liquid.is_cataclysm_storm():
            stream = liquid.storm_stream_direction()
            side = u.Vector(-stream.y, stream.x, 0)
            # Drifting boats can legitimately rescue a swimmer by moving under
            # the circular input route. Choose free deep water across their
            # stream lanes once, rather than repeatedly pushing the pawn off.
            candidates = [center + side * offset for offset in (700., -700., 900., -900.)]
        else:
            side = None
            candidates = [center + u.Vector(x, y, 0) for x, y in
                          ((0, 0), (-300, 0), (300, 0), (0, -300), (0, 300), (-300, -300), (300, -300))]
        self.circle = None
        for candidate in candidates:
            if abs(candidate.x - center.x) + 200. >= half.x or abs(candidate.y - center.y) + 200. >= half.y:
                continue
            if any((candidate.x-r.get_actor_location().x)**2 + (candidate.y-r.get_actor_location().y)**2 < 350.**2 for r in rafts):
                continue
            if side and any(abs((candidate.x-r.get_actor_location().x)*side.x
                                +(candidate.y-r.get_actor_location().y)*side.y) < 500. for r in rafts):
                continue
            probes = [candidate + u.Vector(x, y, 0) for x, y in ((0, 0), (-180, 0), (180, 0), (0, -180), (0, 180))]
            floors = [self.floor(point) for point in probes]
            if all(point is not None and level-point.z >= 160. for point in floors):
                self.circle = candidate
                break
        self.require(self.circle is not None, 'No supported free-water input circle; no forced rescue/teleport fallback')
        foot = self.circle + u.Vector(self.circle_radius, 0, 0)
        foot.z = level - 10. - self.subject.capsule_component.get_scaled_capsule_half_height() - 2.
        self.place_pawn(self.subject, foot, 90., True)
        self.subject.call_method('SetBraceInputHeld', args=(False,))
        self.primary_input(False)
        self.drive = True
        self.movement = self.subject.get_movement_component()
        energy = float(self.movement.get_swim_energy())
        idle = float(prop(self.movement, 'swim_energy_per_second'))
        stroke = float(prop(self.movement, 'swim_stroke_energy_per_second'))
        grace = float(prop(self.movement, 'drowning_grace_seconds'))
        self.require(energy > 0. and grace >= 3., 'A fresh, positive reserve and native drowning grace are required')
        self.grace = grace
        remaining = sum(float(prop(self.event, key)) for key in ('flood_fill_seconds', 'flood_hold_seconds', 'flood_drain_seconds')) \
            - (now - float(prop(self.event, 'phase_started_at')))
        normal_estimate = energy / max(.001, idle + stroke) + grace
        self.report['depletion_plan'] = dict(initial_energy=energy, original_idle_rate=idle, stroke_rate=stroke,
            grace_seconds=grace, expected_full_stroke_seconds=normal_estimate, native_flood_remaining_seconds=remaining,
            liquid=path(liquid), circle_center_cm=vec(self.circle), circle_radius_cm=self.circle_radius)
        if REQUESTED_RATE is not None:
            self.set_rate(REQUESTED_RATE, 'Explicit MC_DROWN_RATE; accelerate only the PIE pawn, never its grace or resource value')
        elif normal_estimate + 2. > remaining:
            self.set_rate(50., 'Native flood remaining time cannot fit normal depletion plus grace with a two-second margin')
        self.started_swimming_at = now
        self.previous_health = float(prop(prop(self.subject, 'status'), 'state').health)
        self.previous_energy = energy
        self.frame_camera(self.subject.get_actor_location() + u.Vector(360, -430, 230), self.subject.get_actor_location())
        self.report['checks']['real_f3_deep_flood_no_peers'] = True
        self.stage('swimming')

    def drive_circle(self):
        position = self.subject.get_actor_location()
        x, y = position.x-self.circle.x, position.y-self.circle.y
        radius = max(1., math.hypot(x, y))
        error = (self.circle_radius-radius) / 65.
        dx, dy = -y/radius + x/radius*error, x/radius + y/radius*error
        length = max(.001, math.hypot(dx, dy))
        self.subject.add_movement_input(u.Vector(dx/length, dy/length, 0), 1., False)

    def swim_label(self):
        for widget in u.WidgetLibrary.get_all_widgets_of_class(self.world, u.MCStaminaWidget, False):
            if bool(prop(widget, 'show_swim_energy')) and widget.is_visible():
                # BindWidget pointer is protected. Inspect the actual named
                # TextBlock in this widget's owned tree, without a test getter.
                text = next((node for node in u.ObjectIterator(u.TextBlock)
                             if node.get_name() == 'StaminaValue' and valid(node.get_outer())
                             and node.get_outer().get_outer() == widget), None)
                if not valid(text):
                    continue
                color = prop(prop(text, 'color_and_opacity'), 'specified_color')
                return dict(widget=path(widget), text=str(text.get_text()), color_rgba=[float(color.r), float(color.g), float(color.b), float(color.a)])
        return None

    def observe_swimmer(self, now):
        status = prop(self.subject, 'status')
        state = prop(status, 'state')
        alive = bool(status.is_alive())
        energy = float(self.movement.get_swim_energy())
        deep = bool(self.movement.is_in_deep_water())
        drowning = bool(self.movement.is_drowning())
        eta = float(self.movement.get_drowning_seconds_remaining())
        accel = self.movement.get_current_acceleration()
        effort = math.hypot(accel.x, accel.y) / max(1., float(self.movement.get_max_acceleration()))
        label = self.swim_label()
        entry = dict(game_seconds=now, phase=self.phase, alive=alive, health=float(state.health), damage_at=float(state.damage_at),
            swim_energy=energy, deep_water=deep, drowning=drowning, eta_seconds=eta, effort=effort,
            movement_mode=enum_name(prop(self.movement, 'movement_mode')),
            hero_cm=vec(self.subject.get_actor_location()), reserve_widget=label,
            event_phase=enum_name(prop(self.event, 'phase')), body_state=enum_name(prop(self.subject, 'tooth_physics').get_body_state()))
        self.report['samples'].append(entry)
        if not alive:
            self.drive = False
            self.require(self.drowning_seen_at is not None and self.sample_before_death is not None,
                         'Subject died before a natural drowning countdown was observed')
            self.require(self.sample_before_death['deep_water'] and self.sample_before_death['swim_energy'] <= .01
                         and self.sample_before_death['eta_seconds'] <= .25,
                         'Death preceded native countdown exhaustion; inspect unrelated hazard telemetry')
            self.require(now-self.drowning_seen_at >= self.grace-.2, 'Drowning grace was shortened by another death path')
            self.require(abs(float(state.damage_at)-now) < .25 and float(state.health) <= 0., 'No contemporaneous native lethal health transition')
            self.require(self.low_seen and self.red_seen, 'Native low-reserve label / red countdown was not witnessed')
            self.report['checks']['natural_countdown_then_authoritative_water_death'] = True
            self.report['death'] = dict(observed_at=now, drowning_observed_at=self.drowning_seen_at,
                preceding_sample=self.sample_before_death, terminal_sample=entry)
            self.stage('death_seen')
            self.begin_clip('water_death', 1.)
            return
        self.require(energy <= self.previous_energy + .05, 'Subject recovered swimming reserve on shelter; no forced re-entry is allowed')
        self.previous_energy = energy
        if not deep:
            if self.surface_breach_at is None:
                self.surface_breach_at = now
                self.report.setdefault('surface_breaches', []).append(dict(started_at=now, position_cm=vec(self.subject.get_actor_location())))
            self.require(entry['movement_mode'] in ('MOVE_FALLING', 'MOVE_SWIMMING'), 'Subject found dry ground/deck; natural depletion proof ended')
            self.require(now-self.surface_breach_at <= 2.5, 'Subject stayed outside deep water beyond a brief natural wave breach')
        elif self.surface_breach_at is not None:
            self.report['surface_breaches'][-1].update(returned_at=now, seconds=now-self.surface_breach_at)
            self.surface_breach_at = None
        self.require(float(state.health) >= self.previous_health-.01, 'Another hazard damaged the subject before drowning')
        if self.movement.is_swim_fatigued() and not self.low_seen:
            self.low_seen = bool(label and 'СИЛЫ ЗАКАНЧИВАЮТСЯ' in label['text'])
            if self.low_seen:
                self.report['checks']['native_low_reserve_warning'] = True
                self.begin_clip('swim_fatigue', 4.)
        if drowning:
            if self.drowning_seen_at is None:
                self.drowning_seen_at = now
                self.report['checks']['energy_depleted_without_assignment'] = True
                self.begin_clip('drowning_countdown', self.grace + .5)
            if label and 'ТОНЕШЬ' in label['text']:
                r, g, b, _ = label['color_rgba']
                if r > .8 and g < .35 and b < .35:
                    self.red_seen = True
                    self.report['checks']['actual_red_eta_widget'] = True
            if self.terminal_eta is not None:
                self.require(eta <= self.terminal_eta+.05, 'Native drowning countdown increased without dry recovery')
            self.terminal_eta = eta
        self.sample_before_death = entry

    def capture(self, now):
        self.keep_camera()
        if self.clip and now > self.clip_until:
            self.clip = None
        if self.capture_hz and self.clip and now >= self.frame_at:
            evidence = self.camera_evidence()
            camera_matches = evidence and evidence['location_error_cm'] < 12. and evidence['rotation_error_degrees'] < 5. \
                and self.pc.get_view_target() == self.camera
            if not camera_matches and self.phase == 'death_seen' and not prop(self.subject, 'status').is_alive() \
                    and self.report['checks'].get('natural_countdown_then_authoritative_water_death'):
                # Native death may hand the camera to the spectator. Preserve
                # the verified countdown frames; never accept the other view
                # as footage from our review camera or change native death.
                self.report.setdefault('postmortem_camera_takeover', dict(game_seconds=now,
                    evidence=evidence, omitted_clip=self.clip, reason='Native death switched the actual camera after the verified lethal transition'))
                self.clip = None
                return
            self.require(camera_matches,
                         'Review camera has not become the actual camera; no false capture accepted')
            self.require(self.pc.get_view_target() == self.camera, 'Actual view target differs from the review camera')
            self.frame_at = now + 1. / self.capture_hz
            self.screenshot('%05d.png' % len(self.report['frames']), True)

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 240., 'Focused probe exceeded 240 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one standalone authority world')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()):
                    self.world = None
                    return
                self.hero = self.subject = self.pc.get_controlled_pawn()
                self.require(isinstance(self.subject, u.MCToothCharacter), 'No native tooth pawn')
                self.old_auto_camera = bool(prop(self.pc, 'auto_manage_active_camera_target'))
                self.report['world'] = self.world.get_path_name()
                self.stage('first_perk')
                return
            self.require(self.world in worlds and len(worlds) == 1, 'Owned PIE ended or was replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            # Reward UI pauses game time (including the first offer near .033s).
            # Its real controller action must run before the duplicate-time gate.
            if self.pc.is_reward_menu_open():
                if self.phase == 'first_perk':
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                else:
                    self.report.setdefault('normal_reward_choices', []).append(dict(game_seconds=now, phase=self.phase))
                    self.pc.choose_reward_perk(0)
                return
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.phase_at
            if self.phase == 'first_perk':
                self.require(elapsed < 25., 'First perk UI did not become ready')
                return
            if self.phase == 'perk_resolution':
                self.require(elapsed < 10., 'First perk choice did not resolve')
                if not self.pc.is_reward_menu_open():
                    self.pc.request_dev_action(u.MCDevAction.CATACLYSM_RAFTS, -1)
                    self.stage('launch')
                return
            if self.phase == 'launch':
                events = [e for e in self.actors(u.MCCataclysmEvent) if enum_name(prop(e, 'phase')) not in ('DORMANT', 'COMPLETE', 'STOPPED')]
                self.require(len(events) == 1, 'F3 did not launch one native Cataclysm')
                self.event = events[0]
                self.tongue = self.actors(u.MCTongue)[0]
                self.require(enum_name(prop(self.event, 'variant')) == 'RAFTS', 'Wrong F3 branch')
                self.stage('wait_deep')
                return
            self.require(valid(self.subject) and valid(self.event), 'Subject/event disappeared before proof completed')
            if self.phase == 'wait_deep':
                self.require(elapsed < 45., 'Native first wave never entered deep flooding')
                self.require(prop(self.subject, 'status').is_alive(), 'Subject died during the opening wave')
                if enum_name(prop(self.event, 'phase')) == 'FLOODING' and now-float(prop(self.event, 'phase_started_at')) >= 4.5:
                    self.start_swimmer(now)
                return
            if self.phase == 'swimming':
                self.require(elapsed < 40., 'No native water death within the bounded swim window')
                if self.drive:
                    self.drive_circle()
                position = self.subject.get_actor_location()
                self.frame_camera(position+u.Vector(360, -430, 230), position)
                if now >= self.sample_at:
                    self.sample_at = now+.05
                    self.observe_swimmer(now)
                self.capture(now)
            elif self.phase == 'death_seen':
                self.capture(now)
                if elapsed >= 1.:
                    self.stage('capture_flush')
            elif self.phase == 'capture_flush' and elapsed >= .8:
                for entry in self.report['frames']:
                    self.require(Path(entry['file']).is_file(), 'Missing genuine engine capture '+entry['file'])
                    data = Path(entry['file']).read_bytes()
                    self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Capture is not an engine PNG')
                    entry.update(sha256=hashlib.sha256(data).hexdigest(), width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
                if self.capture_hz:
                    self.require(len(self.report['clips']['drowning_countdown']) >= 8, 'Too few verified red-countdown frames')
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        self.drive = False
        if not self.done and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                if valid(self.subject):
                    self.subject.consume_movement_input_vector()
                if self.old_rate is not None and valid(self.movement):
                    self.movement.set_editor_property('SwimEnergyPerSecond', self.old_rate)
                    self.report['pie_drain_rate_restored'] = True
                if valid(self.pc) and self.old_auto_camera is not None:
                    self.pc.set_editor_property('bAutoManageActiveCameraTarget', self.old_auto_camera)
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


def main():
    for key in (KEY, support['KEY'], 'mc_approved_environment_solo'):
        old = getattr(builtins, key, None)
        if old is not None and not getattr(old, 'done', True):
            raise RuntimeError('Another owned probe is pending/running; it was preserved')
    probe = CataclysmDrowningSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
