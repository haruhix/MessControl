"""One owned solo F3-raft fixture: actual WASD actions, two slots, and debris.

No network clients, builds, asset saves, forced impulses, health/mask edits or
clock jumps. The local player uses Enhanced Input's actual Forward/Right/Jump
InputActions. One authority AI submits ordinary AddMovementInput every frame;
its pawn owns the normal rowing RPC/heartbeat and presentation. Position and
PIE-duration fixtures, the short scheduler isolation, and a heartbeat-drop
isolation are recorded explicitly. Root must inspect genuine images for quality.

Run only in idle L_Mouth after Editor build. Optional MC_RAFT_OUTPUT,
MC_RAFT_CAPTURE_HZ (0..8), MC_RAFT_HOLD_SECONDS (default 38). Cancel via
builtins.mc_raft_seated_controls_solo.finish('Stopped by operator').
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

KEY = 'mc_raft_seated_controls_solo'
ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_RAFT_OUTPUT', ROOT/'Saved/NutRaftRedesign_20261017/RaftSolo')).resolve()
if OUTPUT.drive.upper() != 'E:':
    raise RuntimeError('All raft probe output must be on E:')
CAPTURE_HZ = max(0., min(8., float(globals().get('MC_RAFT_CAPTURE_HZ', 6))))
HOLD_SECONDS = max(32., min(55., float(globals().get('MC_RAFT_HOLD_SECONDS', 38.))))
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_cataclysm_solo.py'), run_name='raft_seated_support',
    init_globals={'MC_CATACLYSM_MODE': 'rafts', 'MC_CATACLYSM_OUTPUT': OUTPUT,
                  'MC_CATACLYSM_CAPTURE_HZ': CAPTURE_HZ})
Parent, prop, vec, valid, path, enum_name = (
    support[name] for name in ('CataclysmSolo', 'prop', 'vec', 'valid', 'path', 'enum_name'))


def xy(value):
    return [float(value.x), float(value.y)]


def distance(a, b):
    return math.sqrt((a.x-b.x)**2+(a.y-b.y)**2+(a.z-b.z)**2)


def normalized_axes(axes):
    scale = max(1., math.hypot(*axes))
    return [axes[0]/scale, axes[1]/scale]


class RaftSeatedControlsSolo(Parent):
    def __init__(self):
        super().__init__()
        self.bot = self.third = self.liquid = self.scheduler = self.log = None
        self.input_subsystem = None
        self.actions = {}
        self.human_axes = [0., 0.]
        self.ai_axes = [0., 0.]
        self.inject_jump = False
        self.scheduler_tick = self.event_tick = self.bot_tick = None
        self.raft_yaw_saved = None
        self.episode_started_at = None
        self.capture_wall_at = self.sample_wall_at = 0.
        self.contact_samples = []
        self.log_fixture = None
        self.report.update(mode='raft_seated_controls', clips={}, frames=[], screenshots=[], samples=[], checks={},
            scope='One owned standalone authority PIE; real F3 Rafts event, local Enhanced Input and authority-AI movement',
            capture_hz=CAPTURE_HZ,
            clock_semantics={'frames': 'monotonic request timestamps; Shot SHOWUI is asynchronously rendered by the engine',
                             'game_seconds': 'ordinary native variable delta; clientFixedFPS=0 and engine fixed-frame mode disabled; no clock jump or dilation'},
            clip_labels={'raft_slots': 'Два фиксированных места', 'raft_one_rower': 'W — один гребец',
                         'raft_same': 'Оба W — усилия складываются', 'raft_opposed': 'W и S — тяга компенсируется',
                         'raft_diagonal': 'W+D — диагональ каждого ограничена',
                         'raft_exit_board': 'Space — выйти; ПКМ у края — вернуться',
                         'raft_debris_ejection': 'Настоящий плывущий обломок сбрасывает пассажира'},
            limitations=[
                'A position-only fixture places the local hero and one possessed authority AI on the real F3 raft. Native TryClaimSeat reserves slots; this initial placement is not a traversal playthrough.',
                'One extra owned authority-AI pawn briefly attempts the third slot; it is destroyed after the capacity check.',
                'Human ForwardAction/RightAction/JumpAction values are injected through EnhancedInputLocalPlayerSubsystem, triggering the real bindings. Physical keyboard hardware is not used.',
                'AI decisions are disabled; ordinary APawn.AddMovementInput supplies its requested world movement every frame. Native pawn tick performs local-axis conversion, RPC/heartbeat and animation state.',
                'Only the slot/vector checks temporarily use the existing designer minimum MaxYawSpeed=1 degree/s. This bounds the one-frame world-bearing lag of Python authority-AI movement while retaining strict local-vector assertions. The original setting is restored before release, heartbeat loss, boarding and debris; these vector clips are not natural full-spin or balance footage.',
                'The unrelated single-day scheduler is paused. Before the first log, Cataclysm event tick is paused only for vector checks; water, raft, pawn, physics and native clock continue. Event tick is restored before the real debris check.',
                'For the heartbeat-drop check only, the AI pawn actor tick is disabled for 0.65 native seconds. The raft input expires normally; the actor tick is then restored. This is a disclosed heartbeat-loss fixture, not packet-loss or network validation.',
                'PIE-only duration uses fill=3s, hold='+str(HOLD_SECONDS)+'s, drain=3s. No flood/phase timestamps or positions are rewritten after setup.',
                'After the actual Space exit, one disclosed swimmer-position fixture makes the native RMB mantle reachable for the focused boarding check.',
                'A fresh naturally spawned event log is placed once upstream of the occupied raft. Its native movement, warning, swept collision and impact paths provide the hit. No impulse, damage, impact counters or physics-state helpers are invoked.',
                'Force-vector sums are read from native RowingDirection; velocities additionally contain current, angular inertia, drag, banks and collision. This is not isolated force or balance profiling.',
                'No multiplayer/network clients, complete event playthrough, cleanup completion or packaged validation. Root visual review is required; telemetry does not certify animation quality.'])
        self.report['fixture_tuning'] = {'FloodFillSeconds': 3., 'FloodHoldSeconds': HOLD_SECONDS, 'FloodDrainSeconds': 3.}
        self.report['source_sha256'] = {}
        for item in ('Source/MessControl/Public/MCCataclysmRaft.h', 'Source/MessControl/Private/MCCataclysmRaft.cpp',
                     'Source/MessControl/Public/MCToothCharacter.h', 'Source/MessControl/Private/MCToothCharacter.cpp',
                     'Source/MessControl/Private/MCToothMovementComponent.cpp', 'Source/MessControl/Private/MCToothAnimInstance.cpp',
                     'Source/MessControl/Private/MCCataclysmDebris.cpp', 'Source/MessControl/Private/MCRowingStroke.h',
                     'Tools/Unreal/probe_cataclysm_solo.py', 'Tools/Unreal/check_approved_environment_solo.py',
                   'Tools/Unreal/probe_nut_pattern_kinetics_solo.py',
                     'Tools/Unreal/probe_raft_seated_controls_solo.py'):
            self.report['source_sha256'][item] = hashlib.sha256((ROOT/item).read_bytes()).hexdigest()

    def start(self):
        settings = Parent.start.__globals__
        settings['PLAY'] = dict(settings['PLAY'], clientFixedFPS=[0])
        super().start()
        self.engine.set_editor_property('bUseFixedFrameRate', False)
        self.report['clock_configuration'] = dict(clientFixedFPS=[0], engine_fixed_frame_rate=False,
            native_game_clock='ordinary variable delta; no time jump or dilation',
            restoration='Base.finish restores original play and engine settings')
        self.write()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_latest.json').write_text(data, encoding='utf-8')

    def initialize_input(self):
        library = u.get_default_object(u.load_class(None, '/Script/Engine.SubsystemBlueprintLibrary'))
        self.input_subsystem = library.call_method('GetLocalPlayerSubSystemFromPlayerController',
            args=(self.pc, u.load_class(None, '/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem')))
        self.require(valid(self.input_subsystem), 'Local player has no EnhancedInput subsystem')
        # ForwardAction/RightAction/JumpAction are bare private UPROPERTY fields.
        # Both Python get_editor_property and ToolsetLibrary reject those fields
        # in UE5.8. Resolve the real live actions through public active mappings.
        self.actions = {}
        mapped = []
        for action in u.ObjectIterator(u.InputAction):
            if action.get_outer() != self.hero: continue
            keys = self.input_subsystem.call_method('QueryKeysMappedToAction', args=(action,))
            names = {str(key.get_editor_property('KeyName')) for key in keys}
            mapped.append(dict(action=path(action), keys=sorted(names)))
            for name, expected in (('forward', {'W', 'S'}), ('right', {'A', 'D'}), ('jump', {'SpaceBar'})):
                if expected.issubset(names):
                    self.require(name not in self.actions, 'Ambiguous live input mapping for '+name)
                    self.actions[name] = action
        self.require(set(self.actions) == {'forward', 'right', 'jump'},
                     'Native active WASD/Space actions are not ready: '+json.dumps(mapped))
        self.report['human_input_route'] = dict(subsystem=path(self.input_subsystem),
            actions={name: path(action) for name, action in self.actions.items()}, active_mappings=mapped,
            injected_api='InjectInputVectorForAction', hardware_keyboard=False)

    def input_frame(self):
        if valid(self.input_subsystem):
            for name, amount in (('forward', self.human_axes[0]), ('right', self.human_axes[1]),
                                 ('jump', 1. if self.inject_jump else 0.)):
                self.input_subsystem.call_method('InjectInputVectorForAction',
                    args=(self.actions[name], u.Vector(amount, 0, 0), [], []))
        if valid(self.bot) and self.ai_axes != [0., 0.] and self.bot_tick is None:
            # Same world-space intent interface as the project's normal AI movement.
            yaw = math.radians(self.raft.get_actor_rotation().yaw)
            x, y = self.ai_axes
            direction = u.Vector(x*math.cos(yaw)-y*math.sin(yaw), x*math.sin(yaw)+y*math.cos(yaw), 0.)
            self.bot.add_movement_input(direction, 1., False)

    def controls(self, human=(0., 0.), ai=(0., 0.)):
        self.human_axes = [float(v) for v in human]
        self.ai_axes = [float(v) for v in ai]
        self.report.setdefault('input_actions', []).append(dict(game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            wall_seconds=time.monotonic()-self.started, human_axes=self.human_axes[:], authority_ai_axes=self.ai_axes[:]))

    def make_crew(self):
        self.make_weights()
        self.bot, self.third = self.bots
        self.report['authority_ai_fixtures'] = [dict(pawn=path(p), controller=path(c),
            player_state=path(prop(p, 'player_state')), decision_tick=False, network_client=False)
            for p, c in zip(self.bots, self.bot_controllers)]
        for pawn, side in ((self.hero, -46.), (self.bot, 46.)):
            self.place_pawn(pawn, self.deck_point(0, side), self.raft.get_actor_rotation().yaw)
            self.require(self.raft.try_claim_seat(pawn), 'Native authority failed to reserve a free slot')
        self.slot_paths = [path(p) for p in prop(self.raft, 'seat_players')]
        self.require(self.slot_paths == [path(self.hero), path(self.bot)], 'Slot ordering is not stable left/right')
        self.place_pawn(self.third, self.deck_point(95., 0.), self.raft.get_actor_rotation().yaw)
        rejected = not self.raft.try_claim_seat(self.third)
        self.require(rejected and not self.third.is_seated_on_raft(), 'Third pawn occupied a full raft')
        self.report['checks']['third_slot_rejected'] = True
        self.report['third_slot_attempt'] = dict(pawn=path(self.third), occupied_slots=self.slot_paths[:], rejected=True)
        self.third.destroy_actor()
        self.bots = [self.bot]
        self.third = None

    def camera_for_stage(self):
        center = self.raft.get_actor_location()
        # Freeze the world bearing for each review group and follow translation.
        # Rotating a 600cm camera arm after the world tick otherwise introduces
        # enough one-frame PCM error to reject frames during an opposed turn.
        group = 'debris' if self.phase in ('wait_log', 'contact', 'after_contact') else 'crew'
        if getattr(self, 'camera_group', None) != group:
            self.camera_group = group
            if group == 'debris':
                stream = self.liquid.storm_stream_direction()
                across = u.Vector(-stream.y, stream.x, 0.)
                self.camera_offset = -stream*430.-across*450.+u.Vector(0, 0, 330.)
            else:
                self.camera_offset = self.raft.get_actor_forward_vector()*410. - self.raft.get_actor_right_vector()*420. + u.Vector(0, 0, 290.)
            self.report.setdefault('review_camera_offsets', []).append(dict(group=group,
                world_offset_cm=vec(self.camera_offset), game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
                follows_translation=True, follows_raft_rotation=False))
        location = center+self.camera_offset
        self.frame_camera(location, center+u.Vector(0, 0, 65.))
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 80.)
        self.keep_camera()

    def sample(self, now):
        crew = []
        deck_pose = prop(self.raft, 'deck').get_world_transform()
        for pawn in (self.hero, self.bot):
            if not valid(pawn): continue
            slot = int(prop(pawn, 'raft_seat_index'))
            expected = deck_pose.transform_location(u.Vector(0, -46. if slot == 0 else 46., 37.5))
            expected += u.Vector(0, 0, pawn.capsule_component.get_scaled_capsule_half_height()+4.)
            move = pawn.get_movement_component()
            crew.append(dict(pawn=path(pawn), seated=bool(pawn.is_seated_on_raft()), slot=slot,
                seated_raft=path(prop(pawn, 'seated_raft')), world_cm=vec(pawn.get_actor_location()),
                seat_error_cm=distance(pawn.get_actor_location(), expected) if slot in (0, 1) else None,
                movement_mode=enum_name(prop(move, 'movement_mode')), deep_water=bool(move.is_in_deep_water()),
                swimming_reserve=float(move.get_swim_energy()), rowing=bool(pawn.is_rowing()),
                body_state=enum_name(prop(pawn, 'tooth_physics').get_body_state()),
                health=float(prop(prop(pawn, 'status'), 'state').health)))
        entry = dict(game_seconds=now, wall_seconds=time.monotonic()-self.started, phase=self.phase,
            slots=[path(p) for p in prop(self.raft, 'seat_players')], occupants=int(prop(self.raft, 'occupant_count')),
            rowing_count=int(prop(self.raft, 'rowing_count')), rowing_sum=xy(prop(self.raft, 'rowing_direction')),
            rowing_local_vectors=[xy(v) for v in prop(self.raft, 'rowing_directions')],
            raft_cm=vec(self.raft.get_actor_location()), raft_yaw=float(self.raft.get_actor_rotation().yaw),
            raft_velocity_cm_s=vec(self.raft.get_velocity()), crew=crew,
            brace_active=bool(prop(self.hero, 'grip').is_bracing()),
            current_cm_s2=vec(self.liquid.storm_current_at(self.raft.get_actor_location())))
        self.report['samples'].append(entry)
        return entry

    def stable_samples(self, phase):
        entries = [s for s in self.report['samples'] if s['phase'] == phase]
        return entries[2:] # allow the native action and replication bookkeeping to settle

    def verify_vectors(self, phase, target, count):
        samples = self.stable_samples(phase)
        self.require(len(samples) >= 6, 'Too few native vector observations for '+phase)
        self.require(all(s['slots'] == self.slot_paths for s in samples), 'An occupied slot changed during '+phase)
        selected = [s for s in samples if s['rowing_count'] == count and math.dist(s['rowing_sum'], target) < .025]
        self.require(len(selected) >= max(4, len(samples)*.7), 'Native local vector sum mismatched '+phase)
        for sample in selected:
            self.require(all(p['seated'] and p['seat_error_cm'] < 2. and p['movement_mode'] == 'MOVE_NONE'
                and not p['deep_water'] for p in sample['crew']), 'Crew walked/drifted out of reserved slots during '+phase)
        self.report['checks'][phase+'_native_vectors_and_fixed_slots'] = True
        self.report.setdefault('vector_results', {})[phase] = dict(expected_local_sum=target,
            observed_matching=len(selected), observed_total=len(samples), rowers=count)

    def restore_event(self):
        if self.event_tick is not None and valid(self.event):
            self.event.set_actor_tick_enabled(self.event_tick)
            self.report.setdefault('scheduler_isolation', {})['restored_game_seconds'] = float(u.GameplayStatics.get_time_seconds(self.world))
            self.event_tick = None

    def restore_bot(self):
        if self.bot_tick is not None and valid(self.bot):
            self.bot.set_actor_tick_enabled(self.bot_tick)
            self.report.setdefault('heartbeat_loss_fixture', {})['restored_game_seconds'] = float(u.GameplayStatics.get_time_seconds(self.world))
            self.bot_tick = None

    def restore_yaw_tuning(self):
        if self.raft_yaw_saved is not None and valid(self.raft):
            self.raft.set_editor_property('MaxYawSpeed', self.raft_yaw_saved)
            self.report['vector_yaw_isolation'].update(restored_game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
                restored_value=float(prop(self.raft, 'max_yaw_speed')))
            self.require(abs(float(prop(self.raft, 'max_yaw_speed'))-self.raft_yaw_saved) < .001,
                         'Original raft yaw tuning did not restore')
            self.raft_yaw_saved = None

    def select_log(self, now):
        for actor in prop(self.event, 'floating_debris'):
            if not valid(actor) or actor.is_warning() or int(prop(actor, 'impact_count')) != 0: continue
            age = now-float(prop(actor, 'spawned_at'))
            if age > 5.5: continue
            stream = self.liquid.storm_stream_direction()
            point = self.raft.get_actor_location()-stream*390.
            point.z = actor.get_actor_location().z
            self.log = actor
            self.log_fixture = dict(path=path(actor), original_cm=vec(actor.get_actor_location()), upstream_fixture_cm=vec(point),
                spawned_at=float(prop(actor, 'spawned_at')), age_seconds=age,
                crew_before=[dict(pawn=path(p), seat=int(prop(p, 'raft_seat_index')),
                    health=float(prop(prop(p, 'status'), 'state').health)) for p in (self.hero, self.bot)],
                slots_before=[path(p) for p in prop(self.raft, 'seat_players')])
            actor.set_actor_location(point, False, True)
            self.report['native_log_position_fixture'] = self.log_fixture
            self.begin_clip('raft_debris_ejection', 4.5)
            self.stage('contact')
            return True
        return False

    def episode(self, now, elapsed):
        if self.phase == 'wait_hold':
            self.require(elapsed < 28., 'Native raft branch/holding phase did not arrive')
            if enum_name(prop(self.event, 'phase')) != 'FLOODING': return
            self.liquid = prop(self.event, 'liquid')
            if not valid(self.liquid) or enum_name(self.liquid.get_phase()) != 'HOLDING': return
            rafts = [r for r in prop(self.event, 'rafts') if valid(r)]
            self.require(len(rafts) == 2 and self.liquid.is_cataclysm_storm(), 'F3 did not create the native two-raft storm')
            self.require(not any(valid(d) for d in prop(self.event, 'floating_debris')), 'Vector check must start before native log hazards')
            self.raft = min(rafts, key=lambda r: distance(r.get_actor_location(), self.hero.get_actor_location()))
            self.raft_yaw_saved = float(prop(self.raft, 'max_yaw_speed'))
            self.raft.set_editor_property('MaxYawSpeed', 1.)
            self.report['vector_yaw_isolation'] = dict(raft=path(self.raft), original_value=self.raft_yaw_saved,
                fixture_value=1., applied_game_seconds=now,
                reason='Legal designer minimum bounds queued authority-AI WORLD-input bearing lag; strict local-vector assertions remain unchanged')
            self.event_tick = self.event.is_actor_tick_enabled()
            self.event.set_actor_tick_enabled(False)
            self.report['scheduler_isolation'] = dict(event=path(self.event), prior_tick=self.event_tick, paused_game_seconds=now,
                reason='Vector checks before the first log; water, raft, pawn and normal clock keep running')
            self.make_crew()
            self.report['checks']['real_f3_two_raft_event'] = True
            self.begin_clip('raft_slots', 4.2)
            self.stage('slots')
        elif self.phase == 'slots' and elapsed >= 4.:
            self.require([path(p) for p in prop(self.raft, 'seat_players')] == self.slot_paths, 'Slot identities changed at idle')
            self.require(all(p.is_seated_on_raft() for p in (self.hero, self.bot)), 'Crew lost a reserved slot at idle')
            self.report['checks']['two_fixed_authority_slots'] = True
            self.hero.call_method('SetBraceInputHeld', args=(True,))
            self.require(not prop(self.hero, 'grip').is_bracing(), 'Mounted RMB incorrectly acquired invulnerable rail')
            self.hero.call_method('SetBraceInputHeld', args=(False,))
            self.report['checks']['mounted_rmb_has_no_rail_immunity'] = True
            self.controls((1, 0), (0, 0)); self.begin_clip('raft_one_rower', 4.); self.stage('one')
        elif self.phase == 'one' and elapsed >= 3.8:
            self.verify_vectors('one', [1., 0.], 1)
            self.controls((1, 0), (1, 0)); self.begin_clip('raft_same', 4.); self.stage('same')
        elif self.phase == 'same' and elapsed >= 3.8:
            self.verify_vectors('same', [2., 0.], 2)
            self.controls((1, 0), (-1, 0)); self.begin_clip('raft_opposed', 4.); self.stage('opposed')
        elif self.phase == 'opposed' and elapsed >= 3.8:
            self.verify_vectors('opposed', [0., 0.], 2)
            self.controls((1, 1), (1, 1)); self.begin_clip('raft_diagonal', 4.); self.stage('diagonal')
        elif self.phase == 'diagonal' and elapsed >= 3.8:
            q = math.sqrt(2.)
            self.verify_vectors('diagonal', [q, q], 2)
            self.restore_yaw_tuning()
            self.controls((0, 0), (0, 0)); self.stage('release')
        elif self.phase == 'release' and elapsed >= .45:
            self.require(int(prop(self.raft, 'rowing_count')) == 0 and not self.hero.is_rowing() and not self.bot.is_rowing(),
                'Released WASD/AI intent did not clear both rowers')
            self.report['checks']['released_input_stops_native_rowing'] = True
            self.controls((0, 0), (1, 0)); self.stage('timeout_prepare')
        elif self.phase == 'timeout_prepare' and elapsed >= .4:
            self.require(self.bot.is_rowing() and int(prop(self.raft, 'rowing_count')) == 1, 'AI heartbeat did not become active')
            self.controls((0, 0), (0, 0))
            self.bot_tick = self.bot.is_actor_tick_enabled()
            self.bot.set_actor_tick_enabled(False)
            self.report['heartbeat_loss_fixture'] = dict(pawn=path(self.bot), prior_actor_tick=self.bot_tick, disabled_game_seconds=now,
                description='Native input was active; only pawn ActorTick heartbeat stops for0.65s. No input/force/row state is assigned.')
            self.stage('timeout_drop')
        elif self.phase == 'timeout_drop' and elapsed >= .65:
            self.require(int(prop(self.raft, 'rowing_count')) == 0, 'Raft retained an expired input without heartbeat')
            self.report['checks']['native_raft_input_timeout'] = True
            self.restore_bot(); self.stage('timeout_resume')
        elif self.phase == 'timeout_resume' and elapsed >= .3:
            self.require(not self.bot.is_rowing(), 'Native pawn timeout did not clear rowing after tick restoration')
            self.report['checks']['native_pawn_timeout_clears_row_state'] = True
            self.inject_jump = True
            self.begin_clip('raft_exit_board', 5.)
            self.stage('exit')
        elif self.phase == 'exit':
            if elapsed >= .15: self.inject_jump = False
            if elapsed < .7: return
            self.require(not self.hero.is_seated_on_raft() and self.bot.is_seated_on_raft(), 'Actual JumpAction failed to release only its own slot')
            self.require(int(prop(self.bot, 'raft_seat_index')) == 1, 'Second passenger moved from right slot when the left player exited')
            self.report['checks']['space_exit_preserves_other_slot'] = True
            # The real jump is recorded first. This one later placement shortens the swim approach.
            foot = self.water_foot(-165.)
            self.place_pawn(self.hero, foot, self.raft.get_actor_rotation().yaw, True)
            self.hero.call_method('SetBraceInputHeld', args=(True,))
            self.stage('board')
        elif self.phase == 'board':
            self.require(elapsed < 4., 'Native RMB swim mantle did not claim the free left slot')
            if self.hero.is_seated_on_raft():
                self.hero.call_method('SetBraceInputHeld', args=(False,))
                self.require(int(prop(self.hero, 'raft_seat_index')) == 0 and int(prop(self.bot, 'raft_seat_index')) == 1,
                    'Reboarding did not fill the free slot without moving the partner')
                self.report['checks']['native_rmb_mantle_reboards_free_slot'] = True
                self.stage('after_board')
        elif self.phase == 'after_board' and elapsed >= .8:
            self.restore_event(); self.stage('wait_log')
        elif self.phase == 'wait_log':
            self.require(elapsed < 7., 'No fresh event log available for focused contact')
            self.select_log(now)
        elif self.phase == 'contact':
            self.require(elapsed < 4., 'Upstream native log did not reach the occupied raft')
            if not valid(self.log): return
            observation = dict(game_seconds=now, position_cm=vec(self.log.get_actor_location()),
                impacts=int(prop(self.log, 'impact_count')), raft_impacts=int(prop(self.log, 'raft_impact_count')),
                player_impacts=int(prop(self.log, 'player_impact_count')), braced_contacts=int(prop(self.log, 'braced_contact_count')))
            self.contact_samples.append(observation)
            if observation['impacts'] > 0:
                self.require(observation['raft_impacts']+observation['player_impacts'] > 0, 'Selected log hit unrelated scenery')
                self.require(observation['braced_contacts'] == 0, 'Mounted seat retained obsolete rail immunity')
                travelled = math.dist(self.log_fixture['upstream_fixture_cm'][:2], observation['position_cm'][:2])
                self.require(travelled >= 50., 'Log contact had no visible approach')
                self.report['native_contact'] = dict(approach_distance_cm=travelled, observation=observation)
                self.report['checks']['actual_incoming_log_contact'] = True
                self.stage('after_contact')
        elif self.phase == 'after_contact' and elapsed >= .6:
            ejected = [p for p in (self.hero, self.bot) if not p.is_seated_on_raft()]
            self.require(ejected and int(prop(self.raft, 'occupant_count')) < 2, 'Confirmed collision failed to release a passenger slot')
            for before in self.log_fixture['crew_before']:
                pawn = self.hero if before['pawn'] == path(self.hero) else self.bot
                self.require(abs(float(prop(prop(pawn, 'status'), 'state').health)-before['health']) < .001,
                    'Debris contact unexpectedly damaged passenger HP')
            self.report['checks']['native_debris_ejects_real_slot_without_hp_damage'] = True
            self.report['native_contact']['ejected_pawns'] = [path(p) for p in ejected]
            self.report['native_contact']['samples'] = self.contact_samples
            if now >= self.clip_until: self.stage('flush')
        elif self.phase == 'flush' and elapsed >= .7:
            self.finalize_frames()
            self.finish()

    def finalize_frames(self):
        capture_support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_nut_pattern_kinetics_solo.py'),
            run_name='raft_capture_validation_support', init_globals={'MC_NUT_PATTERN_OUTPUT': OUTPUT})
        capture_support['prune_unwritten_sequence_requests'](self.report)
        for entry in self.report['frames']+self.report['screenshots']:
            target = Path(entry['file'])
            self.require(target.is_file(), 'Missing native Shot frame: '+str(target))
            data = target.read_bytes()
            dimensions, png_end = capture_support['inspect_png'](data)
            self.require(dimensions in ((1280,720), (1286,760)), 'Native Shot captured the wrong viewport size')
            entry.update(sha256=hashlib.sha256(data).hexdigest(), width=dimensions[0], height=dimensions[1],
                         png_bytes=png_end, trailing_bytes=len(data)-png_end)
        required = tuple(self.report['clip_labels'])
        if self.capture_hz:
            self.require(all(len(self.report['clips'].get(clip, [])) >= 8 for clip in required), 'A required clip has fewer than8 real frames')
        self.report['checks']['genuine_wall_clock_frames_and_actual_camera'] = True
        self.report['checks']['dirty_map_packages_preserved'] = sorted(self.report['dirty_maps_before']) == sorted(
            p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        self.require(self.report['checks']['dirty_map_packages_preserved'], 'Editor map dirty state changed')

    def screenshot(self, name, sequence=False):
        super().screenshot(name, sequence)
        entry = self.report['frames' if sequence else 'screenshots'][-1]
        entry.update(wall_seconds=time.monotonic()-self.started, game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            native_rowing_count=int(prop(self.raft, 'rowing_count')), native_local_sum=xy(prop(self.raft, 'rowing_direction')),
            seat_players=[path(p) for p in prop(self.raft, 'seat_players')],
            visual_review='pending_root_review')

    def tick(self, _delta):
        if self.done: return
        try:
            self.require(time.monotonic()-self.started < 240., 'Owned raft probe exceeded240 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds: return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one owned authority PIE world')
                self.pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()): return
                self.world = worlds[0]; self.hero = self.pc.get_controlled_pawn(); self.stage('first_perk'); return
            self.require(len(worlds) == 1 and self.world in worlds, 'Owned PIE ended or was replaced')
            self.require(self.pc.get_controlled_pawn() == self.hero, 'Subject pawn changed')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            # Native perk UI can pause the world before its clock reaches1s.
            if self.phase == 'first_perk':
                if self.pc.is_reward_menu_open(): self.pc.choose_reward_perk(0); self.stage('perk_resolution')
                return
            if self.phase == 'perk_resolution':
                if not self.pc.is_reward_menu_open():
                    self.initialize_input(); self.pc.request_dev_action(u.MCDevAction.CATACLYSM_RAFTS, -1); self.stage('launch')
                return
            self.input_frame()
            if self.last_game_time is not None and now <= self.last_game_time: return
            self.last_game_time = now
            elapsed = now-self.phase_at
            if self.phase == 'launch':
                events = [e for e in self.actors(u.MCCataclysmEvent) if enum_name(prop(e, 'phase')) not in ('DORMANT', 'COMPLETE', 'STOPPED')]
                self.require(elapsed < 4. and len(events) <= 1, 'F3 launch did not create one Cataclysm')
                if not events: return
                self.event = events[0]; self.tongue = self.actors(u.MCTongue)[0]; self.episode_started_at = now
                self.event.set_editor_properties({'FloodFillSeconds': 3., 'FloodHoldSeconds': HOLD_SECONDS, 'FloodDrainSeconds': 3.})
                self.scheduler = prop(u.GameplayStatics.get_game_state(self.world), 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_tick = self.scheduler.is_actor_tick_enabled(); self.scheduler.set_actor_tick_enabled(False)
                self.stage('wait_hold'); return
            self.require(valid(self.event) and not prop(self.event, 'failed'), 'Native Cataclysm failed')
            self.require(now-self.episode_started_at < 65., 'Focused raft fixture exceeded65 game seconds')
            self.require(prop(self.hero, 'status').is_alive(), 'Local subject died naturally; HP was never overridden')
            if valid(self.raft):
                self.camera_for_stage()
                wall = time.monotonic()
                if wall >= self.sample_wall_at: self.sample_wall_at = wall+.05; self.sample(now)
                if self.clip and now > self.clip_until: self.clip = None
                if self.capture_hz and self.clip and wall >= self.capture_wall_at:
                    evidence = self.camera_evidence()
                    if evidence and evidence['view_target'] == evidence['expected_target'] and evidence['location_error_cm'] < 12. and evidence['rotation_error_degrees'] < 5.:
                        self.capture_wall_at = wall+1./self.capture_hz
                        self.screenshot('%05d.png' % len(self.report['frames']), True)
            self.episode(now, elapsed)
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if not self.done and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                self.controls(); self.inject_jump=False
                self.restore_yaw_tuning()
                self.restore_bot(); self.input_frame()
                for pawn in (self.hero, self.bot):
                    if valid(pawn):
                        pawn.call_method('SetBraceInputHeld', args=(False,))
                        pawn.call_method('SetRowingInputHeld', args=(False,))
                self.restore_event()
                if valid(self.scheduler) and self.scheduler_tick is not None: self.scheduler.set_actor_tick_enabled(self.scheduler_tick)
                self.report['episode_game_seconds'] = None if self.episode_started_at is None else float(u.GameplayStatics.get_time_seconds(self.world))-self.episode_started_at
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


def main():
    for key in (KEY, 'mc_cataclysm_solo', 'mc_cataclysm_storm_solo', 'mc_rowing_animation_solo', 'mc_approved_environment_solo'):
        old = getattr(builtins, key, None)
        if old is not None and not getattr(old, 'done', True): raise RuntimeError('Existing owned probe is active; preserved: '+key)
    probe = RaftSeatedControlsSolo()
    setattr(builtins, KEY, probe)
    try: probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
