"""Short owned solo PIE check; runtime placements exercise real input and Chaos.

No network peers, saves, cook or package. Restores temporary editor play settings.
"""
import unreal as u
import builtins,json,time,math,traceback
from pathlib import Path

OUT=Path(u.Paths.project_dir())/'Saved/HubBasketball_20261021'
def xyz(v):return [v.x,v.y,v.z]
def norm(a):return (a+180)%360-180

class Probe:
    def __init__(self):
        assert '-MCHubBasketballOwned' in u.SystemLibrary.get_command_line()
        assert not u.EditorLevelLibrary.get_pie_worlds(False)
        self.levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        values=dict(playNetMode='PIE_Standalone',playNumberOfClients=1,runUnderOneProcess=True,bLaunchSeparateServer=False,
                    newWindowWidth=1280,newWindowHeight=720)
        self.old=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(values)))
        assert u.ToolsetLibrary.set_object_properties(self.play,json.dumps(values))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        self.phase='loading';self.started=time.monotonic();self.at=0;self.world=None;self.prev=None;self.samples=[]
        self.report={'passed':False,'checks':{},'scope':'Short solo PIE; real E / primary / Q callbacks and Chaos. Runtime-only placements; no physical keyboard injection or multiplayer.'}
        self.callback=u.register_slate_post_tick_callback(self.tick)
        self.request=u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StartPIE',json.dumps({'options':{'bSimulate':False,'playMode':'PlayMode_InEditorFloating','warmupSeconds':0.}}))

    def check(self,name,value):
        self.report['checks'][name]=bool(value)
        assert value,name

    def stage(self,name):
        self.phase=name;self.at=u.GameplayStatics.get_time_seconds(self.world);self.prev=None

    def key_e(self):
        self.hero.set_handle_input_held(True);self.hero.set_handle_input_held(False)

    def place_ball(self,pos,vel):
        if self.ball.holder:self.ball.release(u.Vector())
        self.ball.set_actor_location(pos,False,True)
        self.ball.body.set_physics_linear_velocity(vel,False)
        self.ball.body.set_physics_angular_velocity_in_radians(u.Vector(),False)

    def tick(self,dt):
        try:
            if time.monotonic()-self.started>100:raise RuntimeError('Solo check timeout: '+self.phase)
            worlds=u.EditorLevelLibrary.get_pie_worlds(False)
            if self.phase=='loading':
                if not worlds:return
                assert len(worlds)==1
                self.world=worlds[0]
                self.hero=u.GameplayStatics.get_player_character(self.world,0)
                if not self.hero or u.GameplayStatics.get_time_seconds(self.world)<2:return
                balls=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubBasketball)
                self.check('one_saved_ball',len(balls)==1);self.ball=balls[0]
                self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                self.center=u.MathLibrary.transform_location(self.ball.hoop.get_actor_transform(),self.ball.local_rim_center)
                self.normal=self.ball.hoop.get_actor_up_vector()
                self.check('free_ball_simulates',self.ball.body.is_simulating_physics())
                self.check('basketball_mass',abs(self.ball.body.get_mass()-.62)<.02)
                self.key_e()
                self.check('pickup_rejects_distance',not self.ball.holder)
                self.hero.set_actor_location(self.ball.get_actor_location()+u.Vector(-165,0,10),False,True)
                self.hero.set_actor_rotation(u.Rotator(0,0,0),True)
                self.stage('approach');return
            now=u.GameplayStatics.get_time_seconds(self.world);elapsed=now-self.at
            p=self.ball.get_actor_location();v=self.ball.body.get_physics_linear_velocity()
            if self.phase=='approach' and elapsed>.2:
                self.key_e();self.stage('hold')
            elif self.phase=='hold':
                self.hero.add_movement_input(u.Vector(0,-1,0),1.,False)
                if elapsed>.6:
                    self.check('e_picks_up',self.ball.holder==self.hero)
                    self.check('held_disables_simulation',not self.ball.body.is_simulating_physics())
                    self.check('held_follows_player',p.distance(self.hero.get_actor_location())<160)
                    self.key_e();self.stage('drop')
            elif self.phase=='drop' and elapsed>.3:
                self.check('e_drops',not self.ball.holder and self.ball.body.is_simulating_physics())
                self.hero.set_actor_location(p+u.Vector(-160,0,10),False,True)
                self.key_e();self.check('can_pick_up_again',self.ball.holder==self.hero)
                self.hero.set_actor_location(u.Vector(self.center.x-700,self.center.y,-8),False,True)
                self.stage('aim')
            elif self.phase=='aim':
                manager=u.GameplayStatics.get_player_camera_manager(self.world,0)
                eye=manager.get_camera_location();rot=manager.get_camera_rotation()
                desired=u.MathLibrary.find_look_at_rotation(eye,self.center)
                error_yaw=norm(desired.yaw-rot.yaw);error_pitch=norm(desired.pitch-rot.pitch)
                gain=min(.12,max(.01,dt*3))
                self.hero.apply_camera_orbit_input(u.Vector2D(error_yaw*gain/self.hero.camera_orbit_sensitivity,error_pitch*gain/self.hero.camera_orbit_sensitivity))
                if elapsed>2.5 and abs(error_yaw)<.08 and abs(error_pitch)<.08:
                    self.hero.set_primary_input_held(True);self.hero.set_primary_input_held(False)
                    self.check('primary_throws',not self.ball.holder and self.ball.body.is_simulating_physics())
                    self.report['launch_velocity']=xyz(self.ball.body.get_physics_linear_velocity())
                    self.stage('flight')
            elif self.phase=='flight':
                self.samples.append({'t':elapsed,'p':xyz(p),'v':xyz(v)})
                if self.prev:
                    prevp,prevv=self.prev
                    d0=(prevp-self.center).dot(self.normal);d1=(p-self.center).dot(self.normal)
                    if d0>0 and d1<=0 and v.z<0 and 'rim_crossing_cm' not in self.report:
                        cross=prevp+(p-prevp)*(d0/(d0-d1));radial=(cross-self.center).length()
                        self.report['rim_crossing_cm']=radial
                    if prevv.z<-150 and v.z>150:self.report['flight_floor_bounce']=True
                if elapsed>4.5:
                    self.check('shot_passes_open_rim',self.report.get('rim_crossing_cm',1000)<33)
                    self.check('thrown_ball_bounces',self.report.get('flight_floor_bounce',False))
                    self.place_ball(u.Vector(self.center.x-260,self.center.y,self.center.z+80),u.Vector(800,0,0))
                    self.stage('backboard')
            elif self.phase=='backboard':
                if v.x<-100:self.report['backboard_reversed_x']=True
                if elapsed>1.:
                    self.check('backboard_rebounds',self.report.get('backboard_reversed_x',False))
                    self.place_ball(self.center+u.Vector(-94,0,250),u.Vector(0,0,-250))
                    self.stage('rim')
            elif self.phase=='rim':
                if v.z>100 or abs(v.x)>100:self.report['rim_deflected_ball']=True
                if elapsed>1.:
                    self.check('rim_rebounds',self.report.get('rim_deflected_ball',False))
                    self.place_ball(u.Vector(self.center.x-400,self.center.y,300),u.Vector())
                    self.bounces=[];self.stage('floor')
            elif self.phase=='floor':
                if self.prev and self.prev[1].z<-150 and v.z>150:
                    self.bounces.append({'incoming':self.prev[1].z,'outgoing':v.z,'ratio':v.z/-self.prev[1].z})
                if elapsed>3.:
                    self.report['floor_bounces']=self.bounces
                    self.check('multiple_physical_bounces',len(self.bounces)>=2)
                    self.check('basketball_restitution',.38<self.bounces[0]['ratio']<.62)
                    self.check('bounces_lose_energy',self.bounces[1]['outgoing']<self.bounces[0]['outgoing'])
                    self.hero.set_actor_location(p+u.Vector(-160,0,10),False,True);self.key_e()
                    self.check('q_has_held_ball',self.ball.holder==self.hero)
                    self.hero.set_throw_input_held(True);self.hero.set_throw_input_held(False)
                    self.check('q_throws',not self.ball.holder and self.ball.body.is_simulating_physics()
                               and self.ball.body.get_physics_linear_velocity().length()>100)
                    self.report['passed']=True;self.finish();return
            self.prev=(p,v)
        except Exception:
            self.report['error']=traceback.format_exc();self.finish()

    def finish(self):
        u.unregister_slate_post_tick_callback(self.callback)
        self.report['phase']=self.phase;self.report['elapsed_wall']=time.monotonic()-self.started
        self.report['trajectory']=self.samples
        (OUT/'solo_report.json').write_text(json.dumps(self.report,indent=2),encoding='utf-8')
        self.levels.editor_request_end_play()
        u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old_throttle)
        print('HUB_BASKETBALL_SOLO_RESULT',json.dumps({k:v for k,v in self.report.items() if k!='trajectory'}))

builtins.mc_basketball_probe=Probe()
