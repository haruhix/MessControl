"""Apply the reviewed composition/lens/shadow candidate to the own menu map.

This pass supplements the environment assembly. No gameplay or project-wide
renderer settings are changed. Native previews remain the acceptance evidence.
"""
import unreal as u

BASE = '/Game/UI/MenuShowcase/AnatomyV2'
es = u.get_editor_subsystem(u.EditorActorSubsystem)
ls = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith(BASE + '/L_MenuAnatomyV2.')
actors = es.get_all_level_actors()
camera = next(a for a in actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
camera.set_actor_location(u.Vector(-1020, 160, 105), False, True)
camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(camera.get_actor_location(), u.Vector(-300, 250, 75)), False)
c = camera.camera_component
c.set_field_of_view(62.)
c.set_editor_property('constrain_aspect_ratio', False)
c.set_editor_property('override_aspect_ratio_axis_constraint', True)
c.set_editor_property('aspect_ratio_axis_constraint', u.AspectRatioAxisConstraint.ASPECT_RATIO_MAINTAIN_XFOV)
pp = c.post_process_settings
for name, value in {'depth_of_field_enabled': True, 'depth_of_field_focal_distance': 410., 'depth_of_field_fstop': 1.1, 'depth_of_field_sensor_width': 170.}.items():
    pp.set_editor_property('override_' + name, True)
    pp.set_editor_property(name, value)
c.set_editor_property('post_process_settings', pp)
hero = next(a for a in actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags])
hero.skeletal_mesh_component.set_cast_shadow(False)
for a in actors:
    label = a.get_actor_label()
    if label == 'MENU V2 | Directed cream key':
        key = a.get_component_by_class(u.SpotLightComponent)
        key.set_inner_cone_angle(14.)
        key.set_outer_cone_angle(32.)
        key.set_cast_shadows(True)
    if label == 'MENU V2 | Warm entrance':
        a.point_light_component.set_intensity(1.)
    if label == 'MENU V2 | Throat depth':
        a.point_light_component.set_intensity(1.6)
    if isinstance(a, u.SkyLight):
        a.light_component.set_intensity(.25)
    if label == 'MENU V2 | Cream key':
        # A local warm bounce from the tongue opens the chin/mitten shadows
        # without raising the whole background or changing global exposure.
        a.set_actor_location(u.Vector(-760, 210, 35), False, True)
        light = a.point_light_component
        light.set_intensity(1.7)
        light.set_attenuation_radius(480.)
        light.set_light_color(u.LinearColor(1, .64, .54, 1), False)
        light.set_editor_property('specular_scale', .10)
        light.set_cast_shadows(False)
    # The cropped ceiling crowns compete with the cleaning focal point. The
    # actual lower curved row remains visible and carries mouth depth.
    if 'SM_Teeth_R_01.' in label:
        a.set_actor_hidden_in_game(True)
        a.set_is_temporarily_hidden_in_editor(True)
assert ls.save_current_level()
u.log('MENU_DIRECTED_LOOK_CANDIDATE_SAVED')
