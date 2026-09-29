"""Cream enamel and broad highlights along the two molar rows."""
import unreal as u
lib=u.EditorAssetLibrary;edit=u.MaterialEditingLibrary;actors=u.get_editor_subsystem(u.EditorActorSubsystem)
mat=lib.load_asset('/Game/Gameplay/Care/M_ArenaToothCare')
for n in edit.get_material_expressions(mat):
    if isinstance(n,u.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='MC Care: cleaningColor':
        n.set_editor_property('code',str(n.get_editor_property('code')).replace('float3(.93,.80,.63)','float3(1,.98,.94)'))
    if isinstance(n,u.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='MC Care: cleaningRoughness':
        n.set_editor_property('code',str(n.get_editor_property('code')).replace('max(Base,.19)','clamp(Base,.16,.24)'))
edit.recompile_material(mat);lib.save_loaded_asset(mat,False)
mi=lib.load_asset('/Game/Art/Materials/MI_ArenaToothReference')
edit.set_material_instance_vector_parameter_value(mi,'Tint',u.LinearColor(.98,.95,.87,1));edit.update_material_instance(mi);lib.save_loaded_asset(mi,False)
for side in (-1,1):
    label='LOOK | Enamel '+('L' if side<0 else 'R')
    light=next((a for a in actors.get_all_level_actors() if a.get_actor_label()==label),None)
    if light is None:light=actors.spawn_actor_from_class(u.RectLight,u.Vector())
    light.set_actor_label(label);light.set_folder_path('Look/ReferenceLighting')
    light.set_actor_location(u.Vector(60,side*270,540),False,False)
    light.set_actor_rotation(u.MathLibrary.find_look_at_rotation(light.get_actor_location(),u.Vector(200,side*880,125)),False)
    c=light.get_component_by_class(u.RectLightComponent);c.set_mobility(u.ComponentMobility.MOVABLE)
    c.set_intensity_units(u.LightUnits.CANDELAS);c.set_intensity(32);c.set_light_color(u.LinearColor(1,.94,.86,1))
    c.set_source_width(450);c.set_source_height(600);c.set_attenuation_radius(1700);c.set_cast_shadows(False)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_REFERENCE_ENAMEL_FINISHED')
