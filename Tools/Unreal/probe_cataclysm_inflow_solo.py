"""One owned solo capture of incoming storm water and its continuous throat outlet.

Run only in idle L_Mouth after the native Editor integration and material save.
Real F3 CataclysmRafts, default event timings, a naturally advancing world and
read-only XY water samples. No clock, current, health, movement-mode or flood
tuning assignments. The unrelated single-day scheduler is temporarily paused.
An optional disclosed observer position fixture uses a real raft after arrival.
Cancellation: builtins.mc_cataclysm_inflow_solo.finish('Stopped by operator').
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

KEY = 'mc_cataclysm_inflow_solo'
ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_INFLOW_OUTPUT',
    Path(u.Paths.project_saved_dir()) / 'Checks/CataclysmInflowSolo')).resolve()
support = runpy.run_path(str(ROOT / 'Tools/Unreal/probe_cataclysm_solo.py'),
    run_name='cataclysm_inflow_support', init_globals={
        'MC_CATACLYSM_MODE': 'rafts', 'MC_CATACLYSM_OUTPUT': OUTPUT,
        'MC_CATACLYSM_CAPTURE_HZ': globals().get('MC_INFLOW_CAPTURE_HZ', 12)})
Parent, prop, vec, valid, path, enum_name = (
    support[name] for name in ('CataclysmSolo', 'prop', 'vec', 'valid', 'path', 'enum_name'))


def length(value):
    return math.sqrt(value.x*value.x + value.y*value.y + value.z*value.z)


def dot(a, b):
    return a.x*b.x + a.y*b.y + a.z*b.z


class CataclysmInflowSolo(Parent):
    def __init__(self):
        super().__init__()
        self.liquid = self.scheduler = None
        self.scheduler_tick = None
        self.episode_started_at = self.holding_started_at = None
        self.points = {}
        self.first_wet = {}
        self.observer_placed = False
        self.water_identity = None
        self.review_view = 'front'
        self.gameplay_attempted = False
        self.report.update(mode='inflow', purpose='front_inlet_and_continuous_throat_outlet',
            fixture_tuning={}, tuning_reason='No flood or gameplay tuning assignments',
            clips={}, samples=[], checks={}, screenshots=[], frames=[],
            scope='One owned standalone authority PIE; real F3 CataclysmRafts and native water',
            clip_labels={'inflow_arrival': 'Быстрый фронт: вход рта → горло',
                         'inflow_holding': 'Непрерывное течение и бурление у горла',
                         'inflow_gameplay': 'Течение из штатной камеры игрока'},
            limitations=[
                'Default fill/hold/drain timings, health, water forces, swimming reserve and native hazards remain unchanged.',
                'The unrelated single-day scheduler is paused and restored; the Cataclysm event, liquid, debris, rafts and game clock keep ticking.',
                'The local observer may be placed on a real raft after arrival. Only actor position is assigned; movement mode, velocity, body state and water membership are not forced.',
                'Temporary review cameras capture arrival and the throat. If the original observer is alive and possessed, the last 3.5 Holding seconds switch to its actual native PlayerCamera, with one ordinary camera-orbit input toward the throat; no camera-component transform or lens tuning is assigned. These are not hardware-input playthroughs.',
                'Three fixed inlet/middle/near-throat XY points read the actual public native StormWetWeightAt, SurfaceHeightAt and StormCurrentAt getters. A fourth off-axis point reads converging current.',
                'Continuous outlet means the native throat ribbon during Holding, not the final Draining phase.',
                'The probe stops after eight seconds of Holding; natural final tail/drain timing, whole-event cleanup, balance, GPU cost and multiplayer remain deferred.',
                'SHOWUI screenshots are genuine engine frames at up to 12 Hz with per-frame camera metadata and SHA256 hashes; artistic quality still requires visual review.'
            ])
        for source in ('Tools/Unreal/probe_cataclysm_inflow_solo.py',
                       'Source/MessControl/Public/MCCoffeeFlood.h',
                       'Source/MessControl/Private/MCCoffeeFlood.cpp',
                       'Source/MessControl/Private/MCCoffeeVisuals.cpp',
                       'Source/MessControl/Private/MCCataclysmStormMath.h'):
            self.report['source_sha256'][source] = hashlib.sha256((ROOT/source).read_bytes()).hexdigest()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_inflow.json').write_text(data, encoding='utf-8')

    def set_points(self):
        origin = prop(self.liquid, 'river_origin')
        direction = self.liquid.storm_stream_direction()
        side = u.Vector(-direction.y, direction.x, 0.)
        settings = prop(self.liquid, 'water_settings')
        outlet = prop(settings, 'drain_point')
        distance = dot(outlet-origin, direction)
        self.require(distance > 1400., 'Storm inlet is not sufficiently upstream of the actual throat')
        self.points = dict(entrance=origin+direction*360.,
            middle=origin+direction*(distance*.5), throat=outlet-direction*150.,
            convergence=outlet-direction*350.+side*350.)
        half = prop(self.liquid, 'half_size')
        side_half = abs(side.x)*half.x+abs(side.y)*half.y
        self.points.update(throat_left=outlet-direction*150.+side*(side_half*.8),
                           throat_right=outlet-direction*150.-side*(side_half*.8))
        self.report['water_geometry'] = dict(arena_center_cm=vec(prop(self.liquid, 'arena_center')),
            arena_half_size_cm=vec(prop(self.liquid, 'half_size')),
            inlet_cm=vec(origin), outlet_cm=vec(outlet), direction_xy=vec(direction),
            source_to_throat_cm=distance, front_speed_cm_s=float(prop(self.liquid, 'river_speed')),
            dry_height_cm=float(prop(settings, 'dry_height')), full_water_height_cm=float(prop(self.liquid, 'height')),
            timings=dict(fill=float(prop(settings, 'fill_seconds')), hold=float(prop(settings, 'hold_seconds')),
                         drain=float(prop(settings, 'drain_seconds'))),
            fixed_probes_cm={name: vec(point) for name, point in self.points.items()})

    def camera_for_water(self):
        direction = self.liquid.storm_stream_direction()
        side = u.Vector(-direction.y, direction.x, 0.)
        height = float(prop(self.liquid, 'height'))
        if self.review_view == 'throat':
            target = prop(prop(self.liquid, 'water_settings'), 'drain_point')-direction*180.
            target.z = height-30.
            camera = target-direction*850.-side*480.+u.Vector(0, 0, 480.)
            fov = 85.
        else:
            # Look upstream from inside the rear half of the mouth. The inlet
            # must be in the frustum, not behind a side-on mid-arena camera.
            target = prop(self.liquid, 'arena_center')-direction*800.
            target.z = height+30.
            camera = prop(prop(self.liquid, 'water_settings'), 'drain_point')-direction*350.-side*1000.
            camera.z = height+950.
            fov = 95.
        self.frame_camera(camera, target)
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', fov)

    def camera_evidence(self):
        if self.review_view == 'gameplay':
            if not valid(self.hero): return None
            manager = prop(self.pc, 'player_camera_manager')
            camera = prop(self.hero, 'camera')
            actual, expected = manager.get_camera_location(), camera.get_world_location()
            rotation, expected_rotation = manager.get_camera_rotation(), camera.get_world_rotation()
            angle_error = max(abs((a-b+180.) % 360.-180.) for a, b in
                ((rotation.pitch, expected_rotation.pitch), (rotation.yaw, expected_rotation.yaw),
                 (rotation.roll, expected_rotation.roll)))
            return dict(view_target=path(self.pc.get_view_target()), expected_target=path(self.hero),
                expected_camera_component=path(camera), actual_cm=vec(actual), expected_cm=vec(expected),
                location_error_cm=length(actual-expected), rotation_error_degrees=angle_error,
                review_view='gameplay', field_of_view=float(prop(camera, 'field_of_view')),
                expected_basis='The possessed hero native PlayerCamera component, including its spring-arm collision and ordinary follow/orbit updates; angular feedback is retained')
        evidence = super().camera_evidence()
        if evidence:
            evidence['review_view'] = self.review_view
            evidence['field_of_view'] = float(prop(prop(self.camera, 'camera_component'), 'field_of_view'))
        return evidence

    def begin_gameplay_camera(self, now):
        self.gameplay_attempted = True
        if not valid(self.hero) or not prop(self.hero, 'status').is_alive() or self.pc.get_controlled_pawn() != self.hero:
            self.report['gameplay_capture_skipped_reason'] = 'The original observer was no longer alive and possessed; native consequences were not overridden'
            return
        self.review_view = 'gameplay'
        self.pc.set_view_target_with_blend(self.hero, 0.)
        direction = self.liquid.storm_stream_direction()
        yaw = math.degrees(math.atan2(direction.y, direction.x))
        rotation = prop(self.hero, 'camera').get_world_rotation()
        sensitivity = max(.02, min(1., float(prop(self.hero, 'camera_orbit_sensitivity'))))
        yaw_delta = (yaw-rotation.yaw+180.) % 360.-180.
        self.hero.apply_camera_orbit_input(u.Vector2D(yaw_delta/sensitivity, (-30.-rotation.pitch)/sensitivity))
        self.report.setdefault('camera_input_actions', []).append(dict(game_seconds=now,
            pawn=path(self.hero), action='ApplyCameraOrbitInput', desired_yaw_degrees=yaw,
            desired_pitch_degrees=-30., view_target=path(self.pc.get_view_target())))
        self.stage('native_player_camera')
        self.begin_clip('inflow_gameplay', 3.5)

    def sample_water(self, now):
        direction = self.liquid.storm_stream_direction()
        side = u.Vector(-direction.y, direction.x, 0.)
        outlet = prop(prop(self.liquid, 'water_settings'), 'drain_point')
        age = now-float(prop(self.liquid, 'started_at'))
        readings = {}
        for name, point in self.points.items():
            current = self.liquid.storm_current_at(point)
            toward = outlet-point
            toward.z = 0.
            toward = toward/max(.001, length(toward))
            wet = float(self.liquid.storm_wet_weight_at(point))
            readings[name] = dict(wet_weight=wet,
                native_surface_height_cm=float(self.liquid.surface_height_at(point)),
                native_wave_offset_cm=float(self.liquid.storm_wave_height_at(point)),
                native_current_cm_s2=vec(current), current_speed_cm_s2=length(current),
                downstream_dot=dot(current, direction), toward_throat_dot=dot(current, toward),
                across_dot=dot(current, side))
            if name in ('entrance', 'middle', 'throat') and wet >= .5 and name not in self.first_wet:
                self.first_wet[name] = age
        entry = dict(game_seconds=now, storm_age_seconds=age, stage=self.phase,
            event_phase=enum_name(prop(self.event, 'phase')), water_phase=enum_name(self.liquid.get_phase()),
            probes=readings, jet_visible=bool(prop(self.liquid, 'jet').is_visible()),
            crown_visible=bool(prop(self.liquid, 'crown').is_visible()),
            throat_ribbon_visible=bool(prop(self.liquid, 'drain_ribbon').is_visible()),
            throat_ribbon_has_mesh=valid(prop(prop(self.liquid, 'drain_ribbon'), 'static_mesh')),
            hero_alive=bool(prop(self.hero, 'status').is_alive()) if valid(self.hero) else None,
            raft_positions_cm=[vec(r.get_actor_location()) for r in prop(self.event, 'rafts') if valid(r)],
            native_debris_spawned=int(prop(self.event, 'debris_spawned')))
        ribbon=prop(self.liquid, 'drain_ribbon')
        low, high = ribbon.get_local_bounds()
        entry['throat_sheet_width_cm'] = float((high.y-low.y)*ribbon.get_world_scale().y)
        self.report['samples'].append(entry)

    def place_observer(self, now):
        rafts = [r for r in prop(self.event, 'rafts') if valid(r)]
        self.require(len(rafts) == 2, 'Native raft branch does not have exactly two refuges')
        self.raft = min(rafts, key=lambda r: length(r.get_actor_location()-prop(self.liquid, 'arena_center')))
        if valid(self.hero) and prop(self.hero, 'status').is_alive():
            foot = self.deck_point(-80., 0.)
            center = foot+u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height()+2.)
            self.hero.set_actor_location(center, False, True)
            self.report.setdefault('position_fixtures', []).append(dict(game_seconds=now,
                pawn=path(self.hero), foot_cm=vec(foot), actor_center_cm=vec(center),
                note='Position only; no movement-mode/velocity/body/water assignment'))
        self.observer_placed = True

    def validate_water(self):
        samples = self.report['samples']
        self.require(len(samples) >= 20, 'Too few native water samples')
        self.require(set(self.first_wet) == {'entrance', 'middle', 'throat'}, 'A required front probe was never reached')
        self.require(self.first_wet['entrance']+.15 < self.first_wet['middle']
            and self.first_wet['middle']+.15 < self.first_wet['throat'], 'Front did not wet entrance, middle, throat in order')
        self.report['first_wet_age_seconds'] = self.first_wet
        checks = self.report['checks']
        checks['front_reaches_entrance_middle_throat_in_order'] = True
        behind = [s for s in samples if s['probes']['entrance']['wet_weight'] > .5
            and s['probes']['throat']['wet_weight'] <= .001]
        self.require(behind, 'No observed wet-front/dry-throat separation; water may still fill uniformly')
        dry = self.report['water_geometry']['dry_height_cm']
        self.require(all(abs(s['probes']['throat']['native_surface_height_cm']-dry) < .1 for s in behind),
            'Unreached throat surface did not remain at the native dry height')
        checks['unreached_throat_stays_dry_while_entrance_is_wet'] = True
        self.require(all(not s['jet_visible'] and not s['crown_visible'] for s in samples),
            'Legacy overhead jet or impact crown was visible in the storm branch')
        checks['legacy_overhead_jet_and_crown_hidden'] = True
        held = [s for s in samples if s['water_phase'] == 'HOLDING']
        self.require(len(held) >= 20, 'No sustained Holding observation')
        self.require(all(s['throat_ribbon_visible'] and s['throat_ribbon_has_mesh'] for s in held),
            'Throat ribbon did not remain visible during Holding')
        checks['continuous_throat_outlet_visible_during_holding'] = True
        self.require(all(s['probes'][name]['wet_weight'] > .95
            and s['probes'][name]['current_speed_cm_s2'] > 100.
            and s['probes'][name]['downstream_dot'] > 100.
            for s in held for name in ('entrance', 'middle', 'throat')),
            'Holding lost continuous inlet-to-throat current or wetness')
        checks['sustained_downstream_current_at_all_three_probes'] = True
        self.require(all(s['probes']['convergence']['toward_throat_dot'] > 100.
            and s['probes']['convergence']['across_dot'] < -5. for s in held),
            'Off-axis Holding current did not converge toward the throat')
        checks['off_axis_current_converges_into_throat'] = True
        heights = [s['probes']['middle']['native_surface_height_cm'] for s in held]
        self.require(max(heights)-min(heights) > 20., 'Held native water surface did not show sustained chop')
        checks['holding_chop_continues_after_initial_front'] = True
        self.require(all(s['probes'][name]['wet_weight']>.95
            and s['probes'][name]['current_speed_cm_s2']>100.
            and s['probes'][name]['toward_throat_dot']>100.
            for s in held for name in ('throat_left','throat_right')),
            'The mouth sides remain dry or do not flow toward the throat')
        checks['both_mouth_sides_stay_wet_and_flow_toward_throat'] = True
        direction = self.liquid.storm_stream_direction()
        side = u.Vector(-direction.y,direction.x,0.)
        half = prop(self.liquid, 'half_size')
        full_width = 2*(abs(side.x)*half.x+abs(side.y)*half.y)
        self.require(all(s['throat_sheet_width_cm']>=full_width*.98 for s in held),
            'The visible throat lip is narrower than the mouth')
        checks['throat_outflow_lip_spans_full_mouth_width'] = True
        self.report['full_width_observation'] = dict(expected_mouth_width_cm=full_width,
            minimum_outflow_sheet_width_cm=min(s['throat_sheet_width_cm'] for s in held),
            minimum_side_wetness=min(s['probes'][n]['wet_weight'] for s in held for n in ('throat_left','throat_right')))
        self.report['holding_observation'] = dict(samples=len(held),
            game_duration_seconds=held[-1]['game_seconds']-held[0]['game_seconds'],
            middle_surface_range_cm=max(heights)-min(heights),
            minimum_downstream_acceleration_cm_s2=min(s['probes'][name]['downstream_dot']
                for s in held for name in ('entrance', 'middle', 'throat')))

    def finalize_frames(self):
        for frame in self.report['frames']:
            file = Path(frame['file'])
            self.require(file.is_file(), 'Genuine capture frame is missing: '+str(file))
            data = file.read_bytes()
            self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Engine frame is not PNG')
            frame.update(sha256=hashlib.sha256(data).hexdigest(),
                width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
        if self.capture_hz:
            self.require(all(len(self.report['clips'].get(name, [])) >= 8
                for name in ('inflow_arrival', 'inflow_holding')), 'Required genuine water clip has too few camera-valid frames')
            if 'inflow_gameplay' in self.report['clips']:
                frames = self.report['clips']['inflow_gameplay']
                self.require(len(frames) >= 8, 'Native PlayerCamera clip has too few camera-valid frames')
                self.require(all(frame['camera_evidence']['view_target'] == path(self.hero)
                    and frame['camera_evidence']['expected_target'] == path(self.hero)
                    and frame['camera_evidence']['review_view'] == 'gameplay' for frame in frames),
                    'Gameplay clip did not use the actual hero view target')
                self.report['checks']['native_player_camera_clip_verified_against_hero_component'] = True
        self.report['checks']['real_engine_frames_have_hashes_and_camera_evidence'] = True

    def tick(self, _delta):
        if self.done: return
        try:
            self.require(time.monotonic()-self.started < 240., 'Owned inflow probe exceeded 240 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds: return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one owned standalone authority PIE')
                self.pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()): return
                self.world = worlds[0]
                self.hero = self.pc.get_controlled_pawn()
                self.report['world'] = path(self.world)
                self.stage('first_perk')
                return
            self.require(len(worlds) == 1 and self.world in worlds, 'Owned PIE ended or was replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.pc.is_reward_menu_open():
                self.pc.choose_reward_perk(0)
                if self.phase == 'first_perk': self.stage('perk_resolution')
                else: self.report.setdefault('normal_reward_choices', []).append(now)
                return
            if self.last_game_time is not None and now <= self.last_game_time: return
            self.last_game_time = now
            if self.phase == 'first_perk': return
            if self.phase == 'perk_resolution':
                self.pc.request_dev_action(u.MCDevAction.CATACLYSM_RAFTS, -1)
                self.stage('launch')
                return
            if self.phase == 'launch':
                events = [e for e in self.actors(u.MCCataclysmEvent)
                    if enum_name(prop(e, 'phase')) not in ('DORMANT', 'COMPLETE', 'STOPPED')]
                self.require(now-self.phase_at < 3. and len(events) <= 1, 'F3 did not launch exactly one event')
                if not events: return
                self.event = events[0]
                self.require(enum_name(prop(self.event, 'variant')) == 'RAFTS', 'F3 branch is not rafts')
                self.tongue = self.actors(u.MCTongue)[0]
                self.episode_started_at = now
                self.report['default_event_timings'] = {name: float(prop(self.event, name))
                    for name in ('flood_fill_seconds', 'flood_hold_seconds', 'flood_drain_seconds')}
                self.scheduler = prop(u.GameplayStatics.get_game_state(self.world), 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_tick = self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
                self.report['checks']['real_f3_64_starts_untuned_raft_event'] = True
                self.stage('wait_storm')
                return
            self.require(now-self.episode_started_at < 40., 'Arrival/8s Holding episode exceeded 40 game seconds')
            self.require(valid(self.event) and not prop(self.event, 'failed'), 'Native event failed')
            if self.phase == 'wait_storm':
                liquid = prop(self.event, 'liquid')
                if not valid(liquid) or not liquid.is_cataclysm_storm(): return
                self.liquid = liquid
                self.water_identity = path(liquid)
                self.set_points()
                self.stage('storm_arrival')
                self.begin_clip('inflow_arrival', float(prop(prop(liquid, 'water_settings'), 'fill_seconds'))+.5)
            elif self.phase == 'capture_flush':
                if now-self.phase_at >= .8:
                    self.finalize_frames()
                    self.finish()
                return
            self.require(valid(self.liquid) and path(self.liquid) == self.water_identity
                and self.liquid.is_cataclysm_storm(), 'Observed storm liquid ended or was replaced too soon')
            if self.review_view != 'gameplay':
                self.camera_for_water()
                self.keep_camera()
            if now >= self.sample_at:
                self.sample_at = now+.05
                self.sample_water(now)
            if self.clip and now > self.clip_until: self.clip = None
            if self.capture_hz and self.clip and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                camera = self.camera_evidence()
                if camera and camera['view_target'] == camera['expected_target'] \
                    and camera['location_error_cm'] < 12. and camera['rotation_error_degrees'] < 5.:
                    self.screenshot('%05d.png' % len(self.report['frames']), True)
            if enum_name(self.liquid.get_phase()) == 'HOLDING':
                if self.holding_started_at is None:
                    self.holding_started_at = now
                    self.place_observer(now)
                    self.stage('storm_holding')
                held = now-self.holding_started_at
                if held >= .5 and self.review_view == 'front':
                    self.review_view = 'throat'
                    self.stage('continuous_throat_outlet')
                    self.begin_clip('inflow_holding', 7.5)
                if held >= 4.5 and not self.gameplay_attempted:
                    self.begin_gameplay_camera(now)
                if held >= 8.:
                    self.validate_water()
                    self.clip = None
                    self.stage('capture_flush')
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if not self.done:
            try:
                if valid(self.scheduler) and self.scheduler_tick is not None:
                    self.scheduler.set_actor_tick_enabled(self.scheduler_tick)
                    self.report['scheduler_restored'] = self.scheduler.is_actor_tick_enabled() == self.scheduler_tick
                if self.episode_started_at is not None and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
                    self.report['episode_game_seconds'] = float(u.GameplayStatics.get_time_seconds(self.world))-self.episode_started_at
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


def main():
    for key in (KEY, 'mc_cataclysm_solo', 'mc_cataclysm_storm_solo', 'mc_rowing_animation_solo',
                'mc_cataclysm_drowning_solo', 'mc_approved_environment_solo'):
        old = getattr(builtins, key, None)
        if old is not None and not getattr(old, 'done', True):
            raise RuntimeError('Existing owned probe is active and was preserved: '+key)
    probe = CataclysmInflowSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
