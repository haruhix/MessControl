"""Check dense brush foam in the actual animated menu."""
import unreal as u,json,builtins,time,traceback,math,re
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuDenseBrushFoam20261023';r=json.loads((OUT/'Integration.json').read_text());ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem);assert not ls.is_in_play_in_editor()
aa=list(es.get_all_level_actors());by={a.get_path_name():a for a in aa}
for path,t in r['actors_before'].items():assert re.sub('0x[0-9A-Fa-f]+','PTR',str(by[path].get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',t),path
for path,p in r['lighting'].items():assert u.ToolsetLibrary.get_object_properties(by[path].light_component,['Intensity','LightColor','Mobility'])==p
for path,p in r['postprocess'].items():assert str(by[path].settings)==p
assert by[r['camera']].camera_component.field_of_view==r['fov'];u.LevelSequenceEditorBlueprintLibrary.close_level_sequence();u.MCGameplayUIEditorLibrary.close_main_menu_preview()
settings=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings');perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False}
(OUT/'PlaySettingsBefore.json').write_text(json.dumps({'play':json.loads(u.ToolsetLibrary.get_object_properties(settings,list(values))),'throttle':perf.get_editor_property('bThrottleCPUWhenNotForeground')},indent=2));assert u.ToolsetLibrary.set_object_properties(settings,json.dumps(values));perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
class Check:
    def __init__(self):self.start=time.monotonic();self.ready=None;self.samples=[];self.last=-1.;self.prev=None;self.wraps=0;self.shot=False;self.handle=u.register_slate_post_tick_callback(self.tick)
    def finish(self,error=None):
        u.unregister_slate_post_tick_callback(self.handle);d={'status':'failed' if error else 'passed','error':error,'elapsed':time.monotonic()-self.start,'samples':self.samples,'natural_cleaner_loop_wraps':self.wraps,'lighting_and_postprocess_preserved':True,'network_test':False}
        if not error:d.update(brush_motion_cm=self.motion,max_attachment_error_cm=self.drift,menu_visible=True,preload_ready=True,collision_disabled=True)
        (OUT/'Validation.json').write_text(json.dumps(d,indent=2));u.log('MENU_DENSE_FOAM_CHECK '+d['status'])
    def tick(self,dt):
        try:
            if time.monotonic()-self.start>90:raise RuntimeError('Foam menu validation timeout')
            if self.ready is None:
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.world=worlds[0];pre=next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem) if s.get_world()==self.world)
                assert not pre.has_failed()
                if not pre.is_ready():return
                aa=u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor);self.hero=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);self.foam=next(a for a in aa if 'MC_MenuDenseBrushFoam' in [str(t) for t in a.tags]);assert self.foam.get_attach_parent_actor()==self.hero
                c=self.foam.static_mesh_component;assert c.is_visible() and c.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION;assert c.static_mesh.get_path_name()==r['mesh'];assert c.get_material(0).get_path_name()==r['material']
                self.player=next(a.sequence_player for a in aa if isinstance(a,u.LevelSequenceActor));self.ready=u.GameplayStatics.get_time_seconds(self.world);return
            now=u.GameplayStatics.get_time_seconds(self.world)-self.ready;qt=self.player.get_current_time().time;frame=qt.frame_number.value+qt.sub_frame
            if self.prev is not None and frame+3<self.prev:self.wraps+=1
            self.prev=frame
            if now-self.last>.08:
                a=self.foam.get_actor_location();b=self.hero.skeletal_mesh_component.get_socket_transform(r['bone'],u.RelativeTransformSpace.RTS_WORLD).transform_location(u.Vector(*r.get('relative_offset',[0,0,0])));self.samples.append({'t':now,'frame':frame,'foam':list(a.to_tuple()),'bone':list(b.to_tuple()),'scale':list(self.foam.get_actor_scale3d().to_tuple())});self.last=now
            if now>1.5 and not self.shot:u.AutomationLibrary.take_high_res_screenshot(1600,900,str(OUT/'Preview.png'));self.shot=True
            if now>9.8:
                self.motion=max(math.dist(self.samples[0]['foam'],s['foam']) for s in self.samples);self.drift=max(math.dist(s['foam'],s['bone']) for s in self.samples)
                assert self.motion>10,self.motion
                assert self.drift<.02,self.drift
                assert all(max(abs(v-r.get('world_scale',1)) for v in s['scale'])<.01 for s in self.samples)
                assert self.wraps>=1
                assert any(w.is_in_viewport() for w in u.ObjectIterator(u.MCMainMenuWidget))
                self.finish()
        except Exception:self.finish(traceback.format_exc())
builtins.mc_menu_dense_foam_check=Check();ls.editor_request_begin_play();print('MENU_DENSE_FOAM_SOLO_STARTED')

