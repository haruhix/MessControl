"""Short owned solo check: RMB aim press/release and reduced physical rebound."""
import unreal as u
import builtins,json,time,traceback
from pathlib import Path
OUT=Path(u.Paths.project_dir())/'Saved/HubBasketball_20261021/AimAndBounce'

class Check:
    def __init__(self):
        assert '-MCHubBasketballOwned' in u.SystemLibrary.get_command_line()
        assert not u.EditorLevelLibrary.get_pie_worlds(False)
        self.play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
        values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False}
        self.old=json.loads(u.ToolsetLibrary.get_object_properties(self.play,list(values)))
        assert u.ToolsetLibrary.set_object_properties(self.play,json.dumps(values))
        self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
        self.started=time.monotonic();self.phase='loading';self.report={'passed':False,'checks':{},'scope':'One solo PIE. Existing brace-input callbacks exercise RMB press/release; real Chaos drop. No multiplayer or physical input injection.'}
        self.callback=u.register_slate_post_tick_callback(self.tick)
        self.request=u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StartPIE',json.dumps({'options':{'bSimulate':False,'playMode':'PlayMode_InEditorFloating','warmupSeconds':0.}}))
    def now(self):return u.GameplayStatics.get_time_seconds(self.world)
    def stage(self,name):self.phase=name;self.at=self.now()
    def require(self,name,ok):
        self.report['checks'][name]=bool(ok);assert ok,name
    def key_e(self):self.hero.set_handle_input_held(True);self.hero.set_handle_input_held(False)
    def tick(self,dt):
        try:
            if time.monotonic()-self.started>100:raise RuntimeError('Check timeout: '+self.phase)
            if self.phase=='loading':
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                assert len(worlds)==1;self.world=worlds[0]
                self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                self.hero=u.GameplayStatics.get_player_character(self.world,0)
                if not self.hero or self.now()<3 or not self.pc.is_world_presentation_ready():return
                self.ball=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubBasketball)[0]
                self.ball.set_actor_location(u.Vector(1445,2505,-16),False,True)
                self.ball.body.set_physics_linear_velocity(u.Vector(),False)
                self.ball.body.set_physics_angular_velocity_in_radians(u.Vector(),False)
                self.hero.set_actor_location(u.Vector(1285,2505,0),False,True)
                self.hero.set_actor_rotation(u.Rotator(),True)
                self.stage('pickup');return
            elapsed=self.now()-self.at
            if self.phase=='pickup' and elapsed>.3:
                self.key_e();self.require('pickup',self.ball.holder==self.hero)
                widgets=[w for w in u.ObjectIterator(u.MCHubWidget.static_class()) if w.get_owning_player()==self.pc and w.is_in_viewport()]
                self.require('one_hub_hud',len(widgets)==1)
                self.reticle=next(t for t in u.ObjectIterator(u.TextBlock.static_class())
                    if t.get_name()=='HubBasketballReticle' and t.get_outer().get_outer()==widgets[0])
                self.stage('idle')
            elif self.phase=='idle' and elapsed>.3:
                self.require('aim_off_by_default',not self.ball.is_aiming())
                self.require('reticle_hidden_by_default',self.reticle.get_visibility()==u.SlateVisibility.COLLAPSED)
                self.hero.set_brace_input_held(True);self.stage('aim')
            elif self.phase=='aim' and elapsed>.15:
                self.require('right_click_aims',self.ball.is_aiming())
                self.require('aim_keeps_ball_held',self.ball.holder==self.hero)
                self.require('reticle_visible_on_right_click',self.reticle.get_visibility()!=u.SlateVisibility.COLLAPSED)
                self.hero.set_brace_input_held(False);self.stage('release')
            elif self.phase=='release' and elapsed>.15:
                self.require('right_click_release_clears_aim',not self.ball.is_aiming())
                self.require('reticle_hidden_on_release',self.reticle.get_visibility()==u.SlateVisibility.COLLAPSED)
                self.hero.set_brace_input_held(True);self.stage('drop')
            elif self.phase=='drop' and elapsed>.15:
                self.require('can_aim_again',self.ball.is_aiming())
                self.key_e();self.hero.set_brace_input_held(False);self.stage('after_drop')
            elif self.phase=='after_drop' and elapsed>.15:
                self.require('drop_clears_aim',not self.ball.holder and not self.ball.is_aiming())
                self.require('drop_hides_reticle',self.reticle.get_visibility()==u.SlateVisibility.COLLAPSED)
                self.ball.set_actor_location(u.Vector(1350,2505,300),False,True)
                self.ball.body.set_physics_linear_velocity(u.Vector(),False)
                self.ball.body.set_physics_angular_velocity_in_radians(u.Vector(),False)
                self.previous=0.;self.bounces=[];self.stage('bounce')
            elif self.phase=='bounce':
                velocity=self.ball.body.get_physics_linear_velocity().z
                if self.previous<-150 and velocity>100:self.bounces.append({'incoming':self.previous,'outgoing':velocity,'ratio':velocity/-self.previous})
                self.previous=velocity
                if elapsed>2.5:
                    self.report['bounces']=self.bounces
                    self.require('reduced_rebound',bool(self.bounces) and .38<self.bounces[0]['ratio']<.62)
                    self.require('rebound_loses_energy',len(self.bounces)>=2 and self.bounces[1]['outgoing']<self.bounces[0]['outgoing'])
                    self.report['passed']=True;self.finish()
        except Exception:
            self.report['error']=traceback.format_exc();self.finish()
    def finish(self):
        u.unregister_slate_post_tick_callback(self.callback)
        self.report['phase']=self.phase;self.report['wall_seconds']=time.monotonic()-self.started
        (OUT/'solo_report.json').write_text(json.dumps(self.report,indent=2),encoding='utf-8')
        u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
        u.ToolsetLibrary.set_object_properties(self.play,json.dumps(self.old))
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old_throttle)
        print('BASKETBALL_AIM_BOUNCE_CHECK',json.dumps(self.report))

builtins.mc_ball_aim_bounce=Check()
