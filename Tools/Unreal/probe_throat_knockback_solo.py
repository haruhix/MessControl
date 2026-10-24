"""ROOT-RUN ONLY: real L_Mouth rim knockdown in one owned normal solo PIE.

The existing F3 Ragdoll action calls the native ToothPhysics ApplyHit with
forward*600 + Z300. A disclosed one-time starting placement on the actual
tongue directs that hit toward the aperture or either side fold. No position,
health, body-state or recovery writes occur after launch. Native Chaos,
throat death checks, recovery and reserve respawn remain in control. This is
a controlled knockback fixture, not an actual boss attack/balance test.

Requires idle clean L_Mouth, -MCHubTransientProfile, and no -MCDevUI. Never
saves packages. Cancels with builtins.mc_throat_knockback_solo.finish('cancel').
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

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_THROAT_OUTPUT', 'E:/CodexWork/ThroatKnockback_20261024/Solo'))
require_drive = lambda p: p.drive.upper() == 'E:'
if not require_drive(ROOT) or not require_drive(OUTPUT):
    raise RuntimeError('All output paths must remain on E:')
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_playtest_recovery_solo.py'), run_name='throat_probe_support')
Parent, prop, valid, require = (support[k] for k in ('PlaytestRecoverySolo', 'prop', 'valid', 'require'))
Parent.__init__.__globals__['OUTPUT'] = OUTPUT
Parent.__init__.__globals__['TEMP'] = Path('E:/CodexTemp/ThroatKnockback_20261024')
KEY = 'mc_throat_knockback_solo'


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def path(obj):
    return obj.get_path_name() if valid(obj) else None


class ThroatKnockbackSolo(Parent):
    def __init__(self):
        super().__init__()
        require('-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(),
                'Transient profile required; real wallet/save files must not be used')
        self.case = None
        self.shots = [('center', 0.), ('right_rim', 1.18), ('left_rim', -1.18)]
        self.shot_index = 0
        self.next_sample = self.next_frame = 0.
        self.capture_hz = max(0., min(6., float(globals().get('MC_THROAT_CAPTURE_HZ', 6.))))
        self.report.update(mode='throat_knockback_solo', cases=[], frames=[],
            fixtures=['One initial grounded pawn placement per shot; no transform writes after native launch',
                      'Actual F3 Ragdoll action invokes native forward*600+Z300 ApplyHit',
                      'Native F3 action suspends unrelated schedulers; all effects remain in the temporary PIE',
                      'Ordinary AddMovementInput after recovery proves restored movement'],
            limitations=['Controlled knockback fixture, not a boss attack or combat balance test.',
                         'Normal game camera/HUD, no SetViewTarget or camera transform writes.',
                         'No multicast/network/cook/package or cold-load resource certification.'],
            source_sha256={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                for name in ('Tools/Unreal/probe_throat_knockback_solo.py',
                             'Source/MessControl/Private/MCThroat.cpp',
                             'Source/MessControl/Private/MCToothPhysicsComponent.cpp')})

    def start(self):
        settings = Parent.start.__globals__
        settings['PLAY'] = dict(settings['PLAY'], clientFixedFPS=[0])
        super().start()
        # This check observes real game time, without fast-forward or fixed-frame
        # stepping. Base.finish still restores the original Editor settings.
        self.engine.set_editor_property('bUseFixedFrameRate', False)

    def actors(self, cls):
        return [a for a in u.GameplayStatics.get_all_actors_of_class(self.world, cls) if valid(a)]

    def floor(self, point):
        result = self.surface.line_trace_component(point+u.Vector(0,0,3000), point-u.Vector(0,0,3000), True, False, False)
        if isinstance(result, (tuple,list)) and len(result) == 4 and isinstance(result[0],u.Vector):
            return result[0], result[1]
        return None

    def setup(self):
        self.throat = self.actors(u.MCThroat)[0]
        self.tongue = self.actors(u.MCTongue)[0]
        self.surface = prop(self.tongue, 'surface')
        values = self.throat.get_intake_aim_target()
        self.inlet = next(v for v in values if isinstance(v,u.Vector))
        self.aperture_radius = next(float(v) for v in values if isinstance(v,(float,int)) and not isinstance(v,bool))
        mouth = prop(self.throat, 'authored_mouth')
        art = mouth.get_world_transform()
        normal = art.transform_direction(u.Vector(0,.9747185974,.2234360218))
        self.inward = u.Vector(normal.x,normal.y,0)
        size = math.hypot(self.inward.x,self.inward.y)
        require(size > .1, 'Invalid actual aperture direction')
        self.inward /= size
        self.right = art.transform_direction(u.Vector(1,0,0))
        self.right = u.Vector(self.right.x,self.right.y,0)
        self.right /= math.hypot(self.right.x,self.right.y)
        self.report['actual_throat'] = dict(actor=path(self.throat), inlet_cm=vec(self.inlet),
            radius_cm=self.aperture_radius, inward=vec(self.inward), right=vec(self.right),
            open_amount=float(self.throat.open_amount()), manual=bool(prop(self.throat,'manual_intake')))
        require(self.throat.open_amount() >= .65, 'Expected the normal open manual throat')
        self.prepare_case()

    def prepare_case(self):
        hero = self.pc.get_controlled_pawn()
        require(valid(hero) and prop(hero,'status').is_alive() and self.ready(), 'Living ready player required')
        self.hero = hero
        physics = prop(hero,'tooth_physics')
        require(physics.get_body_state() == u.MCBodyState.STANDING, 'Cannot reset a fallen fixture to manufacture success')
        name, offset = self.shots[self.shot_index]
        candidates = []
        capsule = hero.get_component_by_class(u.CapsuleComponent)
        half = capsule.get_scaled_capsule_half_height()
        target = self.inlet+self.right*(offset*self.aperture_radius)
        searched = []
        # The centre approach is proven supported on the authored tongue.
        # Keep that real starting ground and aim diagonally toward each fold;
        # the side of the hole itself is steep tissue, not a valid start floor.
        for distance in (260.,340.,440.,540.,600.,800.,1000.):
            probe = self.inlet-self.inward*distance
            floor = self.floor(probe)
            supported = floor is not None and floor[1].z >= .65
            searched.append(dict(lateral_factor=0.,distance_cm=distance,supported=supported,
                probe_cm=vec(probe),floor_cm=vec(floor[0]) if floor else None,
                normal=vec(floor[1]) if floor else None))
            if supported:
                point = floor[0]+u.Vector(0,0,half+4)
                candidates.append((distance,point,floor[1]))
        self.report.setdefault('placement_attempts',[]).append(dict(case=name,requested_target_cm=vec(target),
            requested_lateral_factor=offset,ground_search=searched))
        self.write()
        require(candidates, 'No walkable actual tongue starting point for '+name)
        distance,start,normal = min(candidates,key=lambda row:row[0])
        delta = target-start
        horizontal = math.hypot(delta.x,delta.y)
        require(horizontal > 1.,'Degenerate diagonal fixture direction')
        self.launch_direction = u.Vector(delta.x/horizontal,delta.y/horizontal,0.)
        hero.set_actor_location_and_rotation(start, self.launch_direction.rotator(), False, True)
        hero.get_movement_component().stop_movement_immediately()
        self.case = dict(name=name, pawn=path(hero), target_cm=vec(target), initial_cm=vec(start),
            initial_surface_normal=vec(normal), distance_cm=distance, passed=False,
            requested_lateral_factor=offset,effective_lateral_factor=offset,
            start_lateral_factor=0.,diagonal_target_distance_cm=horizontal,
            starting_ground_search=searched,
            direction=vec(self.launch_direction),native_impulse_cm_s=vec(self.launch_direction*600+u.Vector(0,0,300)),
            samples=[], observed_ragdoll=False, death_seen=False, respawn_seen=False)
        self.report['cases'].append(self.case)
        self.stage('initial_settle')

    def state(self, hero):
        health = prop(prop(hero,'status'),'state')
        physics = prop(hero,'tooth_physics')
        mesh = prop(hero,'mesh')
        names = [mesh.get_bone_name(index) for index in range(mesh.get_num_bones())]
        appearance = prop(hero,'appearance')
        mapping = prop(appearance,'bone_map') if valid(appearance) else {}
        bone = mapping.get('body', 'body')
        if str(bone) not in [str(n) for n in names]:
            bone = next((n for n in names if str(n).lower() in ('body','root','bone')), names[0])
        return dict(pawn=path(hero), game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            body_state=str(physics.get_body_state()), health=float(prop(health,'health')),
            alive=bool(prop(hero,'status').is_alive()), location_cm=vec(hero.get_actor_location()),
            body_cm=vec(mesh.get_socket_location(bone)),
            body_rotation=str(mesh.get_socket_rotation(bone)), velocity_cm_s=vec(hero.get_velocity()),
            movement_mode=str(prop(hero.get_movement_component(),'movement_mode')),
            respawn_at=float(prop(hero,'respawn_at')), swallowed_by=path(prop(hero,'swallowed_by')),
            view_target=path(self.pc.get_view_target()), input_ignored=self.pc.is_move_input_ignored())

    def capture(self, label):
        target = self.output/(label+'.png')
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world,str(target)), 'Actual PIE capture failed')
        row = dict(file=str(target), case=self.case['name'], game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            sha256=hashlib.sha256(target.read_bytes()).hexdigest(), view_target=path(self.pc.get_view_target()))
        self.report['frames'].append(row)

    def launch(self):
        require(prop(self.hero,'tooth_physics').get_body_state() == u.MCBodyState.STANDING, 'Pawn fell before the fixture launch')
        self.capture(self.case['name']+'_00_before')
        self.pc.call_method('ToggleDevPanel',args=())
        self.pc.request_dev_action(u.MCDevAction.RAGDOLL,-1)
        self.pc.call_method('ToggleDevPanel',args=())
        require(prop(self.hero,'tooth_physics').get_body_state() == u.MCBodyState.RAGDOLL, 'Native Ragdoll action did not enter Ragdoll')
        self.case['observed_ragdoll'] = True
        self.case['launched_game_seconds'] = float(u.GameplayStatics.get_time_seconds(self.world))
        self.case['launched_state'] = self.state(self.hero)
        self.next_sample = self.next_frame = 0.
        self.stage('observe_native')

    def tick(self,_dt):
        if self.done:
            return
        try:
            wall = time.monotonic()
            require(wall-self.started < 180., 'Probe exceeded wall time limit')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one owned solo authority world')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world,0)
                self.gs = u.GameplayStatics.get_game_state(self.world)
                if not valid(self.pc) or not valid(self.gs):
                    self.world = None
                    return
                self.stage('startup_ready')
            require(self.world in worlds, 'Owned PIE ended; external replacement was preserved')
            if self.pc.is_reward_menu_open():
                require(self.phase=='startup_ready','Unrelated reward interrupted this fixture')
                if wall-self.choice_at > .4:
                    self.pc.choose_reward_perk(0)
                    self.choice_at=wall
                return
            hero = self.pc.get_controlled_pawn()
            if self.phase=='startup_ready':
                if valid(hero) and self.ready() and not prop(self.gs,'tutorial_active') and not prop(self.gs,'lobby_waiting'):
                    self.ready_since=self.ready_since or wall
                    if wall-self.ready_since > .75:
                        self.setup()
                return
            if self.phase=='initial_settle':
                if wall-self.phase_at > .75:
                    require(self.ready(),'Starting placement lost normal input readiness')
                    self.launch()
                return
            if self.phase=='observe_native':
                game=float(u.GameplayStatics.get_time_seconds(self.world))
                elapsed=game-self.case['launched_game_seconds']
                require(valid(hero),'Native player pawn disappeared without replacement')
                if game>=self.next_sample:
                    self.case['samples'].append(self.state(hero))
                    self.next_sample=game+.10
                if valid(self.hero):
                    self.case['death_seen'] = self.case['death_seen'] or not prop(self.hero,'status').is_alive()
                self.case['respawn_seen'] = self.case['respawn_seen'] or hero!=self.hero
                if self.capture_hz and game>=self.next_frame:
                    self.capture(self.case['name']+'_%04d'%len(self.case['samples']))
                    self.next_frame=game+1./self.capture_hz
                standing=prop(hero,'tooth_physics').get_body_state()==u.MCBodyState.STANDING
                if elapsed>=2.5 and standing and prop(hero,'status').is_alive() and self.ready():
                    self.case['native_outcome']='death_and_reserve_respawn' if hero!=self.hero else 'recovered'
                    self.case['standing_state']=self.state(hero)
                    self.move_hero=hero
                    self.move_start=hero.get_actor_location()
                    # Unreal's mutable FVector Python wrapper must not alias
                    # or negate our persistent aperture basis for later shots.
                    self.move_direction=u.Vector(-float(self.inward.x),-float(self.inward.y),-float(self.inward.z))
                    self.move_game=game
                    self.stage('prove_movement')
                elif elapsed>=15.:
                    self.capture(self.case['name']+'_stuck_15s')
                    self.case['native_outcome']='unresolved_ragdoll_or_death'
                    self.case['final_state']=self.state(hero)
                    raise RuntimeError('Native knockback did not recover/respawn within15 game seconds: '+self.case['name'])
            elif self.phase=='prove_movement':
                require(hero==self.move_hero,'Pawn changed during movement proof')
                game=float(u.GameplayStatics.get_time_seconds(self.world))
                if game-self.move_game < .5:
                    hero.add_movement_input(self.move_direction,1.,False)
                else:
                    displacement=math.dist(vec(hero.get_actor_location()),vec(self.move_start))
                    self.case['movement_displacement_cm']=displacement
                    self.case['final_state']=self.state(hero)
                    require(displacement>15.,'Standing pawn still cannot move after knockback')
                    self.case['passed']=True
                    self.capture(self.case['name']+'_recovered')
                    self.shot_index+=1
                    if self.shot_index>=len(self.shots):
                        self.finish()
                    else:
                        self.prepare_case()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    previous=getattr(builtins,KEY,None)
    require(previous is None or previous.done,'Throat probe is already running')
    probe=ThroatKnockbackSolo()
    setattr(builtins,KEY,probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
    return probe


if __name__=='__main__':
    main()
