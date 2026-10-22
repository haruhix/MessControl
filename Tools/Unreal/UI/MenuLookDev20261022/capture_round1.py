import unreal as u,builtins,json,traceback,time
from pathlib import Path
OUT=Path(globals().get('MC_LOOK_OUTPUT','E:/DEVGAME/MessControl/Artifacts/MenuLookDev_20261022/Round1'));OUT.mkdir(parents=True,exist_ok=True)
if globals().get('MC_LOOK_START_CAPTURE',True):assert not (OUT/'A_Baseline.png').exists()
PPFIELDS=['auto_exposure_bias','color_saturation','color_contrast','color_gain_shadows','color_gain_midtones','color_gain_highlights','color_saturation_shadows','ambient_occlusion_intensity','ambient_occlusion_radius','ambient_occlusion_power','bloom_intensity','bloom_threshold','vignette_intensity','film_slope','film_toe','film_shoulder','white_temp','white_tint','depth_of_field_focal_distance','depth_of_field_fstop','depth_of_field_sensor_width']
LIGHTFIELDS=['intensity','light_color','source_radius','soft_source_radius','specular_scale','diffuse_scale','source_width','source_height','cast_shadows']
NEUTRAL={
 'MENU V2 | soft ambient':{'intensity':.13,'color':(.78,.87,1.)},
 'MENU V2 | Cream key':{'intensity':.4,'color':(1.,.90,.86),'specular_scale':.06},
 'MENU V2 | Warm entrance':{'intensity':.85},
 'MENU V2 | Throat depth':{'intensity':2.1,'color':(.92,.72,.78)},
 'MENU V2 | Directed cream key':{'intensity':9.5,'color':(1.,.93,.85),'source_radius':60.,'soft_source_radius':45.,'specular_scale':.65},
 'MENU V2 | Mint edge':{'intensity':.2,'specular_scale':.08},
 'MENU V2 | Mint shoulder rim':{'intensity':5.5,'color':(.68,.86,1.),'source_radius':60.,'soft_source_radius':45.,'specular_scale':.70},
 'MENU V2 | Enamel reflection card':{'intensity':110.,'source_width':110.,'source_height':210.,'color':(1.,.96,.91),'specular_scale':.7,'diffuse_scale':0.}}
