import unreal as u
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
path='/Game/Art/Materials/M_ThroatSculpt'
mat=lib.load_asset(path) if lib.does_asset_exist(path) else lib.duplicate_asset('/Game/Art/Materials/M_LivingTissue',path)
node=next((n for n in edit.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='Throat depth'),None)
if node is None:
    color=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR)
    position=edit.create_material_expression(mat,u.MaterialExpressionPreSkinnedPosition,500,1900)
    interp=edit.create_material_expression(mat,u.MaterialExpressionVertexInterpolator,700,1900)
    edit.connect_material_expressions(position,'',interp,'')
    node=edit.create_material_expression(mat,u.MaterialExpressionCustom,1000,1900)
    node.set_editor_property('description','Throat depth'); node.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    entries=[]
    for name in ('C','P'):
        entry=u.CustomInput(); entry.set_editor_property('input_name',name); entries.append(entry)
    node.set_editor_property('inputs',entries)
    node.set_editor_property('code','return C*(1.0-.965*smoothstep(260.0,820.0,P.x));')
    edit.connect_material_expressions(color,'',node,'C'); edit.connect_material_expressions(interp,'',node,'P')
    edit.connect_material_property(node,'',u.MaterialProperty.MP_BASE_COLOR)
    scatter=next((n for n in edit.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='Soft red scatter'),None)
    if scatter: edit.connect_material_expressions(node,'',scatter,'Base')
edit.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_SKELETAL_MESH); edit.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
assert not edit.recompile_material(mat)
lib.save_loaded_asset(mat,only_if_is_dirty=False)
mi=lib.load_asset('/Game/Art/Materials/MI_LivingThroat'); edit.set_material_instance_parent(mi,mat)
edit.set_material_instance_scalar_parameter_value(mi,'MicroNormalStrength',.48)
edit.set_material_instance_scalar_parameter_value(mi,'Specular',.65)
edit.set_material_instance_scalar_parameter_value(mi,'RoughnessBias',-.025)
edit.set_material_instance_vector_parameter_value(mi,'TissueTint',u.LinearColor(1.0,.82,.9,1))
edit.update_material_instance(mi); lib.save_loaded_asset(mi,only_if_is_dirty=False)
for name,strength in [('MI_MouthGum',.42),('MI_MouthCheek',.32),('MI_MouthPalate',.42)]:
    mi=lib.load_asset('/Game/Art/Materials/'+name)
    edit.set_material_instance_scalar_parameter_value(mi,'MicroNormalStrength',strength)
    edit.set_material_instance_scalar_parameter_value(mi,'RoughnessBias',-.035)
    edit.set_material_instance_scalar_parameter_value(mi,'Specular',.62)
    edit.update_material_instance(mi); lib.save_loaded_asset(mi,only_if_is_dirty=False)
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if actor.get_actor_label() in ('LOOK | Soft key','LOOK | Soft fill','LOOK | Rim'):
        actor.get_component_by_class(u.RectLightComponent).set_editor_property('specular_scale',1.0)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_THROAT_MATERIAL_REFINED')
