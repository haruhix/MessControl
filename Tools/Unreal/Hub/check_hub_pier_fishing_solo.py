"""Short owned Hub spawn -> pier -> exit -> re-entry standalone PIE proof.

Requires a rebuilt, saved Hub with its real Ready and Fishing zones, no active
PIE, and -MCHubOwned or -MCHubFishingOwned. No teleports, spawned test actors,
gameplay-state overrides, map travel, package saves, network peers or microphone.
Movement uses AddMovementInput; Five/One exercise the live Enhanced Input map.
Native game-window PNGs retain game timestamps for separate genuine clip windows.

Globals: MCHUB_PIER_OUTPUT (fresh E: folder), MCHUB_PIER_CAPTURE_HZ (0..12),
MCHUB_PIER_MAX_FRAMES (<=600), MCHUB_PIER_APPROACH (optional inspected XY route).
Cancel only this probe via builtins.mc_hub_pier_solo.cancel().
"""
import builtins
import hashlib
import json
import math
import time
import traceback
from pathlib import Path

import unreal as u


ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MCHUB_PIER_OUTPUT',
    'E:/CodexWork/HubWardrobeRewards_20261022/PierSolo01')).resolve()
KEY = 'mc_hub_pier_solo'
PLAY = dict(playNetMode='PIE_Standalone', playNumberOfClients=1,
            runUnderOneProcess=True, bLaunchSeparateServer=False,
            newWindowWidth=1280, newWindowHeight=720, clientFixedFPS=[0])


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def valid(obj):
    try:
        return obj is not None and u.SystemLibrary.is_valid(obj)
    except (TypeError, RuntimeError):
        return False


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def dirty():
    return sorted(package.get_path_name() for package in
                  list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()) +
                  list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()))


def dot_xy(a, b):
    denominator = math.hypot(a.x, a.y) * math.hypot(b.x, b.y)
    require(denominator > .001, 'Cannot measure a zero-length XY orientation')
    return float((a.x*b.x + a.y*b.y) / denominator)