class LookSession:
 def __init__(self,capture=True):
  self.w=u.EditorLevelLibrary.get_pie_worlds(False)[0];self.actors=u.GameplayStatics.get_all_actors_of_class(self.w,u.Actor)
  self.p=next(a.sequence_player for a in self.actors if isinstance(a,u.LevelSequenceActor));self.p.pause();p=u.MovieSceneSequencePlaybackParams();p.position_type=u.MovieScenePositionType.FRAME;p.frame=u.FrameTime(u.FrameNumber(217));p.update_method=u.UpdatePositionMethod.JUMP;self.p.set_playback_position(p)
  self.camera=next(a.camera_component for a in self.actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
  self.hero=next(a.skeletal_mesh_component for a in self.actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
  self.tongue=next(a.get_component_by_class(u.StaticMeshComponent) for a in self.actors if 'Tongue | authored' in a.get_actor_label())
  self.lights={a.get_actor_label():a.get_component_by_class(u.LightComponentBase) for a in self.actors if a.get_component_by_class(u.LightComponentBase)}
  self.old_lights={}
  for name,l in self.lights.items():
   d={}
   for n in LIGHTFIELDS:
    try:d[n]=l.get_editor_property(n)
    except:pass
   self.old_lights[name]=d
  pp=self.camera.post_process_settings;self.old_pp={n:pp.get_editor_property(n) for f in PPFIELDS for n in ('override_'+f,f)}
  self.cvars={'r.AntiAliasingMethod':2,'r.ScreenPercentage':150,'r.SecondaryScreenPercentage.GameViewport':100,'r.DynamicRes.OperationMode':0,'r.DynamicRes.TestScreenPercentage':0,'r.TemporalAA.Quality':2}
  self.old_cvars={n:u.SystemLibrary.get_console_variable_float_value(n) for n in self.cvars}
  for n,v in self.cvars.items():u.SystemLibrary.execute_console_command(self.w,n+' '+str(v))
  for x in u.ObjectIterator(u.MCMainMenuWidget):
   if x.is_in_viewport():x.set_visibility(u.SlateVisibility.COLLAPSED)
  cls=u.load_class(None,'/Game/UI/MenuShowcase/AnatomyV2/UI/WBP_WashNSmashMenu_AdaptiveV2.WBP_WashNSmashMenu_AdaptiveV2_C');self.widget=u.WidgetLibrary.get_default_object().call_method('Create',(self.w,cls,u.GameplayStatics.get_player_controller(self.w,0)));self.widget.add_to_viewport(50)
  self.mid_hero=None;self.mid_tongue=None;self.original_materials={};self.mats_changed=False
  self.transforms={a.get_path_name():self.transform(a) for a in self.actors if any(str(t).startswith('MC_MenuV2') for t in a.tags)}
  self.report={'status':'capturing','pose':217,'render':'Native game window with UMG; requested1600x900, TAA150','geometry_camera_layout_animation_unchanged':True,'profiles':[],'initial_light_values':{k:{n:str(v) for n,v in d.items()} for k,d in self.old_lights.items()},'initial_pp':{n:str(v) for n,v in self.old_pp.items()}}
  self.names=['A_Baseline','B_NeutralLight','C_NeutralEnamel','D_WarmRim'];self.i=0;self.n=0;self.started=time.monotonic();self.cb=u.register_slate_post_tick_callback(self.tick) if capture else None
 def transform(self,a):
  l=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d();return [l.x,l.y,l.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]
 def pp(self,d):
  pp=self.camera.post_process_settings
  for n,v in d.items():pp.set_editor_property('override_'+n,True);pp.set_editor_property(n,v)
  self.camera.set_editor_property('post_process_settings',pp)
 def rig(self,table):
  for name,d in table.items():
   l=self.lights[name]
   for n,v in d.items():
    if n=='color':
     if isinstance(l,u.SkyLightComponent):l.set_light_color(u.LinearColor(*v,1))
     else:l.set_light_color(u.LinearColor(*v,1),False)
    elif n=='intensity':l.set_intensity(v)
    else:l.set_editor_property(n,v)
 def neutral(self):
  self.rig(NEUTRAL)
  self.pp({'auto_exposure_bias':-.65,'color_contrast':u.Vector4(1.04,1.04,1.04,1.),'color_gain_shadows':u.Vector4(.97,.99,1.035,1.),'color_gain_midtones':u.Vector4(1.,1.,1.,1.),'color_gain_highlights':u.Vector4(1.015,1.,.985,1.),'color_saturation':u.Vector4(1.,1.,1.,1.),'ambient_occlusion_intensity':.32,'ambient_occlusion_radius':12.,'ambient_occlusion_power':1.,'bloom_intensity':.12,'bloom_threshold':1.5,'vignette_intensity':.18})
 def materials(self):
  if not self.mid_hero:
   self.original_materials['hero']=self.hero.get_material(0);self.original_materials['tongue']=self.tongue.get_material(0)
   self.mid_hero=self.hero.create_dynamic_material_instance(0);self.mid_tongue=self.tongue.create_dynamic_material_instance(0)
  for n,v in {'Normal Strength':.30,'Roughness Strength':.58,'Spec':.5}.items():self.mid_hero.set_scalar_parameter_value(n,v)
  self.mid_hero.set_vector_parameter_value('Albedo Color',u.LinearColor(.98,.97,.94,1.))
  for n,v in {'Source Normal Strength':.60,'Wet Roughness':.345,'Wet Roughness Variation':.05,'Wet Dielectric Specular':.42}.items():self.mid_tongue.set_scalar_parameter_value(n,v)
  self.mid_tongue.set_vector_parameter_value('Tissue Coral',u.LinearColor(.43,.09,.115,1.));self.mats_changed=True
 def warm(self):
  self.neutral();self.materials()
  self.rig({'MENU V2 | Directed cream key':{'intensity':8.0,'color':(.94,.97,1.),'specular_scale':.55},'MENU V2 | Mint shoulder rim':{'intensity':9.0,'color':(1.,.78,.58),'specular_scale':.8},'MENU V2 | Enamel reflection card':{'intensity':100.,'color':(1.,.94,.86)},'MENU V2 | Throat depth':{'intensity':2.8,'color':(.95,.66,.73)}})
  self.pp({'auto_exposure_bias':-.50,'color_gain_shadows':u.Vector4(.96,.99,1.04,1.),'color_gain_highlights':u.Vector4(1.,1.,.99,1.)})
 def write(self):(OUT/'Capture.json').write_text(json.dumps(self.report,indent=2),encoding='utf8')
 def tick(self,dt):
  try:
   self.n+=1
   if self.n<128:return
   name=self.names[self.i];assert u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.w,str(OUT/(name+'.png')))
   changed=[a.get_actor_label() for a in self.actors if a.get_path_name() in self.transforms and any(abs(x-y)>1e-4 for x,y in zip(self.transform(a),self.transforms[a.get_path_name()]))]
   assert not changed,'Unexpected placement/animation change: '+str(changed)
   self.report['profiles'].append({'name':name,'transforms_unchanged':True,'native_image':str(OUT/(name+'.png'))});self.write();self.i+=1;self.n=0
   if self.i==1:self.neutral()
   elif self.i==2:self.materials()
   elif self.i==3:self.warm()
   else:u.unregister_slate_post_tick_callback(self.cb);self.report['status']='complete_pending_visual_review';self.report['elapsed_seconds']=time.monotonic()-self.started;self.write();u.log('LOOK_ROUND1_CAPTURE_COMPLETE')
  except Exception:
   u.unregister_slate_post_tick_callback(self.cb);self.report['status']='failed';self.report['error']=traceback.format_exc();self.write();u.log_error(self.report['error'])
if globals().get('MC_LOOK_START_CAPTURE',True):builtins.mc_look_session=LookSession()
