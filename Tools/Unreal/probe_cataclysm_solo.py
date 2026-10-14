"""Short, owned Cataclysm standalone PIE proof, driven by real F3/input paths.

Run after the Editor build, in idle L_Mouth. MC_CATACLYSM_MODE='rafts' (default)
or 'jellies'; each invocation owns exactly one local authority PIE world.
Two stationary allied NPC pawns are weight fixtures, never network clients.
Cancellation: builtins.mc_cataclysm_solo.finish('Stopped by operator').
No package saves, native damage/care helpers, invented zero dirt, or clock jumps.
The short check leaves full mouth cleanup/balance as explicit manual checkpoints.
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import time
import traceback
import uuid
from pathlib import Path

import unreal as u

KEY = 'mc_cataclysm_solo'
MODE = str(globals().get('MC_CATACLYSM_MODE', 'rafts')).lower()
if MODE not in ('rafts', 'jellies'):
    raise RuntimeError('MC_CATACLYSM_MODE must be rafts or jellies')
ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_CATACLYSM_OUTPUT', Path(u.Paths.project_saved_dir()) / 'Checks/CataclysmSolo')).resolve()
RUN = OUTPUT / 'Runs' / (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '_' + MODE + '_' + uuid.uuid4().hex[:8])
support = runpy.run_path(str(ROOT / 'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='cataclysm_probe_support', init_globals={'MC_ENV_OUTPUT': RUN,
    'MC_ENV_CAPTURE_HZ': globals().get('MC_CATACLYSM_CAPTURE_HZ', 8)})
Base, prop, vec = (support[name] for name in ('ApprovedEnvironmentSolo', 'prop', 'vec'))


def valid(actor):
    return actor is not None and u.SystemLibrary.is_valid(actor)


def path(actor):
    return actor.get_path_name() if valid(actor) else None


def enum_name(value):
    return str(getattr(value, 'name', str(value).rsplit('.', 1)[-1])).upper()


class CataclysmSolo(Base):
    def __init__(self):
        super().__init__()
        self.event = self.raft = None
        self.bots = []
        self.bot_controllers = []
        self.clip = None
        self.clip_until = 0.
        self.observed_phases = []
        self.sink_clock = None
        self.sink_samples = []
        self.brace = False
        self.brush_target = None
        self.kill_target = None
        self.killed_jellies = []
        self.health_changes = []
        self.last_jelly_health = None
        self.initial_patch_paths = set()
        self.resume_primary = False
        self.report.update(mode=MODE, scope='One owned standalone authority PIE on L_Mouth; real F3 Cataclysm and held player input',
            wall_time_limit_seconds=240,
            tuning_reason='Raft hold/fill/drain shortened only for this local check; jelly health remains the native default',
            fixture_tuning={}, clips={}, samples=[], checks={}, run_directory=str(RUN),
            limitations=['Position-only placement of the observer and stationary allied NPC weight fixtures is not a skill/balance playthrough.',
                'Raft mode uses a 26-second deep-water hold; jelly mode uses the unmodified default 360 HP.',
                'Normal Pickaxe and brush inputs drive damage and cleaning; no damage/care helper or health/dirt zeroing is used.',
                'Cleanup persistence and one real brush improvement are checked; complete mouth cleanup remains a manual checkpoint.',
                'No network clients, editor map changes, package saves, build/cook/package or broad test suite.'])
        sources = ['Source/MessControl/Private/MCCataclysmEvent.cpp', 'Source/MessControl/Private/MCCataclysmRaft.cpp',
                   'Source/MessControl/Private/MCCataclysmJelly.cpp', 'Source/MessControl/Private/MCToothMovementComponent.cpp',
                   'Tools/Unreal/probe_cataclysm_solo.py']
        self.report['source_sha256'] = {s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest() for s in sources}
        self.report['dirty_maps_before'] = [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output / 'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT / ('Report_' + MODE + '.json')).write_text(data, encoding='utf-8')

    def spawn(self, cls, pose, owner=None):
        actor = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, cls, pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, owner, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(actor), 'Could not spawn fixture ' + str(cls))
        self.statics.call_method('FinishSpawningActor', args=(actor, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        return actor

    def place_pawn(self, pawn, foot, yaw=0., swimming=False):
        movement = pawn.get_movement_component()
        movement.stop_movement_immediately()
        center = foot + u.Vector(0, 0, pawn.capsule_component.get_scaled_capsule_half_height() + 2.)
        pawn.set_actor_location_and_rotation(center, u.Rotator(yaw=yaw), False, True)
        movement.set_movement_mode(u.MovementMode.MOVE_SWIMMING if swimming else u.MovementMode.MOVE_WALKING)
        if pawn == self.hero:
            self.pc.set_control_rotation(u.Rotator(yaw=yaw))
        self.report.setdefault('position_fixtures', []).append(dict(game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            pawn=path(pawn), foot_cm=vec(foot), yaw=yaw, swimming=swimming))

    def deck_point(self, x, y):
        return prop(self.raft, 'deck').get_world_transform().transform_location(u.Vector(x, y, 37.5))

    def water_foot(self, local_x=-180.):
        pose = self.raft.get_actor_transform()
        point = pose.transform_location(u.Vector(local_x, 0, 0))
        liquid = prop(self.event, 'liquid')
        level = prop(liquid, 'surface').get_world_location().z
        point.z = level - 10. - self.hero.capsule_component.get_scaled_capsule_half_height()
        return point

    def make_weights(self):
        mode = u.GameplayStatics.get_game_mode(self.world)
        for index in range(2):
            controller = self.spawn(u.MCPlaytestBotController, u.Transform())
            controller.set_actor_tick_enabled(False)
            identity = prop(controller, 'player_state')
            self.require(valid(identity) and not identity.call_method('IsOnlyASpectator', args=()),
                         'Allied weight fixture needs a nonspectator PlayerState')
            pawn = self.spawn(prop(mode, 'default_pawn_class'), u.Transform(location=self.hero.get_actor_location() + u.Vector(400 + index * 120, 0, 120)), controller)
            controller.possess(pawn)
            self.require(prop(pawn, 'player_state') == identity, 'Pawn did not inherit its fixture identity')
            self.bot_controllers.append(controller)
            self.bots.append(pawn)
        self.report['weight_fixtures'] = [dict(controller=path(c), pawn=path(p), ai_tick=False)
            for c, p in zip(self.bot_controllers, self.bots)]

    def frame_raft(self):
        center = self.deck_point(0, 0)
        self.frame_camera(center + u.Vector(360, -450, 290), center + u.Vector(0, 0, 35))
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 80.)

    def keep_camera(self):
        if valid(self.camera):
            self.pc.set_editor_property('bAutoManageActiveCameraTarget', False)
            self.pc.set_view_target_with_blend(self.camera, 0.)

    def camera_evidence(self):
        if not valid(self.camera):
            return None
        manager = prop(self.pc, 'player_camera_manager')
        actual = manager.get_camera_location()
        expected = self.camera.get_actor_location()
        error = math.sqrt((actual.x - expected.x)**2 + (actual.y - expected.y)**2 + (actual.z - expected.z)**2)
        rotation = manager.get_camera_rotation()
        target_rotation = self.camera.get_actor_rotation()
        angle_error = max(abs((a - b + 180.) % 360. - 180.) for a, b in
            ((rotation.pitch, target_rotation.pitch), (rotation.yaw, target_rotation.yaw), (rotation.roll, target_rotation.roll)))
        return dict(view_target=path(self.pc.get_view_target()), expected_target=path(self.camera),
                    actual_cm=vec(actual), expected_cm=vec(expected), location_error_cm=error,
                    rotation_error_degrees=angle_error)

    def begin_clip(self, name, seconds=5.):
        self.clip = name
        self.clip_until = float(u.GameplayStatics.get_time_seconds(self.world)) + seconds
        self.report['clips'].setdefault(name, [])

    def screenshot(self, name, sequence=False):
        super().screenshot(name, sequence)
        entry = self.report['frames' if sequence else 'screenshots'][-1]
        entry['clip_window'] = self.clip
        entry['camera_evidence'] = self.camera_evidence()
        if sequence and self.clip:
            self.report['clips'][self.clip].append(entry)

    def sample(self, now):
        if not valid(self.event):
            return
        current = enum_name(prop(self.event, 'phase'))
        if not self.observed_phases or self.observed_phases[-1] != current:
            self.observed_phases.append(current)
        movement = self.hero.get_movement_component()
        base = movement.call_method('GetMovementBaseObject', args=())
        rafts = [dict(path=path(r), occupants=int(prop(r, 'occupant_count')), warning=float(prop(r, 'warning_started_at')),
            sink=float(prop(r, 'sink_started_at')), submerging=bool(r.is_submerging()), position_cm=vec(r.get_actor_location()))
            for r in prop(self.event, 'rafts') if valid(r)]
        jellies = [dict(path=path(j), health=float(prop(j, 'health')), pending=bool(prop(j, 'pending_rupture')),
            ruptured_at=float(prop(j, 'ruptured_at')), defeated=bool(prop(j, 'defeated'))) for j in prop(self.event, 'jellies') if valid(j)]
        self.report['samples'].append(dict(game_seconds=now, stage=self.phase, event_phase=current,
            variant=enum_name(prop(self.event, 'variant')), dirt_left=int(prop(self.event, 'dirt_left')),
            jellies_left=int(prop(self.event, 'jellies_left')), rafts=rafts, jellies=jellies,
            hero_cm=vec(self.hero.get_actor_location()), hero_alive=bool(prop(self.hero, 'status').is_alive()),
            movement_mode=str(prop(movement, 'movement_mode')), movement_base=path(base),
            swim_energy=float(movement.get_swim_energy()), deep_water=bool(movement.is_in_deep_water()),
            brace_target=path(prop(self.hero, 'grip').brace_target()), primary=self.primary))

    def event_patches(self):
        # Batch is protected from Python/toolset reads. Native event ownership,
        # the residue tag and the pre-F3 actor inventory identify this isolated
        # encounter's real patches without exposing production fields for tests.
        return [p for p in self.actors(u.MCMouthSurface)
                if not prop(p, 'ulcer') and path(p) not in self.initial_patch_paths
                and (p.get_owner() == self.event or p.actor_has_tag('MCFoodServedDirt'))
                and int(prop(prop(p, 'status'), 'state').coffee_left) > 0]

    def begin_cleanup_checkpoint(self):
        self.primary_input(False)
        self.hero.call_method('SetBraceInputHeld', args=(False,))
        self.brace = False
        self.require(enum_name(prop(self.event, 'phase')) == 'CLEANUP', 'Hazard did not transition into actual cleanup')
        self.require(int(prop(self.event, 'dirt_left')) > 0, 'No remaining native consequences to verify cleanup gating')
        self.cleanup_dirt_before = int(prop(self.event, 'dirt_left'))
        self.stage('cleanup_wait')

    def begin_brush_checkpoint(self):
        patches = self.event_patches()
        self.require(patches, 'No brushable event patch; inspect personal/tooth care manually')
        self.brush_target = min(patches, key=lambda p: (p.get_actor_location().x - self.hero.get_actor_location().x)**2
            + (p.get_actor_location().y - self.hero.get_actor_location().y)**2)
        self.brush_before = int(prop(prop(self.brush_target, 'status'), 'state').coffee_left)
        self.brush_wipe_before = sum(prop(self.brush_target, 'wipe_mask'))
        center = self.brush_target.get_actor_location()
        half = float(prop(self.brush_target, 'liquid_half_size'))
        foot = self.floor(center - u.Vector(min(half * .2, 75.), 0, 0))
        self.require(foot is not None, 'No supported brush approach to an actual event patch')
        self.move_to(foot)
        self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
        self.frame_camera(center + u.Vector(250, -370, 310), center)
        self.primary_input(True)
        self.begin_clip('cleanup_brush', 4.)
        self.stage('cleanup_brush')

    def begin_jelly_kill(self):
        targets = [j for j in prop(self.event, 'jellies') if valid(j) and float(prop(j, 'health')) > 0]
        self.require(targets, 'No live jelly for normal Pickaxe input')
        self.kill_target = targets[0]
        self.last_jelly_health = float(prop(self.kill_target, 'health'))
        self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
        self.primary_input(True)
        self.stage('jelly_kill')
        self.begin_clip('jelly_rupture', 10.)

    def update_rafts(self, now, elapsed):
        yaw = self.raft.get_actor_rotation().yaw if valid(self.raft) else 0.
        if self.phase == 'wait_branch':
            self.require(elapsed < 45., 'Natural first wave/branch exceeded the short wait')
            if enum_name(prop(self.event, 'phase')) != 'FLOODING':
                return
            if now - float(prop(self.event, 'phase_started_at')) < 3.3:
                return # Actual filling must cover the floor before placing a swimmer beside the raft.
            rafts = [r for r in prop(self.event, 'rafts') if valid(r)]
            self.require(len(rafts) == 2 and not prop(self.event, 'jellies'), 'Raft branch must contain two rafts and no jellies')
            self.require(not prop(prop(self.event, 'liquid'), 'river_flood'), 'Deep branch still uses the shallow river')
            self.report['checks']['raft_variant_exclusive_two_refuges'] = True
            self.raft = rafts[0]
            self.raft_ids = [path(r) for r in rafts]
            self.make_weights()
            self.place_pawn(self.hero, self.deck_point(-65, 0), yaw)
            self.place_pawn(self.bots[0], self.deck_point(65, -34), yaw)
            self.place_pawn(self.bots[1], self.water_foot(), yaw, True)
            self.frame_raft()
            self.stage('two_riders')
            self.begin_clip('raft_capacity', 3.)
        elif self.phase == 'two_riders' and elapsed >= 1.2:
            self.require(int(prop(self.raft, 'occupant_count')) == 2 and not self.raft.is_overloaded(), 'Nearby water pawn incorrectly counts as a third rider')
            self.report['checks']['two_deck_riders_nearby_water_excluded'] = True
            self.place_pawn(self.bots[1], self.deck_point(65, 44), yaw)
            self.stage('warning_cancel')
            self.begin_clip('raft_warning_cancel', 2.)
        elif self.phase == 'warning_cancel':
            self.require(elapsed < 3., 'Third real deck rider never produced overload warning')
            if float(prop(self.raft, 'warning_started_at')) >= 0.:
                self.report['warning_cancel_seen_at'] = now
                self.place_pawn(self.bots[1], self.water_foot(), yaw, True)
                self.stage('warning_cleared')
        elif self.phase == 'warning_cleared' and elapsed >= .4:
            self.require(float(prop(self.raft, 'warning_started_at')) < 0. and float(prop(self.raft, 'sink_started_at')) < 0., 'Removing the extra rider failed to cancel the pre-sink warning')
            self.report['checks']['overload_warning_cancels_before_sink'] = True
            self.place_pawn(self.bots[1], self.deck_point(65, 44), yaw)
            self.stage('warning_commit')
            self.begin_clip('raft_sink_return', 5.)
        elif self.phase == 'warning_commit':
            self.require(elapsed < 3., 'Persistent overload did not start sinking')
            if float(prop(self.raft, 'sink_started_at')) >= 0.:
                self.sink_clock = float(prop(self.raft, 'sink_started_at'))
                self.sink_start_z = self.raft.get_actor_location().z
                self.stage('sink_cycle')
        elif self.phase == 'sink_cycle':
            self.sink_samples.append(dict(time=now, z=self.raft.get_actor_location().z, submerging=bool(self.raft.is_submerging())))
            if now - self.sink_clock >= 2.2:
                depth = self.sink_start_z - min(s['z'] for s in self.sink_samples)
                self.require(depth >= 60. and not self.raft.is_submerging(), 'Raft did not visibly dip and return in its two-second cycle')
                self.require(all(prop(p, 'status').is_alive() for p in [self.hero] + self.bots), 'A two-second dip killed a fresh passenger')
                self.report['checks']['perceptible_two_second_dip_returns_alive'] = True
                self.report['sink_cycle'] = dict(native_started_at=self.sink_clock, depth_cm=depth, samples=self.sink_samples)
                other = [r for r in prop(self.event, 'rafts') if valid(r) and r != self.raft][0]
                for pawn, y in zip(self.bots, (-40., 40.)):
                    foot = prop(other, 'deck').get_world_transform().transform_location(u.Vector(0, y, 37.5))
                    self.place_pawn(pawn, foot, other.get_actor_rotation().yaw)
                self.place_pawn(self.hero, self.water_foot(-168.), yaw, True)
                self.swim_before = float(self.hero.get_movement_component().get_swim_energy())
                self.stage('swim_reserve')
                self.begin_clip('raft_boarding', 8.)
        elif self.phase == 'swim_reserve' and elapsed >= 2.5:
            movement = self.hero.get_movement_component()
            self.require(movement.is_in_deep_water() and movement.get_swim_energy() < self.swim_before - .5, 'Ordinary deep swimming did not spend the separate reserve')
            self.report['checks']['deep_swimming_spends_reserve'] = True
            self.hero.call_method('SetBraceInputHeld', args=(True,))
            self.brace = True
            self.stage('rmb_board')
        elif self.phase == 'rmb_board':
            movement = self.hero.get_movement_component()
            base = movement.call_method('GetMovementBaseObject', args=())
            if base == prop(self.raft, 'deck') and prop(movement, 'movement_mode') == u.MovementMode.MOVE_WALKING:
                self.report['checks']['real_rmb_contact_boards_walkable_deck'] = True
                self.hero.call_method('SetBraceInputHeld', args=(False,))
                self.brace = False
                self.recovery_before = float(movement.get_swim_energy())
                self.stage('deck_recovery')
            else:
                self.require(elapsed < 5., 'Real RMB brace failed to board; inspect hand contact and the capsule sweep')
        elif self.phase == 'deck_recovery' and elapsed >= 2.:
            self.require(self.hero.get_movement_component().get_swim_energy() > self.recovery_before + .5, 'Dry supported deck did not recover swimming reserve')
            self.report['checks']['dry_deck_recovers_reserve'] = True
            self.stage('await_cleanup')
        elif self.phase == 'await_cleanup':
            self.require(elapsed < 40., 'Natural deep water never drained into cleanup')
            if enum_name(prop(self.event, 'phase')) == 'CLEANUP':
                self.report['checks']['native_flood_timer_enters_cleanup_only'] = True
                self.begin_cleanup_checkpoint()

    def update_jellies(self, now, elapsed):
        if self.phase == 'wait_branch':
            self.require(elapsed < 45., 'Natural first wave did not enter jelly branch')
            if enum_name(prop(self.event, 'phase')) != 'JELLY_FIGHT':
                return
            self.require(not prop(self.event, 'rafts') and len(prop(self.event, 'jellies')) >= 2, 'Jelly branch mixed in raft actors or lacks its native pair')
            healths = [float(prop(j, 'health')) for j in prop(self.event, 'jellies') if valid(j)]
            self.require(healths and all(abs(h - 360.) < .001 for h in healths), 'Spawned jellies do not have their actual default 360 HP')
            self.report['native_jelly_spawn_health'] = healths
            self.report['checks']['jelly_variant_exclusive'] = True
            self.begin_jelly_kill()
        elif self.phase == 'jelly_kill':
            self.require(elapsed < 20., 'Normal Pickaxe input did not lethally damage the fixture jelly')
            health = float(prop(self.kill_target, 'health'))
            if health < self.last_jelly_health - .001:
                self.health_changes.append(dict(time=now, before=self.last_jelly_health, after=health, primary=self.primary))
            self.last_jelly_health = health
            if bool(prop(self.kill_target, 'pending_rupture')):
                self.primary_input(False)
                self.require(int(prop(self.event, 'jellies_left')) > 0, 'Lethal warning prematurely released the encounter')
                self.report['checks']['normal_pickaxe_kill_waits_for_rupture'] = bool(self.health_changes)
                self.stage('jelly_rupture')
                return
            center = self.kill_target.get_actor_location()
            floor = self.floor(center - u.Vector(125., 0, 0))
            self.require(floor is not None, 'No supported melee approach to the live jelly')
            self.move_to(floor)
            self.frame_camera(center + u.Vector(290, -320, 260), center)
        elif self.phase == 'jelly_rupture':
            self.require(elapsed < 3., 'Native lethal warning never ruptured')
            if float(prop(self.kill_target, 'ruptured_at')) >= 0.:
                self.require(self.event_patches(), 'Rupture left no persistent brushable event grime')
                self.killed_jellies.append(path(self.kill_target))
                self.report['checks']['rupture_has_persistent_brushable_grime'] = True
                if any(valid(j) and float(prop(j, 'health')) > 0 for j in prop(self.event, 'jellies')):
                    self.begin_jelly_kill()
                else:
                    self.stage('jelly_cleanup_transition')
        elif self.phase == 'jelly_cleanup_transition' and elapsed >= .7:
            self.require(int(prop(self.event, 'jellies_left')) == 0, 'Defeated jellies still block the fight')
            self.report['checks']['all_native_ruptures_enter_cleanup'] = True
            self.report['pickaxe_health_changes'] = self.health_changes
            self.begin_cleanup_checkpoint()

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic() - self.started < 240., 'Short Cataclysm probe exceeded its wall-time limit')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one standalone authority PIE world')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()):
                    self.world = None
                    return
                self.report['world'] = self.world.get_path_name()
                self.stage('first_perk')
                return
            self.require(self.world in worlds and len(worlds) == 1, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'Local controller has no live tooth pawn')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            # Reward screens pause game time while Slate keeps ticking. Resolve
            # their real controller input before rejecting a duplicate game tick.
            if self.pc.is_reward_menu_open():
                if self.phase == 'first_perk':
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                elif self.phase not in ('perk_resolution', 'launch'):
                    self.report.setdefault('normal_reward_choices', []).append(dict(time=now, stage=self.phase))
                    self.resume_primary = self.resume_primary or self.primary
                    self.primary_input(False)
                    self.pc.choose_reward_perk(0)
                return
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now - self.phase_at
            if self.phase == 'first_perk':
                self.require(elapsed < 25., 'Initial normal perk menu did not become ready')
                if now >= 1. and self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                return
            if self.phase == 'perk_resolution':
                self.require(elapsed < 10., 'Initial perk did not resolve')
                if not self.pc.is_reward_menu_open():
                    self.initial_patch_paths = {path(p) for p in self.actors(u.MCMouthSurface)}
                    self.report['pre_f3_patch_paths'] = sorted(self.initial_patch_paths)
                    self.pc.request_dev_action(getattr(u.MCDevAction, 'CATACLYSM_' + MODE.upper()), -1)
                    self.stage('launch')
                return
            if self.phase == 'launch':
                events = [a for a in self.actors(u.MCCataclysmEvent) if enum_name(prop(a, 'phase')) not in ('DORMANT', 'COMPLETE', 'STOPPED')]
                self.require(len(events) == 1, 'F3 did not create exactly one real Cataclysm')
                self.event = events[0]
                self.tongue = self.actors(u.MCTongue)[0]
                self.require(enum_name(prop(self.event, 'variant')) == MODE.upper(), 'F3 deterministic branch mismatched')
                center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
                floor = self.floor(center)
                self.require(floor is not None, 'No supported point for the owned common-wave camera')
                self.frame_camera(floor + u.Vector(extent.x * .12, -extent.y * .55, 850.), floor + u.Vector(0, 0, 210.))
                prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 100.)
                self.begin_clip('common_wave', 7.)
                if MODE == 'rafts':
                    self.event.set_editor_properties({'FloodHoldSeconds': 26., 'FloodFillSeconds': 3., 'FloodDrainSeconds': 3.})
                    self.report['fixture_tuning'] = dict(FloodHoldSeconds=26., FloodFillSeconds=3., FloodDrainSeconds=3.)
                else:
                    health = float(prop(self.event, 'jelly_health'))
                    self.require(abs(health - 360.) < .001, 'Jelly proof requires the unmodified default 360 HP')
                    self.report['native_jelly_start_health'] = health
                self.report['checks']['real_f3_deterministic_start'] = True
                self.stage('wait_branch')
                return
            self.require(valid(self.event) and not prop(self.event, 'failed'), 'Native event reported launch/runtime failure')
            self.require(prop(self.hero, 'status').is_alive(), 'Observer died; native damage/fatigue was left unchanged')
            if self.resume_primary:
                self.primary_input(True)
                self.resume_primary = False
            self.keep_camera()
            if now >= self.sample_at:
                self.sample_at = now + .05
                self.sample(now)
            if self.clip and now > self.clip_until:
                self.clip = None
            if self.capture_hz and self.clip and now >= self.frame_at:
                self.frame_at = now + 1. / self.capture_hz
                camera = self.camera_evidence()
                if camera and self.pc.get_view_target() == self.camera and camera['location_error_cm'] < 12. and camera['rotation_error_degrees'] < 5.:
                    self.screenshot('%05d.png' % len(self.report['frames']), True)
            if self.phase == 'cleanup_wait':
                if elapsed >= 3.:
                    self.require(enum_name(prop(self.event, 'phase')) == 'CLEANUP' and int(prop(self.event, 'dirt_left')) > 0,
                                 'Dirty cleanup completed from elapsed time alone')
                    self.report['checks']['dirty_cleanup_persists_without_timer_completion'] = True
                    self.begin_brush_checkpoint()
            elif self.phase == 'cleanup_brush' and elapsed >= 3.5:
                self.primary_input(False)
                after = int(prop(prop(self.brush_target, 'status'), 'state').coffee_left)
                wiped_after = sum(prop(self.brush_target, 'wipe_mask'))
                self.require(after < self.brush_before or wiped_after < self.brush_wipe_before,
                             'Held normal brush failed to improve actual event grime')
                self.report['checks']['ordinary_brush_reduces_event_grime'] = True
                self.report['brush_checkpoint'] = dict(patch=path(self.brush_target), before=self.brush_before, after=after,
                    wipe_bytes_before=self.brush_wipe_before, wipe_bytes_after=wiped_after,
                    remaining_tasks=int(prop(self.event, 'dirt_left')), phase=enum_name(prop(self.event, 'phase')))
                self.report['observed_event_phases'] = self.observed_phases
                self.stage('capture_flush')
            elif self.phase == 'capture_flush' and elapsed >= .8:
                missing = [f['file'] for f in self.report['frames'] if not Path(f['file']).is_file()]
                self.require(not missing, 'Genuine engine capture frames missing: ' + str(missing[:3]))
                if self.capture_hz:
                    needed = ('raft_sink_return', 'raft_boarding', 'cleanup_brush') if MODE == 'rafts' else ('jelly_rupture', 'cleanup_brush')
                    self.require(all(len(self.report['clips'].get(name, [])) >= 8 for name in needed),
                                 'A required real-game clip has too few verified camera frames')
                for entry in self.report['frames']:
                    frame = Path(entry['file']).read_bytes()
                    self.require(frame[:8] == b'\x89PNG\r\n\x1a\n', 'Engine screenshot is not a PNG')
                    entry['sha256'] = hashlib.sha256(frame).hexdigest()
                    entry['width'] = int.from_bytes(frame[16:20], 'big')
                    entry['height'] = int.from_bytes(frame[20:24], 'big')
                self.report['manual_checkpoints'] = ['Finish every actual patch/player/tooth care task and verify Complete only at zero consequences.',
                    'Assess default 35-second flood hold and natural jelly difficulty without the position-only observer fixture.',
                    'Inspect rendered warning/foam/rupture clips; telemetry does not establish visual quality.']
                self.finish()
            elif MODE == 'rafts':
                self.update_rafts(now, elapsed)
            else:
                self.update_jellies(now, elapsed)
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if not self.done and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                if valid(self.hero):
                    self.hero.call_method('SetBraceInputHeld', args=(False,))
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('A Cataclysm probe is already running; it was left intact')
    probe = CataclysmSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
