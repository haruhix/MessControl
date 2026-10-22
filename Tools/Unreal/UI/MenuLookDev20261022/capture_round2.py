import unreal as u,builtins,runpy,json,traceback,time
from pathlib import Path
OUT=Path('E:/DEVGAME/MessControl/Artifacts/MenuLookDev_20261022/Round2');OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'A_ControlledRim.png').exists()
definitions=runpy.run_path(str(Path(__file__).with_name('capture_round1.py')),init_globals={'MC_LOOK_OUTPUT':str(OUT),'MC_LOOK_START_CAPTURE':False})
s=definitions['LookSession'](capture=False);builtins.mc_look_session_round2=s
s.warm();s.rig({'MENU V2 | Directed cream key':{'intensity':8.5,'color':(1.,.96,.90)},'MENU V2 | Throat depth':{'intensity':3.3,'color':(.90,.86,.92)}})
s.lights['MENU V2 | Mint shoulder rim'].set_lighting_channels(False,True,False)
s.lights['MENU V2 | Directed cream key'].set_lighting_channels(True,True,False)
s.mid_hero.set_scalar_parameter_value('Roughness Strength',.45)
working=next(a.get_component_by_class(u.StaticMeshComponent) for a in s.actors if '| ARENA | Tooth 04 |' in a.get_actor_label())
workmid=working.get_material(0);assert isinstance(workmid,u.MaterialInstanceDynamic)
workmid.set_vector_parameter_value('CleanEnamelTint',u.LinearColor(1.3,1.4,1.68,1.));workmid.set_scalar_parameter_value('CleanEnamelRoughness',.29)
class Capture:
 def __init__(self):
  self.names=['A_ControlledRim','B_NarrowReflection','C_SoftContact'];self.i=0;self.n=0
  self.report={'status':'capturing','pose':217,'profiles':[],'working_material':workmid.get_path_name(),'working_enamel_tint':str(workmid.get_vector_parameter_value('CleanEnamelTint')),'working_enamel_roughness':workmid.get_scalar_parameter_value('CleanEnamelRoughness'),'geometry_camera_and_object_transforms_unchanged':True};self.cb=u.register_slate_post_tick_callback(self.tick)
 def write(self):(OUT/'Capture.json').write_text(json.dumps(self.report,indent=2),encoding='utf8')
 def tick(self,dt):
  try:
   self.n+=1
   if self.n<160:return
   name=self.names[self.i];assert u.MCGameplayUIEditorLibrary.capture_pie_game_window(s.w,str(OUT/(name+'.png')))
   self.report['profiles'].append({'name':name,'hero_cast_shadow':s.hero.get_editor_property('cast_shadow'),'transforms_unchanged':all(all(abs(x-y)<1e-4 for x,y in zip(s.transform(a),s.transforms[a.get_path_name()])) for a in s.actors if a.get_path_name() in s.transforms)});self.write();self.i+=1;self.n=0
   if self.i==1:s.rig({'MENU V2 | Enamel reflection card':{'source_width':60.,'source_height':220.,'intensity':100.,'specular_scale':.70}})
   elif self.i==2:
    s.hero.set_cast_shadow(True);s.rig({'MENU V2 | Directed cream key':{'source_radius':100.,'soft_source_radius':60.}})
    for n,v in {'r.Shadow.FilterMethod':1,'r.Shadow.MaxResolution':4096,'r.Shadow.PCSS.MaxSoftKernelSize':64}.items():u.SystemLibrary.execute_console_command(s.w,n+' '+str(v))
   else:u.unregister_slate_post_tick_callback(self.cb);self.report['status']='complete_pending_visual_review';self.write();u.log('LOOK_ROUND2_COMPLETE')
  except Exception:
   u.unregister_slate_post_tick_callback(self.cb);self.report['status']='failed';self.report['error']=traceback.format_exc();self.write();u.log_error(self.report['error'])
builtins.mc_look_round2_capture=Capture()
