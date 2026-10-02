"""Migrate saved food timers/hints and create two isolated reaction materials in Unreal."""
import json
import unreal as u

lib=u.EditorAssetLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
edit=u.MaterialEditingLibrary
for name, additive in [('M_Reaction',True),('M_ReactionSoft',False),('M_FoodHit',False)]:
    path='/Game/Gameplay/VFX/'+name
    mat=lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name,'/Game/Gameplay/VFX',u.Material,u.MaterialFactoryNew())
    if name != 'M_FoodHit' or edit.get_num_material_expressions(mat)==0:
        edit.delete_all_material_expressions(mat)
        mat.set_editor_property('blend_mode',u.BlendMode.BLEND_ADDITIVE if additive else u.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property('two_sided',True)
        color=edit.create_material_expression(mat,u.MaterialExpressionVectorParameter,-200,0)
        color.set_editor_property('parameter_name','Color');color.set_editor_property('default_value',u.LinearColor(1,.6,.05,1))
        opacity=edit.create_material_expression(mat,u.MaterialExpressionScalarParameter,-200,160)
        opacity.set_editor_property('parameter_name','Opacity');opacity.set_editor_property('default_value',1)
        if name != 'M_FoodHit':
            vertex=edit.create_material_expression(mat,u.MaterialExpressionVertexColor,-450,300)
            tint=edit.create_material_expression(mat,u.MaterialExpressionMultiply,0,0)
            alpha=edit.create_material_expression(mat,u.MaterialExpressionMultiply,0,160)
            assert edit.connect_material_expressions(color,'',tint,'A')
            assert edit.connect_material_expressions(vertex,'',tint,'B')
            assert edit.connect_material_expressions(opacity,'',alpha,'A')
            assert edit.connect_material_expressions(vertex,'A',alpha,'B')
            assert edit.connect_material_property(tint,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
            assert edit.connect_material_property(alpha,'',u.MaterialProperty.MP_OPACITY)
        else:
            assert edit.connect_material_property(color,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
            assert edit.connect_material_property(opacity,'',u.MaterialProperty.MP_OPACITY)
        edit.recompile_material(mat)
        assert lib.save_loaded_asset(mat)

table=lib.load_asset('/Game/Data/DT_BreakfastMenu')
rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
for row in rows:
    if row.get('Kind')=='Food': row['SpoilSeconds']=180.0
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows))
assert lib.save_loaded_asset(table)
plan=lib.load_asset('/Game/Data/DA_Day01')
if plan:
    steps=plan.get_editor_property('steps')
    for step in steps:
        if step.get_editor_property('step')==u.MCDayStep.BREAKFAST_CLEANUP:
            step.set_editor_property('instruction','RMB: cut large food. Click LMB: collect a physical stack; click again: drop. Q: throw. Bring food to the automatic throat.')
        elif step.get_editor_property('step')==u.MCDayStep.STUCK_FOOD:
            step.set_editor_property('instruction','RMB: break stuck food free. Click LMB: collect the pieces. Bring the stack to THROAT and drop it.')
    plan.set_editor_property('steps',steps)
    assert lib.save_loaded_asset(plan)
print('MC_FOOD_REWORK_ASSETS_READY')
