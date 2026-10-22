"""Save the reviewed native light/grade candidate in the own menu scene only.

V6_Base versus V6_FinalLook was independently reviewed at ~5.5 versus ~6/10.
This is an art-direction iteration, not a final-reference-quality claim.
"""
import unreal as u
BASE='/Game/UI/MenuShowcase/AnatomyV2'
es=u.get_editor_subsystem(u.EditorActorSubsystem)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith(BASE+'/L_MenuAnatomyV2.')
actors=es.get_all_level_actors()
for a in actors:
 label=a.get_actor_label();tags={str(t) for t in a.tags}
 if tags & {'MC_MenuV2_Hero','MC_MenuV2_Brush','MC_MenuV2_ContactMesh'} or '| ARENA | Tooth 04 |' in label:
  for c in a.get_components_by_class(u.PrimitiveComponent):c.set_lighting_channels(True,True,False)
 light=a.get_component_by_class(u.LightComponent)
 if label=='MENU V2 | Directed cream key':light.set_intensity(9.)
 if label=='MENU V2 | Cream key':
  light.set_intensity(1.1);light.set_light_color(u.LinearColor(1,.78,.72,1),False)
 if label=='MENU V2 | Throat depth':light.set_light_color(u.LinearColor(.82,.76,.78,1),False)
 if label=='MENU V2 | Warm entrance':light.set_intensity(1.)
 if label in ['MENU V2 | Directed cream key','MENU V2 | Cream key','MENU V2 | Mint shoulder rim']:
  light.set_lighting_channels(True,False,False)
camera=next(a for a in actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
c=camera.camera_component;pp=c.post_process_settings
values={'depth_of_field_focal_distance':435.,'depth_of_field_fstop':1.6,'depth_of_field_sensor_width':100.,
 'color_saturation':u.Vector4(1.,1.,1.,1.),'color_contrast':u.Vector4(1.08,1.08,1.08,1.),
 'color_saturation_shadows':u.Vector4(.84,.84,.84,1.),'color_gain_shadows':u.Vector4(.96,.99,1.05,1.),
 'color_gain_highlights':u.Vector4(1.,1.01,1.035,1.),'bloom_intensity':.18,'bloom_threshold':1.2,'vignette_intensity':.20}
for n,v in values.items():pp.set_editor_property('override_'+n,True);pp.set_editor_property(n,v)
c.set_editor_property('post_process_settings',pp)
label='MENU V2 | Enamel reflection card'
rect=next((a for a in actors if a.get_actor_label()==label),None)
if not rect:
 rect=es.spawn_actor_from_class(u.RectLight,u.Vector(-840,190,245));rect.set_actor_label(label);rect.tags=['MC_MenuV2_Owned']
rect.set_actor_location(u.Vector(-840,190,245),False,True)
rect.set_actor_rotation(u.MathLibrary.find_look_at_rotation(u.Vector(-840,190,245),u.Vector(-630,350,80)),False)
rc=rect.get_component_by_class(u.RectLightComponent);rc.set_mobility(u.ComponentMobility.MOVABLE)
rc.set_editor_property('intensity_units',u.LightUnits.CANDELAS);rc.set_intensity(90.);rc.set_attenuation_radius(750.)
rc.set_editor_property('source_width',160.);rc.set_editor_property('source_height',200.)
rc.set_editor_property('diffuse_scale',0.);rc.set_editor_property('specular_scale',.65)
rc.set_light_color(u.LinearColor(1,.92,.83,1),False);rc.set_cast_shadows(False);rc.set_lighting_channels(False,True,False)
body=u.load_asset(BASE+'/Materials/MI_MenuHeroBody');bag=u.load_asset(BASE+'/Materials/MI_MenuHeroBag')
edit=u.MaterialEditingLibrary
edit.set_material_instance_scalar_parameter_value(body,'Roughness Strength',.48)
edit.set_material_instance_scalar_parameter_value(body,'Spec',.5)
edit.set_material_instance_scalar_parameter_value(bag,'Roughness Strength',.7)
for m in [body,bag]:assert u.EditorAssetLibrary.save_loaded_asset(m)
assert ls.save_current_level()
u.log('MENU_CINEMATIC_LOOK_V6_SAVED_PENDING_MOTION_REVIEW')
