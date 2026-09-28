"""Run through the official Epic MCP console with PIE stopped."""
import unreal as u, json
from pathlib import Path
es=u.get_editor_subsystem(u.EditorActorSubsystem)
actors=es.get_all_level_actors()
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before authoring light rig'
with u.ScopedEditorTransaction('Reference mouth studio lighting'):
    for a in actors:
        if isinstance(a,u.SkyLight):
            c=a.get_component_by_class(u.SkyLightComponent)
            c.set_light_color(u.LinearColor(1.0,.965,.91,1))
            c.set_intensity(.40)
        elif isinstance(a,u.DirectionalLight):
            c=a.get_component_by_class(u.DirectionalLightComponent)
            c.set_mobility(u.ComponentMobility.MOVABLE)
            c.set_intensity(1.8)
            c.set_light_color(u.LinearColor(1.0,.945,.86,1))
            c.set_editor_property('light_source_angle',4.0)
            c.set_editor_property('specular_scale',.25)
            a.set_actor_rotation(u.Rotator(-48,-18,0),False)
        if isinstance(a,u.PostProcessVolume) and a.get_actor_label()=='LOOK | Coral Reference':
            st=a.settings
            values=dict(white_temp=6500.0,white_tint=0.0,color_saturation=u.Vector4(1,1,1,1.0),color_contrast=u.Vector4(1,1,1,1.035),
                        auto_exposure_min_brightness=1.0,auto_exposure_max_brightness=1.0,auto_exposure_bias=.35,
                        bloom_intensity=.16,bloom_threshold=1.5,film_toe=.46,film_shoulder=.26,vignette_intensity=.08,
                        reflection_method=u.ReflectionMethod.SCREEN_SPACE,screen_space_reflection_intensity=65.0,
                        screen_space_reflection_quality=50.0,screen_space_reflection_max_roughness=.45)
            for k,v in values.items(): st.set_editor_property(k,v);st.set_editor_property('override_'+k,True)
            a.set_editor_property('settings',st)
    lights=[('LOOK | Soft key',(-650,0,420),(150,-250,0),85,560,650,(1,.91,.79),True),
            ('LOOK | Soft fill',(-150,500,530),(100,0,0),45,620,400,(.84,.93,1),False),
            ('LOOK | Rim', (850,-60,600),(120,0,0),85,480,240,(1,.91,.80),False)]
    for label,loc,target,intensity,width,height,color,shadows in lights:
        a=next((a for a in actors if a.get_actor_label()==label),None)
        if a is None:a=es.spawn_actor_from_class(u.RectLight,u.Vector(*loc));a.set_actor_label(label)
        a.set_folder_path('Look/ReferenceLighting')
        a.set_actor_location(u.Vector(*loc),False,True)
        a.set_actor_rotation(u.MathLibrary.find_look_at_rotation(u.Vector(*loc),u.Vector(*target)),False)
        c=a.get_component_by_class(u.RectLightComponent)
        c.set_mobility(u.ComponentMobility.MOVABLE)
        c.set_intensity_units(u.LightUnits.CANDELAS)
        c.set_intensity(intensity);c.set_light_color(u.LinearColor(*color,1))
        c.set_source_width(width);c.set_source_height(height)
        c.set_attenuation_radius(2700)
        c.set_cast_shadows(shadows)
        c.set_editor_property('specular_scale',.45 if label=='LOOK | Soft key' else .6)
        c.set_editor_property('indirect_lighting_intensity',.5)
    # The imported gum actors had an engine debug-red material override.
    gum_path='/Game/Gameplay/Care/MI_GumReference'
    gum=u.load_asset(gum_path)
    if not gum:
        gum=u.AssetToolsHelpers.get_asset_tools().create_asset('MI_GumReference','/Game/Gameplay/Care',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    u.MaterialEditingLibrary.set_material_instance_parent(gum,u.load_asset('/Game/Art/Materials/M_Gum'))
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(gum,'Tint',u.LinearColor(.40,.018,.052,1))
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(gum,'Roughness',.24)
    u.MaterialEditingLibrary.update_material_instance(gum)
    assert u.EditorAssetLibrary.save_loaded_asset(gum), 'Gum material save failed'
    for a in actors:
        if a.get_actor_label() in ('SM_Gum','SM_Gum2'):
            for component in a.get_components_by_class(u.StaticMeshComponent): component.set_material(0,gum)
    wall=u.load_asset('/Game/Gameplay/Arena/MI_Wall')
    for name,value in [('Normal Strength',.65),('Roughness Strength',.80),('Spec',.5)]:
        u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(wall,name,value)
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(wall,'Albedo Color',u.LinearColor(.72,.20,.27,1))
    u.MaterialEditingLibrary.update_material_instance(wall)
    assert u.EditorAssetLibrary.save_loaded_asset(wall), 'Wall material save failed'
    enamel_path='/Game/Gameplay/Care/MI_EnamelReference'
    enamel=u.load_asset(enamel_path)
    if not enamel:
        enamel=u.AssetToolsHelpers.get_asset_tools().create_asset('MI_EnamelReference','/Game/Gameplay/Care',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    u.MaterialEditingLibrary.set_material_instance_parent(enamel,u.load_asset('/Game/Art/Materials/M_Enamel'))
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(enamel,'Tint',u.LinearColor(.854,.732,.592,1))
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(enamel,'Roughness',.19)
    u.MaterialEditingLibrary.update_material_instance(enamel)
    assert u.EditorAssetLibrary.save_loaded_asset(enamel)
    for a in actors:
        if any(x in a.get_actor_label().lower() for x in ['tooth','teeth']):
            for component in a.get_components_by_class(u.StaticMeshComponent):
                for slot in range(component.get_num_materials()):
                    old=component.get_material(slot)
                    if old and old.get_path_name()=='/Game/Art/Materials/M_Enamel.M_Enamel': component.set_material(slot,enamel)
    coffee=u.load_asset('/Game/Gameplay/Liquid/MI_CoffeePuddle')
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(coffee,'LiquidColor',u.LinearColor(.11,.027,.004,1))
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(coffee,'EdgeColor',u.LinearColor(.25,.08,.016,1))
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(coffee,'WetSheen',.18)
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(coffee,'Specular',.65)
    u.MaterialEditingLibrary.update_material_instance(coffee)
    assert u.EditorAssetLibrary.save_loaded_asset(coffee)
    assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level(), 'Could not save current map'
u.log('MC_REFERENCE_LIGHTING_SAVED')