class HubPierSolo:
    def __init__(self):
        command = u.SystemLibrary.get_command_line()
        require('-MCHubOwned' in command or '-MCHubFishingOwned' in command, 'Explicit owned Hub launch marker required')
        require(Path(u.Paths.project_dir()).resolve() == ROOT.resolve(), 'Wrong project checkout')
        require(OUT.drive.upper() == 'E:' and not OUT.exists(), 'Choose a fresh output directory on E:')
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        require(not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True), 'Existing PIE preserved')
        require(not dirty(), 'Preserve unsaved editor packages before starting')
        world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(valid(world) and world.get_path_name().split('.', 1)[0] == '/Game/Maps/Hub', 'Open the saved Hub before this check')
        self.editor_world = world
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(PLAY)))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_throttle = self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.phase = 'loading'; self.clip = None
        self.started = self.phase_wall = time.monotonic(); self.phase_at = 0.
        self.world = self.pc = self.hero = self.rod = self.zone = self.subsystem = None
        self.instance = self.pawn_path = self.initial_spawn = self.target = None
        self.callback = self.request = None
        self.done = self.finishing = self.busy = self.requested_pie = False
        self.error = None; self.restore_errors = []
        self.actions = {}; self.injected = None; self.input_index = 0
        self.route = globals().get('MCHUB_PIER_APPROACH', [])
        require(isinstance(self.route, list) and all(isinstance(point, (list, tuple)) and len(point) == 2
            and all(math.isfinite(float(value)) for value in point) for point in self.route), 'Approach route must contain finite XY points')
        self.route_index = 0; self.input_after = None
        self.capture_hz = max(0., min(12., float(globals().get('MCHUB_PIER_CAPTURE_HZ', 6))))
        self.max_frames = max(0, min(600, int(globals().get('MCHUB_PIER_MAX_FRAMES', 450))))
        self.frame_at = self.sample_at = 0.; self.frame_bytes = 0
        self.first_capture_done = False
        self.report = dict(passed=False, status='running', scope=__doc__, output=str(OUT),
            checks={}, stages=[], samples=[], screenshots=[], frames=[], clips={}, input_samples=[], walks=[],
            capture_hz=self.capture_hz, package_saves=False, network=False, teleports=False,
            source_sha256=hashlib.sha256((ROOT/'Tools/Unreal/Hub/check_hub_pier_fishing_solo.py').read_bytes()).hexdigest(),
            limitations=['Movement enters the real zone through AddMovementInput; no physical WASD delivery is tested.',
                'Five/One use active Enhanced Input actions, not physical keyboard hardware or viewport focus.',
                'The first observed game-window capture is timestamped; it is not a claim to capture the render engine first frame.',
                'PNG capture cadence is reported; video repeats native frames without synthesizing motion.',
                'Wardrobe, Shop, fishing catch, multiplayer, cook/package and a full level are outside this check.',
                'Screenshots and encoded videos await visual review; assertions do not certify art quality.'])
        OUT.mkdir(parents=True)

    def now(self):
        return float(u.GameplayStatics.get_time_seconds(self.world)) if valid(self.world) else 0.

    def write(self):
        self.report.update(phase=self.phase, wall_seconds=time.monotonic()-self.started)
        (OUT/'Report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False), encoding='utf-8')

    def check(self, name, condition):
        self.report['checks'][name] = bool(condition)
        require(condition, name)

    def stage(self, name, clip=None):
        self.phase = name; self.phase_at = self.now(); self.phase_wall = time.monotonic()
        if clip is not None:
            self.clip = clip; self.report['clips'].setdefault(clip, [])
        self.report['stages'].append(dict(name=name, clip=self.clip, game_seconds=self.phase_at,
            wall_seconds=self.phase_wall-self.started))
        self.write()

    def start(self):
        self.report['dirty_before'] = dirty()
        require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(PLAY)), 'Standalone PIE preferences rejected')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.requested_pie = True
        self.write()
        self.request = u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps(dict(options=dict(bSimulate=False, playMode='PlayMode_InEditorFloating', warmupSeconds=0.))))
        require(self.request is not None, 'StartPIE did not return its async result')

    def capture(self, name=None, required=True):
        sequence = name is None
        target = OUT/('frames' if sequence else 'screenshots')/(name or ('%05d.png' % len(self.report['frames'])))
        target.parent.mkdir(parents=True, exist_ok=True)
        requested = time.monotonic()-self.started; game_time = self.now()
        captured = u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(target))
        if not captured and not required:
            return False
        require(captured, 'Owned PIE game-window capture failed')
        data = target.read_bytes()
        require(len(data)>24 and data.startswith(b'\x89PNG\r\n\x1a\n'), 'Invalid PNG capture')
        row = dict(file=str(target), time=game_time, phase=self.phase, world=path(self.world),
            capture_request_wall_seconds=requested, capture_complete_wall_seconds=time.monotonic()-self.started,
            width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'),
            bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), visual_review='pending_root_review')
        self.report['frames' if sequence else 'screenshots'].append(row)
        if sequence:
            self.frame_bytes += len(data)
            if self.clip:
                self.report['clips'][self.clip].append(row)
        return True

    def initialize_input(self):
        library = u.get_default_object(u.load_class(None, '/Script/Engine.SubsystemBlueprintLibrary'))
        self.subsystem = library.call_method('GetLocalPlayerSubSystemFromPlayerController',
            args=(self.pc, u.load_class(None, '/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem')))
        require(valid(self.subsystem), 'No local Enhanced Input subsystem')
        mapped = []
        for action in u.ObjectIterator(u.InputAction):
            if action.get_outer() != self.hero:
                continue
            keys = self.subsystem.call_method('QueryKeysMappedToAction', args=(action,))
            names = {str(key.get_editor_property('KeyName')) for key in keys}
            mapped.append(dict(action=path(action), keys=sorted(names)))
            for name in ('Five', 'One'):
                if name in names:
                    require(name not in self.actions, 'Ambiguous active '+name+' mapping')
                    self.actions[name] = action
        require(set(self.actions) == {'Five', 'One'}, 'Five/One active Enhanced Input actions missing')
        self.report['input_route'] = dict(subsystem=path(self.subsystem), active_mappings=mapped,
            method='InjectInputVectorForAction', physical_hardware=False)

    def input_frame(self):
        if valid(self.subsystem):
            for name, action in self.actions.items():
                self.subsystem.call_method('InjectInputVectorForAction',
                    args=(action, u.Vector(1. if self.injected == name else 0., 0., 0.), [], []))

    def press(self, name, expected, after):
        self.injected = name; self.expected_equipped = expected; self.input_after = after
        self.stage('key_hold')

    def equipped(self):
        return bool(self.rod.get_editor_property('bEquipped'))

    def sample(self):
        self.report['samples'].append(dict(phase=self.phase, game_seconds=self.now(),
            position_cm=vec(self.hero.get_actor_location()), equipped=self.equipped(),
            presented=bool(self.rod.is_presented()), inside_authoritative=bool(self.rod.is_inside_fishing_zone()),
            inside_geometry=bool(self.zone.contains_pawn(self.hero)), velocity_cm_s=vec(self.hero.get_velocity())))

    def prepare_walk(self, point, phase, clip=None):
        self.target = u.Vector(float(point[0]), float(point[1]), self.hero.get_actor_location().z)
        self.report['walks'].append(dict(phase=phase, from_cm=vec(self.hero.get_actor_location()),
            target_xy_cm=[float(point[0]),float(point[1])], method='Pawn.AddMovementInput; no teleport/path override'))
        self.stage(phase, clip)

    def walk(self):
        require(time.monotonic()-self.phase_wall < 30., 'Walking leg timed out; inspect scene route/collision')
        current = self.hero.get_actor_location()
        dx, dy = self.target.x-current.x, self.target.y-current.y
        distance = math.hypot(dx,dy)
        if distance <= 35.:
            self.hero.character_movement.stop_movement_immediately()
            return True
        self.hero.add_movement_input(u.Vector(dx/distance,dy/distance,0.), 1., False)
        return False

    def exit_target(self):
        box = self.zone.get_editor_property('Zone')
        center = box.get_world_location(); transform = box.get_world_transform()
        dx,dy = self.initial_spawn.x-center.x,self.initial_spawn.y-center.y
        length = math.hypot(dx,dy)
        require(length>1., 'Spawn cannot establish a safe landward exit direction')
        direction = u.Vector(dx/length,dy/length,0.)
        local_a = u.MathLibrary.inverse_transform_location(transform,center)
        local_b = u.MathLibrary.inverse_transform_location(transform,center+direction)
        local = local_b-local_a; extent = box.get_unscaled_box_extent()
        distances = [extent.x/abs(local.x) if abs(local.x)>1e-5 else 1e9,
                     extent.y/abs(local.y) if abs(local.y)>1e-5 else 1e9]
        point = center+direction*(min(distances)+160.)
        return [point.x,point.y]

    def initialize_world(self, worlds):
        if not worlds:
            return False
        require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one standalone authority PIE')
        if self.world is None:
            self.world = worlds[0]; self.instance = u.GameplayStatics.get_game_instance(self.world)
            require(u.GameplayStatics.get_current_level_name(self.world,True)=='Hub', 'Probe requires actual saved Hub')
        require(worlds[0]==self.world and valid(self.instance), 'Owned PIE was replaced')
        self.pc = u.GameplayStatics.get_player_controller(self.world,0)
        if not valid(self.pc):
            return False
        if not self.first_capture_done:
            self.first_capture_done = self.capture('00_FirstObservedGameWindow.png', required=False)
        self.hero = self.pc.get_controlled_pawn()
        if not isinstance(self.hero,u.MCToothCharacter) or self.now()<2. or not self.pc.is_world_presentation_ready() or self.pc.is_move_input_ignored():
            return False
        zones = list(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubFishingZone))
        require(len(zones)==1 and zones[0].get_editor_property('bEnabled'), 'Expected one enabled authored pier fishing zone')
        self.zone = zones[0]; self.rod = self.hero.get_editor_property('FishingRod')
        require(valid(self.rod), 'No native rod component on the actual pawn')
        self.pawn_path = path(self.hero); self.initial_spawn = self.hero.get_actor_location()
        self.check('spawn_outside_pier',not self.zone.contains_pawn(self.hero) and not self.rod.is_inside_fishing_zone())
        self.check('spawn_rod_stowed',not self.equipped() and not self.rod.is_presented())
        ready = [zone for zone in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubReadyZone)
                 if zone.get_editor_property('bEnabled')]
        require(ready,'No enabled Ready target')
        nearest = min(ready,key=lambda item:(item.get_actor_location()-self.initial_spawn).length())
        toward = nearest.get_actor_location()-self.initial_spawn
        actor_dot = dot_xy(self.hero.get_actor_forward_vector(),toward)
        camera_dot = dot_xy(self.hero.get_editor_property('Camera').get_forward_vector(),toward)
        self.report['spawn'] = dict(position_cm=vec(self.initial_spawn),ready=path(nearest),actor_dot_xy=actor_dot,camera_dot_xy=camera_dot)
        self.check('spawn_faces_ready',actor_dot>.98)
        self.check('initial_camera_faces_ready',camera_dot>.98)
        self.initialize_input()
        self.report['world'] = path(self.world); self.report['pawn'] = self.pawn_path
        self.report['fishing_zone'] = dict(path=path(self.zone),location_cm=vec(self.zone.get_actor_location()),
            half_extent_cm=vec(self.zone.get_editor_property('Zone').get_unscaled_box_extent()))
        self.stage('spawn_ready','SpawnReady'); self.capture('01_SpawnFacesReady_RodStowed.png')
        return True

    def advance(self):
        age = self.now()-self.phase_at
        if self.phase=='spawn_ready' and age>=1.2:
            self.press('Five',False,'walk_to_pier')
        elif self.phase=='key_hold' and age>=.18:
            self.injected=None;self.stage('key_release')
        elif self.phase=='key_release' and age>=.25:
            self.report['input_samples'].append(dict(key=self.last_key,expected_equipped=self.expected_equipped,
                actual_equipped=self.equipped(),inside=self.rod.is_inside_fishing_zone(),game_seconds=self.now(),after=self.input_after))
            self.check('input_'+str(len(self.report['input_samples']))+'_'+self.last_key,self.equipped()==self.expected_equipped)
            after=self.input_after
            if after=='walk_to_pier':
                self.check('five_cannot_equip_outside_pier',not self.rod.is_presented())
                point=self.route[0] if self.route else vec(self.zone.get_actor_location())[:2]
                self.prepare_walk(point,'walk_to_pier','PierAutoEquip')
            elif after=='next_toggle':
                self.input_index+=1;self.stage('inside_toggle_gap')
            elif after=='walk_reentry':
                self.capture('05_OutsidePier_FiveRejected.png')
                self.prepare_walk(vec(self.zone.get_actor_location())[:2],'walk_reentry','PierReentry')
        elif self.phase=='walk_to_pier' and self.walk():
            if self.route_index<len(self.route):
                self.route_index+=1
                point=self.route[self.route_index] if self.route_index<len(self.route) else vec(self.zone.get_actor_location())[:2]
                self.prepare_walk(point,'walk_to_pier')
            else:
                self.stage('entry_settle')
        elif self.phase=='entry_settle' and age>=.7:
            self.check('walking_entry_grants_access',self.zone.contains_pawn(self.hero) and self.rod.is_inside_fishing_zone())
            self.check('walking_entry_auto_equips',self.equipped() and self.rod.is_presented())
            self.check('rod_visual_visible',valid(self.rod.rod_visual) and self.rod.rod_visual.is_visible())
            self.capture('02_PierAutoEquipped.png');self.stage('inside_toggle_gap','PierKeyToggle')
        elif self.phase=='inside_toggle_gap' and age>=.7:
            sequence=[('Five',False),('Five',True),('One',False),('Five',True)]
            if self.input_index<len(sequence):
                key,expected=sequence[self.input_index];self.press(key,expected,'next_toggle')
            else:
                self.capture('03_PierFiveAndTools.png');self.prepare_walk(self.exit_target(),'walk_exit','PierExitAndGate')
        elif self.phase=='walk_exit' and self.walk():
            self.stage('exit_settle')
        elif self.phase=='exit_settle' and age>=.7:
            self.check('walking_exit_revokes_access',not self.zone.contains_pawn(self.hero) and not self.rod.is_inside_fishing_zone())
            self.check('walking_exit_stows_rod',not self.equipped() and not self.rod.is_presented())
            self.check('walking_exit_clears_fishing',all(not self.rod.get_editor_property(name) for name in
                ('bLineOut','bFishingPose','bReeling','bFishHooked')))
            self.capture('04_PierExitStowsRod.png');self.press('Five',False,'walk_reentry')
        elif self.phase=='walk_reentry' and self.walk():
            self.stage('reentry_settle')
        elif self.phase=='reentry_settle' and age>=1.2:
            self.check('walking_reentry_auto_equips',self.rod.is_inside_fishing_zone() and self.equipped() and self.rod.is_presented())
            self.capture('06_PierReentryAutoEquipped.png')
            if self.capture_hz and self.max_frames:
                self.check('each_requested_clip_has_two_native_frames',all(len(rows)>=2 for rows in self.report['clips'].values()))
            self.begin_finish()

    def tick(self,_delta):
        if self.done or self.busy:
            return
        self.busy=True
        try:
            if self.finishing:
                worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
                if not worlds and not self.levels.is_in_play_in_editor():
                    self.report['owned_pie_stopped']=True;self.finish()
                elif time.monotonic()-self.phase_wall>12.:
                    self.error=self.error or 'Owned PIE stop timed out';self.finish()
                return
            require(time.monotonic()-self.started<100.,'Scoped check exceeded 100 wall seconds')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.phase=='loading':
                require(time.monotonic()-self.started<30.,'Saved Hub did not become usable within 30 seconds')
                self.initialize_world(worlds);return
            require(len(worlds)==1 and valid(self.world) and worlds[0]==self.world,'Owned PIE ended/travelled/was replaced')
            require(valid(self.hero) and self.pc.get_controlled_pawn()==self.hero and path(self.hero)==self.pawn_path,'Subject pawn changed')
            require(not self.pc.is_move_input_ignored(),'Unexpected modal blocked the scoped movement check')
            if self.injected is not None:
                self.last_key=self.injected
            self.input_frame()
            current=time.monotonic()
            if current>=self.sample_at:
                self.sample_at=current+.2;self.sample()
            if self.capture_hz and current>=self.frame_at:
                if len(self.report['frames'])<self.max_frames and self.frame_bytes<900*1024*1024:
                    self.frame_at=current+1./self.capture_hz;self.capture()
                else:
                    self.report['capture_limit_reached']=True
            self.advance()
        except Exception:
            self.begin_finish(traceback.format_exc())
        finally:
            self.busy=False

    def cancel(self):
        self.begin_finish('Cancelled explicitly')

    def begin_finish(self,error=None):
        if self.done or self.finishing:
            return
        self.error=self.error or error;self.injected=None
        try:
            self.input_frame()
        except Exception:
            self.restore_errors.append(traceback.format_exc())
        worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
        owned=valid(self.world) and len(worlds)==1 and worlds[0]==self.world
        if worlds and not owned:
            self.report['replacement_pie_preserved']=True
            self.error=self.error or 'Replacement PIE preserved';self.finish();return
        if owned and self.requested_pie:
            self.levels.editor_request_end_play();self.report['owned_pie_end_requested']=True
            self.finishing=True;self.stage('ending_pie')
        else:
            self.finish()

    def finish(self):
        if self.done:
            return
        self.done=True
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback);self.callback=None
        for action in (lambda:require(u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old_play)), 'PIE preferences restore rejected'),
                       lambda:self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old_throttle)):
            try:
                action()
            except Exception:
                self.restore_errors.append(traceback.format_exc())
        if dirty()!=self.report.get('dirty_before',[]):
            self.error=self.error or 'Dirty package state changed; preserved for review'
        if self.restore_errors:
            self.error=self.error or 'Temporary setting restoration failed'
        self.report.update(passed=not bool(self.error),status='failed' if self.error else 'passed',error=self.error,
            restored_settings=not self.restore_errors,restore_errors=self.restore_errors,dirty_after=dirty(),
            capture_total_bytes=self.frame_bytes)
        self.write();u.log('MC_HUB_PIER_SOLO_'+('FAIL ' if self.error else 'PASS ')+str(OUT/'Report.json'))


def main():
    previous=getattr(builtins,KEY,None)
    require(previous is None or getattr(previous,'done',True),'This probe is already active')
    probe=HubPierSolo();setattr(builtins,KEY,probe)
    try:
        probe.start()
    except Exception:
        probe.begin_finish(traceback.format_exc())
    return probe


if __name__=='__main__':
    main()
