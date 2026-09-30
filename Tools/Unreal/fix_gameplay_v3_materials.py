"""Correct texture sampling and verify the new tissue materials on a rendering editor."""
import json
from pathlib import Path
import unreal as u
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
for name in ('Gum','Palate','Exit'):
    mat=lib.load_asset('/Game/Gameplay/MouthV3/M_'+name+'V3')
    for n in edit.get_material_expressions(mat):
        if isinstance(n,u.MaterialExpressionTextureSampleParameter2D) and str(n.get_editor_property('parameter_name'))=='TissueARM':
            n.set_editor_property('sampler_type',u.MaterialSamplerType.SAMPLERTYPE_MASKS)
    assert not edit.recompile_material(mat)
    assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
mat=lib.load_asset('/Game/Gameplay/Hazards/M_SpicyPepper')
for n in edit.get_material_expressions(mat):
    if isinstance(n,u.MaterialExpressionVectorParameter) and str(n.get_editor_property('parameter_name'))=='Glow':
        n.set_editor_property('default_value',u.LinearColor(.65,.002,.001,1))
edit.recompile_material(mat); assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
table=lib.load_asset('/Game/Data/DT_BreakfastMenu')
rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
for r in rows:
    if r['Name']=='SpicyPepper': r['Label']='SPICY PEPPER'
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows,ensure_ascii=False))
assert lib.save_loaded_asset(table,only_if_is_dirty=False)
u.log('MC_GAMEPLAY_V3_MATERIALS_FIXED')
