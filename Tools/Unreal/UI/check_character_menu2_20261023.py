"""Short solo validation of the authored CHARACTER_MENU2 replacement."""
import unreal as u,json,builtins,time,traceback,math,re,hashlib
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/CharacterMenu2_20261023';ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem);assert not ls.is_in_play_in_editor()
r=json.loads((OUT/'Integration.json').read_text());aa=es.get_all_level_actors();by={a.get_path_name():a for a in aa}
for path,t in r['actors_before'].items():assert re.sub('0x[0-9A-Fa-f]+','PTR',str(by[path].get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',t)
for path,p in r['lighting'].items():assert u.ToolsetLibrary.get_object_properties(by[path].light_component,['Intensity','LightColor','Mobility'])==p
for path,p in r['postprocess'].items():assert str(by[path].settings)==p
assert hashlib.sha256(Path('D:/PROJECT/GAME/TEETH CONTROL/teeth/MENU/CHARACTER_MENU2.blend').read_bytes()).hexdigest()==r['source_sha256']
u.LevelSequenceEditorBlueprintLibrary.close_level_sequence();u.MCGameplayUIEditorLibrary.close_main_menu_preview()
settings=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings');perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False}
(OUT/'PlaySettingsBefore.json').write_text(json.dumps({'play':json.loads(u.ToolsetLibrary.get_object_properties(settings,list(values))),'throttle':perf.get_editor_property('bThrottleCPUWhenNotForeground')},indent=2));assert u.ToolsetLibrary.set_object_properties(settings,json.dumps(values));perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
class Check:
    def __init__(self):self.started=time.monotonic();self.ready=None;self.samples=[];self.wraps=0;self.previous=None;self.last=-1.;self.shot=False;self.handle=u.register_slate_post_tick_callback(self.tick)
    def finish(self,error=None):
        u.unregister_slate_post_tick_callback(self.handle);report={'status':'failed' if error else 'passed','error':error,'elapsed':time.monotonic()-self.started,'samples':self.samples,'natural_loop_wraps':self.wraps,'lighting_and_postprocess_preserved':True,'source_unchanged':True,'network_test':False}
        if not error:report.update(menu_visible=True,preload_ready=True,head_motion_cm=self.motion,head_rotation_degrees=self.rotation,blink_peak=max(s['blink'] for s in self.samples))
        (OUT/'Validation.json').write_text(json.dumps(report,indent=2));u.log('CHARACTER_MENU2_CHECK '+report['status'])
    def tick(self,dt):
        try:
            if time.monotonic()-self.started>90:raise RuntimeError('Menu replacement timeout')
            if self.ready is None:
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.world=worlds[0];pre=next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem) if s.get_world()==self.world)
                assert not pre.has_failed()
                if not pre.is_ready():return
                aa=u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor);a=next(a for a in aa if 'MC_MenuCharacter2' in [str(t) for t in a.tags]);assert not any('MC_MenuLove' in [str(t) for t in a.tags] for a in aa)
                self.comp=a.skeletal_mesh_component;assert self.comp.skeletal_mesh_asset.get_path_name()==r['mesh'];data=self.comp.get_editor_property('animation_data');assert data.anim_to_play.get_path_name()==r['clip'] and data.saved_looping and data.saved_playing
                assert self.comp.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
                assert u.GameplayStatics.get_player_pawn(self.world,0) is None
                assert isinstance(u.GameplayStatics.get_player_controller(self.world,0).get_view_target(),u.CameraActor)
                self.ready=u.GameplayStatics.get_time_seconds(self.world);return
            now=u.GameplayStatics.get_time_seconds(self.world)-self.ready;p=self.comp.get_position()
            if self.previous is not None and p+.1<self.previous:self.wraps+=1
            self.previous=p
            if now-self.last>.06:
                t=self.comp.get_socket_transform('head_x',u.RelativeTransformSpace.RTS_COMPONENT)
                self.samples.append({'t':now,'position':p,'head':list(t.translation.to_tuple()),'rotation':list(t.rotation.to_tuple()),'blink':self.comp.get_anim_instance().get_curve_value('Eyes_Blink')});self.last=now
            if now>1.2 and not self.shot:u.AutomationLibrary.take_high_res_screenshot(1600,900,str(OUT/'Preview.png'));self.shot=True
            if now>9.8:
                self.motion=max(math.dist(self.samples[0]['head'],s['head']) for s in self.samples)
                def qangle(a,b):
                    dot=abs(sum(x*y for x,y in zip(a,b)))/math.sqrt(sum(x*x for x in a)*sum(x*x for x in b));return math.degrees(2*math.acos(min(1,dot)))
                self.rotation=max(qangle(self.samples[0]['rotation'],s['rotation']) for s in self.samples)
                assert self.motion>.1 or self.rotation>1.,(self.motion,self.rotation)
                assert self.wraps>=1
                assert max(s['blink'] for s in self.samples)>.5
                assert any(w.is_in_viewport() for w in u.ObjectIterator(u.MCMainMenuWidget))
                self.finish()
        except Exception:self.finish(traceback.format_exc())
builtins.mc_menu2_check=Check();ls.editor_request_begin_play();print('CHARACTER_MENU2_SOLO_STARTED')
