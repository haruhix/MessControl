"""Owned solo check of food carry pose, trajectory agreement and a longer shot.

Pauses its actions at 'review' for a native Slate screenshot. Call
builtins.mc_ball_arc_check.resume() to continue. No asset saves or network peers.
"""
import unreal as u
import builtins,json,time,traceback,math
from pathlib import Path

OUT=Path(u.Paths.project_dir())/'Saved/HubBasketball_20261021/TrajectoryUpdate'
def vec(v):return [v.x,v.y,v.z]
def normal(a):return (a+180)%360-180

class Probe:
    def __init__(self):
        assert '-MCHubBasketballOwned' in u.SystemLibrary.get_command_line()
        assert not u.EditorLevelLibrary.get_pie_worlds(False)
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False,'newWindowWidth':1280,'newWindowHeight':720}
        self.old=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(values)))
        assert u.ToolsetLibrary.set_object_properties(self.play,json.dumps(values))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        self.started=time.monotonic();self.phase='loading';self.world=None;self.shot=0;self.previous=None
        self.report={'passed':False,'checks':{},'shots':[],'scope':'Owned standalone PIE; real carry pose, native throw input callbacks, preview/Chaos comparison; no multiplayer or package.'}
        self.callback=u.register_slate_post_tick_callback(self.tick)
        self.request=u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StartPIE',json.dumps({'options':{'bSimulate':False,'playMode':'PlayMode_InEditorFloating','warmupSeconds':0.}}))
    def now(self):return u.GameplayStatics.get_time_seconds(self.world)
    def stage(self,name):self.phase=name;self.at=self.now();self.previous=None
    def check(self,name,ok):
        self.report['checks'][name]=bool(ok)
        assert ok,name
    def write(self):
        self.report['phase']=self.phase
        (OUT/'solo_report.json').write_text(json.dumps(self.report,indent=2),encoding='utf-8')
    def key_e(self):self.hero.set_handle_input_held(True);self.hero.set_handle_input_held(False)
    def prepare(self,distance,side):
        self.hero.set_actor_location(self.center+u.Vector(-distance,side,-self.center.z-8),False,True)
        self.hero.set_actor_rotation(u.Rotator(0,0,0),True)
        self.ball.set_actor_location(self.hero.get_actor_location()+u.Vector(160,0,-8),False,True)
        self.ball.body.set_physics_linear_velocity(u.Vector(),False)
        self.ball.body.set_physics_angular_velocity_in_radians(u.Vector(),False)
        self.stage('pickup')
    def aim(self,dt):
        camera=u.GameplayStatics.get_player_camera_manager(self.world,0)
        desired=u.MathLibrary.find_look_at_rotation(camera.get_camera_location(),self.center)
        rotation=camera.get_camera_rotation()
        x=normal(desired.yaw-rotation.yaw);y=normal(desired.pitch-rotation.pitch)
        gain=min(.12,max(.01,dt*3))/self.hero.camera_orbit_sensitivity
        self.hero.apply_camera_orbit_input(u.Vector2D(x*gain,y*gain))
        return abs(x)<.05 and abs(y)<.05
    def resume(self):
        assert self.phase=='review'
        self.begin_shot()
    def begin_shot(self):
        self.predicted=list(self.ball.predict_throw_trajectory())
        launch=self.ball.launch_velocity()
        self.data={'distance_to_rim':(self.center-self.ball.get_actor_location()).length(),'planned_velocity':vec(launch),
                   'predicted_path':[vec(v) for v in self.predicted],'samples':[],'preview_errors_cm':[]}
        if self.shot==0:self.hero.set_primary_input_held(True);self.hero.set_primary_input_held(False)
        else:self.hero.set_throw_input_held(True);self.hero.set_throw_input_held(False)
        actual=self.ball.body.get_physics_linear_velocity()
        self.check('shot_'+str(self.shot)+'_uses_preview_velocity',(actual-launch).length()<2)
        self.check('shot_'+str(self.shot)+'_releases',not self.ball.holder)
        self.data['actual_velocity']=vec(actual)
        self.stage('flight')
    def tick(self,dt):
        try:
            if self.phase=='review':return
            if time.monotonic()-self.started>150:raise RuntimeError('Check timeout: '+self.phase)
            if self.phase=='loading':
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                assert len(worlds)==1;self.world=worlds[0]
                self.hero=u.GameplayStatics.get_player_character(self.world,0)
                self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                if not self.hero or self.now()<3 or not self.pc.is_world_presentation_ready() or self.pc.is_move_input_ignored() or self.pc.is_look_input_ignored():return
                balls=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubBasketball)
                self.check('one_ball',len(balls)==1);self.ball=balls[0]
                self.check('stronger_speed_saved',self.ball.throw_speed>=2100)
                self.center=u.MathLibrary.transform_location(self.ball.hoop.get_actor_transform(),self.ball.local_rim_center)
                self.normal=self.ball.hoop.get_actor_up_vector()
                self.prepare(1000,-280);return
            elapsed=self.now()-self.at
            if self.phase=='pickup' and elapsed>.3:
                self.key_e();self.check('pickup_'+str(self.shot),self.ball.holder==self.hero)
                self.hero.set_brace_input_held(True);self.stage('aim')
            elif self.phase=='aim':
                aimed=self.aim(dt)
                if elapsed>3 and aimed:
                    points=self.ball.predict_throw_trajectory()
                    self.check('preview_has_arc_'+str(self.shot),len(points)>15 and max(p.z for p in points)>self.ball.get_actor_location().z+100)
                    if self.shot==0:
                        mapping={str(k):v for k,v in self.hero.appearance.bone_map.items()}
                        hand=self.hero.get_actor_location()+self.hero.get_actor_forward_vector()*73+self.hero.get_actor_right_vector()*12+u.Vector(0,0,20)
                        errors=[]
                        for i,role in enumerate(['hand_l','hand_r']):
                            target=hand+self.hero.get_actor_right_vector()*(-20 if i==0 else 20)
                            actual=self.hero.mesh.get_socket_location(mapping.get(role,role))
                            errors.append((actual-target).length())
                        self.report['food_pose_hand_errors_cm']=errors
                        self.check('uses_food_holding_pose',max(errors)<5)
                        self.check('tool_hidden_while_holding',not self.hero.brush.is_visible())
                        if globals().get('BASKETBALL_SKIP_REVIEW',False):self.begin_shot()
                        else:self.stage('review');self.write();print('BASKETBALL_ARC_VISUAL_READY')
                    else:
                        self.check('long_shot_stronger_than_before',self.ball.launch_velocity().length()>1400)
                        self.begin_shot()
            elif self.phase=='flight':
                p=self.ball.get_actor_location();v=self.ball.body.get_physics_linear_velocity()
                self.data['samples'].append({'t':elapsed,'p':vec(p),'v':vec(v)})
                if elapsed<min(.7,(len(self.predicted)-2)/30):
                    # Compare spatial paths: the engine clamps physics delta on
                    # a slow frame, while the world clock can advance farther.
                    errors=[]
                    for a,b in zip(self.predicted,self.predicted[1:]):
                        ab=b-a;length2=ab.dot(ab)
                        blend=max(0.,min(1.,(p-a).dot(ab)/length2)) if length2>1e-6 else 0.
                        errors.append((p-(a+ab*blend)).length())
                    self.data['preview_errors_cm'].append(min(errors))
                if self.previous:
                    oldp,oldv=self.previous
                    d0=(oldp-self.center).dot(self.normal);d1=(p-self.center).dot(self.normal)
                    if d0>0 and d1<=0 and v.z<0 and 'rim_crossing_cm' not in self.data:
                        cross=oldp+(p-oldp)*(d0/(d0-d1));self.data['rim_crossing_cm']=(cross-self.center).length()
                self.previous=(p,v)
                if elapsed>3.4:
                    self.check('preview_matches_flight_'+str(self.shot),bool(self.data['preview_errors_cm']) and max(self.data['preview_errors_cm'])<12)
                    self.check('shot_reaches_open_rim_'+str(self.shot),self.data.get('rim_crossing_cm',1000)<33)
                    self.report['shots'].append(self.data)
                    if self.shot==0:
                        self.shot=1;self.prepare(1800,0)
                    else:self.report['passed']=True;self.finish()
        except Exception:
            self.report['error']=traceback.format_exc();self.finish()
    def finish(self):
        u.unregister_slate_post_tick_callback(self.callback)
        self.report['wall_seconds']=time.monotonic()-self.started;self.write()
        u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
        u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.throttle)
        print('BASKETBALL_ARC_CHECK_DONE',self.report['passed'],self.report.get('error',''))

builtins.mc_ball_arc_check=Probe()
