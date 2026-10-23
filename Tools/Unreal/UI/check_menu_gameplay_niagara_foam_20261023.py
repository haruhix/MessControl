"""Check dense brush foam in the actual animated menu."""
import unreal as u,json,builtins,time,traceback,math,re
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuNiagaraBubbles20261023';r=json.loads((OUT/'Integration.json').read_text());ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem);assert not ls.is_in_play_in_editor()
aa=list(es.get_all_level_actors());by={a.get_path_name():a for a in aa};snapshot=r.get('sync_fix',r.get('rotation_fix',r))
for path,t in snapshot['actors_before'].items():assert re.sub('0x[0-9A-Fa-f]+','PTR',str(by[path].get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',t),path
for path,p in snapshot['lighting'].items():assert u.ToolsetLibrary.get_object_properties(by[path].light_component,['Intensity','LightColor','Mobility'])==p
for path,p in snapshot['postprocess'].items():assert str(by[path].settings)==p
assert by[snapshot['camera']].camera_component.field_of_view==snapshot['fov'];u.LevelSequenceEditorBlueprintLibrary.close_level_sequence();u.MCGameplayUIEditorLibrary.close_main_menu_preview()
settings=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings');perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));values={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False}
(OUT/'PlaySettingsBefore.json').write_text(json.dumps({'play':json.loads(u.ToolsetLibrary.get_object_properties(settings,list(values))),'throttle':perf.get_editor_property('bThrottleCPUWhenNotForeground')},indent=2));assert u.ToolsetLibrary.set_object_properties(settings,json.dumps(values));perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
class Check:
    def __init__(self):self.start=time.monotonic();self.ready=None;self.samples=[];self.last=-1.;self.prev=None;self.wraps=0;self.shot=False;self.handle=u.register_slate_post_tick_callback(self.tick)
    def finish(self,error=None):
        u.unregister_slate_post_tick_callback(self.handle);d={'status':'failed' if error else 'passed','error':error,'elapsed':time.monotonic()-self.start,'samples':self.samples,'natural_cleaner_loop_wraps':self.wraps,'lighting_and_postprocess_preserved':True,'network_test':False}
        if not error:d.update(brush_motion_cm=self.motion,max_attachment_error_cm=self.drift,max_effect_rotation_change_deg=self.rotation_drift,max_brush_rotation_change_deg=self.brush_rotation,menu_visible=True,preload_ready=True,system_active=True,bubble_spawn_rate_per_second=r['bubble_rate'],foam_spawn_rate_per_second=r['foam_rate'],same_frame_bristle_contact_verified='sync_fix' in r)
        (OUT/'Validation.json').write_text(json.dumps(d,indent=2));u.log('MENU_NIAGARA_BUBBLES_CHECK '+d['status'])
    def tick(self,dt):
        try:
            if time.monotonic()-self.start>90:raise RuntimeError('Foam menu validation timeout')
            if self.ready is None:
                worlds=u.EditorLevelLibrary.get_pie_worlds(False)
                if not worlds:return
                self.world=worlds[0];pre=next(s for s in u.ObjectIterator(u.MCWorldPreloadSubsystem) if s.get_world()==self.world)
                assert not pre.has_failed()
                if not pre.is_ready():return
                aa=u.GameplayStatics.get_all_actors_of_class(self.world,u.Actor);self.hero=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);self.foam=next(a for a in aa if 'MC_MenuNiagaraBrushFoam' in [str(t) for t in a.tags]);assert self.foam.get_attach_parent_actor()==self.hero
                c=self.foam.get_component_by_class(u.NiagaraComponent);assert c.is_visible() and c.is_active();assert c.get_asset().get_path_name().split('.')[0]==r['target'];assert not any('MC_MenuDenseBrushFoam' in [str(t) for t in a.tags] for a in aa)
                assert c.get_editor_property('absolute_rotation') and not c.get_editor_property('absolute_location') and not c.get_editor_property('absolute_scale')
                if 'sync_fix' in r:
                    assert c.get_tick_behavior()==u.NiagaraTickBehavior.FORCE_TICK_LAST
                    assert self.hero.skeletal_mesh_component.visibility_based_anim_tick_option==u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES
                    fit=json.loads((ROOT/'ArtSource/UI/MenuDenseBrushFoam20261023/ExportReport.json').read_text());self.fitted_contact=u.Vector(*fit['center_in_bone_cm'])*.01+u.Vector(*fit['normal_in_bone'])*.05
                    assert (c.get_editor_property('relative_location')-self.fitted_contact).length()<.000001
                self.player=next(a.sequence_player for a in aa if isinstance(a,u.LevelSequenceActor));self.ready=u.GameplayStatics.get_time_seconds(self.world);return
            now=u.GameplayStatics.get_time_seconds(self.world)-self.ready;qt=self.player.get_current_time().time;frame=qt.frame_number.value+qt.sub_frame
            if self.prev is not None and frame+3<self.prev:self.wraps+=1
            self.prev=frame
            if now-self.last>(0. if 'sync_fix' in r else .08):
                a=self.foam.get_actor_location();bone=self.hero.skeletal_mesh_component.get_socket_transform(r['bone'],u.RelativeTransformSpace.RTS_WORLD);b=bone.transform_location(u.Vector(*r.get('relative_offset',[0,0,0])));self.samples.append({'t':now,'frame':frame,'foam':list(a.to_tuple()),'bone':list(b.to_tuple()),'scale':list(self.foam.get_actor_scale3d().to_tuple()),'rotation':list(self.foam.get_actor_transform().rotation.to_tuple()),'brush_rotation':list(bone.rotation.to_tuple())});self.last=now
                if 'sync_fix' in r:
                    fitted=bone.transform_location(self.fitted_contact);assert (a-fitted).length()<.02
                    self.samples[-1]['fitted_contact']=list(fitted.to_tuple())
            if now>1.5 and not self.shot:u.AutomationLibrary.take_high_res_screenshot(1600,900,str(OUT/'Preview.png'));self.shot=True
            if now>9.8:
                self.motion=max(math.dist(self.samples[0]['foam'],s['foam']) for s in self.samples);self.drift=max(math.dist(s['foam'],s['bone']) for s in self.samples)
                assert self.motion>10,self.motion
                assert self.drift<.02,self.drift
                expected_scale=r.get('world_scale',[1.,1.,1.]);expected_scale=expected_scale if isinstance(expected_scale,list) else [expected_scale]*3
                assert all(max(abs(v-e) for v,e in zip(s['scale'],expected_scale))<.01 for s in self.samples)
                def angle(a,b):return math.degrees(2*math.acos(min(1.,abs(sum(x*y for x,y in zip(a,b)))/math.sqrt(sum(x*x for x in a)*sum(y*y for y in b)))))
                self.rotation_drift=max(angle(r.get('fixed_world_rotation_quat',self.samples[0]['rotation']),s['rotation']) for s in self.samples)
                self.brush_rotation=max(angle(self.samples[0]['brush_rotation'],s['brush_rotation']) for s in self.samples)
                assert self.rotation_drift<.001,self.rotation_drift
                assert self.brush_rotation>1.,self.brush_rotation
                assert self.wraps>=1
                assert any(w.is_in_viewport() for w in u.ObjectIterator(u.MCMainMenuWidget))
                self.finish()
        except Exception:self.finish(traceback.format_exc())
builtins.mc_menu_niagara_bubbles_check=Check();ls.editor_request_begin_play();print('MENU_NIAGARA_BUBBLES_SOLO_STARTED')

