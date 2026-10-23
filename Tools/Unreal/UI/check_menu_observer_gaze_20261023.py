"""Verify the observer's natural menu cycle and preserved scene authoring."""
import unreal as u,json,builtins,time,traceback,math,re
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuObserverGaze20261023';ls=u.get_editor_subsystem(u.LevelEditorSubsystem);assert not ls.is_in_play_in_editor()
r=json.loads((OUT/'Integration.json').read_text());aa=list(u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors());by={a.get_path_name():a for a in aa}
for path,t in r['actors_before'].items():assert re.sub('0x[0-9A-Fa-f]+','PTR',str(by[path].get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',t),path
for path,s in r['lighting'].items():assert u.ToolsetLibrary.get_object_properties(by[path].light_component,['Intensity','LightColor','Mobility'])==s
for path,s in r['postprocess'].items():assert str(by[path].settings)==s
assert by[r['camera']].camera_component.field_of_view==r['fov']
u.LevelSequenceEditorBlueprintLibrary.close_level_sequence();u.MCGameplayUIEditorLibrary.close_main_menu_preview()
settings=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings');perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False}
(OUT/'PlaySettingsBefore.json').write_text(json.dumps({'play':json.loads(u.ToolsetLibrary.get_object_properties(settings,list(values))),'throttle':perf.get_editor_property('bThrottleCPUWhenNotForeground')},indent=2));assert u.ToolsetLibrary.set_object_properties(settings,json.dumps(values));perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
def qm(a,b):
    x,y,z,w=a;X,Y,Z,W=b;return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)
def qi(q):return (-q[0],-q[1],-q[2],q[3])
def qv(q,v):return qm(qm(q,(*v,0.)),qi(q))[:3]
def angle(a,b):
    d=sum(x*y for x,y in zip(a,b))/math.sqrt(sum(x*x for x in a)*sum(x*x for x in b));return math.degrees(math.acos(max(-1,min(1,d))))
class Check:
    def __init__(self):self.started=time.monotonic();self.ready=None;self.samples=[];self.last=-1.;self.wraps=0;self.previous=None;self.workshot=False;self.camshot=False;self.handle=u.register_slate_post_tick_callback(self.tick)
    def finish(self,error=None):
        u.unregister_slate_post_tick_callback(self.handle);report={'status':'failed' if error else 'passed','error':error,'elapsed':time.monotonic()-self.started,'samples':self.samples,'natural_loop_wraps':self.wraps,'scene_transforms_preserved':True,'lighting_and_postprocess_preserved':True,'network_test':False}
        if not error:
            report.update(menu_visible=True,preload_ready=True,body_max_movement_cm=self.motion,camera_gaze_error_degrees=self.camera_error,head_rotation_range_degrees=self.head_range,eye_rotation_range_degrees=self.eye_range,blink_peak=max(s['blink'] for s in self.samples))
        (OUT/'Validation.json').write_text(json.dumps(report,indent=2));u.log('MENU_OBSERVER_CHECK '+report['status'])
    def tick(self,dt):
        try:
            if time.monotonic()-self.started>90:raise RuntimeError('Menu observer timeout')
            if self.ready is None:
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.world=worlds[0];preload=next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem) if s.get_world()==self.world)
                assert not preload.has_failed()
                if not preload.is_ready():return
                aa=u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor);self.love=next(a for a in aa if 'MC_MenuLove' in [str(t) for t in a.tags]);self.clean=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);self.cam=next(a for a in aa if 'MC_MenuV2_Camera' in [str(t) for t in a.tags]);self.comp=self.love.skeletal_mesh_component
                ref=self.comp.skeletal_mesh_asset.skeleton.get_reference_pose();self.eye_ref=tuple(ref.get_bone_pose('c_eye_l',u.AnimPoseSpaces.WORLD).rotation.to_tuple());self.ready=u.GameplayStatics.get_time_seconds(self.world)
                self.initial={n:list(self.comp.get_socket_location(n).to_tuple()) for n in ['root_x','foot_l','foot_r','hand_l','hand_r']};self.head0=None;return
            now=u.GameplayStatics.get_time_seconds(self.world)-self.ready;p=self.comp.get_position()
            if self.previous is not None and p+.1<self.previous:self.wraps+=1
            self.previous=p
            if now-self.last>.06:
                comp=self.comp;eye=comp.get_socket_transform('c_eye_l',u.RelativeTransformSpace.RTS_COMPONENT);edelta=qm(tuple(eye.rotation.to_tuple()),qi(self.eye_ref));direction=qv(tuple(comp.get_world_transform().rotation.to_tuple()),qv(edelta,(0.,1.,0.)))
                loc=comp.get_socket_location('c_eye_l');target=self.cam.get_actor_location()-loc
                body={n:list(comp.get_socket_location(n).to_tuple()) for n in self.initial};head=tuple(comp.get_socket_transform('head_x',u.RelativeTransformSpace.RTS_COMPONENT).rotation.to_tuple())
                row={'t':now,'position':p,'body':body,'head':list(head),'eye':list(eye.rotation.to_tuple()),'blink':comp.get_anim_instance().get_curve_value('Eyes_Blink'),'delight':comp.get_anim_instance().get_curve_value('Eyes_delight'),'camera_error':angle(direction,target.to_tuple())};self.samples.append(row);self.last=now
            if 1.<p<2. and not self.workshot:u.AutomationLibrary.take_high_res_screenshot(1600,900,str(OUT/'WatchingWork.png'));self.workshot=True
            if 5.9<p<6.8 and not self.camshot:u.AutomationLibrary.take_high_res_screenshot(1600,900,str(OUT/'LookingAtCamera.png'));self.camshot=True
            if now>19.2:
                self.motion=max(math.dist(self.initial[n],s['body'][n]) for s in self.samples for n in self.initial)
                self.camera_error=max(s['camera_error'] for s in self.samples if 5.8<s['position']<7. or 13.7<s['position']<14.5)
                self.head_range=max(math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(self.samples[0]['head'],s['head'])))))) for s in self.samples)
                assert self.motion<.01,self.motion
                assert self.camera_error<2.,self.camera_error
                assert self.head_range<.1,self.head_range
                self.eye_range=max(math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(self.samples[0]['eye'],s['eye'])))))) for s in self.samples)
                assert self.eye_range>10.,self.eye_range
                assert max(s['blink'] for s in self.samples)>.8
                assert self.wraps>=1
                assert any(w.is_in_viewport() for w in u.ObjectIterator(u.MCMainMenuWidget))
                self.finish()
        except Exception:self.finish(traceback.format_exc())
builtins.mc_menu_observer_check=Check();ls.editor_request_begin_play();print('MENU_OBSERVER_SOLO_STARTED')

