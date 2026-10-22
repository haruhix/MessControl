"""Environment-only composition checkpoint. Existing gameplay is untouched."""
import unreal as u
es=u.get_editor_subsystem(u.EditorActorSubsystem);ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/UI/MenuShowcase/AnatomyV2/L_MenuAnatomyV2.')
actors=es.get_all_level_actors()
camera=next(a for a in actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
camera.set_actor_location(u.Vector(-1020,160,105),False,True)
camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(camera.get_actor_location(),u.Vector(-300,200,115)),False)
for a in actors:
 if isinstance(a,u.StaticMeshActor) and '| ARENA | Tooth ' in a.get_actor_label():
  scale=a.get_actor_scale3d();scale.z=.82 if 'Tooth 04 |' in a.get_actor_label() else .72;a.set_actor_scale3d(scale)
  # Frame the interior from behind the near incisors, as in the concept. These
  # near-lens copies are kept in the preview map and excluded from this shot.
  near_lens=a.get_actor_location().x < -700
  a.set_actor_hidden_in_game(near_lens);a.set_is_temporarily_hidden_in_editor(near_lens)
 if isinstance(a,u.StaticMeshActor) and 'Teeth_' in a.get_actor_label():
  label=a.get_actor_label()
  if 'SM_Teeth_Forwart' in label:
   a.set_actor_hidden_in_game(True);a.set_is_temporarily_hidden_in_editor(True)
  else:
   a.set_actor_hidden_in_game(False);a.set_is_temporarily_hidden_in_editor(False)
c=camera.camera_component;c.set_field_of_view(58.);c.set_editor_property('constrain_aspect_ratio',False)
c.set_editor_property('override_aspect_ratio_axis_constraint',True);c.set_editor_property('aspect_ratio_axis_constraint',u.AspectRatioAxisConstraint.ASPECT_RATIO_MAINTAIN_YFOV)
pp=c.get_editor_property('post_process_settings')
for k,v in {'depth_of_field_enabled':True,'depth_of_field_focal_distance':430.,'depth_of_field_fstop':1.8,'depth_of_field_sensor_width':70.,'dynamic_global_illumination_method':u.DynamicGlobalIlluminationMethod.NONE,'auto_exposure_bias':-.65}.items():
 pp.set_editor_property('override_'+k,True);pp.set_editor_property(k,v)
c.set_editor_property('post_process_settings',pp)
fog=next((a for a in actors if 'MC_MenuV2_Fog' in [str(t) for t in a.tags]),None)
if not fog:fog=es.spawn_actor_from_class(u.ExponentialHeightFog,u.Vector(0,0,100));fog.tags=['MC_MenuV2_Fog'];fog.set_actor_label('MENU V2 | Plum depth atmosphere')
f=fog.get_component_by_class(u.ExponentialHeightFogComponent)
for k,v in {'fog_density':.025,'fog_height_falloff':.15,'fog_max_opacity':.45,'start_distance':200.,'fog_inscattering_luminance':u.LinearColor(.075,.028,.045,1)}.items():f.set_editor_property(k,v)
f.set_volumetric_fog(False)
mat=u.load_asset('/Game/UI/MenuShowcase/AnatomyV2/Materials/M_Soft_MI_Menu_MI_Pressure_Soft');edit=u.MaterialEditingLibrary
ridge=next((n for n in edit.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='Menu tongue broad ridge'),None)
if not ridge:
 ridge=edit.create_material_expression(mat,u.MaterialExpressionCustom);ridge.set_editor_property('description','Menu tongue broad ridge');inp=u.CustomInput();inp.set_editor_property('input_name','P');ridge.set_editor_property('inputs',[inp])
 p=edit.create_material_expression(mat,u.MaterialExpressionWorldPosition);edit.connect_material_expressions(p,'',ridge,'P')
ridge.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
ridge.set_editor_property('code','float x=(P.x+850.0)/450.0; float y=(P.y+160.0)/260.0; return float3(0,0,30.0*exp(-x*x-y*y));')
edit.connect_material_property(ridge,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
assert not list(edit.recompile_material(mat));u.EditorAssetLibrary.save_loaded_asset(mat)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
assert ls.save_current_level()
u.log('MENU_ENVIRONMENT_CHECKPOINT_SAVED')
