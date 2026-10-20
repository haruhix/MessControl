"""Controlled small-mint gameplay/VFX fixture in one owned real L_Mouth PIE.

Waits for the real resource barrier and ordinary first perk. Chooses that perk,
disables only the runtime sequence actor's tick before any food is issued, then
calls the real SmallMintCandy.Start using the saved DA_SingleDay.SmallMint.
Runtime pawn placement isolates a standing wave, ordinary pickaxe self-rescue,
a best-effort jump through the repeat, and normal pickaxe destruction nearby.
Self rescue uses one ordinary held primary input. Mint destruction uses repeated
SwingBrush click callbacks (the real SwingAction Started binding).
No clock, radius, health, attack schedule, camera zoom, profile or package edits.
The existing character camera stays active; only normal orbit input aims it.

Requires an idle saved owned Editor (-MCHubOwned or -MCFirstRelaxOwned), fresh
DLL/art, and no concurrent probe. Run() never builds or launches an Editor.
Optional MC_SMALL_MINT_OUTPUT (fresh E: directory), MC_SMALL_MINT_CAPTURE_HZ
(15..24; default15). A1280x720 client is requested; the owned-window PNG API
also includes native window chrome. Actual sizes/timing are recorded, and output
is capped at1000frames/500MiB for subsequent client-area video encoding.
FrameTimes.csv records achieved wall timing; capture rate is not simulated.
Cancel only this probe: builtins.mc_small_mint_solo.cancel().
"""
import builtins
import hashlib
import importlib.util
import math
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_SMALL_MINT_OUTPUT',
    'E:/CodexWork/MessControlNextRelax_20261020/SoloMint04')).resolve()
