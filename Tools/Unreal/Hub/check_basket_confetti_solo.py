"""Short solo PIE integration probe: actual primary throw, score direction, FX and cleanup.
Runtime fixture moves only; restores temporary play/performance settings and saves no map.
"""
import unreal as u
import builtins,json,time,traceback
from pathlib import Path

OUT=Path(globals().get('MC_BASKET_CONFETTI_OUTPUT',str(Path(u.Paths.project_saved_dir())/'HubBasketballConfetti_20261021'))).resolve()
assert OUT.drive.upper()=='E:','Store probe output on E:'
OUT.mkdir(parents=True,exist_ok=True)
def xyz(v):return [v.x,v.y,v.z]
def norm(a):return (a+180)%360-180

class Probe:
    def __init__(self):
        assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
        # The native CDO must not deserialize the cooked stateless system before Niagara startup.
        assert u.get_default_object(u.MCHubBasketball).get_editor_property('BasketConfetti') is None
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        settings=dict(playNetMode='PIE_Standalone',playNumberOfClients=1,runUnderOneProcess=True,bLaunchSeparateServer=False,newWindowWidth=1280,newWindowHeight=720)
        self.old=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(settings)))
        assert u.ToolsetLibrary.set_object_properties(self.play,json.dumps(settings))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        self.start=time.monotonic();self.phase='loading';self.world=None;self.frames=[];self.shots=[]
        self.report={'passed':False,'checks':{'native_cdo_effect_unassigned':True},'scope':'One solo PIE, real held-ball primary throw and Chaos; runtime fixtures for direction/miss checks.',
                     'native_cdo_effect':None,'output':str(OUT)}
        self.callback=u.register_slate_post_tick_callback(self.tick)
        self.request=u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StartPIE',json.dumps({'options':{'bSimulate':False,'playMode':'PlayMode_InEditorFloating','warmupSeconds':0}}))

    def check(self,n,v):
        self.report['checks'][n]=bool(v)
        assert v,n

    def stage(self,p):self.phase=p;self.at=u.GameplayStatics.get_time_seconds(self.world)
    def place(self,p,v):
        if self.ball.get_editor_property('Holder'):self.ball.release(u.Vector())
        self.ball.set_actor_location(p,False,True)
        self.ball.body.set_physics_linear_velocity(v,False)
        self.ball.body.set_physics_angular_velocity_in_radians(u.Vector(),False)
    def fx(self):
        return [x for x in u.ObjectIterator(u.NiagaraComponent) if x.get_world()==self.world and x.get_asset()==self.ball.get_editor_property('BasketConfetti') and x.is_active()]
    def screenshot(self,name):
        path=OUT/(name+'.png')
        u.SystemLibrary.execute_console_command(self.world,'HighResShot 1280x720 filename="'+str(path).replace('\\','/')+'"',u.GameplayStatics.get_player_controller(self.world,0))
        self.frames.append({'file':str(path),'game_time':u.GameplayStatics.get_time_seconds(self.world)})

    def tick(self,dt):
        try:
            assert time.monotonic()-self.start<100,'Timed out in '+self.phase
            if self.phase=='loading':
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.check('solo_world',len(worlds)==1);self.world=worlds[0]
                self.hero=u.GameplayStatics.get_player_character(self.world,0)
                if not self.hero or u.GameplayStatics.get_time_seconds(self.world)<2:return
                pc=u.GameplayStatics.get_player_controller(self.world,0)
                if not pc or not pc.is_world_presentation_ready():return
                self.report['ready_game_time']=u.GameplayStatics.get_time_seconds(self.world)
                balls=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubBasketball)
                self.check('one_ball',len(balls)==1);self.ball=balls[0]
                self.check('effect_assigned',bool(self.ball.get_editor_property('BasketConfetti')))
                self.report['effect_after_begin_play']=self.ball.get_editor_property('BasketConfetti').get_path_name()
                self.center=u.MathLibrary.transform_location(self.ball.hoop.get_actor_transform(),self.ball.local_rim_center)
                self.up=self.ball.hoop.get_actor_up_vector()
                self.hero.set_actor_location(self.ball.get_actor_location()+u.Vector(-160,0,10),False,True)
                self.hero.set_actor_rotation(u.Rotator(),True)
                self.stage('pickup');return
            elapsed=u.GameplayStatics.get_time_seconds(self.world)-self.at
            if self.phase=='pickup' and elapsed>.3:
                self.report['pickup_diagnostics']={'hero':xyz(self.hero.get_actor_location()),'ball':xyz(self.ball.get_actor_location()),'class':self.hero.get_class().get_path_name(),'reach':self.ball.pickup_reach,'holder':str(self.ball.get_editor_property('Holder')),'move_ignored':u.GameplayStatics.get_player_controller(self.world,0).is_move_input_ignored()}
                self.hero.set_handle_input_held(True);self.hero.set_handle_input_held(False)
                self.check('picked_up',self.ball.get_editor_property('Holder')==self.hero)
                self.hero.set_actor_location(u.Vector(self.center.x-1000,self.center.y,-8),False,True)
                self.stage('aim')
            elif self.phase=='aim':
                cam=u.GameplayStatics.get_player_camera_manager(self.world,0)
                desired=u.MathLibrary.find_look_at_rotation(cam.get_camera_location(),self.center)
                rot=cam.get_camera_rotation();yaw=norm(desired.yaw-rot.yaw);pitch=norm(desired.pitch-rot.pitch)
                gain=min(.12,max(.01,dt*3))
                self.hero.apply_camera_orbit_input(u.Vector2D(yaw*gain/self.hero.camera_orbit_sensitivity,pitch*gain/self.hero.camera_orbit_sensitivity))
                if elapsed>2.5 and abs(yaw)<.08 and abs(pitch)<.08:
                    self.check('holding_never_scores',self.ball.get_editor_property('BasketsMade')==0 and not self.fx())
                    self.hero.set_primary_input_held(True);self.hero.set_primary_input_held(False)
                    self.check('primary_threw',not self.ball.get_editor_property('Holder'))
                    self.report['launch_velocity']=xyz(self.ball.body.get_physics_linear_velocity())
                    self.stage('flight')
            elif self.phase=='flight':
                if self.ball.get_editor_property('BasketsMade'):
                    self.check('clean_shot_scores_once',self.ball.get_editor_property('BasketsMade')==1)
                    effects=self.fx();self.check('one_live_confetti_system',len(effects)==1)
                    self.report['effect_location']=xyz(effects[0].get_world_location())
                    self.stage('celebration')
                elif elapsed>5:raise AssertionError('Actual primary shot did not score')
            elif self.phase=='celebration':
                if len(self.frames)<8 and elapsed>=.15+len(self.frames)*.34:self.screenshot('confetti_%02d'%len(self.frames))
                if elapsed>4.3:
                    self.check('no_duplicate_from_rebound',self.ball.get_editor_property('BasketsMade')==1)
                    self.check('effect_finishes_and_returns_to_pool',not self.fx())
                    self.place(self.center+u.Vector(0,350,260),u.Vector(0,0,-300));self.stage('miss')
            elif self.phase=='miss' and elapsed>1:
                self.check('outside_opening_does_not_score',self.ball.get_editor_property('BasketsMade')==1 and not self.fx())
                self.place(self.center-self.up*180,u.Vector(0,0,900));self.stage('upward')
            elif self.phase=='upward' and elapsed>.27:
                self.check('upward_pass_does_not_score',self.ball.get_editor_property('BasketsMade')==1 and not self.fx())
                self.place(self.center+u.Vector(0,350,200),u.Vector());self.stage('reset')
            elif self.phase=='reset' and elapsed>.15:
                self.place(self.center+self.up*220,u.Vector(0,0,-100));self.stage('second')
            elif self.phase=='second' and elapsed>1.1:
                self.check('next_basket_celebrates_again',self.ball.get_editor_property('BasketsMade')==2 and len(self.fx())==1)
                self.report['passed']=True;self.finish()
        except Exception:
            self.report['error']=traceback.format_exc();self.finish()

    def finish(self):
        u.unregister_slate_post_tick_callback(self.callback)
        self.report.update(phase=self.phase,wall_seconds=time.monotonic()-self.start,frames=self.frames)
        (OUT/'solo_report.json').write_text(json.dumps(self.report,indent=2))
        u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
        u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.throttle)
        print('BASKET_CONFETTI_SOLO_RESULT',json.dumps(self.report))

builtins.mc_basket_confetti_probe=Probe()
