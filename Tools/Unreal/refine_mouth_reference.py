"""Tune tissue, enamel and studio lighting against the mouth reference."""
import unreal as u
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
for path in ('/Game/Gameplay/Arena/MI_TonguePain','/Game/Gameplay/Arena/Pressure/MI_Pressure_Soft','/Game/Gameplay/Arena/Pressure/MI_Pressure_Deep'):
    material=lib.load_asset(path)
    for name,value in [('Metalic',0),('Spec',.45),('Normal Strength',.70),('Roughness Strength',1.10)]:
        edit.set_material_instance_scalar_parameter_value(material,name,value)
    edit.set_material_instance_vector_parameter_value(material,'Albedo Color',u.LinearColor(.87,.96,1.03,1))
    edit.update_material_instance(material); lib.save_loaded_asset(material,only_if_is_dirty=False)
for a in actors.get_all_level_actors():
    if a.get_actor_label()=='SM_Hole':
        a.set_actor_hidden_in_game(True); a.set_is_temporarily_hidden_in_editor(True)
    if a.get_actor_label()=='LOOK | Soft key':
        c=a.get_component_by_class(u.RectLightComponent)
        c.set_editor_property('specular_scale',.50)
        a.set_actor_location(u.Vector(-400,-460,680),False,True)
        a.set_actor_rotation(u.MathLibrary.find_look_at_rotation(a.get_actor_location(),u.Vector(100,0,0)),False)
        c.set_source_width(360); c.set_source_height(480)
    if a.get_actor_label()=='LOOK | Soft fill': a.get_component_by_class(u.RectLightComponent).set_editor_property('specular_scale',.35)
    if a.get_actor_label()=='LOOK | Rim': a.get_component_by_class(u.RectLightComponent).set_editor_property('specular_scale',.55)
    if isinstance(a,u.MCThroat):
        channels=u.LightingChannels(); channels.set_editor_property('channel0',True); channels.set_editor_property('channel1',True)
        a.get_editor_property('sculpted_tissue').set_editor_property('lighting_channels',channels)
        a.get_editor_property('uvula').set_editor_property('lighting_channels',channels)
        a.get_editor_property('sculpted_tissue').set_update_animation_in_editor(True)
        a.get_editor_property('sculpted_tissue').set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
        a.get_editor_property('sculpted_tissue').set_morph_target('SwallowOpen',1)
        for i in range(a.get_editor_property('uvula').get_num_materials()):
            a.get_editor_property('uvula').set_material(i,lib.load_asset('/Game/Art/Materials/MI_MouthPalate'))
    if a.get_actor_label()=='ART | Sculpted palate and cheeks':
        channels=u.LightingChannels(); channels.set_editor_property('channel0',True); channels.set_editor_property('channel1',True)
        a.static_mesh_component.set_editor_property('lighting_channels',channels)
light=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='LOOK | Mucosa key'),None)
if light is None: light=actors.spawn_actor_from_class(u.RectLight,u.Vector(550,-500,450))
light.set_actor_label('LOOK | Mucosa key'); light.set_folder_path('Look/ReferenceLighting')
light.set_actor_rotation(u.MathLibrary.find_look_at_rotation(light.get_actor_location(),u.Vector(1900,0,100)),False)
c=light.get_component_by_class(u.RectLightComponent); c.set_mobility(u.ComponentMobility.MOVABLE)
c.set_intensity_units(u.LightUnits.CANDELAS); c.set_intensity(150); c.set_light_color(u.LinearColor(1,.84,.78,1))
c.set_source_width(260); c.set_source_height(600); c.set_attenuation_radius(3300); c.set_cast_shadows(False)
c.set_editor_property('specular_scale',.8)
channels=u.LightingChannels(); channels.set_editor_property('channel0',False); channels.set_editor_property('channel1',True)
c.set_editor_property('lighting_channels',channels)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_MOUTH_REFERENCE_REFINED')