KEY = 'mc_small_mint_solo'
spec = importlib.util.spec_from_file_location('mc_small_mint_loading_support',
    ROOT / 'Tools/Unreal/check_resource_loading_solo.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
support.OUT = support.helpers.OUT = OUT
helpers = support.helpers
valid, path, prop, enum_name, require = support.valid, support.path, support.prop, support.enum_name, support.require


class SmallMintSolo(support.ResourceLoadingSolo):
    def __init__(self):
        require(hasattr(u, 'MCSmallMintCandy') and hasattr(u, 'MCSmallMintCandySettings'), 'New mint DLL is not loaded')
        super().__init__()
        self.capture_hz = max(15., min(24., float(globals().get('MC_SMALL_MINT_CAPTURE_HZ', 15))))
        self.mint = self.tongue = None
        self.first_landed = self.first_wave_at = self.repeat_wave_at = None
        self.health_before = self.position_before = None
        self.primary_held = self.jump_held = False
        self.jump_attempted = self.jump_released = self.jump_cleared = False
        self.jump_peak = 0.
        self.expected_repeat_at = None
        self.owned_pawn = None
        self.frozen_seen = self.repeat_seen = self.vfx_front_seen = False
        self.next_pickaxe_at = 0.
        self.pickaxe_requests = 0
        self.ice_contacts = self.mint_contacts = 0
        self.last_ice_health = self.last_mint_health = None
        self.report.update(scope=__doc__, controlled_runtime_fixture=True,
            gameplay_overrides=['Disable only native runtime sequence tick after normal first perk',
                'Spawn one real mint through world-context GameplayStatics and call its public Start',
                'Place owned pawn on verified tongue footprints for standing and close tool contact'],
            source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            limitations=['Controlled isolated VFX/tool fixture, not a full natural second-Relax run.',
                'No time/radius/health/schedule/camera-zoom override; ordinary character camera remains active.',
                'Jump uses normal held input timed from replicated wave fields; a missed manual-input window is reported, not forged.',
                'Normal input callbacks are invoked by Python; physical keyboard/mouse delivery is not tested.',
                'SwingDuration and ConfirmedHitCount are native non-reflected members; SwingStartedAt is protected. The probe uses a conservative 1.10-second destruction-click cadence when no reflected duration getter exists; native cooldown validation remains active. Input requests and observed ice/mint health reductions are reported, without an invented accepted-swing/native-hit count.',
                'No multiplayer, packaging, asset saves or quantitative performance validation.',
                'Requested capture15–24Hz is bounded by real game-window readback speed; CSV is the timing evidence.'])
        self.report['limitations'].append('CapturePIEGameWindow includes native window chrome; a1280x720 client previously produced1286x760 PNG. Actual dimensions are retained for final video cropping/letterboxing.')

    def floor(self, point):
        result = prop(self.tongue, 'surface').line_trace_component(
            point+u.Vector(0, 0, 2500), point-u.Vector(0, 0, 2500), True, False, False)
        if result is None or result is False:
            return None
        values = list(result) if isinstance(result, (tuple, list)) else [result]
        for value in reversed(values):
            try:
                impact = value.get_editor_property('impact_point')
                if isinstance(impact, u.Vector):
                    return impact
            except Exception:
                pass
        if values and isinstance(values[0], u.Vector):
            return values[0]
        raise RuntimeError('Unknown LineTraceComponent result ' + repr(result))

    def place_at_distance(self, distance):
        center = prop(self.mint, 'landing_point')
        for index in range(24):
            angle = math.pi + index*math.tau/24.
            point = center+u.Vector(math.cos(angle)*distance, math.sin(angle)*distance, 0)
            floor = self.floor(point)
            if floor is None:
                continue
            supports = [self.floor(floor+u.Vector(dx, dy, 0))
                for dx, dy in ((60, 0), (-60, 0), (0, 60), (0, -60))]
            if any(item is None or abs(item.z-floor.z)>35. for item in supports):
                continue
            self.hero.get_movement_component().stop_movement_immediately()
            height = prop(self.hero, 'capsule_component').get_scaled_capsule_half_height()
            location = floor+u.Vector(0, 0, height+2)
            yaw = u.MathLibrary.find_look_at_rotation(location, center).yaw
            self.hero.set_actor_location_and_rotation(location, u.Rotator(yaw=yaw), False, True)
            self.hero.get_movement_component().set_movement_mode(u.MovementMode.MOVE_WALKING)
            self.report['requests'].append(dict(method='Runtime owned-pawn placement', phase=self.phase,
                requested_distance_cm=distance, location_cm=helpers.vec(location), floor_cm=helpers.vec(floor),
                game_seconds=self.now()))
            return
        raise RuntimeError('No supported tongue footprint at distance ' + str(distance))

    def aim_normal_camera(self):
        camera = prop(self.hero, 'camera')
        view = camera.get_world_rotation()
        center = prop(self.mint, 'landing_point')
        pivot = self.hero.get_actor_location()+u.Vector(0, 0, float(prop(self.hero, 'camera_orbit_height')))
        desired = u.MathLibrary.find_look_at_rotation(pivot, center+u.Vector(0, 0, 55))
        desired.pitch = -38.
        sensitivity = max(.02, min(1., float(prop(self.hero, 'camera_orbit_sensitivity'))))
        yaw = (desired.yaw-view.yaw+180.)%360.-180.
        self.hero.apply_camera_orbit_input(u.Vector2D(yaw/sensitivity, (desired.pitch-view.pitch)/sensitivity))
        self.report['requests'].append(dict(method='Normal ApplyCameraOrbitInput, no zoom',
            game_seconds=self.now(), yaw=float(desired.yaw), pitch=float(desired.pitch)))

    def primary(self, held):
        if self.primary_held != held:
            self.hero.set_primary_input_held(bool(held));self.primary_held = bool(held)
            self.report['requests'].append(dict(method='SetPrimaryInputHeld', held=bool(held), game_seconds=self.now()))

    def jump(self, held):
        if self.jump_held != held:
            self.hero.set_jump_input_held(bool(held));self.jump_held = bool(held)
            self.report['requests'].append(dict(method='SetJumpInputHeld', held=bool(held), game_seconds=self.now()))

    def pickaxe_click(self):
        now = self.now()
        if now < self.next_pickaxe_at:
            return
        inventory = prop(self.hero, 'inventory')
        if prop(inventory, 'selected') != u.MCToolSlot.PICKAXE:
            inventory.server_select(u.MCToolSlot.PICKAXE)
            self.report['requests'].append(dict(method='Normal ServerSelect Pickaxe', game_seconds=now,
                selected=enum_name(prop(inventory, 'selected'))))
            if prop(inventory, 'selected') != u.MCToolSlot.PICKAXE:
                self.next_pickaxe_at = now + .2
                return
        self.hero.swing_brush()
        duration_getter = getattr(inventory, 'swing_duration', None)
        duration = max(.1, float(duration_getter())) + .05 if callable(duration_getter) else 1.10
        self.next_pickaxe_at = now + duration
        self.pickaxe_requests += 1
        self.report['requests'].append(dict(method='SwingBrush (ordinary SwingAction Started callback)',
            phase=self.phase, game_seconds=now, request_number=self.pickaxe_requests,
            selected=enum_name(prop(inventory, 'selected')), next_click_at=self.next_pickaxe_at,
            cadence_source='reflected SwingDuration + .05' if callable(duration_getter) else 'conservative 1.10-second cadence; native validator retained'))

    def setup_fixture(self):
        require(isinstance(self.hero, u.MCToothCharacter) and valid(self.sequence), 'Normal gameplay pawn/director missing')
        require(not self.food_rows(), 'Food already issued; preserve this run and retry a fresh owned probe')
        self.owned_pawn = path(self.hero)
        self.sequence.set_actor_tick_enabled(False)
        tongues = self.actors(u.MCTongue)
        require(len(tongues) == 1, 'One real authored tongue is required')
        self.tongue = tongues[0]
        transform = u.Transform()
        statics = u.get_default_object(u.GameplayStatics)
        self.mint = statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world,
            u.MCSmallMintCandy, transform, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            self.hero, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        require(valid(self.mint), 'World-context deferred mint spawn failed')
        statics.call_method('FinishSpawningActor', args=(self.mint, transform,
            u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        config = prop(u.load_asset('/Game/Gameplay/CoreLoop/DA_SingleDay'), 'small_mint')
        require(self.mint.start(self.tongue, config, 41791), 'The real mint Start failed')
        self.place_at_distance(550.)
        self.aim_normal_camera()
        prop(self.hero, 'inventory').server_select(u.MCToolSlot.PICKAXE)
        self.health_before = float(prop(prop(prop(self.hero, 'status'), 'state'), 'health'))
        self.position_before = helpers.vec(self.hero.get_actor_location())
        self.last_ice_health = float(prop(self.hero, 'ice_leg_health'))
        self.last_mint_health = float(prop(self.mint, 'health'))
        self.report['fixture'] = dict(actor=path(self.mint), tongue=path(self.tongue),
            sequence=path(self.sequence), native_stage_at_stop=enum_name(prop(self.sequence, 'stage')),
            settings_source='/Game/Gameplay/CoreLoop/DA_SingleDay.SmallMint', health_before=self.health_before,
            starting_position_cm=self.position_before, camera_zoom_changed=False)
        self.stage('mint_fall')

    def waves(self):
        return [wave for wave in self.actors(u.MCHazardWave) if wave.get_owner() == self.mint]

    def snapshot(self):
        settings = prop(self.mint, 'settings')
        center, position = prop(self.mint, 'landing_point'), self.hero.get_actor_location()
        floor = self.floor(position)
        sole = position.z-prop(self.hero, 'capsule_component').get_scaled_capsule_half_height()
        self.jump_peak = max(self.jump_peak, sole-floor.z) if floor is not None else self.jump_peak
        waves = self.waves()
        for presenter_owner in [self.mint] + waves:
            for component in presenter_owner.get_components_by_class(u.ProceduralMeshComponent):
                tags = [str(value) for value in prop(component, 'component_tags')]
                if 'MC.FrostNova.Front' in tags and component.is_visible():
                    self.vfx_front_seen = True
        row = dict(phase=self.phase, game_seconds=self.now(), wall_seconds=time.monotonic()-self.started,
            mint_stage=enum_name(prop(self.mint, 'stage')), mint_health=float(prop(self.mint, 'health')),
            mint_max_health=float(prop(self.mint, 'max_health')), landed_at=float(prop(self.mint, 'landed_at')),
            next_wave_at=float(prop(self.mint, 'next_wave_at')), waves_emitted=int(prop(self.mint, 'waves_emitted')),
            hero_position_cm=helpers.vec(position), hero_health=float(prop(prop(prop(self.hero, 'status'), 'state'), 'health')),
            body_state=enum_name(prop(self.hero, 'tooth_physics').get_body_state()),
            frozen_legs=bool(self.hero.has_frozen_legs()), foot_ice_health=float(prop(self.hero, 'ice_leg_health')),
            sole_clearance_cm=float(sole-floor.z) if floor is not None else None,
            wave_travel_seconds=float(prop(settings, 'wave_travel_seconds')), wave_radius_cm=float(prop(settings, 'wave_radius')),
            mint_distance_xy_cm=math.hypot(position.x-center.x, position.y-center.y),
            waves=[dict(actor=path(wave), started_at=float(prop(wave, 'started_at')),
                freeze_only=bool(prop(wave, 'freeze_legs')), damage=float(prop(wave, 'damage')),
                hit_count=int(prop(wave, 'hit_count'))) for wave in waves], frost_front_seen=self.vfx_front_seen)
        row['selected_tool'] = enum_name(prop(prop(self.hero, 'inventory'), 'selected'))
        if row['foot_ice_health'] < self.last_ice_health - .001 and row['mint_health'] > 0:
            self.ice_contacts += 1
            self.report['requests'].append(dict(method='Observed real pickaxe ice-health reduction',
                phase=self.phase, game_seconds=self.now(), before=self.last_ice_health,
                after=row['foot_ice_health'], observed_ice_health_reductions=self.ice_contacts))
        if row['mint_health'] < self.last_mint_health - .001:
            self.mint_contacts += 1
            self.report['requests'].append(dict(method='Observed real pickaxe mint-health reduction',
                phase=self.phase, game_seconds=self.now(), before=self.last_mint_health,
                after=row['mint_health'], observed_mint_health_reductions=self.mint_contacts))
        self.last_ice_health, self.last_mint_health = row['foot_ice_health'], row['mint_health']
        row.update(pickaxe_click_requests=self.pickaxe_requests,
            observed_ice_health_reductions=self.ice_contacts, observed_mint_health_reductions=self.mint_contacts)
        self.report['tool_contacts'] = dict(click_requests=self.pickaxe_requests,
            observed_ice_health_reductions=self.ice_contacts, observed_mint_health_reductions=self.mint_contacts,
            native_hit_counter_available=False, native_counter_unavailable_reason='ConfirmedHitCount is not reflected; health reductions are independently observed')
        require(abs(row['hero_health']-self.health_before)<.01, 'Unexpected health damage during the isolated mint fixture')
        if time.monotonic() >= self.sample_at:
            self.sample_at = time.monotonic()+.08
            self.report['samples'].append(row)
        return row

    def advance_mint(self):
        elapsed = self.now()-self.phase_game
        if self.phase == 'observe_preparation':
            require(time.monotonic()-self.phase_wall<150, 'Normal load/perk prerequisite timed out')
            require(not self.preparation.has_failed(), 'Real preparation failed before fixture')
            if self.preparation.is_ready() and self.pc.is_reward_menu_open():
                self.check('real_preload_ready_before_fixture', True, '')
                self.pc.choose_reward_perk(0)
                self.stage('await_perk_resolution')
            return
        if self.phase == 'await_perk_resolution':
            require(time.monotonic()-self.phase_wall<15, 'Normal first perk did not resolve')
            if not self.pc.is_reward_menu_open():
                self.check('ordinary_first_perk_resolved', True, '')
                self.setup_fixture()
            return
        row = self.snapshot()
        if row['waves_emitted'] >= 1 and self.first_wave_at is None:
            self.first_wave_at = min((item['started_at'] for item in row['waves']), default=self.now())
            delay = self.first_wave_at-row['landed_at']
            self.check('first_wave_three_to_five_seconds_after_landing', 2.98<=delay<=5.2,
                'Actual first pulse violated the landing-relative 3–5 second contract: ' + str(delay))
        if self.phase == 'mint_fall' and row['mint_stage'] == 'ACTIVE':
            self.first_landed = row['landed_at']
            self.capture('01_mint_landed.png')
            self.stage('await_standing_front')
        elif self.phase == 'await_standing_front':
            require(elapsed<15, 'Standing player did not meet the first real front')
            if row['frozen_legs']:
                self.frozen_seen = True
                self.check('actual_first_front_freezes_standing_feet', True, '')
                self.check('front_is_freeze_only', all(item['freeze_only'] and item['damage']==0 for item in row['waves']), 'Wrong wave payload')
                self.capture('02_actual_frozen_feet.png')
                self.stage('native_self_rescue')
                self.primary(True)
        elif self.phase == 'native_self_rescue':
            require(elapsed<12, 'One ordinary held pickaxe input failed to release frozen feet')
            if not row['frozen_legs']:
                self.check('ordinary_pickaxe_self_rescue', row['foot_ice_health']==0 and self.ice_contacts>0 and self.primary_held,
                    'One held pickaxe input did not clear its real foot-ice health')
                self.primary(False)
                self.expected_repeat_at = row['next_wave_at']
                self.jump_peak = 0.
                self.stage('repeat_jump')
        elif self.phase == 'repeat_jump':
            predicted = self.expected_repeat_at+row['mint_distance_xy_cm']/max(1., row['wave_radius_cm'])*row['wave_travel_seconds']
            if not self.jump_attempted and self.now()>=predicted-.22:
                self.jump_attempted = True;self.jump_requested_at = self.now();self.jump(True)
            if self.jump_held and self.now()-self.jump_requested_at>=.30:
                self.jump(False);self.jump_released = True
            if row['waves_emitted']>=2:
                self.repeat_seen = True
                if self.repeat_wave_at is None:
                    self.repeat_wave_at = max((item['started_at'] for item in row['waves']), default=self.now())
                crossed_at = self.repeat_wave_at+row['mint_distance_xy_cm']/max(1., row['wave_radius_cm'])*row['wave_travel_seconds']
                if self.now()>crossed_at+.55:
                    self.jump(False)
                    self.jump_cleared = not row['frozen_legs'] and self.jump_attempted and self.jump_peak>12
                    self.report['jump_trial'] = dict(attempted=self.jump_attempted, cleared=self.jump_cleared,
                        measured_peak_sole_clearance_cm=self.jump_peak,
                        limitation=None if self.jump_cleared else 'Real input missed the short crossing window; no freeze state was overridden.')
                    self.check('real_mint_repeats_while_alive', True, '')
                    if row['frozen_legs']:
                        self.stage('repeat_rescue');self.primary(True)
                    else:
                        self.place_at_distance(125.);self.stage('native_break_mint');self.pickaxe_click()
            require(elapsed<20, 'No second real mint front appeared')
        elif self.phase == 'repeat_rescue':
            require(elapsed<12, 'Repeat self rescue failed')
            if not row['frozen_legs']:
                self.primary(False)
                self.place_at_distance(125.);self.stage('native_break_mint');self.pickaxe_click()
        elif self.phase == 'native_break_mint':
            require(elapsed<25, 'Normal repeated pickaxe clicks did not break the actual mint')
            if row['mint_stage']=='COMPLETE':
                self.check('normal_pickaxe_broke_real_mint', row['mint_health']==0, 'Mint completed without reaching zero health')
                self.check('mint_completion_cancels_owned_fronts', not row['waves'], 'An owned front survived mint destruction')
                self.check('no_health_damage_from_mint', abs(row['hero_health']-self.health_before)<.01, 'Health changed')
                self.report['frost_nova_geometry_observed'] = self.vfx_front_seen
                self.check('real_frost_nova_geometry_present', self.vfx_front_seen,
                    'The required nova art was not observed; fallback ring alone is not a VFX pass')
                self.capture('03_mint_broken.png')
                self.stage('after_broken')
            else:
                self.pickaxe_click()
        elif self.phase == 'after_broken' and elapsed>=1.:
            self.check('broken_mint_stays_complete', row['mint_stage']=='COMPLETE' and not row['waves'], 'Mint resumed after destruction')
            self.begin_finish()

    def tick(self, _delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            if self.phase == 'ending_pie':
                if not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True):
                    self.report['cleanup']['owned_pie_stopped'] = True;self.finish()
                elif time.monotonic()-self.phase_wall>12:
                    self.error = self.error or 'Owned PIE did not stop within twelve seconds';self.finish()
                return
            require(time.monotonic()-self.started<300., 'Controlled mint solo exceeded 300-second wall bound')
            if self.phase == 'warming_editor':
                if time.monotonic()-self.started>=3:
                    self.start()
                return
            require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
            if not self.refresh_world():
                require(time.monotonic()-self.phase_wall<150, 'Owned gameplay world/subsystem did not appear')
                return
            if self.owned_pawn:
                require(path(self.hero)==self.owned_pawn, 'Owned normal pawn was replaced; preserve replacement')
            now_wall = time.monotonic()
            if valid(self.mint) and now_wall>=self.frame_at:
                if len(self.report['frames'])<1000 and self.frame_bytes<496*1024*1024:
                    self.frame_at=now_wall+1./self.capture_hz
                    self.capture('%05d.png' % len(self.report['frames']), sequence=True)
                else:
                    self.report['capture_limit_reached'] = True
            self.advance_mint()
        except Exception:
            self.begin_finish(traceback.format_exc())
        finally:
            self.busy = False

    def begin_finish(self, error=None):
        if valid(self.hero) and self.owned_worlds():
            try:
                self.primary(False);self.jump(False)
            except Exception:
                self.report['cleanup']['input_release_error'] = traceback.format_exc()
        super().begin_finish(error)


def main():
    for key in (KEY, support.KEY, helpers.KEY):
        prior = getattr(builtins, key, None)
        require(prior is None or getattr(prior, 'done', True), 'Another owned probe is active: ' + key)
    probe = SmallMintSolo()
    setattr(builtins, KEY, probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback = u.register_slate_post_tick_callback(probe.tick)


if __name__ == '__main__':
    main()
