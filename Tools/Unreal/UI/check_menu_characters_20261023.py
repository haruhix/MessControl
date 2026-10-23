"""Short real menu check: three passive actors, independent repeats and facial-only love."""
import unreal as u,json,builtins,time,traceback,math
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuAdditionalCharacters20261023'
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);assert not ls.is_in_play_in_editor()
u.LevelSequenceEditorBlueprintLibrary.close_level_sequence();u.MCGameplayUIEditorLibrary.close_main_menu_preview()
settings=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False,'newWindowWidth':1600,'newWindowHeight':900}
perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
before={'play':json.loads(u.ToolsetLibrary.get_object_properties(settings,list(values))),'throttle':perf.get_editor_property('bThrottleCPUWhenNotForeground')}
(OUT/'PlaySettingsBefore.json').write_text(json.dumps(before,indent=2))
assert u.ToolsetLibrary.set_object_properties(settings,json.dumps(values));perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
class Check:
    def __init__(self):
        self.start=time.monotonic();self.ready=None;self.samples={};self.report={'status':'running'};self.last=-1.;self.shot=False;self.handle=u.register_slate_post_tick_callback(self.tick)
    def finish(self,error=None):
        u.unregister_slate_post_tick_callback(self.handle);self.report.update(status='failed' if error else 'passed',error=error,samples=self.samples,elapsed=time.monotonic()-self.start)
        (OUT/'Validation.json').write_text(json.dumps(self.report,indent=2));u.log('MENU_CAST_CHECK '+self.report['status'])
    def tick(self,dt):
        try:
            if time.monotonic()-self.start>90:raise RuntimeError('Menu readiness timeout')
            if self.ready is None:
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.world=worlds[0];preload=next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem) if s.get_world()==self.world)
                assert not preload.has_failed(),str(preload.get_detail_text())
                if not preload.is_ready():return
                aa=u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor)
                self.cast={next(str(t) for t in a.tags if str(t) in ['MC_MenuDance1','MC_MenuDance2','MC_MenuLove']):a for a in aa if 'MC_MenuAdditionalCharacter' in [str(t) for t in a.tags]}
                assert len(self.cast)==3
                assert len([a for a in aa if isinstance(a,u.SkeletalMeshActor)])==5
                assert u.GameplayStatics.get_player_pawn(self.world,0) is None
                assert u.GameplayStatics.get_game_mode(self.world).get_class().get_name()=='MCMainMenuGameMode'
                for tag,a in self.cast.items():
                    c=a.skeletal_mesh_component;assert c.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
                    data=c.get_editor_property('animation_data');assert data.saved_looping and data.saved_playing
                    self.samples[tag]=[]
                pc=u.GameplayStatics.get_player_controller(self.world,0);assert isinstance(pc.get_view_target(),u.CameraActor)
                self.report.update(preload_ready=True,menu_game_mode=True,passive_actor_count=3,total_skeletal_actors=5,camera=pc.get_view_target().get_name(),saved_looping=True)
                self.ready=u.GameplayStatics.get_time_seconds(self.world);return
            now=u.GameplayStatics.get_time_seconds(self.world)-self.ready
            if now-self.last>=.12:
                for tag,a in self.cast.items():
                    c=a.skeletal_mesh_component
                    self.samples[tag].append({'t':now,'position':c.get_position(),'hand':list(c.get_socket_location('hand_l').to_tuple()),'head':list(c.get_socket_location('head_x').to_tuple()),'delight':c.get_anim_instance().get_curve_value('Eyes_delight'),'blink':c.get_anim_instance().get_curve_value('Eyes_Blink')})
                self.last=now
            if now>2.0 and not self.shot:
                u.AutomationLibrary.take_high_res_screenshot(1600,900,str(OUT/'Preview.png'),force_game_view=True);self.shot=True
            if now>max(7.2,max(a.skeletal_mesh_component.get_editor_property('animation_data').anim_to_play.get_play_length() for a in self.cast.values())+.4):
                metrics={}
                for tag,samples in self.samples.items():
                    wraps=sum(b['position']+.05<a['position'] for a,b in zip(samples,samples[1:]))
                    motion=max(math.dist(samples[0]['hand'],s['hand']) for s in samples)
                    metrics[tag]={'wraps':wraps,'hand_motion_cm':motion,'delight_range':[min(s['delight'] for s in samples),max(s['delight'] for s in samples)],'blink_max':max(s['blink'] for s in samples)}
                    assert wraps>=1,(tag,metrics[tag])
                    if tag!='MC_MenuLove':assert motion>5,(tag,motion)
                    else:assert motion<.001 and metrics[tag]['delight_range'][1]>.9 and metrics[tag]['blink_max']>.5,metrics[tag]
                assert any(w.is_in_viewport() for w in u.ObjectIterator(u.MCMainMenuWidget))
                self.report.update(metrics=metrics,menu_visible=True);self.finish()
        except Exception:self.finish(traceback.format_exc())
builtins.mc_menu_cast_check=Check();ls.editor_request_begin_play();print('MENU_CAST_SOLO_STARTED')




