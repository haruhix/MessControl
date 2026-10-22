"""Menu-only art direction: motivated lighting, material separation and grading.

Applies only to the AnatomyV2 preview. Never changes shared gameplay materials,
renderer defaults or the gameplay mouth. Re-running updates named nodes/lights.
"""
import unreal as u,json
from pathlib import Path
BASE='/Game/UI/MenuShowcase/AnatomyV2'
es=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith(BASE+'/L_MenuAnatomyV2.')
lib=u.EditorAssetLibrary;e=u.MaterialEditingLibrary
actors=es.get_all_level_actors()
hero=next(a for a in actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
body=lib.load_asset(BASE+'/Materials/MI_MenuHeroBody') or lib.duplicate_asset('/Game/Gameplay/CharacterCurrent/Materials/MI_Character',BASE+'/Materials/MI_MenuHeroBody')
for key,value in [('Coffee',.10),('Damage',0.),('HitFlash',0.),('BodyStretch',0.),('Roughness Strength',.85),('Normal Strength',.5),('Spec',.38)]:e.set_material_instance_scalar_parameter_value(body,key,value)
e.set_material_instance_vector_parameter_value(body,'Albedo Color',u.LinearColor(.94,.90,.83,1))
e.update_material_instance(body);lib.save_loaded_asset(body);hero.skeletal_mesh_component.set_material(0,body)
cam=next(a for a in actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
cam.set_actor_location(u.Vector(-985,160,174),False,True)
cam.set_actor_rotation(u.MathLibrary.find_look_at_rotation(cam.get_actor_location(),u.Vector(-450,300,50)),False)
cam.camera_component.set_field_of_view(55.)

# One motivated key, cool rear separation, low level warm bounce and plum depth.
lighting={
 'Cream key':((-770,130,245),7.5,(.98,.92,.84),True,950),
 'Warm entrance':((-1000,90,190),7.,(.86,.83,.88),False,2600),
 'Throat depth':((190,-80,175),5.5,(.85,.50,.64),False,2600),
 'Mint edge':((-520,535,190),2.8,(.56,.83,.86),False,850),
}
for a in actors:
 if not isinstance(a,u.PointLight):continue
 for name,(pos,power,col,shadows,radius) in lighting.items():
  if name in a.get_actor_label():
   a.set_actor_location(u.Vector(*pos),False,True);c=a.point_light_component
   c.set_intensity(power);c.set_light_color(u.LinearColor(*col,1),False)
   c.set_attenuation_radius(radius);c.set_cast_shadows(shadows)
   c.set_editor_property('source_radius',65.);c.set_editor_property('soft_source_radius',90.)
   c.set_editor_property('specular_scale',.38)

# Keep the actual tissue maps, but remove the dull uniform brick red.
for name,col,rough,spec in [
 ('MI_Pressure_Soft',(.38,.17,.215),.25,.5),
 ('MI_TissueV10_EdgeFade',(.32,.125,.175),.36,.38),
 ('MI_Roof_0',(.27,.09,.145),.40,.34),
 ('M_ExitClimate',(.27,.09,.145),.41,.34),
]:
 m=lib.load_asset(BASE+'/Materials/M_Soft_MI_Menu_'+name)
 if not m:continue
 nodes=e.get_material_expressions(m)
 # The first tint and final base blend are the authored tissue inputs.
 tint=next(n for n in nodes if isinstance(n,u.MaterialExpressionConstant3Vector))
 tint.set_editor_property('constant',u.LinearColor(*col,1))
 base=e.get_material_property_input_node(m,u.MaterialProperty.MP_BASE_COLOR)
 if isinstance(base,u.MaterialExpressionLinearInterpolate):base.set_editor_property('const_alpha',.46)
 normal=e.get_material_property_input_node(m,u.MaterialProperty.MP_NORMAL)
 if isinstance(normal,u.MaterialExpressionLinearInterpolate):normal.set_editor_property('const_alpha',.65 if name=='MI_Pressure_Soft' else .48)
 for prop,value in [(u.MaterialProperty.MP_ROUGHNESS,rough),(u.MaterialProperty.MP_SPECULAR,spec)]:
  n=e.get_material_property_input_node(m,prop);n.set_editor_property('r',value)
 assert not list(e.recompile_material(m));lib.save_loaded_asset(m)

pp=cam.camera_component.get_editor_property('post_process_settings')
values={
 'auto_exposure_method':u.AutoExposureMethod.AEM_MANUAL,'auto_exposure_bias':-.65,
 'dynamic_global_illumination_method':u.DynamicGlobalIlluminationMethod.NONE,
 'depth_of_field_focal_distance':410.,'depth_of_field_fstop':1.2,'depth_of_field_sensor_width':54.,
 'bloom_intensity':.24,'bloom_threshold':1.2,'vignette_intensity':.2,
 'ambient_occlusion_intensity':.9,'ambient_occlusion_radius':90.,'ambient_occlusion_power':1.25,'ambient_occlusion_quality':80.,
 'color_saturation':u.Vector4(1.02,1.04,1.03,1.),'color_contrast':u.Vector4(1.025,1.025,1.025,1.),
 'color_gain_highlights':u.Vector4(1.025,1.005,.97,1.),
 'color_gain_shadows':u.Vector4(.97,.985,1.025,1.),
}
for key,value in values.items():
 pp.set_editor_property('override_'+key,True);pp.set_editor_property(key,value)
pp.set_editor_property('override_auto_exposure_apply_physical_camera_exposure',True)
pp.set_editor_property('auto_exposure_apply_physical_camera_exposure',False)
cam.camera_component.set_editor_property('post_process_settings',pp)
cam.camera_component.set_editor_property('post_process_blend_weight',1.)
for a in actors:
 if 'MC_MenuV2_Foam' in [str(t) for t in a.tags]:a.set_actor_relative_scale3d(u.Vector(.28,.28,.28))
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(cam.get_actor_location(),cam.get_actor_rotation())
assert levels.save_current_level()
u.log('MENU_V2_POLISH_APPLIED')
